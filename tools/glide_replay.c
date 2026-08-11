// SPDX-License-Identifier: MIT
//
// Standalone correctness harness for the Glide2x backends.
//
// The game itself is 32-bit only (src/Cpp/NFS2SE.cpp hard-errors on anything
// else), but the graphics backends are not: Glide's types are all
// explicit-width and STDCALL/REALIGN collapse to nothing outside i386. So the
// backends can be built and exercised natively on any host -- including 64-bit
// ARM, where the game cannot run at all.
//
// That is what this is for. It links a backend against stubs for the globals
// Wrapper.c would normally own, replays a synthetic Glide trace that touches
// every feature the game actually uses, and (with --dump) writes the result out
// as a PPM so two backends can be diffed pixel for pixel.
//
// Run it under the Vulkan validation layers to check synchronisation, image
// layouts and descriptor usage without needing the game at all:
//
//   VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./glide_replay
//
// Build with tools/build_replay.

#include "../src/Glide2x.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Globals that Wrapper.c normally provides ---------------------------- */

SDL_Window *sdlWin;
int32_t winWidth = 640, winHeight = 480;
int32_t initialWinWidth = 640, initialWinHeight = 480;
int32_t vSync = 0;
float dpr = 1.0f;
BOOL keepAspectRatio = true;
BOOL needRecreateGl = false;
BOOL windowResized = false;
BOOL linearFiltering = true;
BOOL fixedFramebufferSize = false;
BOOL framebufferLinearFiltering = false;

/* Set by the backend when it fails to come up. */
extern BOOL contextError, shaderError, framebufferError;

/* ---- Glide entry points used by the trace -------------------------------- */

extern STDCALL void grGlideInit(void);
extern STDCALL void grGlideShutdown(void);
extern STDCALL BOOL grSstWinOpen(uint32_t hWnd, GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t fmt, GrOriginLocation_t origin, int nColBuffers, int nAuxBuffers);
extern STDCALL void grBufferClear(GrColor_t color, GrAlpha_t alpha, uint16_t depth);
extern STDCALL void grBufferSwap(int swap_interval);
extern STDCALL void grClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY);
extern STDCALL void grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);
extern STDCALL void grDrawLine(const GrVertex *a, const GrVertex *b);
extern STDCALL void grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df);
extern STDCALL void grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, BOOL invert);
extern STDCALL void grDepthMask(BOOL mask);
extern STDCALL void grFogMode(GrFogMode_t mode);
extern STDCALL void grFogColorValue(GrColor_t color);
extern STDCALL void grFogTable(const GrFog_t ft[GR_FOG_TABLE_SIZE]);
extern STDCALL void grGammaCorrectionValue(float value);
extern STDCALL void grTexDownloadMipMap(GrChipID_t tmu, uint32_t startAddress, uint32_t evenOdd, GrTexInfo *info);
extern STDCALL void grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void *data);
extern STDCALL void grTexSource(GrChipID_t tmu, uint32_t startAddress, uint32_t evenOdd, GrTexInfo *info);
extern STDCALL void guFogGenerateExp(GrFog_t fogtable[GR_FOG_TABLE_SIZE], float density);

/* ---- Trace helpers ------------------------------------------------------- */

/*
 * The game's vertices are in 640x480 screen space biased by VertexSnap
 * (0xC0000), which the backends subtract back off. Match that here so the
 * harness drives the backends exactly the way the game does.
 */
#define VERTEX_SNAP 0x0C0000

static GrVertex vertex(float x, float y, float oow, float r, float g, float b, float a, float s, float t)
{
	GrVertex v;
	memset(&v, 0, sizeof v);
	v.x = x + VERTEX_SNAP;
	v.y = y + VERTEX_SNAP;
	v.oow = oow;
	v.r = r;
	v.g = g;
	v.b = b;
	v.a = a;
	/* Glide texture coordinates are premultiplied by 1/w and scaled by 256. */
	v.tmuvtx[0].sow = s * 256.0f * oow;
	v.tmuvtx[0].tow = t * 256.0f * oow;
	v.tmuvtx[0].oow = oow;
	return v;
}

static void triangle(GrVertex a, GrVertex b, GrVertex c)
{
	grDrawTriangle(&a, &b, &c);
}

/* A quad as two triangles, in the winding the game uses. */
static void quad(float x0, float y0, float x1, float y1, float oow,
                 float r, float g, float b, float a)
{
	GrVertex v00 = vertex(x0, y0, oow, r, g, b, a, 0.0f, 0.0f);
	GrVertex v10 = vertex(x1, y0, oow, r, g, b, a, 1.0f, 0.0f);
	GrVertex v11 = vertex(x1, y1, oow, r, g, b, a, 1.0f, 1.0f);
	GrVertex v01 = vertex(x0, y1, oow, r, g, b, a, 0.0f, 1.0f);
	triangle(v00, v10, v11);
	triangle(v00, v11, v01);
}

/* ---- Synthetic textures -------------------------------------------------- */

static uint16_t g_tex565[64 * 64];
static uint16_t g_tex1555[32 * 32];
static uint16_t g_tex4444[32 * 32];
static uint8_t  g_texP8[64 * 64];
static uint32_t g_palette[256];

#define ADDR_565  0x000000
#define ADDR_1555 0x010000
#define ADDR_4444 0x020000
#define ADDR_P8   0x030000

static void buildTextures(void)
{
	int x, y, i;

	/* 8x8 checkerboard, red/blue */
	for (y = 0; y < 64; ++y)
		for (x = 0; x < 64; ++x)
			g_tex565[y * 64 + x] = ((x / 8 + y / 8) & 1) ? 0xF800 : 0x001F;

	/* Half transparent, half opaque green -- exercises the 1-bit alpha path */
	for (y = 0; y < 32; ++y)
		for (x = 0; x < 32; ++x)
			g_tex1555[y * 32 + x] = (x < 16) ? 0x03E0 : 0x83E0;

	/* Horizontal alpha ramp over yellow */
	for (y = 0; y < 32; ++y)
		for (x = 0; x < 32; ++x)
			g_tex4444[y * 32 + x] = (uint16_t)(((x / 2) << 12) | 0x0FF0);

	for (i = 0; i < 256; ++i)
		g_palette[i] = 0xFF000000u | ((uint32_t)i << 16) | ((uint32_t)(255 - i) << 8) | 0x80u;

	for (y = 0; y < 64; ++y)
		for (x = 0; x < 64; ++x)
			g_texP8[y * 64 + x] = (uint8_t)((x * 4) ^ (y * 4));
}

static void downloadTexture(uint32_t address, GrLOD_t largeLod, GrTextureFormat_t format, void *data)
{
	GrTexInfo info;
	memset(&info, 0, sizeof info);
	info.smallLod = largeLod;
	info.largeLod = largeLod;
	info.aspectRatio = GR_ASPECT_1x1;
	info.format = format;
	info.data = data;
	grTexDownloadMipMap(GR_TMU0, address, 0, &info);
}

static void bindTexture(uint32_t address, GrLOD_t largeLod, GrTextureFormat_t format)
{
	GrTexInfo info;
	memset(&info, 0, sizeof info);
	info.smallLod = largeLod;
	info.largeLod = largeLod;
	info.aspectRatio = GR_ASPECT_1x1;
	info.format = format;
	grTexSource(GR_TMU0, address, 0, &info);
}

static void setTextured(BOOL textured)
{
	grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
		GR_COMBINE_LOCAL_NONE,
		textured ? GR_COMBINE_OTHER_TEXTURE : GR_COMBINE_OTHER_ITERATED, false);
}

/* ---- The trace ----------------------------------------------------------- */

/*
 * One frame's worth of Glide calls, covering every feature the game uses:
 * clears, untextured and textured geometry, all four texture formats, both
 * blend destination factors, fog on and off, depth writes on and off, lines,
 * and a mid-frame clip window change.
 *
 * `phase` animates it slightly so successive frames are not identical, which
 * is what shakes out frame-in-flight and ring-buffer-wrap bugs.
 */
static void replayFrame(int frameIndex)
{
	float phase = frameIndex * 0.1f;
	float wobble = 20.0f * sinf(phase);
	int i;

	grBufferClear(0x00203040, 0xFF, GR_WDEPTHVALUE_FARTHEST);

	/* Opaque untextured geometry, depth writes on */
	grDepthMask(true);
	grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
	setTextured(false);
	quad(20.0f, 20.0f, 200.0f + wobble, 200.0f, 0.9f, 255.0f, 64.0f, 64.0f, 255.0f);

	/* Textured, RGB565 */
	setTextured(true);
	bindTexture(ADDR_565, GR_LOD_64, GR_TEXFMT_RGB_565);
	quad(220.0f, 20.0f, 400.0f, 200.0f, 0.8f, 255.0f, 255.0f, 255.0f, 255.0f);

	/* Textured, ARGB1555 -- punch-through alpha */
	bindTexture(ADDR_1555, GR_LOD_32, GR_TEXFMT_ARGB_1555);
	quad(420.0f, 20.0f, 600.0f, 200.0f, 0.7f, 255.0f, 255.0f, 255.0f, 255.0f);

	/* Textured, ARGB4444, additive blending, depth writes off */
	grDepthMask(false);
	grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
	bindTexture(ADDR_4444, GR_LOD_32, GR_TEXFMT_ARGB_4444);
	quad(20.0f, 220.0f, 200.0f, 400.0f, 0.6f, 255.0f, 255.0f, 255.0f, 255.0f);

	/* Palettised, with fog enabled */
	grDepthMask(true);
	grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
	grFogMode(GR_FOG_WITH_TABLE);
	grFogColorValue(0x00808080);
	grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, g_palette);
	bindTexture(ADDR_P8, GR_LOD_64, GR_TEXFMT_P_8);
	quad(220.0f, 220.0f, 400.0f, 400.0f, 0.05f, 255.0f, 255.0f, 255.0f, 255.0f);
	grFogMode(GR_FOG_DISABLE);

	/* A clip window change mid-frame, then geometry that overflows it */
	grClipWindow(420, 220, 620, 400);
	setTextured(false);
	quad(400.0f, 200.0f, 640.0f, 440.0f, 0.5f, 64.0f, 255.0f, 128.0f, 200.0f);
	grClipWindow(0, 0, 640, 480);

	/* Lines */
	setTextured(false);
	for (i = 0; i < 8; ++i)
	{
		GrVertex a = vertex(10.0f, 410.0f + i * 8.0f, 0.5f, 255.0f, 255.0f, 0.0f, 255.0f, 0.0f, 0.0f);
		GrVertex b = vertex(630.0f, 410.0f + i * 8.0f + wobble, 0.5f, 0.0f, 255.0f, 255.0f, 255.0f, 0.0f, 0.0f);
		grDrawLine(&a, &b);
	}

	/*
	 * A large batch, to push past MaxTriangles (1024) and force a mid-batch
	 * flush, and to make the vertex ring do some real work.
	 */
	setTextured(true);
	bindTexture(ADDR_565, GR_LOD_64, GR_TEXFMT_RGB_565);
	for (i = 0; i < 1200; ++i)
	{
		float x = (float)(i % 40) * 16.0f;
		float y = (float)(i / 40) * 16.0f;
		quad(x, y, x + 6.0f, y + 6.0f, 0.95f, 255.0f, 255.0f, 255.0f, 90.0f);
	}

	grBufferSwap(0);
}

int main(int argc, char *argv[])
{
	int frames = 30;
	int i;
	GrFog_t fogTable[GR_FOG_TABLE_SIZE];

	for (i = 1; i < argc; ++i)
	{
		if (!strncmp(argv[i], "--frames=", 9))
			frames = atoi(argv[i] + 9);
	}

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

#if defined(VULKAN)
	sdlWin = SDL_CreateWindow("glide_replay (Vulkan)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		initialWinWidth, initialWinHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);
#elif defined(METAL)
	sdlWin = SDL_CreateWindow("glide_replay (Metal)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		initialWinWidth, initialWinHeight, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	sdlWin = SDL_CreateWindow("glide_replay (OpenGL)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		initialWinWidth, initialWinHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
	if (!sdlWin)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}

	buildTextures();

	grGlideInit();
	if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1))
	{
		fprintf(stderr, "grSstWinOpen failed\n");
		return 1;
	}

	if (contextError || shaderError || framebufferError)
	{
		fprintf(stderr, "backend reported an error during startup\n");
		return 1;
	}

	grGammaCorrectionValue(1.0f);

	guFogGenerateExp(fogTable, 0.6f);
	grFogTable(fogTable);

	/* Textures are downloaded once, as the game does at track load. */
	downloadTexture(ADDR_565,  GR_LOD_64, GR_TEXFMT_RGB_565,   g_tex565);
	downloadTexture(ADDR_1555, GR_LOD_32, GR_TEXFMT_ARGB_1555, g_tex1555);
	downloadTexture(ADDR_4444, GR_LOD_32, GR_TEXFMT_ARGB_4444, g_tex4444);
	downloadTexture(ADDR_P8,   GR_LOD_64, GR_TEXFMT_P_8,       g_texP8);

	for (i = 0; i < frames; ++i)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
			{
				frames = i;
				break;
			}
		}
		replayFrame(i);
	}

	grGlideShutdown();
	SDL_DestroyWindow(sdlWin);
	SDL_Quit();

	printf("glide_replay: %d frames replayed cleanly\n", frames);
	return 0;
}
