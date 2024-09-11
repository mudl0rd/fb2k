#include "../SDK/foobar2000.h"
#include "../../../3rdparty-deps/SoundTouch/SoundTouch.h"
#include "dsp_guids.h"
#define MYVERSION "0.52"

static pfc::string_formatter g_get_component_about()
{
	pfc::string_formatter about;
	about << "A special effect DSP array for foobar2000.\n\n";
	about << "Written by mudlord.\n";
	about << "\n";
	about << "Portions by Jon Watte, Jezar Wakefield.\n";
	about << "Using SoundTouch library version " << SOUNDTOUCH_VERSION << "\n";
	about << "SoundTouch (c) Olli Parviainen\n";
	about << "\n";
	return about;
}


DECLARE_COMPONENT_VERSION_COPY(
"Effect DSP",
MYVERSION,
g_get_component_about()
);
VALIDATE_COMPONENT_FILENAME("foo_dsp_effect.dll");
