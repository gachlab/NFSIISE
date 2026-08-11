// SPDX-License-Identifier: MIT
//
// Device, swapchain, render pass and pipeline setup for the Vulkan backend.
// Included by Glide2x/Vulkan.c -- not a standalone translation unit.

/* Instance and device */

static void createInstance(void)
{
	VkApplicationInfo appInfo;
	VkInstanceCreateInfo createInfo;
	const char *extensions[16];
	unsigned int sdlExtensionCount = 0;
	uint32_t extensionCount = 0;
	uint32_t availableCount = 0;
	VkExtensionProperties *available;
	uint32_t i;
	BOOL portability = false;

	if (!SDL_Vulkan_GetInstanceExtensions(sdlWin, &sdlExtensionCount, NULL))
		fail(&contextError);
	if (sdlExtensionCount > (sizeof extensions / sizeof *extensions) - 1)
		fail(&contextError);
	if (!SDL_Vulkan_GetInstanceExtensions(sdlWin, &sdlExtensionCount, extensions))
		fail(&contextError);
	extensionCount = sdlExtensionCount;

	/*
	 * MoltenVK is not a conformant implementation, so the loader hides it
	 * unless portability enumeration is asked for explicitly. Harmless to skip
	 * where the extension does not exist.
	 */
	vkEnumerateInstanceExtensionProperties(NULL, &availableCount, NULL);
	available = (VkExtensionProperties *)malloc(availableCount * sizeof(VkExtensionProperties));
	if (available)
	{
		vkEnumerateInstanceExtensionProperties(NULL, &availableCount, available);
		for (i = 0; i < availableCount; ++i)
		{
			if (!strcmp(available[i].extensionName, "VK_KHR_portability_enumeration"))
			{
				extensions[extensionCount++] = "VK_KHR_portability_enumeration";
				portability = true;
				break;
			}
		}
		free(available);
	}

	memset(&appInfo, 0, sizeof appInfo);
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Need For Speed II SE";
	appInfo.pEngineName = "NFSIISE";
	/*
	 * 1.1 for negative viewport height (VK_KHR_maintenance1 promoted to core),
	 * which is what lets the projection maths stay identical to OpenGL's.
	 */
	appInfo.apiVersion = VK_API_VERSION_1_1;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = extensionCount;
	createInfo.ppEnabledExtensionNames = extensions;
	if (portability)
		createInfo.flags |= 0x00000001; /* VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR */

	VK_CHECK(vkCreateInstance(&createInfo, NULL, &g_instance), &contextError);
}

static BOOL deviceHasExtension(VkPhysicalDevice device, const char *name)
{
	uint32_t count = 0;
	VkExtensionProperties *properties;
	uint32_t i;
	BOOL found = false;

	vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
	properties = (VkExtensionProperties *)malloc(count * sizeof(VkExtensionProperties));
	if (!properties)
		return false;
	vkEnumerateDeviceExtensionProperties(device, NULL, &count, properties);

	for (i = 0; i < count; ++i)
	{
		if (!strcmp(properties[i].extensionName, name))
		{
			found = true;
			break;
		}
	}

	free(properties);
	return found;
}

static void pickPhysicalDevice(void)
{
	uint32_t deviceCount = 0;
	VkPhysicalDevice *devices;
	uint32_t i, j;
	int bestScore = -1;

	vkEnumeratePhysicalDevices(g_instance, &deviceCount, NULL);
	if (deviceCount == 0)
		fail(&contextError);

	devices = (VkPhysicalDevice *)malloc(deviceCount * sizeof(VkPhysicalDevice));
	if (!devices)
		fail(&contextError);
	vkEnumeratePhysicalDevices(g_instance, &deviceCount, devices);

	for (i = 0; i < deviceCount; ++i)
	{
		VkPhysicalDeviceProperties properties;
		uint32_t familyCount = 0;
		VkQueueFamilyProperties *families;
		int score;
		int32_t family = -1;

		if (!deviceHasExtension(devices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			continue;

		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount, NULL);
		families = (VkQueueFamilyProperties *)malloc(familyCount * sizeof(VkQueueFamilyProperties));
		if (!families)
			continue;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount, families);

		for (j = 0; j < familyCount; ++j)
		{
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, g_surface, &present);
			if ((families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
			{
				family = (int32_t)j;
				break;
			}
		}
		free(families);

		if (family < 0)
			continue;

		vkGetPhysicalDeviceProperties(devices[i], &properties);
		score = (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 2 :
		        (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? 1 : 0;

		if (score > bestScore)
		{
			bestScore = score;
			g_physicalDevice = devices[i];
			g_queueFamily = (uint32_t)family;
			g_nonCoherentAtomSize = properties.limits.nonCoherentAtomSize;
		}
	}

	free(devices);

	if (bestScore < 0)
		fail(&contextError);

	vkGetPhysicalDeviceMemoryProperties(g_physicalDevice, &g_memoryProperties);
}

static void createDevice(void)
{
	VkPhysicalDeviceFeatures supported;
	VkPhysicalDeviceFeatures enabled;
	VkDeviceQueueCreateInfo queueInfo;
	VkDeviceCreateInfo deviceInfo;
	const char *extensions[4];
	uint32_t extensionCount = 0;
	float priority = 1.0f;

	vkGetPhysicalDeviceFeatures(g_physicalDevice, &supported);
	g_wideLinesSupported = supported.wideLines ? true : false;

	memset(&enabled, 0, sizeof enabled);
	enabled.wideLines = supported.wideLines;

	extensions[extensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	/* Required to be enabled if the device advertises it (MoltenVK does). */
	if (deviceHasExtension(g_physicalDevice, "VK_KHR_portability_subset"))
		extensions[extensionCount++] = "VK_KHR_portability_subset";

	memset(&queueInfo, 0, sizeof queueInfo);
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = g_queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	memset(&deviceInfo, 0, sizeof deviceInfo);
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = extensionCount;
	deviceInfo.ppEnabledExtensionNames = extensions;
	deviceInfo.pEnabledFeatures = &enabled;

	VK_CHECK(vkCreateDevice(g_physicalDevice, &deviceInfo, NULL, &g_device), &contextError);
	vkGetDeviceQueue(g_device, g_queueFamily, 0, &g_queue);

	if (!g_wideLinesSupported)
	{
		/* MoltenVK cannot do this, and Metal has no line width at all. Glide
		 * only uses lines for a handful of HUD elements. */
		fprintf(stderr, "Vulkan: wideLines unsupported, lines will be 1 pixel wide\n");
	}
}

/* Swapchain */

static VkFormat pickDepthFormat(void)
{
	static const VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM,
	};
	uint32_t i;

	for (i = 0; i < sizeof candidates / sizeof *candidates; ++i)
	{
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(g_physicalDevice, candidates[i], &properties);
		if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			return candidates[i];
	}

	fail(&framebufferError);
	return VK_FORMAT_D16_UNORM;
}

/*
 * Chosen before the render passes are built, because the display render pass
 * has to declare the swapchain's format as its attachment format -- while the
 * swapchain's framebuffers in turn need that render pass. Splitting the format
 * choice out is what breaks the cycle.
 *
 * A UNORM format is required rather than SRGB: see shaders/display.frag.
 */
static void pickSurfaceFormat(void)
{
	VkSurfaceFormatKHR *formats;
	uint32_t formatCount = 0;
	uint32_t i;

	vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &formatCount, NULL);
	if (formatCount == 0)
		fail(&contextError);

	formats = (VkSurfaceFormatKHR *)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
	if (!formats)
		fail(&contextError);
	vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &formatCount, formats);

	g_swapchainFormat = formats[0].format;
	for (i = 0; i < formatCount; ++i)
	{
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
		{
			g_swapchainFormat = formats[i].format;
			break;
		}
	}
	free(formats);
}

static void createSwapchain(void)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkPresentModeKHR *presentModes;
	uint32_t presentModeCount = 0;
	VkSwapchainCreateInfoKHR createInfo;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t imageCount;
	uint32_t i;
	int drawableWidth = 0, drawableHeight = 0;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &capabilities);

	/* vSync mirrors what the OpenGL backend passed to SDL_GL_SetSwapInterval. */
	if (vSync == 0)
	{
		vkGetPhysicalDeviceSurfacePresentModesKHR(g_physicalDevice, g_surface, &presentModeCount, NULL);
		presentModes = (VkPresentModeKHR *)malloc(presentModeCount * sizeof(VkPresentModeKHR));
		if (presentModes)
		{
			vkGetPhysicalDeviceSurfacePresentModesKHR(g_physicalDevice, g_surface, &presentModeCount, presentModes);
			for (i = 0; i < presentModeCount; ++i)
			{
				if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
					break;
				}
				if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
					presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
			free(presentModes);
		}
	}

	g_swapchainExtent = capabilities.currentExtent;
	if (g_swapchainExtent.width == 0xFFFFFFFFu)
	{
		SDL_Vulkan_GetDrawableSize(sdlWin, &drawableWidth, &drawableHeight);
		g_swapchainExtent.width = (uint32_t)drawableWidth;
		g_swapchainExtent.height = (uint32_t)drawableHeight;
	}
	if (g_swapchainExtent.width < capabilities.minImageExtent.width)
		g_swapchainExtent.width = capabilities.minImageExtent.width;
	if (g_swapchainExtent.height < capabilities.minImageExtent.height)
		g_swapchainExtent.height = capabilities.minImageExtent.height;
	if (g_swapchainExtent.width > capabilities.maxImageExtent.width)
		g_swapchainExtent.width = capabilities.maxImageExtent.width;
	if (g_swapchainExtent.height > capabilities.maxImageExtent.height)
		g_swapchainExtent.height = capabilities.maxImageExtent.height;

	imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;
	if (imageCount > MaxSwapchainImages)
		imageCount = MaxSwapchainImages;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = g_surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = g_swapchainFormat;
	createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	createInfo.imageExtent = g_swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform = capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	VK_CHECK(vkCreateSwapchainKHR(g_device, &createInfo, NULL, &g_swapchain), &contextError);

	g_swapchainImageCount = MaxSwapchainImages;
	VK_CHECK(vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchainImageCount, g_swapchainImages), &contextError);

	for (i = 0; i < g_swapchainImageCount; ++i)
	{
		VkImageViewCreateInfo viewInfo;
		VkFramebufferCreateInfo framebufferInfo;
		VkSemaphoreCreateInfo semaphoreInfo;

		memset(&viewInfo, 0, sizeof viewInfo);
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = g_swapchainImages[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = g_swapchainFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		VK_CHECK(vkCreateImageView(g_device, &viewInfo, NULL, &g_swapchainViews[i]), &contextError);

		memset(&framebufferInfo, 0, sizeof framebufferInfo);
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = g_displayRenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &g_swapchainViews[i];
		framebufferInfo.width = g_swapchainExtent.width;
		framebufferInfo.height = g_swapchainExtent.height;
		framebufferInfo.layers = 1;
		VK_CHECK(vkCreateFramebuffer(g_device, &framebufferInfo, NULL, &g_swapchainFramebuffers[i]), &contextError);

		memset(&semaphoreInfo, 0, sizeof semaphoreInfo);
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VK_CHECK(vkCreateSemaphore(g_device, &semaphoreInfo, NULL, &g_renderFinished[i]), &contextError);
	}
}

static void destroySwapchain(void)
{
	uint32_t i;

	for (i = 0; i < g_swapchainImageCount; ++i)
	{
		if (g_swapchainFramebuffers[i] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(g_device, g_swapchainFramebuffers[i], NULL);
		if (g_swapchainViews[i] != VK_NULL_HANDLE)
			vkDestroyImageView(g_device, g_swapchainViews[i], NULL);
		if (g_renderFinished[i] != VK_NULL_HANDLE)
			vkDestroySemaphore(g_device, g_renderFinished[i], NULL);
		g_swapchainFramebuffers[i] = VK_NULL_HANDLE;
		g_swapchainViews[i] = VK_NULL_HANDLE;
		g_renderFinished[i] = VK_NULL_HANDLE;
	}
	g_swapchainImageCount = 0;

	if (g_swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(g_device, g_swapchain, NULL);
		g_swapchain = VK_NULL_HANDLE;
	}
}

static void recreateSwapchain(void)
{
	vkDeviceWaitIdle(g_device);
	destroySwapchain();
	createSwapchain();
}

/* Render passes */

static void createRenderPasses(void)
{
	VkAttachmentDescription attachments[2];
	VkAttachmentReference colorRef, depthRef;
	VkSubpassDescription subpass;
	VkSubpassDependency dependencies[2];
	VkRenderPassCreateInfo createInfo;

	g_fbDepthFormat = pickDepthFormat();

	/*
	 * Game pass. loadOp is LOAD because Glide clears explicitly (grBufferClear)
	 * and because the pass gets reopened several times per frame around texture
	 * uploads -- a CLEAR here would wipe the frame each time.
	 */
	memset(attachments, 0, sizeof attachments);
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[1].format = g_fbDepthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	memset(&colorRef, 0, sizeof colorRef);
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	memset(&depthRef, 0, sizeof depthRef);
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	memset(&subpass, 0, sizeof subpass);
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	/*
	 * Consecutive instances of this render pass are not implicitly ordered, and
	 * texture uploads slot in between them, so both directions are declared
	 * with masks broad enough to cover draw, clear, transfer and sampling.
	 */
	memset(dependencies, 0, sizeof dependencies);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	createInfo.attachmentCount = 2;
	createInfo.pAttachments = attachments;
	createInfo.subpassCount = 1;
	createInfo.pSubpasses = &subpass;
	createInfo.dependencyCount = 2;
	createInfo.pDependencies = dependencies;
	VK_CHECK(vkCreateRenderPass(g_device, &createInfo, NULL, &g_gameRenderPass), &contextError);

	/*
	 * Display pass. Clearing to black on load is what blacks out the
	 * letterbox/pillarbox bars, so clearUnusedArea() has nothing to do.
	 */
	memset(attachments, 0, sizeof attachments);
	attachments[0].format = g_swapchainFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	memset(&subpass, 0, sizeof subpass);
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	memset(dependencies, 0, sizeof dependencies);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	createInfo.attachmentCount = 1;
	createInfo.dependencyCount = 1;
	VK_CHECK(vkCreateRenderPass(g_device, &createInfo, NULL, &g_displayRenderPass), &contextError);
}

/* Pipelines */

static VkShaderModule createShaderModule(const uint32_t *code, size_t size)
{
	VkShaderModuleCreateInfo createInfo;
	VkShaderModule module = VK_NULL_HANDLE;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = size;
	createInfo.pCode = code;
	VK_CHECK(vkCreateShaderModule(g_device, &createInfo, NULL, &module), &shaderError);

	return module;
}

static void createSamplers(void)
{
	VkSamplerCreateInfo createInfo;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.minFilter = VK_FILTER_LINEAR;
	createInfo.magFilter = linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.maxLod = VK_LOD_CLAMP_NONE;
	createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	VK_CHECK(vkCreateSampler(g_device, &createInfo, NULL, &g_gameSampler), &contextError);

	createInfo.magFilter = (!fixedFramebufferSize || !framebufferLinearFiltering) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	VK_CHECK(vkCreateSampler(g_device, &createInfo, NULL, &g_displaySampler), &contextError);
}

static void createPipelineLayouts(void)
{
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo layoutInfo;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo pipelineLayoutInfo;

	memset(&binding, 0, sizeof binding);
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	memset(&layoutInfo, 0, sizeof layoutInfo);
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;
	VK_CHECK(vkCreateDescriptorSetLayout(g_device, &layoutInfo, NULL, &g_descriptorSetLayout), &contextError);

	memset(&range, 0, sizeof range);
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(GamePushConstants);

	memset(&pipelineLayoutInfo, 0, sizeof pipelineLayoutInfo);
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &g_descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &range;
	VK_CHECK(vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, NULL, &g_gamePipelineLayout), &contextError);

	range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(DisplayPushConstants);
	VK_CHECK(vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, NULL, &g_displayPipelineLayout), &contextError);
}

static void createPipelines(void)
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription bindings[4];
	VkVertexInputAttributeDescription attributes[4];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineViewportStateCreateInfo viewportState;
	VkPipelineRasterizationStateCreateInfo rasterization;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipelineColorBlendAttachmentState blendAttachment;
	VkPipelineColorBlendStateCreateInfo colorBlend;
	VkDynamicState dynamicStates[3];
	VkPipelineDynamicStateCreateInfo dynamicState;
	VkGraphicsPipelineCreateInfo createInfo;
	uint32_t variant;

	g_gameVertModule = createShaderModule(g_gameVertSpv, sizeof g_gameVertSpv);
	g_gameFragModule = createShaderModule(g_gameFragSpv, sizeof g_gameFragSpv);
	g_displayVertModule = createShaderModule(g_displayVertSpv, sizeof g_displayVertSpv);
	g_displayFragModule = createShaderModule(g_displayFragSpv, sizeof g_displayFragSpv);

	memset(stages, 0, sizeof stages);
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = g_gameVertModule;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = g_gameFragModule;
	stages[1].pName = "main";

	/* Four tightly packed streams, matching the OpenGL client arrays. */
	memset(bindings, 0, sizeof bindings);
	bindings[0].binding = 0; bindings[0].stride = sizeof(float) * 3;
	bindings[1].binding = 1; bindings[1].stride = sizeof(float) * 4;
	bindings[2].binding = 2; bindings[2].stride = 4;
	bindings[3].binding = 3; bindings[3].stride = 1;

	memset(attributes, 0, sizeof attributes);
	attributes[0].location = 0; attributes[0].binding = 0; attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[1].location = 1; attributes[1].binding = 1; attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[2].location = 2; attributes[2].binding = 2; attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
	attributes[3].location = 3; attributes[3].binding = 3; attributes[3].format = VK_FORMAT_R8_UNORM;

	memset(&vertexInput, 0, sizeof vertexInput);
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 4;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 4;
	vertexInput.pVertexAttributeDescriptions = attributes;

	memset(&inputAssembly, 0, sizeof inputAssembly);
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	memset(&viewportState, 0, sizeof viewportState);
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	memset(&rasterization, 0, sizeof rasterization);
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	/* grCullMode is a no-op in the OpenGL backend and culling was never
	 * enabled there, so the game relies on both faces being drawn. */
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	memset(&multisample, 0, sizeof multisample);
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	memset(&depthStencil, 0, sizeof depthStencil);
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.maxDepthBounds = 1.0f;

	memset(&blendAttachment, 0, sizeof blendAttachment);
	blendAttachment.blendEnable = VK_TRUE;
	blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	memset(&colorBlend, 0, sizeof colorBlend);
	colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &blendAttachment;

	dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
	dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;
	dynamicStates[2] = VK_DYNAMIC_STATE_LINE_WIDTH;

	memset(&dynamicState, 0, sizeof dynamicState);
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 3;
	dynamicState.pDynamicStates = dynamicStates;

	memset(&createInfo, 0, sizeof createInfo);
	createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	createInfo.stageCount = 2;
	createInfo.pStages = stages;
	createInfo.pVertexInputState = &vertexInput;
	createInfo.pInputAssemblyState = &inputAssembly;
	createInfo.pViewportState = &viewportState;
	createInfo.pRasterizationState = &rasterization;
	createInfo.pMultisampleState = &multisample;
	createInfo.pDepthStencilState = &depthStencil;
	createInfo.pColorBlendState = &colorBlend;
	createInfo.pDynamicState = &dynamicState;
	createInfo.layout = g_gamePipelineLayout;
	createInfo.renderPass = g_gameRenderPass;

	/* bit 2 = blend dst ONE, bit 1 = depth write, bit 0 = line topology */
	for (variant = 0; variant < PipelineVariants; ++variant)
	{
		VkBlendFactor dstFactor = (variant & 4) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

		blendAttachment.dstColorBlendFactor = dstFactor;
		blendAttachment.dstAlphaBlendFactor = dstFactor;
		depthStencil.depthWriteEnable = (variant & 2) ? VK_TRUE : VK_FALSE;
		inputAssembly.topology = (variant & 1) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VK_CHECK(vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &createInfo, NULL, &g_gamePipelines[variant]), &shaderError);
	}

	/* Display pipeline: full screen strip, no depth, no blending. */
	stages[0].module = g_displayVertModule;
	stages[1].module = g_displayFragModule;

	bindings[0].stride = sizeof(float) * 2;
	bindings[1].stride = sizeof(float) * 2;
	attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	vertexInput.vertexBindingDescriptionCount = 2;
	vertexInput.vertexAttributeDescriptionCount = 2;

	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;
	blendAttachment.blendEnable = VK_FALSE;
	createInfo.pDepthStencilState = NULL;
	createInfo.layout = g_displayPipelineLayout;
	createInfo.renderPass = g_displayRenderPass;

	VK_CHECK(vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &createInfo, NULL, &g_displayPipeline), &shaderError);
}

/* Per-frame resources */

static void createFrames(void)
{
	uint32_t i;

	for (i = 0; i < FramesInFlight; ++i)
	{
		Frame *frame = &g_frames[i];
		VkCommandPoolCreateInfo poolInfo;
		VkCommandBufferAllocateInfo allocInfo;
		VkFenceCreateInfo fenceInfo;
		VkSemaphoreCreateInfo semaphoreInfo;

		memset(&poolInfo, 0, sizeof poolInfo);
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = g_queueFamily;
		VK_CHECK(vkCreateCommandPool(g_device, &poolInfo, NULL, &frame->commandPool), &contextError);

		memset(&allocInfo, 0, sizeof allocInfo);
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = frame->commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		VK_CHECK(vkAllocateCommandBuffers(g_device, &allocInfo, &frame->commandBuffer), &contextError);

		memset(&fenceInfo, 0, sizeof fenceInfo);
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		VK_CHECK(vkCreateFence(g_device, &fenceInfo, NULL, &frame->fence), &contextError);

		memset(&semaphoreInfo, 0, sizeof semaphoreInfo);
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VK_CHECK(vkCreateSemaphore(g_device, &semaphoreInfo, NULL, &frame->imageAvailable), &contextError);

		createBuffer(VertexRingBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&frame->vertexBuffer, &frame->vertexMemory, &frame->vertexMapped);

		createBuffer(StagingRingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&frame->stagingBuffer, &frame->stagingMemory, &frame->stagingMapped);
	}
}

static void destroyFrames(void)
{
	uint32_t i;

	for (i = 0; i < FramesInFlight; ++i)
	{
		Frame *frame = &g_frames[i];

		if (frame->vertexMemory != VK_NULL_HANDLE)
		{
			vkUnmapMemory(g_device, frame->vertexMemory);
			vkDestroyBuffer(g_device, frame->vertexBuffer, NULL);
			vkFreeMemory(g_device, frame->vertexMemory, NULL);
		}
		if (frame->stagingMemory != VK_NULL_HANDLE)
		{
			vkUnmapMemory(g_device, frame->stagingMemory);
			vkDestroyBuffer(g_device, frame->stagingBuffer, NULL);
			vkFreeMemory(g_device, frame->stagingMemory, NULL);
		}
		if (frame->imageAvailable != VK_NULL_HANDLE)
			vkDestroySemaphore(g_device, frame->imageAvailable, NULL);
		if (frame->fence != VK_NULL_HANDLE)
			vkDestroyFence(g_device, frame->fence, NULL);
		if (frame->commandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(g_device, frame->commandPool, NULL);

		memset(frame, 0, sizeof *frame);
	}
}

/* Dummy texture, bound whenever the game draws untextured geometry */

static void createDummyTexture(void)
{
	static const uint32_t white = 0xFFFFFFFFu;
	Frame *frame;
	VkDeviceSize offset;
	VkBufferImageCopy copy;

	createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		&g_dummyImage, &g_dummyMemory, &g_dummyView);

	g_dummySet = allocateDescriptorSet();
	writeDescriptorSet(g_dummySet, g_dummyView, g_gameSampler);

	beginCommandBuffer();
	frame = currentFrame();

	imageBarrier(frame->commandBuffer, g_dummyImage, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	offset = ringAlloc(&frame->stagingOffset, StagingRingBytes, sizeof white, 16);
	memcpy(frame->stagingMapped + offset, &white, sizeof white);

	memset(&copy, 0, sizeof copy);
	copy.bufferOffset = offset;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = 1;
	copy.imageExtent.height = 1;
	copy.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(frame->commandBuffer, frame->stagingBuffer, g_dummyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	imageBarrier(frame->commandBuffer, g_dummyImage, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

/* Offscreen render target */

static void destroyFrameBuffer(void)
{
	if (g_fbFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(g_device, g_fbFramebuffer, NULL);
		g_fbFramebuffer = VK_NULL_HANDLE;
	}
	if (g_fbDepthView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(g_device, g_fbDepthView, NULL);
		vkDestroyImage(g_device, g_fbDepthImage, NULL);
		vkFreeMemory(g_device, g_fbDepthMemory, NULL);
		g_fbDepthView = VK_NULL_HANDLE;
		g_fbDepthImage = VK_NULL_HANDLE;
		g_fbDepthMemory = VK_NULL_HANDLE;
	}
	if (g_fbView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(g_device, g_fbView, NULL);
		vkDestroyImage(g_device, g_fbImage, NULL);
		vkFreeMemory(g_device, g_fbMemory, NULL);
		g_fbView = VK_NULL_HANDLE;
		g_fbImage = VK_NULL_HANDLE;
		g_fbMemory = VK_NULL_HANDLE;
	}
	g_framebufferHeight = 0;
	g_framebufferWidth = 0;
}

static void createFrameBuffer(void)
{
	int32_t shorterEdge = fixedFramebufferSize ? SDL_min(initialWinWidth, initialWinHeight) : SDL_min(winWidth, winHeight);
	int32_t w, h;
	VkImageView attachments[2];
	VkFramebufferCreateInfo framebufferInfo;
	Frame *frame;

	if (g_framebufferHeight == shorterEdge)
		return;

	w = shorterEdge * 4 / 3;
	h = shorterEdge;

	vkDeviceWaitIdle(g_device);
	destroyFrameBuffer();

	createImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		&g_fbImage, &g_fbMemory, &g_fbView);

	createImage(w, h, g_fbDepthFormat,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		&g_fbDepthImage, &g_fbDepthMemory, &g_fbDepthView);

	attachments[0] = g_fbView;
	attachments[1] = g_fbDepthView;

	memset(&framebufferInfo, 0, sizeof framebufferInfo);
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = g_gameRenderPass;
	framebufferInfo.attachmentCount = 2;
	framebufferInfo.pAttachments = attachments;
	framebufferInfo.width = w;
	framebufferInfo.height = h;
	framebufferInfo.layers = 1;
	if (vkCreateFramebuffer(g_device, &framebufferInfo, NULL, &g_fbFramebuffer) != VK_SUCCESS)
		fail(&framebufferError);

	if (g_fbDescriptorSet == VK_NULL_HANDLE)
		g_fbDescriptorSet = allocateDescriptorSet();
	writeDescriptorSet(g_fbDescriptorSet, g_fbView, g_displaySampler);

	/*
	 * The game render pass declares COLOR_ATTACHMENT_OPTIMAL / DEPTH_STENCIL_
	 * ATTACHMENT_OPTIMAL as its initial layouts, so move both images out of
	 * UNDEFINED once, here, rather than special-casing the first pass.
	 */
	beginCommandBuffer();
	frame = currentFrame();
	imageBarrier(frame->commandBuffer, g_fbImage, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	imageBarrier(frame->commandBuffer, g_fbDepthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

	g_framebufferHeight = shorterEdge;
	g_framebufferWidth = w;
}

/* Context */

static void createContext(void)
{
	createInstance();

	if (!SDL_Vulkan_CreateSurface(sdlWin, g_instance, &g_surface))
		fail(&contextError);

	pickPhysicalDevice();
	createDevice();
	createPipelineLayouts();
	createSamplers();
	pickSurfaceFormat();
	createRenderPasses();
	createSwapchain();
	createPipelines();
	createFrames();
	createDummyTexture();
}

static void destroyContext(void)
{
	uint32_t i;

	if (g_device == VK_NULL_HANDLE)
		return;

	vkDeviceWaitIdle(g_device);

	destroyFrameBuffer();

	for (i = 0; i < g_texturePoolCount; ++i)
	{
		Texture *tex = &g_texturePool[i];
		if (tex->view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(g_device, tex->view, NULL);
			vkDestroyImage(g_device, tex->image, NULL);
			vkFreeMemory(g_device, tex->memory, NULL);
		}
		tex->view = VK_NULL_HANDLE;
		tex->image = VK_NULL_HANDLE;
		tex->memory = VK_NULL_HANDLE;
		tex->set = VK_NULL_HANDLE;
		tex->valid = false;
	}

	if (g_dummyView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(g_device, g_dummyView, NULL);
		vkDestroyImage(g_device, g_dummyImage, NULL);
		vkFreeMemory(g_device, g_dummyMemory, NULL);
		g_dummyView = VK_NULL_HANDLE;
	}

	destroyFrames();
	destroySwapchain();

	for (i = 0; i < PipelineVariants; ++i)
	{
		if (g_gamePipelines[i] != VK_NULL_HANDLE)
			vkDestroyPipeline(g_device, g_gamePipelines[i], NULL);
		g_gamePipelines[i] = VK_NULL_HANDLE;
	}
	if (g_displayPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(g_device, g_displayPipeline, NULL);

	vkDestroyShaderModule(g_device, g_gameVertModule, NULL);
	vkDestroyShaderModule(g_device, g_gameFragModule, NULL);
	vkDestroyShaderModule(g_device, g_displayVertModule, NULL);
	vkDestroyShaderModule(g_device, g_displayFragModule, NULL);

	vkDestroyRenderPass(g_device, g_gameRenderPass, NULL);
	vkDestroyRenderPass(g_device, g_displayRenderPass, NULL);

	vkDestroySampler(g_device, g_gameSampler, NULL);
	vkDestroySampler(g_device, g_displaySampler, NULL);

	vkDestroyPipelineLayout(g_device, g_gamePipelineLayout, NULL);
	vkDestroyPipelineLayout(g_device, g_displayPipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(g_device, g_descriptorSetLayout, NULL);

	for (i = 0; i < g_descriptorPoolCount; ++i)
		vkDestroyDescriptorPool(g_device, g_descriptorPools[i], NULL);
	g_descriptorPoolCount = 0;
	g_descriptorPoolRemaining = 0;
	g_fbDescriptorSet = VK_NULL_HANDLE;
	g_dummySet = VK_NULL_HANDLE;

	vkDestroyDevice(g_device, NULL);
	g_device = VK_NULL_HANDLE;

	vkDestroySurfaceKHR(g_instance, g_surface, NULL);
	vkDestroyInstance(g_instance, NULL);
	g_instance = VK_NULL_HANDLE;
	g_surface = VK_NULL_HANDLE;

	g_gameRenderPassActive = false;
	g_swapchainImageAcquired = false;
	g_frameIndex = 0;
}

static void recreateContext(void)
{
	uint32_t i;

	destroyContext();
	createContext();

	/* Restore textures. The Glide-side copy in g_textureMem survives, so each
	 * live texture can simply be re-uploaded from it. */
	for (i = 0; i < g_texturePoolCount; ++i)
	{
		Texture *tex = &g_texturePool[i];
		if (tex->data == NULL)
			continue;
		tex->createdFormat = VK_FORMAT_UNDEFINED;
		tex->createdSize = 0;
		uploadTexture(tex);
	}
}
