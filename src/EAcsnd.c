// SPDX-License-Identifier: MIT

#include "Wrapper.h"
#include "LowMem.h"

extern BOOL linearSoundInterpolation;

static void (REGPARM *getSamples)(void *samples, uint32_t num_samples_per_chn);

#ifdef NFS_CPP
	void wrap_regparm2(void *this, void *func, int32_t arg0, int32_t arg1);
	extern void *audio_game_thread;

	/*
	 * The game takes the sample buffer in a 32-bit register, so the truncation
	 * is deliberate -- every buffer passed here comes from LowMem and fits.
	 * Spelling it out keeps the build quiet, so that an accidental truncation
	 * somewhere else still shows up as a warning instead of being lost in the
	 * noise.
	 */
	#define getSamplesFunc(a,b) \
		wrap_regparm2(audio_game_thread, getSamples, GAME_ADDR(a), b)
#else
	#define getSamplesFunc(a,b) \
		getSamples(a, b)
#endif

typedef void (*FadeInOut)(MAYBE_THIS_SINGLE);
static FadeInOut fadeInOut;

#include <SDL2/SDL_audio.h>

#define CHN_CNT 2

static SDL_AudioDeviceID audioDevice;
static BOOL unPaused, canGetSamples;
static uint32_t buffer_pos;
static uint8_t *buffer;
/*
 * The game writes samples through a pointer we hand it, and it keeps that
 * pointer in a 32-bit register. SDL's stream buffer, and anything on the C
 * stack, live far above 2 GiB on a 64-bit host, so neither can be given to it
 * directly -- the address arrives truncated and the game scribbles on whatever
 * happens to be at the low half. Everything the game fills in goes through
 * this instead, and is copied out afterwards.
 */
#define GAME_SCRATCH_SAMPLES 256
static uint8_t *gameScratch;

static uint8_t *getGameScratch(void)
{
	if (!gameScratch)
		gameScratch = (uint8_t *)lowMemAlloc(GAME_SCRATCH_SAMPLES * CHN_CNT * sizeof(int16_t));
	return gameScratch;
}

static void audioCallback(void *userdata, uint8_t *stream, int32_t len)
{
	if (!buffer)
	{
		int32_t i;
		uint8_t *scratch = getGameScratch();
		const int32_t chunk = 256 * CHN_CNT * sizeof(int16_t);
		for (i = 0; i < len; i += chunk)
		{
			int32_t remaining = len - i;
			getSamplesFunc(scratch, 256);
			memcpy(stream + i, scratch, remaining < chunk ? remaining : chunk);
		}
	}
	else
	{
		while (buffer_pos < len)
		{
			getSamplesFunc(buffer + buffer_pos, 256);
			buffer_pos += 256 * CHN_CNT * sizeof(int16_t);
		}
		memcpy(stream, buffer, len);
		memcpy(buffer, buffer + len, buffer_pos -= len);
	}
}
static void audioCallbackInterp(void *userdata, uint8_t *stream, int32_t len)
{
	int16_t *samples = (int16_t *)getGameScratch();
	int16_t *buffer_16b;
	uint32_t i, c;
	while (buffer_pos < len)
	{
		buffer_16b = (int16_t *)(buffer + buffer_pos);
		getSamplesFunc(samples, 256);
		for (i = 0; i < (256 - 1) * CHN_CNT; i += CHN_CNT)
		{
			for (c = 0; c < CHN_CNT; ++c)
			{
				buffer_16b[c] = samples[i + c];
				buffer_16b[c + CHN_CNT] = (samples[i + c] + samples[i + c + CHN_CNT]) >> 1;
			}
			buffer_16b += CHN_CNT << 1;
		}
		for (c = 0; c < CHN_CNT; ++c)
			buffer_16b[c] = buffer_16b[c + CHN_CNT] = samples[i + c];
		buffer_pos += 512 * CHN_CNT * sizeof(int16_t);
	}
	memcpy(stream, buffer, len);
	memcpy(buffer, buffer + len, buffer_pos -= len);
}

/**/

REALIGN uint32_t iSNDdllversion_(void)
{
	return 0x60002;
}

REALIGN STDCALL uint32_t iSNDdirectsetfunctions_wrap(void (REGPARM *arg1)(), void (*arg2)(), void (*arg3)(), FadeInOut arg4, void (*arg5)())
{
	getSamples = arg1;
	fadeInOut  = arg4;
	return 0;
}
REALIGN REGPARM uint32_t iSNDdirectcaps_(GameAddr hWnd)
{
	return 0x23E0F; //?
}
REALIGN REGPARM uint32_t iSNDdirectstart_(uint32_t arg1, GameAddr hWnd)
{
	if (canGetSamples)
		return 0;

	SDL_AudioSpec audioSpecIn =
	{
		linearSoundInterpolation ? 44100 : 22050,
		AUDIO_S16,
		CHN_CNT,
		0,
		1024,
		0,
		0,
		linearSoundInterpolation ? audioCallbackInterp : audioCallback,
		NULL
	};
	SDL_AudioSpec audioSpecOut;
	audioDevice = SDL_OpenAudioDevice(NULL, 0, &audioSpecIn, &audioSpecOut, 0);
	if (!audioDevice)
		buffer = (uint8_t *)lowMemAlloc(256 * CHN_CNT * sizeof(int16_t));
	else
	{
		uint32_t bufferSize = (audioSpecOut.samples + 255) & ~255; //Aligned to 256
		if (linearSoundInterpolation || bufferSize != audioSpecOut.samples)
		{
			bufferSize += linearSoundInterpolation ? 512 : 256;
			bufferSize *= CHN_CNT * sizeof(int16_t);
			buffer = (uint8_t *)lowMemAlloc(bufferSize);
		}
	}
	canGetSamples = true;
	return 0;
}
REALIGN void iSNDdirectserve_(MAYBE_THIS_SINGLE)
{
	if (canGetSamples)
	{
		if (!unPaused && audioDevice)
		{
			SDL_PauseAudioDevice(audioDevice, 0);
			unPaused = true;
		}
#ifdef NFS_CPP
		fadeInOut(this);
#else
		fadeInOut();
#endif
		if (!audioDevice)
			getSamplesFunc(buffer, 256);
	}
}
REALIGN uint32_t iSNDdirectstop_(void)
{
	canGetSamples = false;
	if (audioDevice)
	{
		SDL_CloseAudioDevice(audioDevice);
		unPaused = false;
		audioDevice = 0;
	}
	buffer_pos = 0;
	lowMemFree(buffer);
	buffer = NULL;
	return 0;
}
