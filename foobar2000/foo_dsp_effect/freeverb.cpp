#include "freeverb.h"
#include "../SDK/foobar2000.h"

comb::comb() {
	filterstore = 0;
	bufidx = 0;
}

void comb::setbuffer(audio_sample *buf, int size) {
	buffer = buf;
	bufsize = size;
}

void comb::mute() {
	for (int i = 0; i < bufsize; i++)
		buffer[i] = 0;
}

void comb::setdamp(float val) {
	damp1 = val;
	damp2 = 1 - val;
}

float comb::getdamp() {
	return damp1;
}

void comb::setfeedback(float val) {
	feedback = val;
}

float comb::getfeedback() {
	return feedback;
}



allpass::allpass() {
	bufidx = 0;
}

void allpass::setbuffer(audio_sample *buf, int size) {
	buffer = buf;
	bufsize = size;
}

void allpass::mute() {
	for (int i = 0; i < bufsize; i++)
		buffer[i] = 0;
}

void allpass::setfeedback(float val) {
	feedback = val;
}

float allpass::getfeedback() {
	return feedback;
}



revmodel::revmodel() {
		bufcomb = NULL;
		bufallpass = NULL;
	
}

revmodel::~revmodel()
{
	if (bufcomb) {
		for (int c = 0; c < num_comb; ++c)
		{
			delete[] bufcomb[c];
			bufcomb[c] = NULL;
		}
		delete[] bufcomb;
		bufcomb = NULL;
	}

	if (bufallpass) {
		for (int c = 0; c < num_allpass; ++c)
		{
			delete[] bufallpass[c];
			bufallpass[c] = NULL;
		}
		delete[] bufallpass;
		bufallpass = NULL;
	}
}

void revmodel::init(int srate,bool stereo)
{
	static const int comb_lengths[8] = { 1116,1188,1277,1356,1422,1491,1557,1617 };
	static const int allpass_lengths[4] = { 225,341,441,556 };
	int stereosep = stereo ? 23 : 0;
	double r = (srate * (1.0 / 44100.0));
	if (bufcomb) {
		for (int c = 0; c < num_comb; ++c)
		{
			delete[] bufcomb[c];
			bufcomb[c] = NULL;
		}
		delete[] bufcomb;
		bufcomb = NULL;
	}

   bufcomb= new audio_sample *[num_comb];
   for (int c = 0; c < num_comb; c++)
   {
	   int sz = (size_t)(r * (comb_lengths[c] + stereosep) + .5);
	   bufcomb[c] = new audio_sample[sz]();
	   combL[c].setbuffer(bufcomb[c], sz);
	   combL[c].setfeedback(0.5f);
   }

   if (bufallpass) {
	   for (int a = 0; a < num_allpass; ++a)
	   {
		   delete[] bufallpass[a];
		   bufallpass[a] = NULL;
	   }
	   delete []bufallpass;
	   bufallpass = NULL;
   }
   bufallpass = new audio_sample *[num_allpass];
   for (int a = 0;a< num_allpass; a++)
   {
	   int sz = (size_t)(r * (allpass_lengths[a] + stereosep) + .5);
	   bufallpass[a] = new audio_sample[sz]();
	   allpassL[a].setbuffer(bufallpass[a], sz);
	   allpassL[a].setfeedback(0.5f);
   }
	setwet(initialwet);
	setroomsize(initialroom);
	setdry(initialdry);
	setdamp(initialdamp);
	setwidth(initialwidth);
	setmode(initialmode);
	
}

void revmodel::mute() {
	int i;

	if (getmode() >= freezemode)
		return;

	for (i = 0; i < numcombs; i++) {
		combL[i].mute();
	}

	for (i = 0; i < numallpasses; i++) {
		allpassL[i].mute();
	}
}

audio_sample revmodel::processsample(audio_sample in)
{
	audio_sample samp = in;
	audio_sample mono_out = 0.0f;
	audio_sample mono_in = samp;
	audio_sample input = (mono_in)*gain;
	for (int i = 0; i < numcombs; i++)
	{
		mono_out += combL[i].process(input);
	}
	for (int i = 0; i < numallpasses; i++)
	{
		mono_out = allpassL[i].process(mono_out);
	}
	samp = mono_in * dry + mono_out * wet1;
	return samp;
}

void revmodel::update() {
	int i;
	wet1 = wet * (width / 2 + 0.5f);

	if (mode >= freezemode) {
		roomsize1 = 1;
		damp1 = 0;
		gain = muted;
	} else {
		roomsize1 = roomsize;
		damp1 = damp;
		gain = fixedgain;
	}

	for (i = 0; i < numcombs; i++) {
		combL[i].setfeedback(roomsize1);
	}

	for (i = 0; i < numcombs; i++) {
		combL[i].setdamp(damp1);
	}
}

void revmodel::setroomsize(float value) {
	roomsize = (value * scaleroom) + offsetroom;
	update();
}

float revmodel::getroomsize() {
	return (roomsize - offsetroom) / scaleroom;
}

void revmodel::setdamp(float value) {
	damp = value * scaledamp;
	update();
}

float revmodel::getdamp() {
	return damp / scaledamp;
}

void revmodel::setwet(float value) {
	wet = value * scalewet;
	update();
}

float revmodel::getwet() {
	return wet / scalewet;
}

void revmodel::setdry(float value) {
	dry = value * scaledry;
}

float revmodel::getdry() {
	return dry / scaledry;
}

void revmodel::setwidth(float value) {
	width = value;
	update();
}

float revmodel::getwidth() {
	return width;
}

void revmodel::setmode(float value) {
	mode = value;
	update();
}

float revmodel::getmode() {
	if (mode >= freezemode)
		return 1;
	else
		return 0;
}
