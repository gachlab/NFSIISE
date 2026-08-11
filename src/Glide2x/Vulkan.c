// SPDX-License-Identifier: MIT
//
// Vulkan backend for the Glide2x wrapper.
//
// This is a port of Glide2x/OpenGL2.c and deliberately mirrors its structure so
// the two can be compared side by side (and diffed pixel for pixel with
// tools/glide_replay). The Glide feature set the game actually uses is tiny:
// one vertex format, two shader programs, three texture formats plus an 8-bit
// palettised one, two blend modes, and no framebuffer readback at all.
//
// Notes on the parts that are NOT a straight transliteration:
//
//  * Depth range. OpenGL clips to [-1, 1], Vulkan to [0, 1]. The game feeds
//    1/w as Z and the ortho matrix is set up so that oow=1 (nearest) lands on
//    the near plane. matrixOrtho() below folds the halving into the matrix, so
//    the Z written to the depth buffer matches OpenGL's window-space depth and
//    grBufferClear() can keep passing depth/65535 unchanged.
//
//  * Y orientation. Rather than flip the projection (and then have to flip the
//    winding, the scissor and the blit), the game pass uses a negative-height
//    viewport, which makes Vulkan's NDC behave exactly like OpenGL's. That lets
//    the matrix code stay byte for byte identical to the OpenGL backend. The
//    picture then lands top-down in the offscreen image, which is the natural
//    orientation for sampling it in the display pass, so the display pass uses
//    an ordinary positive viewport and OpenGL's texture coordinates unchanged.
//
//  * Mid-frame texture uploads. Glide uploads textures between draws. Vulkan
//    forbids transfers inside a render pass, so the game render pass is opened
//    lazily (loadOp = LOAD) and closed again around every upload.
//
//  * Texture storage. OpenGL kept one 512K-entry array of texture handles.
//    Doing that with Vulkan handles would cost ~25 MiB of BSS in a 32-bit
//    process, so the address -> texture mapping is a compact index table into a
//    small pool instead.

#include "../Glide2x.h"

#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_video.h>

#include <vulkan/vulkan.h>
#include <SDL2/SDL_vulkan.h>

#include <signal.h>
#include <string.h>

#include "shaders/game_vert.spv.h"
#include "shaders/game_frag.spv.h"
#include "shaders/display_vert.spv.h"
#include "shaders/display_frag.spv.h"

#ifdef WIN32
	#undef near
	#undef far
#endif

BOOL contextError = false;
BOOL shaderError = false;
BOOL framebufferError = false;

/* Same limits as the OpenGL backend */
#define MaxTriangles 0x000400
#define TextureMem   0x200000
#define VertexSnap   0x0C0000
#define Near        -1.0f
#define Far          0.0f

/* Vulkan-specific limits */
#define FramesInFlight     2
#define MaxTexturePoolSize 8192
#define VertexRingBytes    (8 * 1024 * 1024)
#define StagingRingBytes   (8 * 1024 * 1024)
#define MaxSwapchainImages 8
#define DescriptorsPerPool 1024

/* Pipeline variants: blend dst factor x depth write x topology */
#define PipelineVariants 8

extern BOOL keepAspectRatio, needRecreateGl, windowResized, linearFiltering, fixedFramebufferSize, framebufferLinearFiltering;
extern int32_t vSync, winWidth, winHeight, initialWinWidth, initialWinHeight;
extern SDL_Window *sdlWin;

/* Matrix 4x4 */

static float g_matrix[16];

static inline void matrixLoadIdentity()
{
	g_matrix[ 0] = 1;
	g_matrix[ 5] = 1;
	g_matrix[10] = 1;
	g_matrix[12] = 0;
	g_matrix[13] = 0;
	g_matrix[14] = 0;
	g_matrix[15] = 1;
}
static inline void matrixScale2(float x, float y)
{
	g_matrix[ 0] *= x;
	g_matrix[ 5] *= y;
}
static inline void matrixOrtho(float left, float right, float bottom, float top, float near, float far)
{
	const float rl = (right - left);
	const float tb = (top - bottom);
	const float fn = (far - near);
	g_matrix[ 0] = 2 / rl;
	g_matrix[ 5] = 2 / tb;
	g_matrix[12] = -(left + right) / rl;
	g_matrix[13] = -(top + bottom) / tb;
	/*
	 * OpenGL would use -2/fn and -(far+near)/fn to land in [-1, 1]. Vulkan's
	 * depth range is [0, 1], so this is the OpenGL result remapped with
	 * z' = (z + 1) / 2, which keeps window-space depth identical between the
	 * two backends.
	 */
	g_matrix[10] = -1 / fn;
	g_matrix[14] = (1 - (far + near) / fn) / 2;
}

/* Vertex streams, laid out exactly as the OpenGL backend had them */

typedef struct
{
	struct
	{
		uint8_t r, g, b, a;
	} vertex[3];
} ColorValues;
typedef struct
{
	uint8_t vertex[3];
} FogCoord;
typedef struct
{
	struct
	{
		float s, t, r, q;
	} vertex[3];
} TextureCoord;
typedef struct
{
	struct
	{
		float x, y, z;
	} vertex[3];
} Vertices;
static ColorValues g_colorValues[MaxTriangles];
static FogCoord g_fogCoord[MaxTriangles];
static TextureCoord g_textureCoord[MaxTriangles];
static Vertices g_vertices[MaxTriangles];

static float g_textureCoordDisp[4][2] = {
	{0.0f, 1.0f},
	{0.0f, 0.0f},
	{1.0f, 1.0f},
	{1.0f, 0.0f},
};
static float g_verticesDisp[4][2] = {
	{-1.0f, +1.0f},
	{-1.0f, -1.0f},
	{+1.0f, +1.0f},
	{+1.0f, -1.0f},
};

/* Push constants, matching the layout declared by the shaders in shaders/ */

typedef struct
{
	float uMatrix[16];
	float uFogColor[4];
	float uTextureEnabled;
	float uFogEnabled;
} GamePushConstants;

typedef struct
{
	float uGamma;
} DisplayPushConstants;

/* Texture bookkeeping */

typedef struct
{
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkDescriptorSet set;
	void *data;
	void *palette;
	GrTextureFormat_t fmt;
	uint32_t size;
	/* What the VkImage was actually created as, so a re-download to the same
	 * Glide address with a different shape can be detected. */
	VkFormat createdFormat;
	uint32_t createdSize;
	BOOL valid;
} Texture;

static Texture g_texturePool[MaxTexturePoolSize];
static uint32_t g_texturePoolCount;
/* startAddress >> 2 -> pool index + 1 (0 means "nothing here") */
static uint16_t g_textureSlot[TextureMem >> 2];
static BOOL g_texturePoolExhausted;

static uint8_t *g_lfb, g_textureMem[TextureMem], g_fogTable[0x10000];
static uint32_t *g_palette;
/*
 * The OpenGL backend sized this 0x400 words, which overflows for any
 * palettised texture larger than 32x32. Size it for the largest texture Glide
 * can hand us instead.
 */
static uint32_t g_tmpTexture[256 * 256];
static uint32_t g_trianglesCount;

/* Per-frame resources */

typedef struct
{
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkFence fence;
	VkSemaphore imageAvailable;

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;
	uint8_t *vertexMapped;
	VkDeviceSize vertexOffset;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	uint8_t *stagingMapped;
	VkDeviceSize stagingOffset;

	BOOL recording;
	BOOL submitted;
} Frame;

/* Vulkan objects */

static VkInstance g_instance;
static VkPhysicalDevice g_physicalDevice;
static VkDevice g_device;
static uint32_t g_queueFamily;
static VkQueue g_queue;
static VkSurfaceKHR g_surface;
static VkPhysicalDeviceMemoryProperties g_memoryProperties;
static BOOL g_wideLinesSupported;
static VkDeviceSize g_nonCoherentAtomSize;

static VkSwapchainKHR g_swapchain;
static VkFormat g_swapchainFormat;
static VkExtent2D g_swapchainExtent;
static uint32_t g_swapchainImageCount;
static VkImage g_swapchainImages[MaxSwapchainImages];
static VkImageView g_swapchainViews[MaxSwapchainImages];
static VkFramebuffer g_swapchainFramebuffers[MaxSwapchainImages];
/*
 * The present-wait semaphore has to be per swapchain image, not per frame in
 * flight: with 2 frames and 3 images, reusing a frame's semaphore can signal it
 * again while a still-pending present is waiting on it
 * (VUID-vkQueueSubmit-pSignalSemaphores-00067). The acquire semaphore is fine
 * per frame, since its submit's fence is waited on before the frame is reused.
 */
static VkSemaphore g_renderFinished[MaxSwapchainImages];
static uint32_t g_swapchainIndex;
static BOOL g_swapchainImageAcquired;

static VkRenderPass g_gameRenderPass;
static VkRenderPass g_displayRenderPass;

static VkDescriptorSetLayout g_descriptorSetLayout;
static VkPipelineLayout g_gamePipelineLayout;
static VkPipelineLayout g_displayPipelineLayout;
static VkPipeline g_gamePipelines[PipelineVariants];
static VkPipeline g_displayPipeline;

static VkShaderModule g_gameVertModule, g_gameFragModule;
static VkShaderModule g_displayVertModule, g_displayFragModule;

static VkSampler g_gameSampler;
static VkSampler g_displaySampler;

static VkDescriptorPool g_descriptorPools[64];
static uint32_t g_descriptorPoolCount;
static uint32_t g_descriptorPoolRemaining;

static Frame g_frames[FramesInFlight];
static uint32_t g_frameIndex;

/* Offscreen render target ("framebuffer" in the OpenGL backend's terms) */

static VkImage g_fbImage;
static VkDeviceMemory g_fbMemory;
static VkImageView g_fbView;
static VkImage g_fbDepthImage;
static VkDeviceMemory g_fbDepthMemory;
static VkImageView g_fbDepthView;
static VkFramebuffer g_fbFramebuffer;
static VkDescriptorSet g_fbDescriptorSet;
static VkFormat g_fbDepthFormat;
static int32_t g_framebufferHeight;
static int32_t g_framebufferWidth;

/* Dummy 1x1 texture, bound whenever the game draws untextured geometry: the
 * shader always declares a sampler, so the descriptor must always be valid. */
static VkImage g_dummyImage;
static VkDeviceMemory g_dummyMemory;
static VkImageView g_dummyView;
static VkDescriptorSet g_dummySet;

/* Mirrored render state */

static BOOL g_textureEnabled;
static BOOL g_fogEnabled;
static float g_fogColor[3];
static float g_gammaValue = 1.0f;
static BOOL g_depthMask = true;
static VkBlendFactor g_blendDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
static int32_t g_currentTexture = -1;
static VkViewport g_viewport;
static VkRect2D g_scissor;
static float g_lineWidth = 1.0f;
static BOOL g_gameRenderPassActive;

static void flushCommandBufferAndWait(void);
static void beginGameRenderPass(void);
static void endGameRenderPass(void);
static void drawTriangles(void);
static void uploadTexture(Texture *tex);
static void createFrameBuffer(void);
static void destroyFrameBuffer(void);
static void createContext(void);
static void destroyContext(void);
static void recreateContext(void);
static void recreateSwapchain(void);

static void fail(BOOL *flag)
{
	*flag = true;
	raise(SIGABRT);
}

/*
 * Report before aborting. Without this a failure anywhere in setup is an
 * indistinguishable SIGABRT, which is miserable to diagnose on a user's
 * machine -- and there is no OpenGL-style glGetError to fall back on.
 */
static void failAt(BOOL *flag, VkResult result, const char *call, const char *file, int line)
{
	fprintf(stderr, "Vulkan: %s failed with VkResult %d at %s:%d\n", call, (int)result, file, line);
	fail(flag);
}

#define VK_CHECK(expr, flag) \
	do { \
		VkResult vkCheckResult_ = (expr); \
		if (vkCheckResult_ != VK_SUCCESS) \
			failAt(flag, vkCheckResult_, #expr, __FILE__, __LINE__); \
	} while (0)

static uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
	uint32_t i;
	for (i = 0; i < g_memoryProperties.memoryTypeCount; ++i)
	{
		if ((typeBits & (1u << i)) && (g_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}
	fail(&contextError);
	return 0;
}

static inline void backendGetDrawableSize(SDL_Window *win, int *w, int *h)
{
	SDL_Vulkan_GetDrawableSize(win, w, h);
}

/*
 * The OpenGL backend scissor-cleared the letterbox bars here. The Vulkan
 * display render pass clears the whole swapchain image to black on load, which
 * covers the bars for free, so this only has to report whether bars exist.
 */
static inline BOOL clearUnusedArea(int32_t xOffset, int32_t yOffset, int32_t visibleWidth, int32_t visibleHeight)
{
	(void)visibleWidth;
	(void)visibleHeight;
	return keepAspectRatio && (xOffset > 0 || yOffset > 0);
}

/* Buffers and images */

static void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *memory, uint8_t **mapped)
{
	VkBufferCreateInfo bufferInfo;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocInfo;

	memset(&bufferInfo, 0, sizeof bufferInfo);
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK(vkCreateBuffer(g_device, &bufferInfo, NULL, buffer), &contextError);

	vkGetBufferMemoryRequirements(g_device, *buffer, &requirements);

	memset(&allocInfo, 0, sizeof allocInfo);
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
	VK_CHECK(vkAllocateMemory(g_device, &allocInfo, NULL, memory), &contextError);
	VK_CHECK(vkBindBufferMemory(g_device, *buffer, *memory, 0), &contextError);

	if (mapped)
		VK_CHECK(vkMapMemory(g_device, *memory, 0, VK_WHOLE_SIZE, 0, (void **)mapped), &contextError);
}

static void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
	VkImageCreateInfo imageInfo;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocInfo;
	VkImageViewCreateInfo viewInfo;

	memset(&imageInfo, 0, sizeof imageInfo);
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(g_device, &imageInfo, NULL, image), &contextError);

	vkGetImageMemoryRequirements(g_device, *image, &requirements);

	memset(&allocInfo, 0, sizeof allocInfo);
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(g_device, &allocInfo, NULL, memory), &contextError);
	VK_CHECK(vkBindImageMemory(g_device, *image, *memory, 0), &contextError);

	memset(&viewInfo, 0, sizeof viewInfo);
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = *image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspect;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	VK_CHECK(vkCreateImageView(g_device, &viewInfo, NULL, view), &contextError);
}

static void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
	VkImageMemoryBarrier barrier;

	memset(&barrier, 0, sizeof barrier);
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;

	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/* Command buffer lifecycle */

static Frame *currentFrame(void)
{
	return &g_frames[g_frameIndex];
}

static void beginCommandBuffer(void)
{
	Frame *frame = currentFrame();
	VkCommandBufferBeginInfo beginInfo;

	if (frame->recording)
		return;

	VK_CHECK(vkResetCommandPool(g_device, frame->commandPool, 0), &contextError);

	memset(&beginInfo, 0, sizeof beginInfo);
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(frame->commandBuffer, &beginInfo), &contextError);

	frame->recording = true;
	frame->vertexOffset = 0;
	frame->stagingOffset = 0;
}

/*
 * Submits whatever has been recorded so far and blocks until it retires. Used
 * when a ring buffer runs dry mid-frame: the recorded draws still reference the
 * regions we want to reuse, so they have to actually execute first.
 */
static void flushCommandBufferAndWait(void)
{
	Frame *frame = currentFrame();
	VkSubmitInfo submitInfo;

	if (!frame->recording)
		return;

	endGameRenderPass();

	VK_CHECK(vkEndCommandBuffer(frame->commandBuffer), &contextError);
	frame->recording = false;

	memset(&submitInfo, 0, sizeof submitInfo);
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame->commandBuffer;

	VK_CHECK(vkResetFences(g_device, 1, &frame->fence), &contextError);
	VK_CHECK(vkQueueSubmit(g_queue, 1, &submitInfo, frame->fence), &contextError);
	VK_CHECK(vkWaitForFences(g_device, 1, &frame->fence, VK_TRUE, UINT64_MAX), &contextError);

	beginCommandBuffer();
}

static VkDeviceSize ringAlloc(VkDeviceSize *offset, VkDeviceSize capacity, VkDeviceSize size, VkDeviceSize alignment)
{
	VkDeviceSize aligned = (*offset + alignment - 1) & ~(alignment - 1);

	if (aligned + size > capacity)
	{
		flushCommandBufferAndWait();
		/* flushCommandBufferAndWait() restarted the command buffer, which
		 * reset both ring offsets back to zero. */
		aligned = 0;
		if (size > capacity)
			fail(&contextError);
	}

	*offset = aligned + size;
	return aligned;
}

/* Descriptor sets */

static VkDescriptorSet allocateDescriptorSet(void)
{
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorSet set = VK_NULL_HANDLE;

	if (g_descriptorPoolRemaining == 0)
	{
		VkDescriptorPoolSize poolSize;
		VkDescriptorPoolCreateInfo poolInfo;

		if (g_descriptorPoolCount >= (uint32_t)(sizeof g_descriptorPools / sizeof *g_descriptorPools))
			fail(&contextError);

		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = DescriptorsPerPool;

		memset(&poolInfo, 0, sizeof poolInfo);
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = DescriptorsPerPool;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		VK_CHECK(vkCreateDescriptorPool(g_device, &poolInfo, NULL, &g_descriptorPools[g_descriptorPoolCount]), &contextError);

		++g_descriptorPoolCount;
		g_descriptorPoolRemaining = DescriptorsPerPool;
	}

	memset(&allocInfo, 0, sizeof allocInfo);
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = g_descriptorPools[g_descriptorPoolCount - 1];
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &g_descriptorSetLayout;
	VK_CHECK(vkAllocateDescriptorSets(g_device, &allocInfo, &set), &contextError);

	--g_descriptorPoolRemaining;
	return set;
}

static void writeDescriptorSet(VkDescriptorSet set, VkImageView view, VkSampler sampler)
{
	VkDescriptorImageInfo imageInfo;
	VkWriteDescriptorSet write;

	memset(&imageInfo, 0, sizeof imageInfo);
	imageInfo.sampler = sampler;
	imageInfo.imageView = view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	memset(&write, 0, sizeof write);
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(g_device, 1, &write, 0, NULL);
}

/* Render passes */

static void beginGameRenderPass(void)
{
	Frame *frame = currentFrame();
	VkRenderPassBeginInfo beginInfo;

	if (g_gameRenderPassActive)
		return;

	beginCommandBuffer();

	memset(&beginInfo, 0, sizeof beginInfo);
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = g_gameRenderPass;
	beginInfo.framebuffer = g_fbFramebuffer;
	beginInfo.renderArea.extent.width = g_framebufferWidth;
	beginInfo.renderArea.extent.height = g_framebufferHeight;

	vkCmdBeginRenderPass(frame->commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	g_gameRenderPassActive = true;
}

static void endGameRenderPass(void)
{
	Frame *frame = currentFrame();

	if (!g_gameRenderPassActive)
		return;

	vkCmdEndRenderPass(frame->commandBuffer);
	g_gameRenderPassActive = false;
}

/* Drawing */

static VkPipeline gamePipeline(BOOL lineTopology)
{
	uint32_t index = 0;
	if (g_blendDstFactor == VK_BLEND_FACTOR_ONE)
		index |= 4;
	if (g_depthMask)
		index |= 2;
	if (lineTopology)
		index |= 1;
	return g_gamePipelines[index];
}

static VkDescriptorSet currentTextureSet(void)
{
	if (g_currentTexture >= 0)
	{
		Texture *tex = &g_texturePool[g_currentTexture];
		if (tex->valid && tex->set != VK_NULL_HANDLE)
			return tex->set;
	}
	return g_dummySet;
}

static void bindCommonState(VkCommandBuffer cmd, BOOL lineTopology)
{
	GamePushConstants pc;
	VkDescriptorSet set = currentTextureSet();

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gamePipeline(lineTopology));
	vkCmdSetViewport(cmd, 0, 1, &g_viewport);
	vkCmdSetScissor(cmd, 0, 1, &g_scissor);
	/* Line width is declared dynamic, so it must always be set. Values other
	 * than 1.0 additionally require the wideLines feature. */
	vkCmdSetLineWidth(cmd, (lineTopology && g_wideLinesSupported) ? g_lineWidth : 1.0f);

	memcpy(pc.uMatrix, g_matrix, sizeof pc.uMatrix);
	pc.uFogColor[0] = g_fogColor[0];
	pc.uFogColor[1] = g_fogColor[1];
	pc.uFogColor[2] = g_fogColor[2];
	pc.uFogColor[3] = 1.0f;
	pc.uTextureEnabled = g_textureEnabled ? 1.0f : 0.0f;
	pc.uFogEnabled = g_fogEnabled ? 1.0f : 0.0f;
	vkCmdPushConstants(cmd, g_gamePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_gamePipelineLayout, 0, 1, &set, 0, NULL);
}

/* Ring offsets of the four streams staged by stageVertexStreams() */
static VkDeviceSize g_streamOffsets[4];

/*
 * Copies one batch's four vertex streams into the frame's ring. OpenGL pointed
 * straight at the client arrays with glVertexAttribPointer; Vulkan has no
 * client-side vertex arrays, so the streams get staged instead. They stay four
 * separate tightly packed bindings rather than being interleaved, which keeps
 * the layout identical to the OpenGL backend.
 *
 * All four are carved out of a SINGLE ring allocation on purpose. ringAlloc()
 * can submit and wait when the ring wraps, which closes the render pass and
 * rewinds the ring -- so it must be able to happen at most once, before any of
 * this draw has been recorded. Hence this runs before beginGameRenderPass().
 */
static void stageVertexStreams(uint32_t vertexCount)
{
	Frame *frame = currentFrame();
	VkDeviceSize sizes[4];
	const void *sources[4];
	VkDeviceSize total = 0, base, cursor;
	uint32_t i;

	sizes[0] = (VkDeviceSize)vertexCount * sizeof(float) * 3;
	sizes[1] = (VkDeviceSize)vertexCount * sizeof(float) * 4;
	sizes[2] = (VkDeviceSize)vertexCount * 4;
	sizes[3] = (VkDeviceSize)vertexCount * 1;

	sources[0] = g_vertices;
	sources[1] = g_textureCoord;
	sources[2] = g_colorValues;
	sources[3] = g_fogCoord;

	for (i = 0; i < 4; ++i)
		total = ((total + 15) & ~(VkDeviceSize)15) + sizes[i];

	base = ringAlloc(&frame->vertexOffset, VertexRingBytes, total, 16);

	cursor = base;
	for (i = 0; i < 4; ++i)
	{
		cursor = (cursor + 15) & ~(VkDeviceSize)15;
		g_streamOffsets[i] = cursor;
		memcpy(frame->vertexMapped + cursor, sources[i], (size_t)sizes[i]);
		cursor += sizes[i];
	}
}

static void bindVertexStreams(VkCommandBuffer cmd)
{
	Frame *frame = currentFrame();
	VkBuffer buffers[4];
	uint32_t i;

	for (i = 0; i < 4; ++i)
		buffers[i] = frame->vertexBuffer;

	vkCmdBindVertexBuffers(cmd, 0, 4, buffers, g_streamOffsets);
}

static void drawPrimitives(uint32_t vertexCount, BOOL lineTopology)
{
	VkCommandBuffer cmd;

	/* Must come first: see stageVertexStreams(). */
	stageVertexStreams(vertexCount);

	beginGameRenderPass();
	cmd = currentFrame()->commandBuffer;

	bindCommonState(cmd, lineTopology);
	bindVertexStreams(cmd);
	vkCmdDraw(cmd, vertexCount, 1, 0, 0);
}

static void drawTriangles(void)
{
	uint32_t count = g_trianglesCount;

	if (count == 0)
		return;

	/* Cleared up front: drawPrimitives() can recurse into a queue flush, and
	 * that must not re-enter this batch. */
	g_trianglesCount = 0;

	drawPrimitives(count * 3, false);
}

/* Texture upload */

static VkFormat textureFormat(GrTextureFormat_t fmt)
{
	switch (fmt)
	{
		case GR_TEXFMT_RGB_565:
			return VK_FORMAT_R5G6B5_UNORM_PACK16;
		case GR_TEXFMT_ARGB_1555:
			/* grTexDownloadMipMap already rotated ARGB1555 into RGBA5551. */
			return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
		case GR_TEXFMT_ARGB_4444:
			/* Likewise rotated into RGBA4444. */
			return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
		case GR_TEXFMT_P_8:
			/* Expanded to RGBA8 against the current palette by grTexSource. */
			return VK_FORMAT_R8G8B8A8_UNORM;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

/*
 * Creates or reuses the VkImage for a texture and, when source is non-NULL,
 * stages pixels into it. Palettised textures pass source = NULL here: like the
 * OpenGL backend, they only get real pixels in grTexSource, once the palette
 * they should be expanded against is known.
 */
static void uploadTextureData(Texture *tex, const void *source)
{
	Frame *frame;
	VkCommandBuffer cmd;
	VkDeviceSize stagingOffset = 0;
	VkDeviceSize dataSize;
	VkFormat format = textureFormat(tex->fmt);
	VkBufferImageCopy copy;
	BOOL freshImage = false;

	if (format == VK_FORMAT_UNDEFINED || tex->size == 0)
		return;

	dataSize = (VkDeviceSize)tex->size * tex->size * (tex->fmt == GR_TEXFMT_P_8 ? 4 : 2);

	/*
	 * Step 1: drop a stale image. The game reuses Glide texture addresses, and
	 * not always for the same shape. Recorded draws may still reference the old
	 * image, so drain the queue first -- rare enough that the stall is fine.
	 */
	if (tex->image != VK_NULL_HANDLE && (tex->createdFormat != format || tex->createdSize != tex->size))
	{
		flushCommandBufferAndWait();
		vkDestroyImageView(g_device, tex->view, NULL);
		vkDestroyImage(g_device, tex->image, NULL);
		vkFreeMemory(g_device, tex->memory, NULL);
		tex->view = VK_NULL_HANDLE;
		tex->image = VK_NULL_HANDLE;
		tex->memory = VK_NULL_HANDLE;
		tex->valid = false;
	}

	/* Step 2: create the image if there is not one yet. */
	if (tex->image == VK_NULL_HANDLE)
	{
		createImage(tex->size, tex->size, format,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT,
			&tex->image, &tex->memory, &tex->view);
		tex->createdFormat = format;
		tex->createdSize = tex->size;
		freshImage = true;

		if (tex->set == VK_NULL_HANDLE)
			tex->set = allocateDescriptorSet();
		writeDescriptorSet(tex->set, tex->view, g_gameSampler);
	}

	/*
	 * Step 3: stage the pixels. Both this and step 1 can submit and wait, which
	 * rewinds the rings -- so all of that has to happen before step 4 records
	 * the barrier the copy depends on.
	 */
	beginCommandBuffer();
	if (source)
	{
		frame = currentFrame();
		stagingOffset = ringAlloc(&frame->stagingOffset, StagingRingBytes, dataSize, 16);
		memcpy(frame->stagingMapped + stagingOffset, source, (size_t)dataSize);
	}

	/* Step 4: transfers are illegal inside a render pass. */
	frame = currentFrame();
	cmd = frame->commandBuffer;
	endGameRenderPass();

	if (!source)
	{
		/*
		 * A palettised texture with no palette bound yet: there are no pixels
		 * to upload, but the descriptor is already live, so the image still has
		 * to reach a layout that is legal to sample. Clear it rather than just
		 * transitioning out of UNDEFINED -- sampling undefined contents could
		 * show garbage, whereas an incomplete OpenGL texture reads as black.
		 */
		if (freshImage)
		{
			VkClearColorValue black;
			VkImageSubresourceRange range;

			memset(&black, 0, sizeof black);
			memset(&range, 0, sizeof range);
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.levelCount = 1;
			range.layerCount = 1;

			imageBarrier(cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			vkCmdClearColorImage(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

			imageBarrier(cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		}
		return;
	}

	if (freshImage)
		imageBarrier(cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	else
		imageBarrier(cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	memset(&copy, 0, sizeof copy);
	copy.bufferOffset = stagingOffset;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = tex->size;
	copy.imageExtent.height = tex->size;
	copy.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(cmd, frame->stagingBuffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	imageBarrier(cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	tex->valid = true;
}

/* Non-palettised: pixels come straight from the Glide-side texture memory. */
static void uploadTexture(Texture *tex)
{
	uploadTextureData(tex, tex->fmt == GR_TEXFMT_P_8 ? NULL : tex->data);
}


static Texture *textureForAddress(uint32_t startAddress, BOOL create)
{
	uint32_t slot = startAddress >> 2;

	if (slot >= (TextureMem >> 2))
		return NULL;

	if (g_textureSlot[slot] != 0)
		return &g_texturePool[g_textureSlot[slot] - 1];

	if (!create)
		return NULL;

	if (g_texturePoolCount >= MaxTexturePoolSize)
	{
		if (!g_texturePoolExhausted)
		{
			g_texturePoolExhausted = true;
			fprintf(stderr, "Vulkan: texture pool exhausted (%d entries), further textures will not render\n", MaxTexturePoolSize);
		}
		return NULL;
	}

	g_textureSlot[slot] = (uint16_t)(g_texturePoolCount + 1);
	return &g_texturePool[g_texturePoolCount++];
}

#include "Vulkan/Setup.c"
#include "Vulkan/Entrypoints.c"
