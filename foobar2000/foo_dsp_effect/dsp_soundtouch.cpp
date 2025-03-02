#include "../helpers/foobar2000+atl.h"
#include "../../libPPUI/win32_utility.h"
#include "../../libPPUI/win32_op.h" // WIN32_OP()
#include "../SDK/ui_element.h"
#include "../helpers/BumpableElem.h"
#include "resource.h"
#include "../../../3rdparty-deps/SoundTouch/SoundTouch.h"
#include "../../libPPUI/CDialogResizeHelper.h"
#include "circular_buffer.h"
#include "dsp_guids.h"
#include <coreDarkMode.h>

#include <memory>

namespace {


	using namespace soundtouch;

	static void RunDSPConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback);
	static void RunDSPConfigPopupRate(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback);
	static void RunDSPConfigPopupTempo(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback);
#define BUFFER_SIZE 2048
#define BUFFER_SIZE_RBTEMPO 1024

	static double clamp_ml(double x, double upper, double lower)
	{
		return min(upper, max(x, lower));
	}

	class CEditMod : public CWindowImpl<CEditMod, CEdit >
	{
	public:
		BEGIN_MSG_MAP(CEditMod)
			MESSAGE_HANDLER(WM_CHAR, OnChar)
		END_MSG_MAP()

		CEditMod(HWND hWnd = NULL) { }
		LRESULT OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			switch (wParam)
			{
			case '\r': //Carriage return
				::PostMessage(m_parent, WM_USER, 0x1988, 0L);
				return 0;
				break;
			}
			return DefWindowProc(uMsg, wParam, lParam);
		}
		void AttachToDlgItem(HWND parent)
		{
			m_parent = parent;
		}
	private:
		UINT m_dlgItem;
		HWND m_parent;
	};



	class dsp_pitch : public dsp_impl_base
	{
		SoundTouch* p_soundtouch;
		int m_rate, m_ch, m_ch_mask;
		float pitch_amount;
		bool st_enabled;
		metadb_handle_ptr current_track;
		pfc::array_t<float>buf;
	private:
		void flushchunks(bool flush)
		{
			if (p_soundtouch && st_enabled)
			{

				int samps = 0;
				size_t out_samples_gen =p_soundtouch->numSamples();
				buf.grow_size(out_samples_gen * m_ch);
				float* ptr = buf.get_ptr();
				do {
					out_samples_gen = p_soundtouch->receiveSamples(ptr,out_samples_gen);
					samps += out_samples_gen;
					ptr += out_samples_gen * m_ch;
				} while (out_samples_gen != 0);
				audio_chunk* chunk = insert_chunk();
				chunk->set_data_32(buf.get_ptr(), samps, m_ch, m_rate);

			}
		}
	public:
		dsp_pitch(dsp_preset const& in) : pitch_amount(0.00), m_rate(0), m_ch(0), m_ch_mask(0)
		{
			p_soundtouch = 0;
			st_enabled = false;
			parse_preset(pitch_amount, st_enabled, in);
		}
		~dsp_pitch() {
			if (p_soundtouch)
			{
				p_soundtouch->clear();
				delete p_soundtouch;
				p_soundtouch = 0;
			}
		}

		// Every DSP type is identified by a GUID.
		static GUID g_get_guid() {
			// Create these with guidgen.exe.
			// {A7FBA855-56D4-46AC-8116-8B2A8DF2FB34}

			return guid_pitch;
		}

		static void g_get_name(pfc::string_base& p_out) {
			p_out = "Pitch Shift";
		}

		virtual void on_endoftrack(abort_callback& p_abort) {
			flushchunks(true);

		}

		virtual void on_endofplayback(abort_callback& p_abort) {
			//same as flush, only at end of playback
			flushchunks(true);
		}

		// The framework feeds input to our DSP using this method.
		// Each chunk contains a number of samples with the same
		// stream characteristics, i.e. same sample rate, channel count
		// and channel configuration.
		virtual bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) {
			t_size sample_count = chunk->get_sample_count();
			audio_sample* src = chunk->get_data();
			if (pitch_amount == 0.0)
			{
				st_enabled = false;
				return true;
			}
			if (!st_enabled)
				st_enabled = true;

			if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
			{

				flushchunks(false);

				m_rate = chunk->get_srate();
				m_ch = chunk->get_channels();
				m_ch_mask = chunk->get_channel_config();
				if (p_soundtouch)
				{
					delete p_soundtouch;
				}
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				if (p_soundtouch)
				{
					p_soundtouch->setSampleRate(m_rate);
					p_soundtouch->setChannels(m_ch);
					p_soundtouch->setPitchSemiTones(pitch_amount);
					bool usequickseek = true;
					bool useaafilter = true; //seems clearer without it
					p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, true);
					p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
				}
				buf.set_size(4096 * m_ch);
				return false;
			}
			if (p_soundtouch)
			{

#ifdef _WIN64


				if (sample_count)
				{
					buf.grow_size(sample_count * m_ch);
					audio_math::convert(src, (float*)buf.get_ptr(), sample_count * m_ch);
					p_soundtouch->putSamples(buf.get_ptr(), sample_count);
				}
#else
				if (sample_count)
				{
					buf.grow_size(sample_count * m_ch);
					p_soundtouch->putSamples(src, sample_count);
				}
#endif
				flushchunks(false);
			}
			return false;
		}

		virtual void flush() {
			if (!st_enabled)return;
			m_rate = 0;
			m_ch = 0;
			m_ch_mask = 0;
		}

		virtual double get_latency() {
			return 0.0;
		}


		virtual bool need_track_change_mark() {
			return true;
		}

		static bool g_get_default_preset(dsp_preset& p_out)
		{
			make_preset(0.0, false, p_out);
			return true;
		}
		static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
		{
			::RunDSPConfigPopup(p_data, p_parent, p_callback);
		}
		static bool g_have_config_popup() { return true; }
		static void make_preset(float pitch, bool enabled, dsp_preset& out)
		{
			dsp_preset_builder builder;
			builder << pitch;
			builder << enabled;
			builder.finish(g_get_guid(), out);
		}
		static void parse_preset(float& pitch, bool& enabled, const dsp_preset& in)
		{
			try
			{
				dsp_preset_parser parser(in);
				parser >> pitch;
				parser >> enabled;
			}
			catch (exception_io_data) { pitch = 0.0; enabled = false; }
		}
	};

	class dsp_tempo : public dsp_impl_base
	{
		SoundTouch* p_soundtouch;
		int m_rate, m_ch, m_ch_mask;
		int pitch_shifter;
		float tempo_amount;
		bool st_enabled;
		pfc::array_t<float>buf;
		metadb_handle_ptr current_track;
	private:
		void flushchunks(bool flush)
		{
			if (p_soundtouch && st_enabled)
			{

				int samps = 0;
				size_t out_samples_gen = p_soundtouch->numSamples();
				buf.grow_size(out_samples_gen * m_ch);
				float* ptr = buf.get_ptr();
				do {
					out_samples_gen = p_soundtouch->receiveSamples(ptr, out_samples_gen);
					samps += out_samples_gen;
					ptr += out_samples_gen * m_ch;
				} while (out_samples_gen != 0);
				audio_chunk* chunk = insert_chunk();
				chunk->set_data_32(buf.get_ptr(), samps, m_ch, m_rate);

			}
		}

	public:
		dsp_tempo(dsp_preset const& in) : tempo_amount(0.00), m_rate(0), m_ch(0), m_ch_mask(0)
		{
			p_soundtouch = NULL;
			st_enabled = false;
			pitch_shifter = 0;
			parse_preset(tempo_amount, st_enabled, in);
		}
		~dsp_tempo() {

			if (p_soundtouch)
			{
				p_soundtouch->clear();
				delete p_soundtouch;
				p_soundtouch = NULL;
			}
		}

		// Every DSP type is identified by a GUID.
		static GUID g_get_guid() {
			// {44BCACA2-9EDD-493A-BB8F-9474F4B5A76B}

			return guid_tempo;
		}

		// We also need a name, so the user can identify the DSP.
		// The name we use here does not describe what the DSP does,
		// so it would be a bad name. We can excuse this, because it
		// doesn't do anything useful anyway.
		static void g_get_name(pfc::string_base& p_out) {
			p_out = "Tempo Shift";
		}

		virtual void on_endoftrack(abort_callback& p_abort) {

			flushchunks(true);
		}

		virtual void on_endofplayback(abort_callback& p_abort) {
			//same as flush, only at end of playback
			flushchunks(true);
		}

		// The framework feeds input to our DSP using this method.
		// Each chunk contains a number of samples with the same
		// stream characteristics, i.e. same sample rate, channel count
		// and channel configuration.




		virtual bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) {
			t_size sample_count = chunk->get_sample_count();
			audio_sample* src = chunk->get_data();


			if (tempo_amount == 0)
			{
				st_enabled = false;
				return true;
			}
			if (!st_enabled)st_enabled = true;

			if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
			{
				flushchunks(false);


				m_rate = chunk->get_srate();
				m_ch = chunk->get_channels();
				m_ch_mask = chunk->get_channel_config();


				if (p_soundtouch)
				{
					delete p_soundtouch;
					p_soundtouch = NULL;
				}
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				if (p_soundtouch)
				{
					p_soundtouch->setSampleRate(m_rate);
					p_soundtouch->setChannels(m_ch);
					p_soundtouch->setTempoChange(tempo_amount);
					bool usequickseek = true;
					bool useaafilter = true; //seems clearer without it
					p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, true);
					p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
				}
				return false;
			}

			if (p_soundtouch && pitch_shifter == 0)
			{
				t_size sample_count = chunk->get_sample_count();
				if (sample_count)
				{
#ifdef _WIN64


					if (sample_count)
					{
						buf.grow_size(sample_count * m_ch);
						audio_math::convert(src, (float*)buf.get_ptr(), sample_count * m_ch);
						p_soundtouch->putSamples(buf.get_ptr(), sample_count);
					}
#else
					if (sample_count)
					{
						buf.grow_size(sample_count * m_ch);
						p_soundtouch->putSamples(src, sample_count);
					}
#endif
					flushchunks(false);
				}

			}
			return false;
		}
		virtual void flush() {
			if (!st_enabled)return;

			if (p_soundtouch) {
				p_soundtouch->clear();

			}
			m_rate = 0;
			m_ch = 0;
			m_ch_mask = 0;
		}

		virtual double get_latency() {
			return 0;
		}

		virtual bool need_track_change_mark() {
			return false;
		}

		static bool g_get_default_preset(dsp_preset& p_out)
		{
			make_preset(0.0, false, p_out);
			return true;
		}
		static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
		{
			::RunDSPConfigPopupTempo(p_data, p_parent, p_callback);
		}
		static bool g_have_config_popup() { return true; }
		static void make_preset(float pitch, bool enabled, dsp_preset& out)
		{
			dsp_preset_builder builder;
			builder << pitch;
			builder << enabled;
			builder.finish(g_get_guid(), out);
		}
		static void parse_preset(float& pitch, bool& enabled, const dsp_preset& in)
		{
			try
			{
				dsp_preset_parser parser(in);
				parser >> pitch;
				parser >> enabled;
			}
			catch (exception_io_data) { pitch = 0.0; enabled = false; }
		}
	};

	class dsp_rate : public dsp_impl_base
	{
		SoundTouch* p_soundtouch;
		int m_rate, m_ch, m_ch_mask;
		float pitch_amount;
		pfc::array_t<float>buf;
		bool st_enabled;
		metadb_handle_ptr current_track;
		bool gettrackdata;
	private:
		void flushchunks(bool flush)
		{
			if (p_soundtouch && st_enabled)
			{

				int samps = 0;
				size_t out_samples_gen = p_soundtouch->numSamples();
				buf.grow_size(out_samples_gen * m_ch);
				float* ptr = buf.get_ptr();
				do {
					out_samples_gen = p_soundtouch->receiveSamples(ptr, out_samples_gen);
					samps += out_samples_gen;
					ptr += out_samples_gen * m_ch;
				} while (out_samples_gen != 0);
				audio_chunk* chunk = insert_chunk();
				chunk->set_data_32(buf.get_ptr(), samps, m_ch, m_rate);

			}
		}
	public:
		dsp_rate(dsp_preset const& in) : pitch_amount(0.00), m_rate(0), m_ch(0), m_ch_mask(0)
		{
			p_soundtouch = 0;
			st_enabled = false;
			parse_preset(pitch_amount, st_enabled, in);

		}
		~dsp_rate() {
			if (p_soundtouch)
			{
				p_soundtouch->clear();
				delete p_soundtouch;
				p_soundtouch = 0;
			}

		}

		// Every DSP type is identified by a GUID.
		static GUID g_get_guid() {
			// {8C12D81E-BB88-4056-B4C0-EAFA4E9F3B95}

			return guid_pbrate;
		}

		// We also need a name, so the user can identify the DSP.
		// The name we use here does not describe what the DSP does,
		// so it would be a bad name. We can excuse this, because it
		// doesn't do anything useful anyway.
		static void g_get_name(pfc::string_base& p_out) {
			p_out = "Playback Rate Shift";
		}

		virtual void on_endoftrack(abort_callback& p_abort) {
			// This method is called when a track ends.
			// We need to do the same thing as flush(), so we just call it.
			flushchunks(true);

		}

		virtual void on_endofplayback(abort_callback& p_abort) {
			// This method is called on end of playback instead of flush().
			// We need to do the same thing as flush(), so we just call it.
			flushchunks(true);

		}

		// The framework feeds input to our DSP using this method.
		// Each chunk contains a number of samples with the same
		// stream characteristics, i.e. same sample rate, channel count
		// and channel configuration.
		virtual bool on_chunk(audio_chunk* chunk, abort_callback& p_abort) {



			if (pitch_amount == 0.0)
			{
				st_enabled = false;
				return true;
			}
			if (!st_enabled) st_enabled = true;


			if (chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask)
			{
				if (p_soundtouch)
				{
					flushchunks(false);
					delete p_soundtouch;
				}

				m_rate = chunk->get_srate();
				m_ch = chunk->get_channels();
				m_ch_mask = chunk->get_channel_config();
				p_soundtouch = new SoundTouch;
				if (!p_soundtouch) return 0;
				p_soundtouch->setSampleRate(m_rate);
				p_soundtouch->setChannels(m_ch);
				p_soundtouch->setRateChange(pitch_amount);
				st_enabled = true;
				return false;


			}
			if (p_soundtouch && st_enabled)
			{
				t_size sample_count = chunk->get_sample_count();
				if (sample_count)
				{
					audio_sample* current = chunk->get_data();
#ifdef _WIN64
					buf.grow_size(sample_count * m_ch);
					audio_math::convert(current, (float*)buf.get_ptr(), sample_count * m_ch);
					p_soundtouch->putSamples(buf.get_ptr(), sample_count);

#else
					if (sample_count)
					{
						buf.grow_size(sample_count * m_ch);
						p_soundtouch->putSamples(current, sample_count);
					}
#endif
					flushchunks(false);
				}
			}
			return false;
		}

		virtual void flush() {
			if (!st_enabled)return;
			if (p_soundtouch) {
				p_soundtouch->clear();

			}
			m_rate = 0;
			m_ch = 0;
			m_ch_mask = 0;
		}

		virtual double get_latency() {
			return 0;
		}

		virtual bool need_track_change_mark() {
			return false;
		}

		static bool g_get_default_preset(dsp_preset& p_out)
		{
			make_preset(0.0, false, p_out);
			return true;
		}
		static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
		{
			::RunDSPConfigPopupRate(p_data, p_parent, p_callback);
		}
		static bool g_have_config_popup() { return true; }
		static void make_preset(float pitch, bool enabled, dsp_preset& out)
		{
			dsp_preset_builder builder;
			builder << pitch;
			builder << enabled;
			builder.finish(g_get_guid(), out);
		}
		static void parse_preset(float& pitch, bool& enabled, const dsp_preset& in)
		{
			try
			{
				dsp_preset_parser parser(in);
				parser >> pitch;
				parser >> enabled;
			}
			catch (exception_io_data) { pitch = 0.0; enabled = false; }
		}
	};

	class CMyDSPPopupPitch : public CDialogImpl<CMyDSPPopupPitch>
	{
	public:
		CMyDSPPopupPitch(const dsp_preset& initData, dsp_preset_edit_callback& callback) : m_initData(initData), m_callback(callback) { }
		enum { IDD = IDD_PITCH };
		enum
		{
			pitchmin = 0,
			pitchmax = 2400

		};
		BEGIN_MSG_MAP(CMyDSPPopup)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOKButton)
			COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnButton)
			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset)
			COMMAND_HANDLER_EX(IDC_PITCHTYPE, CBN_SELCHANGE, OnChange)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)
			MSG_WM_HSCROLL(OnScroll)

		END_MSG_MAP()
	private:
		fb2k::CCoreDarkModeHooks m_hooks;

		void DSPConfigChange(dsp_chain_config const& cfg)
		{
			if (m_hWnd != NULL) {
				ApplySettings();
			}
		}


		void ApplySettings()
		{
			dsp_preset_impl preset2;
			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset2)) {
				float  pitch;
				bool enabled;
				dsp_pitch::parse_preset(pitch, enabled, preset2);
				float pitch_val = pitch *= 100.00;
				slider_drytime.SetPos((double)(pitch_val + 1200));
				RefreshLabel(pitch_val / 100.00);
			}
		}
		LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			float pitch = 0;
			float pitch3 = (pitch * 100) + 1200;
			slider_drytime.SetPos((double)(pitch3));

			bool enabled = false;
			dsp_preset_impl preset;
			enabled = true;
			dsp_pitch::make_preset(pitch, enabled, preset);
			m_callback.on_preset_changed(preset);
			RefreshLabel(pitch);


			return 0;
		}

		BOOL OnInitDialog(CWindow, LPARAM)
		{
			slider_drytime = GetDlgItem(IDC_PITCH);
			slider_drytime.SetRange(0, pitchmax);
			pitch_edit.AttachToDlgItem(m_hWnd);
			pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
			CWindow w = GetDlgItem(IDC_PITCHALGO);
			CWindow wind(m_hWnd);
			wind.SetWindowTextW(L"Pitch Shift DSP Configuration");
			w.ShowWindow(SW_HIDE);
			w = GetDlgItem(IDC_TEMPOTYPE);
			w.ShowWindow(SW_HIDE);
			int pitch_type;
			{
				float  pitch;
				bool enabled = true;
				dsp_pitch::parse_preset(pitch, enabled, m_initData);
				pitch *= 100.00;
				slider_drytime.SetPos((double)(pitch + 1200));
				RefreshLabel(pitch / 100.00);
			}
			m_hooks.AddDialogWithControls(m_hWnd);
			return TRUE;
		}


		void GetEditText()
		{
			CString sWindowText;
			pitch_edit.GetWindowText(sWindowText);
			float pitch2 = _ttof(sWindowText);
			pitch2 = clamp_ml(pitch2, 12.0, -12.0);
			if (pitch_s != sWindowText)
			{

				float pitch3 = pitch2 * 100.00;
				slider_drytime.SetPos((double)(pitch3 + 1200));
				dsp_preset_impl preset;
				dsp_pitch::make_preset(pitch2, true, preset);
				m_callback.on_preset_changed(preset);
			}
		}
		void OnOKButton(UINT, int id, CWindow)
		{
			GetEditText();
			EndDialog(id);
		}

		void OnButton(UINT, int id, CWindow)
		{
			EndDialog(id);
		}

		void OnChange(UINT scrollID, int id, CWindow window)
		{
			float pitch;
			pitch = slider_drytime.GetPos() - 1200;
			pitch /= 100.00;

			bool enabled = false;
			{
				dsp_preset_impl preset;
				enabled = true;
				dsp_pitch::make_preset(pitch, enabled, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
		}

		void OnScroll(UINT scrollID, int id, CWindow window)
		{
			float pitch;
			pitch = slider_drytime.GetPos() - 1200;
			pitch /= 100.00;

			bool enabled = false;
			if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
			{
				bool enabled = false;
				dsp_preset_impl preset;
				enabled = true;
				dsp_pitch::make_preset(pitch, enabled, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
		}


		LRESULT OnEditControlChange2(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			float pitch;
			pitch = slider_drytime.GetPos() - 1200;
			pitch /= 100.00;

			pitch += (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
			return true;
		}

		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{
				GetEditText();
			}
			return 0;
		}

		void RefreshLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "Pitch: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			pitch_s = sWindowText;
			pitch_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "semitones";
			::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

		}


		const dsp_preset& m_initData; // modal dialog so we can reference this caller-owned object.
		dsp_preset_edit_callback& m_callback;
		CEditMod pitch_edit;
		CString pitch_s;
		CTrackBarCtrl slider_drytime, slider_wettime, slider_dampness, slider_roomwidth, slider_roomsize;
	};

	static void RunDSPConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
	{
		CMyDSPPopupPitch popup(p_data, p_callback);

		if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
	}

	class CMyDSPPopupRate : public CDialogImpl<CMyDSPPopupRate>
	{
	public:
		CMyDSPPopupRate(const dsp_preset& initData, dsp_preset_edit_callback& callback) : m_initData(initData), m_callback(callback) { }
		enum { IDD = IDD_PITCH };
		enum
		{
			pitchmin = 0,
			pitchmax = 15000

		};
		BEGIN_MSG_MAP(CMyDSPPopup)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOKButton)
			COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnButton)
			MSG_WM_HSCROLL(OnHScroll)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)
			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
		END_MSG_MAP()
	private:
		fb2k::CCoreDarkModeHooks m_hooks;
		void DSPConfigChange(dsp_chain_config const& cfg)
		{
			if (m_hWnd != NULL) {
				ApplySettings();
			}
		}


		void ApplySettings()
		{
			dsp_preset_impl preset2;
			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset2)) {
				float  rate;
				bool enabled;
				int pitch_type;
				dsp_rate::parse_preset(rate, enabled, preset2);
				slider_drytime.SetPos((double)(rate + 5000));
				RefreshLabel(rate);
			}
		}

		BOOL OnInitDialog(CWindow, LPARAM)
		{
			slider_drytime = GetDlgItem(IDC_PITCH);
			slider_drytime.SetRange(0, pitchmax);
			pitch_edit.AttachToDlgItem(m_hWnd);
			pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
			CWindow w = GetDlgItem(IDC_PITCHALGO);
			CWindow wind(m_hWnd);
			wind.SetWindowTextW(L"Playback Rate Shift DSP Configuration");
			w.ShowWindow(SW_HIDE);
			w = GetDlgItem(IDC_TEMPOTYPE);
			w.ShowWindow(SW_HIDE);

			{
				float  pitch;
				bool enabled;
				dsp_rate::parse_preset(pitch, enabled, m_initData);
				pitch *= 100;
				slider_drytime.SetPos((double)(pitch + 5000));
				RefreshLabel(pitch / 100);
			}
			m_hooks.AddDialogWithControls(m_hWnd);
			return TRUE;
		}

		void OnButton(UINT, int id, CWindow)
		{
			EndDialog(id);
		}

		void OnOKButton(UINT, int id, CWindow)
		{
			GetEditText();
			EndDialog(id);
		}

		void OnHScroll(UINT nSBCode, UINT nPos, CWindow window)
		{
			float pitch;
			pitch = slider_drytime.GetPos() - 5000;
			pitch /= 100;
			{
				if ((LOWORD(nSBCode) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
				{
					dsp_preset_impl preset;

					dsp_rate::make_preset(pitch, true, preset);
					m_callback.on_preset_changed(preset);
				}

			}
			RefreshLabel(pitch);
		}

		void RefreshLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "PB Rate: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			pitch_s = sWindowText;
			pitch_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "%";
			::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

		}

		void GetEditText()
		{
			CString sWindowText;
			pitch_edit.GetWindowText(sWindowText);
			float pitch2 = _ttof(sWindowText);
			pitch2 = clamp_ml(pitch2, 100.0, -50.0);
			//  pitch2 = round(pitch2);
			if (pitch_s != sWindowText)
			{

				float pitch3 = (pitch2 * 100) + 5000;
				slider_drytime.SetPos((double)(pitch3));

				dsp_preset_impl preset;
				dsp_rate::make_preset(pitch2, true, preset);
				m_callback.on_preset_changed(preset);
			}
		}

		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{
				GetEditText();
			}
			return 0;
		}

		LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			float pitch = 0;
			float pitch3 = (pitch * 100) + 5000;
			slider_drytime.SetPos((double)(pitch3));
			dsp_preset_impl preset;

			dsp_rate::make_preset(pitch, true, preset);
			m_callback.on_preset_changed(preset);
			RefreshLabel(0);
			return 0;
		}

		const dsp_preset& m_initData; // modal dialog so we can reference this caller-owned object.
		dsp_preset_edit_callback& m_callback;
		CEditMod pitch_edit;
		CString pitch_s;
		CTrackBarCtrl slider_drytime, slider_wettime, slider_dampness, slider_roomwidth, slider_roomsize;
	};
	static void RunDSPConfigPopupRate(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
	{
		CMyDSPPopupRate popup(p_data, p_callback);
		if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
	}

	class CMyDSPPopupTempo : public CDialogImpl<CMyDSPPopupTempo>
	{
	public:
		CMyDSPPopupTempo(const dsp_preset& initData, dsp_preset_edit_callback& callback) : m_initData(initData), m_callback(callback) { }
		enum { IDD = IDD_PITCH };
		enum
		{
			pitchmin = 0,
			tempomax = 19000

		};
		BEGIN_MSG_MAP(CMyDSPPopup)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOKButton)
			COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnButton)
			COMMAND_HANDLER_EX(IDC_TEMPOTYPE, CBN_SELCHANGE, OnChange)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)

			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
		MSG_WM_HSCROLL(OnScroll)
		END_MSG_MAP()
	private:
		fb2k::CCoreDarkModeHooks m_hooks;
		BOOL OnInitDialog(CWindow, LPARAM)
		{
			slider_drytime = GetDlgItem(IDC_PITCH);
			slider_drytime.SetRange(0, tempomax);
			pitch_edit.AttachToDlgItem(m_hWnd);
			pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
			CWindow wind(m_hWnd);
			wind.SetWindowTextW(L"Tempo Shift DSP Configuration");
			int pitch_type;
			{
				float  pitch;
				bool enabled;
				dsp_tempo::parse_preset(pitch, enabled, m_initData);
				float tempo2 = pitch * 100;
				slider_drytime.SetPos((double)(tempo2 + 9500));
				RefreshLabel(pitch);
			}
			m_hooks.AddDialogWithControls(m_hWnd);
			return TRUE;
		}

		void GetEditText()
		{
			CString sWindowText;
			pitch_edit.GetWindowText(sWindowText);
			float pitch2 = _ttof(sWindowText);
			pitch2 = pfc::clip_t<t_float32>(pitch2, -95.0, 95.0);
			if (pitch_s != sWindowText)
			{
				float pitch3 = pitch2 * 100.00;
				slider_drytime.SetPos((double)(pitch3 + 9500));
				int p_type; //filter type
				p_type = SendDlgItemMessage(IDC_TEMPOTYPE, CB_GETCURSEL);
				{
					dsp_preset_impl preset;
					dsp_tempo::make_preset(pitch2, true, preset);
					m_callback.on_preset_changed(preset);
				}
			}
		}

		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{
				GetEditText();
			}
			return 0;
		}


		LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			float pitch = 0;
			float pitch3 = (pitch * 100) + 9500;
			slider_drytime.SetPos((double)(pitch3));
			{
				dsp_preset_impl preset;
				dsp_tempo::make_preset(pitch, true, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
			return 0;
		}

		void OnOKButton(UINT, int id, CWindow)
		{
			GetEditText();
			EndDialog(id);
		}

		void OnButton(UINT, int id, CWindow)
		{
			EndDialog(id);
		}

		void OnChange(UINT, int id, CWindow)
		{
			float pitch;
			float tempos = slider_drytime.GetPos() - 9500;
			pitch = tempos / 100.;
			{
				dsp_preset_impl preset;
				dsp_tempo::make_preset(pitch, true, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
		}

		void OnScroll(UINT scrollID, int id, CWindow window)
		{
			float pitch;
			float tempos = slider_drytime.GetPos() - 9500;
			pitch = tempos / 100.;
			if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_drytime.m_hWnd)
			{
				dsp_preset_impl preset;
				dsp_tempo::make_preset(pitch, true, preset);
				m_callback.on_preset_changed(preset);
			}
			RefreshLabel(pitch);
		}


		void RefreshLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "Tempo: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFOS1, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			pitch_s = sWindowText;
			pitch_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "%";
			::uSetDlgItemText(*this, IDC_PITCHINFOS2, msg);

		}
		const dsp_preset& m_initData; // modal dialog so we can reference this caller-owned object.
		dsp_preset_edit_callback& m_callback;
		CTrackBarCtrl slider_drytime;
		CString pitch_s;
		CEditMod pitch_edit;
	};
	static void RunDSPConfigPopupTempo(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
	{
		CMyDSPPopupTempo popup(p_data, p_callback);
		if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
	}



	// {7448B6AD-7FBE-4F72-9F4F-A740620A9C38}
	static const GUID guid_pitchelement =
	{ 0x7448b6ad, 0x7fbe, 0x4f72,{ 0x9f, 0x4f, 0xa7, 0x40, 0x62, 0xa, 0x9c, 0x38 } };
	// {51EB4363-834B-4863-8729-32C392525C31}
	static const GUID guid_tempoelement =
	{ 0x51eb4363, 0x834b, 0x4863, { 0x87, 0x29, 0x32, 0xc3, 0x92, 0x52, 0x5c, 0x31 } };
	// {5D125AD0-4745-4D16-A4C7-99D4238330B9}
	static const GUID guid_rateelement =
	{ 0x5d125ad0, 0x4745, 0x4d16, { 0xa4, 0xc7, 0x99, 0xd4, 0x23, 0x83, 0x30, 0xb9 } };

	static const CDialogResizeHelper::Param chorus_uiresize[] = {
		// Dialog resize handling matrix, defines how the controls scale with the dialog
		//			 L T R B
		{IDC_PITCHINFO_UI, 0,0,0,0  },
		{IDC_PITCHINFO_UI2,    0,0,0,0 },
		{IDC_PITCH_EDIT,    0,0,0,0  },
		{IDC_PITCHENABLED_UI, 0,0,0,0 },
		{IDC_RESET,  0,0,0,0 },
	{IDC_PITCH_UI, 0,0,1,0},
	// current position of a control is determined by initial_position + factor * (current_dialog_size - initial_dialog_size)
	// where factor is the value from the table above
	// applied to all four values - left, top, right, bottom
	// 0,0,0,0 means that a control doesn't react to dialog resizing (aligned to top+left, no resize)
	// 1,1,1,1 means that the control is aligned to bottom+right but doesn't resize
	// 0,0,1,0 means that the control disregards vertical resize (aligned to top) and changes its width with the dialog
	};
	static const CRect resizeMinMax(200, 150, 1000, 1000);



	class uielem_tempo : public CDialogImpl<uielem_tempo>, public ui_element_instance, private play_callback_impl_base {
	public:
		uielem_tempo(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb), m_resizer(chorus_uiresize, resizeMinMax) {
			tempo = 0.0; tempo_enabled = false;
		}


		enum { IDD = IDD_PITCH_ELEMENT };
		enum
		{
			pitchmin = 0,
			pitchmax = 2400,
			tempomax = 19000,

		};
		BEGIN_MSG_MAP_EX(uielem_tempo)
			CHAIN_MSG_MAP_MEMBER(m_resizer)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)
			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
		MSG_WM_HSCROLL(OnScroll)
		END_MSG_MAP()

		void initialize_window(HWND parent) {
			WIN32_OP(Create(parent) != NULL);
		}
		HWND get_wnd() { return m_hWnd; }
		void set_configuration(ui_element_config::ptr config) {
			shit = parseConfig(config);
			if (m_hWnd != NULL) {
				ApplySettings();
			}
			m_callback->on_min_max_info_change();
		}
		ui_element_config::ptr get_configuration() { return makeConfig(); }
		static GUID g_get_guid() {
			return guid_tempoelement;
		}
		static void g_get_name(pfc::string_base& out) { out = "Tempo"; }
		static ui_element_config::ptr g_get_default_configuration() {
			return makeConfig(true);
		}
		static const char* g_get_description() { return "Changes the tempo of the current track."; }
		static GUID g_get_subclass() {
			return ui_element_subclass_dsp;
		}

		ui_element_min_max_info get_min_max_info() {
			ui_element_min_max_info ret;

			// Note that we play nicely with separate horizontal & vertical DPI.
			// Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
			CSize DPI = QueryScreenDPIEx(*this);

			if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
				DPI = CSize(96, 96);
			}


			ret.m_min_width = MulDiv(200, DPI.cx, 96);
			ret.m_min_height = MulDiv(150, DPI.cy, 96);
			ret.m_max_width = MulDiv(1000, DPI.cx, 96);
			ret.m_max_height = MulDiv(1000, DPI.cy, 96);

			// Deal with WS_EX_STATICEDGE and alike that we might have picked from host
			ret.adjustForWindow(*this);

			return ret;
		}

		static uint32_t parseConfig(ui_element_config::ptr cfg) {
			::ui_element_config_parser in(cfg);
			uint32_t flags; in >> flags;
			return 0;

		}
		static ui_element_config::ptr makeConfig(bool init = false) {
			ui_element_config_builder out;

			if (init)
			{
				uint32_t crap = 1;
				out << crap;
			}
			else
			{
				uint32_t crap = 2;
				out << crap;
			}
			return out.finish(g_get_guid());
		}

		BOOL OnInitDialog(CWindow hwnd, LPARAM) {
			tempo_edit.AttachToDlgItem(m_hWnd);
			tempo_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));

			slider_tempo = GetDlgItem(IDC_PITCH_UI);
			m_buttonTempoEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
			slider_tempo.SetRange(0, tempomax);
			m_ownTempoUpdate = false;


			m_hooks.AddDialogWithControls(m_hWnd);

			ApplySettings();

			tempo_tag = GetDlgItem(IDC_TAG_EDIT);
			metadb_handle_ptr out;
			if (!static_api_ptr_t<playback_control>()->get_now_playing(out))
			{
				pfc::string_formatter msg;
				msg.reset();
				msg << "None";
				CString sWindowText;
				sWindowText = msg.c_str();
				tempo_tag.SetWindowText(sWindowText);

			}
			else
				on_playback_new_track(out);

			return FALSE;
		}



	protected:
		const ui_element_instance_callback::ptr m_callback;

	private:

		CDialogResizeHelper m_resizer;
		fb2k::CCoreDarkModeHooks m_hooks;
		uint32_t shit;
		float tempo;
		bool tempo_enabled;
		CTrackBarCtrl slider_tempo;
		CButton m_buttonTempoEnabled;
		bool m_ownTempoUpdate, m_ownRateUpdate;
		CEditMod tempo_edit;
		CString tempo_s;

		float tempo_tagval;
		CEdit tempo_tag;



		void on_playback_new_track(metadb_handle_ptr p_track) {
			ApplySettings();
			service_ptr_t<metadb_info_container> out;
			if (p_track->get_info_ref(out))
			{
				const file_info& file_inf = out->info();
				if (file_inf.meta_exists("tempo_amt"))
				{
					const char* meta = file_inf.meta_get("tempo_amt", 0);
					tempo_tagval = pfc::string_to_float(meta, strlen("tempo_amt"));
					pfc::string_formatter msg;
					msg.reset();
					msg << pfc::format_float(tempo_tagval, 0, 2);
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);

					msg.reset();
					msg << "tempo_amt tag:";
					msg << (tempo_tagval < 0 ? "-" : "+");
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
				else
				{
					tempo_tagval = 0.0;
					pfc::string_formatter msg;
					msg.reset();
					msg << "None";
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);
					msg.reset();
					msg << "No tempo_amt tag";
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
			}
		}


		void ApplySettings()
		{
			dsp_preset_impl preset;
			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_tempo, preset)) {
				SetTempoEnabled(true);
				dsp_tempo::parse_preset(tempo, tempo_enabled, preset);
				SetTempoEnabled(tempo_enabled);
				SetConfig();
			}
			else {
				SetTempoEnabled(false);
				SetConfig();
			}
		}

		void GetConfig()
		{
			float tempos = slider_tempo.GetPos() - 9500;
			tempo = tempos / 100.;
			tempo_enabled = IsTempoEnabled();
			RefreshTempoLabel(tempo);
		}


		void SetConfig()
		{
			float tempo2 = tempo * 100;
			slider_tempo.SetPos((double)(tempo2 + 9500));
			RefreshTempoLabel(tempo2 / 100);

		}

		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{

				CString tempowindowtext;
				tempo_edit.GetWindowText(tempowindowtext);
				float tempo2 = _ttof(tempowindowtext);
				tempo2 = pfc::clip_t<t_float32>(tempo2, -95.0, 95.0);
				if (tempo_s != tempowindowtext)
				{
					tempo = tempo2;
					float tempo3 = tempo * 100.00;
					slider_tempo.SetPos((double)(tempo3 + 9500));
					OnConfigChanged();
				}
			}
			return 0;
		}

		void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {

			if (IsTempoEnabled()) {
				GetConfig();
				dsp_preset_impl preset;
				dsp_tempo::make_preset(tempo, tempo_enabled, preset);
				//yes change api;
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
			}
			else {
				static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_tempo);
			}

		}

		void RefreshTempoLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "Tempo: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			tempo_s = sWindowText;
			tempo_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "%";
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

		}


		void OnConfigChanged() {

			if (IsTempoEnabled()) {
				TempoEnable(tempo, tempo_enabled);
			}
			else {
				TempoDisable();
			}
		}

		void TempoEnable(float pitch, bool enabled) {
			dsp_preset_impl preset;
			dsp_tempo::make_preset(pitch, enabled, preset);
			static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
		}

		void SetTempoEnabled(bool state) { m_buttonTempoEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
		bool IsTempoEnabled() { return m_buttonTempoEnabled == NULL || m_buttonTempoEnabled.GetCheck() == BST_CHECKED; }
		void TempoDisable() {
			static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_tempo);
		}

		LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			tempo = 0;
			float tempo3 = (tempo * 100) + 9500;
			slider_tempo.SetPos((double)(tempo3));
			SetConfig();
			OnConfigChanged();
			return 0;
		}

		void OnScroll(UINT scrollID, int pos, CWindow window)
		{
			GetConfig();
			if (IsTempoEnabled())
			{

				if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_tempo.m_hWnd)
				{
					TempoEnable(tempo, tempo_enabled);
				}
			}
		}


	};

	class uielem_rate : public CDialogImpl<uielem_rate>, public ui_element_instance, private play_callback_impl_base {
	public:
		uielem_rate(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb), m_resizer(chorus_uiresize, resizeMinMax) {
			rate = 0.0; rate_enabled = false;
		}
		enum { IDD = IDD_PITCH_ELEMENT };
		enum
		{
			pitchmin = 0,
			pitchmax = 2400,
			tempomax = 19000,

		};
		BEGIN_MSG_MAP_EX(uielem_rate)
			CHAIN_MSG_MAP_MEMBER(m_resizer)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)
			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset3);
		MSG_WM_HSCROLL(OnScroll)
		END_MSG_MAP()

		void initialize_window(HWND parent) {
			WIN32_OP(Create(parent) != NULL);
		}
		HWND get_wnd() { return m_hWnd; }
		void set_configuration(ui_element_config::ptr config) {
			shit = parseConfig(config);
			if (m_hWnd != NULL) {
				ApplySettings();
			}
			m_callback->on_min_max_info_change();
		}
		ui_element_config::ptr get_configuration() { return makeConfig(); }
		static GUID g_get_guid() {
			return guid_rateelement;
		}
		static void g_get_name(pfc::string_base& out) { out = "Playback Rate"; }
		static ui_element_config::ptr g_get_default_configuration() {
			return makeConfig(true);
		}
		static const char* g_get_description() { return "Changes the playback rate of the current track."; }
		static GUID g_get_subclass() {
			return ui_element_subclass_dsp;
		}

		ui_element_min_max_info get_min_max_info() {
			ui_element_min_max_info ret;

			// Note that we play nicely with separate horizontal & vertical DPI.
			// Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
			CSize DPI = QueryScreenDPIEx(*this);

			if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
				DPI = CSize(96, 96);
			}


			ret.m_min_width = MulDiv(200, DPI.cx, 96);
			ret.m_min_height = MulDiv(150, DPI.cy, 96);
			ret.m_max_width = MulDiv(1000, DPI.cx, 96);
			ret.m_max_height = MulDiv(1000, DPI.cy, 96);

			// Deal with WS_EX_STATICEDGE and alike that we might have picked from host
			ret.adjustForWindow(*this);

			return ret;
		}
	private:
		float tempo_tagval;
		CEdit tempo_tag;

		void on_playback_new_track(metadb_handle_ptr p_track) {
			ApplySettings();
			service_ptr_t<metadb_info_container> out;
			if (p_track->get_info_ref(out))
			{
				const file_info& file_inf = out->info();
				if (file_inf.meta_exists("pbrate_amt"))
				{
					const char* meta = file_inf.meta_get("pbrate_amt", 0);
					tempo_tagval = pfc::string_to_float(meta, strlen("pbrate_amt"));
					pfc::string_formatter msg;
					msg.reset();
					msg << pfc::format_float(tempo_tagval, 0, 2);
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);

					msg.reset();
					msg << "pbrate_amt tag:";
					msg << (tempo_tagval < 0 ? "-" : "+");
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
				else
				{
					tempo_tagval = 0.0;
					pfc::string_formatter msg;
					msg.reset();
					msg << "None";
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);
					msg.reset();
					msg << "No pbrate_amt tag";
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
			}
		}



		CDialogResizeHelper m_resizer;
		fb2k::CCoreDarkModeHooks m_hooks;
		void SetRateEnabled(bool state) { m_buttonRateEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
		bool IsRateEnabled() { return m_buttonRateEnabled == NULL || m_buttonRateEnabled.GetCheck() == BST_CHECKED; }
		void RateDisable() {
			static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pbrate);
		}

		void RateEnable(float pitch, bool enabled) {
			dsp_preset_impl preset;
			dsp_rate::make_preset(pitch, enabled, preset);
			static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
		}

		LRESULT OnReset3(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			rate = 0;
			float rate3 = (rate * 100) + 5000;
			slider_rate.SetPos((double)(rate3));
			SetConfig();
			OnConfigChanged();
			return 0;
		}

		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{
				CString sWindowText;
				rate_edit.GetWindowText(sWindowText);
				float rate2 = _ttof(sWindowText);
				rate2 = pfc::clip_t<t_float32>(rate2, -100.0, 50.0);
				//  pitch2 = round(pitch2);
				if (rate_s != sWindowText)
				{
					rate = rate2;
					float rate3 = (rate * 100) + 5000;
					slider_rate.SetPos((double)(rate3));
					OnConfigChanged();
				}
			}
			return 0;
		}


		void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {
			if (IsRateEnabled()) {
				GetConfig();
				dsp_preset_impl preset;
				dsp_rate::make_preset(rate, rate_enabled, preset);
				//yes change api;
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
			}
			else {
				static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pbrate);
			}

		}

		void OnScroll(UINT scrollID, int pos, CWindow window)
		{
			GetConfig();
			if (IsRateEnabled())
			{

				if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_rate.m_hWnd)
				{
					RateEnable(rate, rate_enabled);
				}
			}
		}

		void ApplySettings()
		{
			{
				dsp_preset_impl preset;
				if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset)) {
					SetRateEnabled(true);
					dsp_rate::parse_preset(rate, rate_enabled, preset);
					SetRateEnabled(rate_enabled);
					SetConfig();
				}
				else {
					SetRateEnabled(false);
					SetConfig();
				}
			}

		}

		void OnConfigChanged() {

			if (IsRateEnabled()) {
				RateEnable(rate, rate_enabled);
			}
			else {
				RateDisable();
			}
		}


		void GetConfig()
		{

			float rate_sl = slider_rate.GetPos() - 5000;
			rate = rate_sl / 100;
			rate_enabled = IsRateEnabled();
			RefreshRateLabel(rate);
		}

		void SetConfig()
		{
			slider_rate.SetPos((double)((rate * 100) + 5000));
			RefreshRateLabel(rate);

		}

		void RefreshRateLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "PB Rate: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			rate_s = sWindowText;
			rate_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "%";
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

		}

		static uint32_t parseConfig(ui_element_config::ptr cfg) {
			::ui_element_config_parser in(cfg);
			uint32_t flags; in >> flags;
			return 0;

		}
		static ui_element_config::ptr makeConfig(bool init = false) {
			ui_element_config_builder out;

			if (init)
			{
				uint32_t crap = 1;
				out << crap;
			}
			else
			{
				uint32_t crap = 2;
				out << crap;
			}
			return out.finish(g_get_guid());
		}

		BOOL OnInitDialog(CWindow hwnd, LPARAM) {
			rate_edit.AttachToDlgItem(m_hWnd);
			rate_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
			slider_rate = GetDlgItem(IDC_PITCH_UI);
			m_buttonRateEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
			slider_rate.SetRange(0, 15000);
			m_hooks.AddDialogWithControls(m_hWnd);
			ApplySettings();

			tempo_tag = GetDlgItem(IDC_TAG_EDIT);
			metadb_handle_ptr out;
			if (!static_api_ptr_t<playback_control>()->get_now_playing(out))
			{
				pfc::string_formatter msg;
				msg.reset();
				msg << "None";
				CString sWindowText;
				sWindowText = msg.c_str();
				tempo_tag.SetWindowText(sWindowText);

			}
			else
				on_playback_new_track(out);
			return FALSE;
		}

		uint32_t shit;

		float rate;
		bool rate_enabled;
		CTrackBarCtrl slider_rate;
		CButton m_buttonRateEnabled;
		bool m_ownRateUpdate;
		CEditMod rate_edit;
		CString rate_s;
	protected:
		const ui_element_instance_callback::ptr m_callback;


	};


	class uielem_pitch : public CDialogImpl<uielem_pitch>, public ui_element_instance, private play_callback_impl_base {
	public:
		uielem_pitch(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_callback(cb), m_resizer(chorus_uiresize, resizeMinMax) {
			pitch = 0.0; pitch_enabled = false;
		}
		enum { IDD = IDD_PITCH_ELEMENT };
		enum
		{
			pitchmin = 0,
			pitchmax = 2400,
			tempomax = 19000,

		};
		BEGIN_MSG_MAP_EX(uielem_pitch)
			CHAIN_MSG_MAP_MEMBER(m_resizer)
			MSG_WM_INITDIALOG(OnInitDialog)
			COMMAND_HANDLER_EX(IDC_PITCHENABLED_UI, BN_CLICKED, OnEnabledToggle)
			MESSAGE_HANDLER(WM_USER, OnEditControlChange)
			COMMAND_HANDLER(IDC_RESET, BN_CLICKED, OnReset);
		MSG_WM_HSCROLL(OnScroll)
		END_MSG_MAP()



		void initialize_window(HWND parent) {
			WIN32_OP(Create(parent) != NULL);
		}
		HWND get_wnd() { return m_hWnd; }
		void set_configuration(ui_element_config::ptr config) {
			shit = parseConfig(config);
			if (m_hWnd != NULL) {
				ApplySettings();
			}
			m_callback->on_min_max_info_change();
		}
		ui_element_config::ptr get_configuration() { return makeConfig(); }
		static GUID g_get_guid() {
			return guid_pitchelement;
		}
		static void g_get_name(pfc::string_base& out) { out = "Pitch"; }
		static ui_element_config::ptr g_get_default_configuration() {
			return makeConfig(true);
		}
		static const char* g_get_description() { return "Changes the pitch of the current track."; }
		static GUID g_get_subclass() {
			return ui_element_subclass_dsp;
		}

		ui_element_min_max_info get_min_max_info() {
			ui_element_min_max_info ret;

			// Note that we play nicely with separate horizontal & vertical DPI.
			// Such configurations have not been ever seen in circulation, but nothing stops us from supporting such.
			CSize DPI = QueryScreenDPIEx(*this);

			if (DPI.cx <= 0 || DPI.cy <= 0) { // sanity
				DPI = CSize(96, 96);
			}


			ret.m_min_width = MulDiv(200, DPI.cx, 96);
			ret.m_min_height = MulDiv(150, DPI.cy, 96);
			ret.m_max_width = MulDiv(1000, DPI.cx, 96);
			ret.m_max_height = MulDiv(1000, DPI.cy, 96);

			// Deal with WS_EX_STATICEDGE and alike that we might have picked from host
			ret.adjustForWindow(*this);

			return ret;
		}
	private:
		float tempo_tagval;
		CEdit tempo_tag;

		void on_playback_new_track(metadb_handle_ptr p_track) {
			ApplySettings();
			service_ptr_t<metadb_info_container> out;
			if (p_track->get_info_ref(out))
			{
				const file_info& file_inf = out->info();
				if (file_inf.meta_exists("pitch_amt"))
				{
					const char* meta = file_inf.meta_get("pitch_amt", 0);
					tempo_tagval = pfc::string_to_float(meta, strlen("pitch_amt"));
					pfc::string_formatter msg;
					msg.reset();
					msg << pfc::format_float(tempo_tagval, 0, 2);
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);

					msg.reset();
					msg << "pitch_amt tag:";
					msg << (tempo_tagval < 0 ? "-" : "+");
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
				else
				{
					tempo_tagval = 0.0;
					pfc::string_formatter msg;
					msg.reset();
					msg << "None";
					CString sWindowText;
					sWindowText = msg.c_str();
					tempo_tag.SetWindowText(sWindowText);
					msg.reset();
					msg << "No pitch_amt tag";
					::uSetDlgItemText(*this, IDC_PITCHTAG, msg);
				}
			}
		}


		CDialogResizeHelper m_resizer;
		fb2k::CCoreDarkModeHooks m_hooks;

		void SetPitchEnabled(bool state) { m_buttonPitchEnabled.SetCheck(state ? BST_CHECKED : BST_UNCHECKED); }
		bool IsPitchEnabled() { return m_buttonPitchEnabled == NULL || m_buttonPitchEnabled.GetCheck() == BST_CHECKED; }
		void PitchDisable() {
			static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pitch);
		}

		void PitchEnable(float pitch, bool enabled) {
			dsp_preset_impl preset;
			dsp_pitch::make_preset(pitch, enabled, preset);
			static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
		}

		void OnEnabledToggle(UINT uNotifyCode, int nID, CWindow wndCtl) {
			if (IsPitchEnabled()) {
				GetConfig();
				dsp_preset_impl preset;
				dsp_pitch::make_preset(pitch, pitch_enabled, preset);
				//yes change api;
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
			}
			else {
				static_api_ptr_t<dsp_config_manager>()->core_disable_dsp(guid_pitch);
			}

		}
		LRESULT OnEditControlChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			if (wParam == 0x1988)
			{
				CString pitchwindowtext;
				pitch_edit.GetWindowText(pitchwindowtext);
				float pitch2 = _ttof(pitchwindowtext);
				pitch2 = pfc::clip_t<t_float32>(pitch2, -12.0, 12.0);
				if (pitch_s != pitchwindowtext)
				{
					pitch = pitch2;
					float pitch3 = pitch * 100.00;
					slider_pitch.SetPos((double)(pitch3 + 1200));
					OnConfigChanged();
				}
			}
			return 0;
		}

		LRESULT OnReset(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			pitch = 0;
			float pitch3 = (pitch * 100) + 1200;
			slider_pitch.SetPos((double)(pitch3));
			SetConfig();
			OnConfigChanged();
			return 0;
		}

		void OnScroll(UINT scrollID, int pos, CWindow window)
		{
			GetConfig();
			if (IsPitchEnabled())
			{

				if ((LOWORD(scrollID) != SB_THUMBTRACK) && window.m_hWnd == slider_pitch.m_hWnd)
				{
					PitchEnable(pitch, pitch_enabled);
				}
			}
		}

		//set settings if from another control
		void ApplySettings()
		{
			{
				dsp_preset_impl preset;
				if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset)) {
					SetPitchEnabled(true);
					dsp_pitch::parse_preset(pitch, pitch_enabled, preset);
					SetPitchEnabled(pitch_enabled);
					SetConfig();
				}
				else {
					SetPitchEnabled(false);
					SetConfig();
				}
			}
		}

		void OnConfigChanged() {
			if (IsPitchEnabled()) {
				PitchEnable(pitch, pitch_enabled);
			}
			else {
				PitchDisable();
			}
		}


		void GetConfig()
		{
			float pitch_sl = slider_pitch.GetPos() - 1200;
			pitch = pitch_sl / 100.00;
			pitch_enabled = IsPitchEnabled();

			RefreshPitchLabel(pitch);
		}

		void SetConfig()
		{
			float pitch2 = pitch * 100.00;
			slider_pitch.SetPos((double)(pitch2 + 1200));

			RefreshPitchLabel(pitch2 / 100);

		}

		void RefreshPitchLabel(float  pitch)
		{
			pfc::string_formatter msg;
			msg << "Pitch: ";
			msg << (pitch < 0 ? "" : "+");
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI, msg);
			msg.reset();
			msg << pfc::format_float(pitch, 0, 2);
			CString sWindowText;
			sWindowText = msg.c_str();
			pitch_s = sWindowText;
			pitch_edit.SetWindowText(sWindowText);
			msg.reset();
			msg << "semitones";
			::uSetDlgItemText(*this, IDC_PITCHINFO_UI2, msg);

		}


		static uint32_t parseConfig(ui_element_config::ptr cfg) {
			::ui_element_config_parser in(cfg);
			uint32_t flags; in >> flags;
			return 0;

		}
		static ui_element_config::ptr makeConfig(bool init = false) {
			ui_element_config_builder out;

			if (init)
			{
				uint32_t crap = 1;
				out << crap;
			}
			else
			{
				uint32_t crap = 2;
				out << crap;
			}
			return out.finish(g_get_guid());
		}

		BOOL OnInitDialog(CWindow hwnd, LPARAM) {
			pitch_edit.AttachToDlgItem(m_hWnd);
			pitch_edit.SubclassWindow(GetDlgItem(IDC_PITCH_EDIT));
			slider_pitch = GetDlgItem(IDC_PITCH_UI);
			m_buttonPitchEnabled = GetDlgItem(IDC_PITCHENABLED_UI);
			slider_pitch.SetRange(0, pitchmax);
			m_ownPitchUpdate = false;

			m_hooks.AddDialogWithControls(m_hWnd);

			ApplySettings();

			tempo_tag = GetDlgItem(IDC_TAG_EDIT);
			metadb_handle_ptr out;
			if (!static_api_ptr_t<playback_control>()->get_now_playing(out))
			{
				pfc::string_formatter msg;
				msg.reset();
				msg << "None";
				CString sWindowText;
				sWindowText = msg.c_str();
				tempo_tag.SetWindowText(sWindowText);

			}
			else
				on_playback_new_track(out);
			return FALSE;
		}
		uint32_t shit;

		float  pitch;
		bool pitch_enabled;
		CTrackBarCtrl slider_pitch;
		CButton m_buttonPitchEnabled;
		bool m_ownPitchUpdate;
		CEditMod pitch_edit;
		CString pitch_s;
	protected:
		const ui_element_instance_callback::ptr m_callback;
	};

	class myElem2 : public  ui_element_impl_withpopup< uielem_pitch > {
		bool get_element_group(pfc::string_base& p_out)
		{
			p_out = "Effect DSP";
			return true;
		}
		bool get_menu_command_description(pfc::string_base& out) {
			out = "Opens a window for pitch control.";
			return true;
		}

		bool get_popup_specs(ui_size& defSize, pfc::string_base& title)
		{
			defSize = { 200,100 };
			title = "Pitch DSP";
			return true;
		}
	};

	class myElem3 : public  ui_element_impl_withpopup< uielem_tempo > {
		bool get_element_group(pfc::string_base& p_out)
		{
			p_out = "Effect DSP";
			return true;
		}
		bool get_menu_command_description(pfc::string_base& out) {
			out = "Opens a window for tempo control.";
			return true;
		}

		bool get_popup_specs(ui_size& defSize, pfc::string_base& title)
		{
			defSize = { 200,100 };
			title = "Tempo DSP";
			return true;
		}
	};

	class myElem4 : public  ui_element_impl_withpopup< uielem_rate > {
		bool get_element_group(pfc::string_base& p_out)
		{
			p_out = "Effect DSP";
			return true;
		}
		bool get_menu_command_description(pfc::string_base& out) {
			out = "Opens a window for playback rate control.";
			return true;
		}

		bool get_popup_specs(ui_size& defSize, pfc::string_base& title)
		{
			defSize = { 200,100 };
			title = "Playback Rate DSP";
			return true;
		}
	};

	class play_callback_st : public play_callback_static
	{
		float prev_pitch;
		bool  prev_pitchb;
		bool prev_bak = true;
		bool prev_bak2 = true;
		bool prev_bak3 = true;
		float prev_temp;
		bool prev_tempb;
		float prev_rate;
		bool prev_rateb;
		virtual void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {}
		virtual void on_playback_new_track(metadb_handle_ptr p_track)
		{
			service_ptr_t<metadb_info_container> out;
			dsp_preset_impl preset2;
			dsp_preset_impl preset3;
			dsp_preset_impl preset4;



			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset4)) {
				dsp_rate::parse_preset(prev_rate, prev_rateb, preset4);
			}


			if (p_track->get_info_ref(out))
			{
				const file_info& file_inf = out->info();


				if (file_inf.meta_exists("pitch_amt"))
				{
					if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset2)) {
						dsp_pitch::parse_preset(prev_pitch, prev_pitchb, preset2);
						prev_bak = true;
					}
					dsp_preset_impl preset;
					const char* meta = file_inf.meta_get("pitch_amt", 0);
					double pitch2 = pfc::string_to_float(meta, strlen("pitch_amt"));
					dsp_pitch::make_preset(pitch2, true, preset);
					static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
				}
				else
				{
					prev_bak = false;
				}

				if (file_inf.meta_exists("tempo_amt"))
				{
					if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_tempo, preset3)) {
						dsp_tempo::parse_preset(prev_temp, prev_tempb, preset3);
						prev_bak2 = true;
					}

					const char* meta = file_inf.meta_get("tempo_amt", 0);
					double pitch2 = pfc::string_to_float(meta, strlen("tempo_amt"));
					dsp_preset_impl preset;
					if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_tempo, preset)) {
						dsp_tempo::make_preset(pitch2, true, preset);
						static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
					}

				}
				else prev_bak2 = false;

				if (file_inf.meta_exists("pbrate_amt"))
				{
					if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset4)) {
						dsp_tempo::parse_preset(prev_rate, prev_rateb, preset4);
						prev_bak3 = true;
					}


					const char* meta = file_inf.meta_get("pbrate_amt", 0);
					double pitch2 = pfc::string_to_float(meta, strlen("pbrate_amt"));
					dsp_preset_impl preset;
					if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset)) {
						dsp_rate::make_preset(pitch2, true, preset);
						static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
					}

				}
				else prev_bak3 = false;
			}
		}
		virtual void on_playback_stop(play_control::t_stop_reason p_reason) {
			dsp_preset_impl preset2;
			dsp_preset_impl preset3;
			dsp_preset_impl preset4;

			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pitch, preset2) && prev_bak) {
				dsp_pitch::make_preset(prev_pitch, prev_pitchb, preset2);
				prev_bak = false;
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset2, dsp_config_manager::default_insert_last);
			}
			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_tempo, preset3) && prev_bak2) {
				prev_bak2 = false;
				dsp_tempo::make_preset(prev_temp, prev_tempb, preset3);
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset3, dsp_config_manager::default_insert_last);
			}
			if (static_api_ptr_t<dsp_config_manager>()->core_query_dsp(guid_pbrate, preset4) && prev_bak3) {
				prev_bak3 = false;
				dsp_rate::make_preset(prev_rate, prev_rateb, preset4);
				static_api_ptr_t<dsp_config_manager>()->core_enable_dsp(preset4, dsp_config_manager::default_insert_last);
			}


		}
		virtual void on_playback_seek(double p_time) {}
		virtual void on_playback_pause(bool p_state) {}
		virtual void on_playback_edited(metadb_handle_ptr p_track) {
		}
		virtual void on_playback_dynamic_info(const file_info& info) {}
		virtual void on_playback_dynamic_info_track(const file_info& info) {}
		virtual void on_playback_time(double p_time) {}
		virtual void on_volume_change(float p_new_val) {};

		virtual unsigned get_flags()
		{
			return flag_on_playback_new_track | flag_on_playback_stop
				;
		}
	};
	static play_callback_static_factory_t<play_callback_st> mirc_callback_factory;


	static service_factory_single_t<myElem2> g_myElemFactory2;
	static service_factory_single_t<myElem3> g_myElemFactory3;
	static service_factory_single_t<myElem4> g_myElemFactory4;

	static dsp_factory_t<dsp_tempo> g_dsp_tempo_factory;
	static dsp_factory_t<dsp_pitch> g_dsp_pitch_factory;
	static dsp_factory_t<dsp_rate> g_dsp_rate_factory;

}

