// SPDX-License-Identifier: MIT

#ifndef DINPUT_H
#define DINPUT_H

#include "Wrapper.h"

#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_haptic.h>

#define DIRECTINPUT_VERSION 0x0500

typedef struct
{
	uint32_t a, b, c, d;
} GUID;
typedef uint32_t IID[4];

typedef struct
{
	uint32_t size;
	GUID guidInstance;
	GUID guidProduct;
	uint32_t devType;
	char tszInstanceName[MAX_PATH];
	char tszProductName[MAX_PATH];
#if (DIRECTINPUT_VERSION >= 0x0500)
	GUID guidFFDriver;
	uint16_t usagePage;
	uint16_t usage;
#endif
} DIDEVICEINSTANCEA;

typedef struct
{
	uint32_t dwSize;
	GUID guidType;
	uint32_t dwOfs;
	uint32_t dwType;
	uint32_t dwFlags;
	char tszName[MAX_PATH];
#if (DIRECTINPUT_VERSION >= 0x0500)
	uint32_t dwFFMaxForce;
	uint32_t dwFFForceResolution;
	uint16_t wCollectionNumber;
	uint16_t wDesignatorIndex;
	uint16_t wUsagePage;
	uint16_t wUsage;
	uint32_t dwDimension;
	uint16_t wExponent;
	uint16_t wReserved;
#endif
} DIDEVICEOBJECTINSTANCEA;

typedef struct
{
	const GUID *pguid;
	uint32_t dwOfs;
	uint32_t dwType;
	uint32_t dwFlags;
} DIOBJECTDATAFORMAT;

typedef struct
{
	uint32_t dwSize;
	uint32_t dwObjSize;
	uint32_t dwFlags;
	uint32_t dwDataSize;
	uint32_t dwNumObjs;
	DIOBJECTDATAFORMAT *rgodf;
} DIDATAFORMAT;

typedef struct
{
	uint32_t dwSize;
	uint32_t dwHeaderSize;
	uint32_t dwObj;
	uint32_t dwHow;
} DIPROPHEADER;
typedef struct
{
	DIPROPHEADER diph;
	uint32_t dwData;
} DIPROPDWORD;

typedef struct
{
	uint32_t size;
	uint32_t flags;
	uint32_t devType;
	uint32_t axes;
	uint32_t buttons;
	uint32_t nPOVs;
	uint32_t FFSamplePeriod;
	uint32_t FFMinTimeResolution;
	uint32_t firmwareRevision;
	uint32_t hardwareRevision;
	uint32_t FFDriverVersion;
} DIDEVCAPS;

typedef struct
{
	uint32_t dwOfs;
	uint32_t dwData;
	uint32_t dwTimeStamp;
	uint32_t dwSequence;
#if (DIRECTINPUT_VERSION >= 0x0800)
	uintptr_t uAppData;
#endif
} DIDEVICEOBJECTDATA;

typedef struct
{
	int32_t axes[8];
// 	uint32_t lX;
// 	uint32_t lY;
// 	uint32_t lZ;
// 	uint32_t lRx; //unsupported
// 	uint32_t lRy; //unsupported
// 	uint32_t lRz;
//	uint32_t rglSlider[2];
	uint32_t rgdwPOV[4]; //unsupported
	uint8_t  buttons[32]; //only 15 buttons
} DIJOYSTATE;

typedef struct
{
	uint32_t sSize;
	uint32_t attackLevel;
	uint32_t attackTime;
	uint32_t fadeLevel;
	uint32_t fadeTime;
} DIENVELOPE;

typedef struct
{
	int32_t magnitude;
} DICONSTANTFORCE;
typedef struct
{
	uint32_t magnitude;
	int32_t offset;
	uint32_t phase;
	uint32_t period;
} DIPERIODIC;
typedef struct
{
	int32_t offset;
	int32_t positiveCoefficient;
	int32_t negativeCoefficient;
	uint32_t positiveSaturation;
	uint32_t negativeSaturation;
	uint32_t deadBand;
} DICONDITION;

typedef struct
{
	uint32_t size;
	uint32_t flags;
	uint32_t duration;
	uint32_t samplePeriod;
	uint32_t gain;
	uint32_t triggerButton;
	uint32_t triggerRepeatInterval;
	uint32_t cAxes;
	/*
	 * The game builds this struct itself and passes it in, so every field has
	 * to sit where a 32-bit build put it. Native pointers here would be 8
	 * bytes each, shifting cbTypeSpecificParams and typeSpecificParams out of
	 * place and making the whole struct read as garbage.
	 */
	GameAddr rgdwAxes;             /* uint32_t * */
	GameAddr rglDirection;         /* uint32_t * */
	GameAddr envelope;             /* DIENVELOPE * */
	uint32_t cbTypeSpecificParams;
	GameAddr typeSpecificParams;   /* void * */
} DIEFFECT;
ASSERT_GAME_LAYOUT(DIEFFECT, 52);

typedef void DIEFFESCAPE;
typedef void DIEFFECTINFOA;

typedef BOOL (STDCALL *DIENUMDEVICESCALLBACKA)(const DIDEVICEINSTANCEA *, void *);

typedef struct
{
	uint32_t ref;
	BOOL is_device;
} DirectInputObject;

/*
 * COM-style method slots.
 *
 * The game reaches these by indexing the object at fixed BYTE offsets --
 * `call(to32i(edx + 0xC))` means "the fourth method" only if every slot is 4
 * bytes wide. A native function pointer is 8 bytes in a 64-bit build, which
 * makes offset 0xC land in the middle of the second slot and call address
 * zero. So the slots hold GameAddr and the original prototype is kept beside
 * each one as a comment.
 *
 * Safe because the wrapper only ever fills these in; it never calls through
 * them itself, and every function it stores lives in .text below 2 GiB (the
 * build is -no-pie, see LowMem.h).
 */
typedef struct DirectInputEffect
{
	/*** IUnknown methods ***/
	GameAddr QueryInterface;	/* uint32_t STDCALL (void **this, const IID *const riid, void **object) */
	GameAddr AddRef;	/* uint32_t STDCALL (void **this) */
	GameAddr Release;	/* uint32_t STDCALL (void **this) */
	/*** IDirectInputEffect methods ***/
	GameAddr Initialize;	/* uint32_t STDCALL (struct DirectInputEffect **this, void *hInstance, uint32_t, GUID *) */
	GameAddr GetEffectGuid;	/* uint32_t STDCALL (struct DirectInputEffect **this, const GUID *const) */
	GameAddr GetParameters;	/* uint32_t STDCALL (struct DirectInputEffect **this, DIEFFECT *, uint32_t) */
	GameAddr SetParameters;	/* uint32_t STDCALL (struct DirectInputEffect **this, const DIEFFECT *eff, uint32_t flags) */
	GameAddr Start;	/* uint32_t STDCALL (struct DirectInputEffect **this, uint32_t iterations, uint32_t flags) */
	GameAddr Stop;	/* uint32_t STDCALL (struct DirectInputEffect **this) */
	GameAddr GetEffectStatus;	/* uint32_t STDCALL (struct DirectInputEffect **this, uint32_t *) */
	GameAddr Download;	/* uint32_t STDCALL (struct DirectInputEffect **this) */
	GameAddr Unload;	/* uint32_t STDCALL (struct DirectInputEffect **this) */
	GameAddr Escape;	/* uint32_t STDCALL (struct DirectInputEffect **this, DIEFFESCAPE *) */
	/* My variables */
	GUID guid;

	SDL_HapticEffect effect;
	BOOL playing;
	uint8_t *gain;

	// Rumble
	SDL_Joystick *joy;

	// Haptic
	SDL_Haptic *haptic;
	int32_t effect_idx;
	BOOL useCartesian;
	int16_t constantToSineDivider;
} DirectInputEffect;

typedef struct DirectInputDevice
{
	/*** IUnknown methods ***/
	GameAddr QueryInterface;	/* uint32_t STDCALL (void **this, const IID *const riid, void **object) */
	GameAddr AddRef;	/* uint32_t STDCALL (void **this) */
	GameAddr Release;	/* uint32_t STDCALL (void **this) */
	/*** IDirectInputDeviceA methods ***/
	GameAddr GetCapabilities;	/* uint32_t STDCALL (struct DirectInputDevice **this, DIDEVCAPS *devCaps) */
	GameAddr EnumObjects;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *callback, void *ref, uint32_t dwFlags) */
	GameAddr GetProperty;	/* uint32_t STDCALL (struct DirectInputDevice **this, const GUID *const rguidProp, DIPROPHEADER *pdiph) */
	GameAddr SetProperty;	/* uint32_t STDCALL (struct DirectInputDevice **this, const GUID *const rguidProp, const DIPROPHEADER *pdiph) */
	GameAddr Acquire;	/* uint32_t STDCALL (struct DirectInputDevice **this) */
	GameAddr Unacquire;	/* uint32_t STDCALL (struct DirectInputDevice **this) */
	GameAddr GetDeviceState;	/* uint32_t STDCALL (struct DirectInputDevice **this, uint32_t cbData, void *data) */
	GameAddr GetDeviceData;	/* uint32_t STDCALL (struct DirectInputDevice **this, uint32_t cbObjectData, DIDEVICEOBJECTDATA *rgdod, uint32_t *pdwInOut, uint32_t dwFlags) */
	GameAddr SetDataFormat;	/* uint32_t STDCALL (struct DirectInputDevice **this, const DIDATAFORMAT *df) */
	GameAddr SetEventNotification;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *hEvent) */
	GameAddr SetCooperativeLevel;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *hwnd, uint32_t dwFlags) */
	GameAddr GetObjectInfo;	/* uint32_t STDCALL (struct DirectInputDevice **this, DIDEVICEOBJECTINSTANCEA *pdidoi, uint32_t dwObj, uint32_t dwHow) */
	GameAddr GetDeviceInfo;	/* uint32_t STDCALL (struct DirectInputDevice **this, DIDEVICEINSTANCEA *pdidi) */
	GameAddr RunControlPanel;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *hwndOwner, uint32_t dwFlags) */
	GameAddr Initialize;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *hinst, uint32_t dwVersion, const GUID *const rguid) */
	/*** IDirectInputDevice2A methods ***/
	GameAddr CreateEffect;	/* uint32_t STDCALL (struct DirectInputDevice **this, const GUID *const rguid, const DIEFFECT *eff, DirectInputEffect ***deff, void *punkOuter) */
	GameAddr EnumEffects;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *callback, void *pvRef, uint32_t effType) */
	GameAddr GetEffectInfo;	/* uint32_t STDCALL (struct DirectInputDevice **this, DIEFFECTINFOA *pdei, const GUID *const rguid) */
	GameAddr GetForceFeedbackState;	/* uint32_t STDCALL (struct DirectInputDevice **this, uint32_t *out) */
	GameAddr SendForceFeedbackCommand;	/* uint32_t STDCALL (struct DirectInputDevice **this, uint32_t flags) */
	GameAddr EnumCreatedEffectObjects;	/* uint32_t STDCALL (struct DirectInputDevice **this, void *callback, void *pvRef, uint32_t fl) */
	GameAddr Escape;	/* uint32_t STDCALL (struct DirectInputDevice **this, DIEFFESCAPE *pesc) */
	GameAddr Poll;	/* uint32_t STDCALL (struct DirectInputDevice **this ) */
	GameAddr SendDeviceData;	/* uint32_t STDCALL (struct DirectInputDevice **this, uint32_t cbObjectData, const DIDEVICEOBJECTDATA *rgdod, uint32_t *inOut, uint32_t fl) */
	/* My variables */
	GUID guid;
	uint32_t lastX, lastY;
	uint8_t escPressed, resetPressed, dpadPressed[4];
	SDL_Joystick *joy;
	BOOL rumble;
	BOOL useCartesian;
	uint8_t gain;
	SDL_Haptic *haptic;
	int32_t num_effects;
	DirectInputEffect **effects;
} DirectInputDevice;

typedef struct
{
	/*** IUnknown methods ***/
	GameAddr QueryInterface;	/* uint32_t STDCALL (void **this, const IID *const riid, void **object) */
	GameAddr AddRef;	/* uint32_t STDCALL (void **this) */
	GameAddr Release;	/* uint32_t STDCALL (void **this) */
	/*** IDirectInputA methods ***/
	GameAddr CreateDevice;	/* uint32_t STDCALL (void **this, const GUID *const rguid, DirectInputDevice ***directInputDevice, void *unkOuter) */
	GameAddr EnumDevices;	/* uint32_t STDCALL (void **this, uint32_t devType, DIENUMDEVICESCALLBACKA callback, void *ref, uint32_t dwFlags) */
	GameAddr GetDeviceStatus;	/* uint32_t STDCALL (void **this, const GUID *const rguidInstance) */
	GameAddr RunControlPanel;	/* uint32_t STDCALL (void **this, void *hwndOwner, uint32_t dwFlags) */
	GameAddr Initialize;	/* uint32_t STDCALL (void **this, void *hInstance, uint32_t dwVersion) */
} DirectInput;

#endif // DINPUT_H
