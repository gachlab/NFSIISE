// SPDX-License-Identifier: MIT
//
// The Glide2x entry points for the Vulkan backend.
// Included by Glide2x/Vulkan.c -- not a standalone translation unit.
//
// Ordered to match Glide2x/OpenGL2.c so the two can be read side by side.

REALIGN STDCALL void grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)
{
	drawTriangles();
	switch (rgb_df)
	{
		case GR_BLEND_ONE:
			g_blendDstFactor = VK_BLEND_FACTOR_ONE;
			break;
		case GR_BLEND_ONE_MINUS_SRC_ALPHA:
			g_blendDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
	}
}
REALIGN STDCALL void grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, BOOL invert)
{
	drawTriangles();
	g_textureEnabled = (other == GR_COMBINE_OTHER_TEXTURE);
}
REALIGN STDCALL void grAlphaTestFunction(GrCmpFnc_t function)
{
	/* Folded into the fragment shader's discard, as in the OpenGL backend. */
}
REALIGN STDCALL void grAlphaTestReferenceValue(GrAlpha_t value)
{
}
REALIGN STDCALL void grClipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY)
{
	float ratio = g_framebufferHeight / 480.0f;

	int32_t scaledMinX = minX * ratio;
	int32_t scaledMinY = minY * ratio;
	int32_t scaledMaxX = maxX * ratio + 0.5f;
	int32_t scaledMaxY = maxY * ratio + 0.5f;

	drawTriangles();

	/*
	 * Negative height flips Vulkan's NDC to match OpenGL's, which is what lets
	 * the matrix maths below stay identical to the OpenGL backend's.
	 */
	g_viewport.x = (float)scaledMinX;
	g_viewport.y = (float)scaledMaxY;
	g_viewport.width = (float)(scaledMaxX - scaledMinX);
	g_viewport.height = -(float)(scaledMaxY - scaledMinY);
	g_viewport.minDepth = 0.0f;
	g_viewport.maxDepth = 1.0f;

	/* Vulkan scissors are always top-left based, unlike glScissor. */
	g_scissor.offset.x = scaledMinX;
	g_scissor.offset.y = scaledMinY;
	g_scissor.extent.width = (uint32_t)(scaledMaxX - scaledMinX);
	g_scissor.extent.height = (uint32_t)(scaledMaxY - scaledMinY);

	g_lineWidth = 2.0f * ratio;

	matrixLoadIdentity();
	matrixOrtho(scaledMinX, scaledMaxX, scaledMaxY, scaledMinY, Near, Far);
	matrixScale2(ratio, ratio);
}
REALIGN STDCALL void grBufferClear(GrColor_t color, GrAlpha_t alpha, uint16_t depth)
{
	float r, g, b, a;
	VkClearAttachment attachments[2];
	VkClearRect rect;
	uint32_t attachmentCount = 1;

	convertColor(color, &alpha, &r, &g, &b, &a);

	drawTriangles();

	/* vkCmdClearAttachments is a render pass command. */
	beginGameRenderPass();

	memset(attachments, 0, sizeof attachments);
	attachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	attachments[0].colorAttachment = 0;
	attachments[0].clearValue.color.float32[0] = r;
	attachments[0].clearValue.color.float32[1] = g;
	attachments[0].clearValue.color.float32[2] = b;
	attachments[0].clearValue.color.float32[3] = a;

	/*
	 * glClear(GL_DEPTH_BUFFER_BIT) is gated on the depth write mask, but
	 * vkCmdClearAttachments is not -- so gate it here to keep the two backends
	 * in step when the game clears with depth writes disabled.
	 */
	if (g_depthMask)
	{
		attachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		attachments[1].clearValue.depthStencil.depth = depth / 65535.0f;
		attachmentCount = 2;
	}

	memset(&rect, 0, sizeof rect);
	rect.rect = g_scissor;
	rect.layerCount = 1;

	vkCmdClearAttachments(currentFrame()->commandBuffer, attachmentCount, attachments, 1, &rect);
}
REALIGN STDCALL void grChromakeyMode(GrChromakeyMode_t mode)
{
}
REALIGN STDCALL void grChromakeyValue(GrColor_t value)
{
}

/* Stages the display quad's two vec2 streams, mirroring stageVertexStreams(). */
static void stageDisplayStreams(void)
{
	Frame *frame = currentFrame();
	VkDeviceSize size = sizeof g_verticesDisp;
	VkDeviceSize base;

	base = ringAlloc(&frame->vertexOffset, VertexRingBytes, size * 2 + 16, 16);

	g_streamOffsets[0] = base;
	memcpy(frame->vertexMapped + base, g_verticesDisp, (size_t)size);

	g_streamOffsets[1] = (base + size + 15) & ~(VkDeviceSize)15;
	memcpy(frame->vertexMapped + g_streamOffsets[1], g_textureCoordDisp, (size_t)size);
}

REALIGN STDCALL void grBufferSwap(int swap_interval)
{
	countFrame();

	Frame *frame;
	VkCommandBuffer cmd;
	VkResult result;
	VkViewport savedViewport = g_viewport;
	VkRect2D savedScissor = g_scissor;
	VkRenderPassBeginInfo passInfo;
	VkClearValue clearValue;
	VkViewport viewport;
	VkRect2D scissor;
	VkBuffer buffers[2];
	DisplayPushConstants pc;
	VkSubmitInfo submitInfo;
	VkPresentInfoKHR presentInfo;
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	int32_t xOffset = 0, yOffset = 0;
	int32_t visibleWidth = 0, visibleHeight = 0;
	int32_t scaledMaxX, scaledMaxY;
	float widthRatio = winWidth / 640.0f;
	float heightRatio = winHeight / 480.0f;
	BOOL presentable = true;

	drawTriangles();

	/* Both of these can wrap the ring and force a submit+wait, so they run
	 * before anything for the display pass is recorded. */
	beginCommandBuffer();
	stageDisplayStreams();

	frame = currentFrame();
	cmd = frame->commandBuffer;

	endGameRenderPass();

	result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX, frame->imageAvailable, VK_NULL_HANDLE, &g_swapchainIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreateSwapchain();
		result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX, frame->imageAvailable, VK_NULL_HANDLE, &g_swapchainIndex);
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		/* Drop this frame's presentation; the game work still gets submitted so
		 * the offscreen target and any texture uploads stay consistent. */
		presentable = false;
	}

	if (presentable)
	{
		if (keepAspectRatio)
		{
			if (widthRatio > heightRatio)
				widthRatio = heightRatio;
			else if (heightRatio > widthRatio)
				heightRatio = widthRatio;

			xOffset = winWidth  / 2 - widthRatio  * 320;
			yOffset = winHeight / 2 - heightRatio * 240;

			visibleWidth  = 640 * widthRatio  + 0.5f;
			visibleHeight = 480 * heightRatio + 0.5f;
		}

		/* The display render pass clears to black on load, which is what blacks
		 * out the bars; this only reports whether there are any. */
		clearUnusedArea(xOffset, yOffset, visibleWidth, visibleHeight);

		scaledMaxX = 640.0f * widthRatio  + 0.5f;
		scaledMaxY = 480.0f * heightRatio + 0.5f;

		imageBarrier(cmd, g_fbImage, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		memset(&clearValue, 0, sizeof clearValue);
		clearValue.color.float32[3] = 1.0f;

		memset(&passInfo, 0, sizeof passInfo);
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = g_displayRenderPass;
		passInfo.framebuffer = g_swapchainFramebuffers[g_swapchainIndex];
		passInfo.renderArea.extent = g_swapchainExtent;
		passInfo.clearValueCount = 1;
		passInfo.pClearValues = &clearValue;
		vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

		/*
		 * Ordinary positive-height viewport here (unlike the game pass): the
		 * offscreen image is stored top-down, and a positive viewport is what
		 * makes OpenGL's unmodified display texture coordinates land correctly.
		 */
		viewport.x = (float)xOffset;
		viewport.y = (float)yOffset;
		viewport.width = (float)scaledMaxX;
		viewport.height = (float)scaledMaxY;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		scissor.offset.x = xOffset;
		scissor.offset.y = yOffset;
		scissor.extent.width = (uint32_t)scaledMaxX;
		scissor.extent.height = (uint32_t)scaledMaxY;
		/* Clamp: winWidth/winHeight and the surface extent can disagree for a
		 * frame around a resize, and an out-of-bounds scissor is an error. */
		if ((uint32_t)scissor.offset.x + scissor.extent.width > g_swapchainExtent.width)
			scissor.extent.width = g_swapchainExtent.width - (uint32_t)scissor.offset.x;
		if ((uint32_t)scissor.offset.y + scissor.extent.height > g_swapchainExtent.height)
			scissor.extent.height = g_swapchainExtent.height - (uint32_t)scissor.offset.y;

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_displayPipeline);
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdSetLineWidth(cmd, 1.0f);

		pc.uGamma = g_gammaValue;
		vkCmdPushConstants(cmd, g_displayPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_displayPipelineLayout, 0, 1, &g_fbDescriptorSet, 0, NULL);

		buffers[0] = frame->vertexBuffer;
		buffers[1] = frame->vertexBuffer;
		vkCmdBindVertexBuffers(cmd, 0, 2, buffers, g_streamOffsets);

		vkCmdDraw(cmd, 4, 1, 0, 0);

		vkCmdEndRenderPass(cmd);

		imageBarrier(cmd, g_fbImage, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	}

	VK_CHECK(vkEndCommandBuffer(cmd), &contextError);
	frame->recording = false;

	memset(&submitInfo, 0, sizeof submitInfo);
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	if (presentable)
	{
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &frame->imageAvailable;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &g_renderFinished[g_swapchainIndex];
	}

	VK_CHECK(vkResetFences(g_device, 1, &frame->fence), &contextError);
	VK_CHECK(vkQueueSubmit(g_queue, 1, &submitInfo, frame->fence), &contextError);

	if (presentable)
	{
		memset(&presentInfo, 0, sizeof presentInfo);
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &g_renderFinished[g_swapchainIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &g_swapchain;
		presentInfo.pImageIndices = &g_swapchainIndex;

		result = vkQueuePresentKHR(g_queue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			recreateSwapchain();
	}

	/* Advance and wait for the frame we are about to overwrite. */
	g_frameIndex = (g_frameIndex + 1) % FramesInFlight;
	frame = currentFrame();
	VK_CHECK(vkWaitForFences(g_device, 1, &frame->fence, VK_TRUE, UINT64_MAX), &contextError);
	beginCommandBuffer();

	if (needRecreateGl)
	{
		recreateContext();
		needRecreateGl = false;
		windowResized = true;
	}

	if (windowResized)
	{
		recreateSwapchain();
		createFrameBuffer();
		grClipWindow(0, 0, 640, 480);
		windowResized = false;
	}
	else
	{
		g_viewport = savedViewport;
		g_scissor = savedScissor;
	}
}
REALIGN STDCALL void grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, BOOL invert)
{
}
REALIGN STDCALL void grCullMode(GrCullMode_t mode)
{
	/* The OpenGL backend never enabled culling either. */
}
REALIGN STDCALL void grDepthBiasLevel(int16_t level)
{
}
REALIGN STDCALL void grDepthBufferFunction(GrCmpFnc_t function)
{
	/* Fixed at LESS_OR_EQUAL, as in the OpenGL backend. */
}
REALIGN STDCALL void grDepthBufferMode(GrDepthBufferMode_t mode)
{
}
REALIGN STDCALL void grDepthMask(BOOL mask)
{
	drawTriangles();
	g_depthMask = mask;
}
REALIGN STDCALL void grDitherMode(GrDitherMode_t mode)
{
}
REALIGN STDCALL void grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
	const GrVertex *grVertices[3] = {a, b, c};
	uint32_t i;
	for (i = 0; i < 3; ++i)
	{
		const GrVertex *grVertex = grVertices[i];

		g_colorValues[g_trianglesCount].vertex[i].r = grVertex->r;
		g_colorValues[g_trianglesCount].vertex[i].g = grVertex->g;
		g_colorValues[g_trianglesCount].vertex[i].b = grVertex->b;
		g_colorValues[g_trianglesCount].vertex[i].a = grVertex->a;

		g_fogCoord[g_trianglesCount].vertex[i] = 255 - g_fogTable[(uint16_t)(1.0f / grVertex->oow)];

		g_textureCoord[g_trianglesCount].vertex[i].s = grVertex->tmuvtx[0].sow / 256.0f;
		g_textureCoord[g_trianglesCount].vertex[i].t = grVertex->tmuvtx[0].tow / 256.0f;
		g_textureCoord[g_trianglesCount].vertex[i].q = grVertex->oow;

		g_vertices[g_trianglesCount].vertex[i].x = grVertex->x - VertexSnap;
		g_vertices[g_trianglesCount].vertex[i].y = grVertex->y - VertexSnap;
		g_vertices[g_trianglesCount].vertex[i].z = grVertex->oow;
	}
	if (++g_trianglesCount >= MaxTriangles)
		drawTriangles();
}
REALIGN STDCALL void grDrawLine(const GrVertex *a, const GrVertex *b)
{
	const GrVertex *grVertices[2] = {a, b};
	uint32_t i;

	drawTriangles();

	for (i = 0; i < 2; ++i)
	{
		g_colorValues->vertex[i].r = grVertices[i]->r;
		g_colorValues->vertex[i].g = grVertices[i]->g;
		g_colorValues->vertex[i].b = grVertices[i]->b;
		g_colorValues->vertex[i].a = grVertices[i]->a;

		g_vertices->vertex[i].x = grVertices[i]->x - VertexSnap;
		g_vertices->vertex[i].y = grVertices[i]->y - VertexSnap;
		g_vertices->vertex[i].z = grVertices[i]->oow;
	}

	drawPrimitives(2, true);
}
REALIGN STDCALL void grFogColorValue(GrColor_t fogcolor)
{
	drawTriangles();
	convertColor(fogcolor, NULL, &g_fogColor[0], &g_fogColor[1], &g_fogColor[2], NULL);
}
REALIGN STDCALL void grFogMode(GrFogMode_t mode)
{
	drawTriangles();
	switch (mode)
	{
		case GR_FOG_DISABLE:
			g_fogEnabled = false;
			break;
		case GR_FOG_WITH_TABLE:
			g_fogEnabled = true;
			break;
	}
}
REALIGN STDCALL void grGammaCorrectionValue(float value)
{
	/* Consumed as a push constant by the display pass, so nothing to rebind. */
	g_gammaValue = value;
}
REALIGN STDCALL void grGlideInit(void)
{
}
REALIGN STDCALL void grGlideShutdown(void)
{
	destroyContext();
	g_palette = NULL;
}
REALIGN STDCALL BOOL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, BOOL pixelPipeline, GrLfbInfo_t *info)
{
	/* Same stub as the OpenGL backend: the game writes into this and the
	 * result is discarded. Nothing is ever read back from the framebuffer. */
	memset(info, 0, sizeof(GrLfbInfo_t));
	if (type == GR_LFB_WRITE_ONLY)
	{
		info->lfbPtr = GAME_ADDR(g_lfb = (uint8_t *)lowMemAlloc(640*480*2));
		info->strideInBytes = 2;
		return true;
	}
	return false;
}
REALIGN STDCALL BOOL grLfbUnlock(GrLock_t type, GrBuffer_t buffer)
{
	lowMemFree(g_lfb);
	g_lfb = NULL;
	return true;
}
REALIGN STDCALL void grRenderBuffer(GrBuffer_t buffer)
{
}
REALIGN STDCALL void grSstIdle(void)
{
}
REALIGN STDCALL BOOL grSstIsBusy(void)
{
	return false;
}
REALIGN STDCALL BOOL grSstQueryHardware(GrHwConfiguration *hwconfig)
{
	return true;
}
REALIGN STDCALL void grSstSelect(int which_sst)
{
}
REALIGN STDCALL uint32_t grSstStatus(void)
{
	return 0x0FFFF03F;
}
REALIGN STDCALL void grSstWinClose(void)
{
}
REALIGN STDCALL BOOL grSstWinOpen(uint32_t hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers)
{
	createContext();
	createFrameBuffer();

	handleDpr();

	grClipWindow(0, 0, 640, 480);

	return true;
}
REALIGN STDCALL uint32_t grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt)
{
	uint32_t size = 256 >> lodmax;
	size *= size;
	switch (fmt)
	{
		case GR_TEXFMT_P_8:
			break;
		case GR_TEXFMT_RGB_565:
		case GR_TEXFMT_ARGB_1555:
		case GR_TEXFMT_ARGB_4444:
			size <<= 1;
			break;
	}
	return size;
}
REALIGN STDCALL void grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode)
{
}
REALIGN STDCALL void grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor, GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, BOOL rgb_invert, BOOL alpha_invert)
{
}
REALIGN STDCALL void grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
}
REALIGN STDCALL void grTexDownloadMipMap(GrChipID_t tmu, uint32_t startAddress, uint32_t evenOdd, GrTexInfo *info)
{
	Texture *tex = textureForAddress(startAddress, true);
	uint16_t *dataIn, *dataOut;
	uint32_t sqrSize, i;

	if (!tex)
		return;

	tex->data = &g_textureMem[startAddress];
	tex->palette = NULL;
	tex->fmt = info->format;
	tex->size = 256 >> info->largeLod;

	dataIn  = (uint16_t *)GAME_PTR(info->data);
	dataOut = (uint16_t *)tex->data;

	drawTriangles();

	/* ARGB -> RGBA rotation, or a straight copy. Identical to the OpenGL
	 * backend: the Vulkan formats chosen in textureFormat() expect the same
	 * component order the OpenGL ones did. */
	sqrSize = tex->size * tex->size;
	switch (tex->fmt)
	{
		case GR_TEXFMT_ARGB_1555:
			for (i = 0; i < sqrSize; ++i)
			{
				uint16_t value = dataIn[i];
				dataOut[i] = (value << 1) | (value >> 15);
			}
			break;
		case GR_TEXFMT_ARGB_4444:
			for (i = 0; i < sqrSize; ++i)
			{
				uint16_t value = dataIn[i];
				dataOut[i] = (value << 4) | (value >> 12);
			}
			break;
		default:
			memcpy(dataOut, dataIn, sqrSize * (tex->fmt == GR_TEXFMT_P_8 ? 1 : 2));
			break;
	}

	uploadTexture(tex);
}
REALIGN STDCALL void grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void *data)
{
	if (type == GR_TEXTABLE_PALETTE)
		g_palette = (uint32_t *)data;
}
REALIGN STDCALL void grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode)
{
	/* Always linear */
}
REALIGN STDCALL uint32_t grTexMaxAddress(GrChipID_t tmu)
{
	return TextureMem;
}
REALIGN STDCALL uint32_t grTexMinAddress(GrChipID_t tmu)
{
	return 0;
}
REALIGN STDCALL void grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, BOOL lodBlend)
{
	/* mode = 0 */
}
REALIGN STDCALL void grTexSource(GrChipID_t tmu, uint32_t startAddress, uint32_t evenOdd, GrTexInfo *info)
{
	Texture *tex = textureForAddress(startAddress, false);

	drawTriangles();

	g_currentTexture = tex ? (int32_t)(tex - g_texturePool) : -1;

	if (tex && info->format == GR_TEXFMT_P_8 && g_palette && tex->palette != g_palette)
	{
		/* Update only when the palette or the texture changes (assume every
		 * palette has a different pointer). When the texture changes, palette
		 * is NULL. */
		const uint8_t *data = (const uint8_t *)tex->data;
		uint32_t size = 256 >> info->largeLod;
		uint32_t sqrSize = size * size, i;

		if (sqrSize > sizeof g_tmpTexture / sizeof *g_tmpTexture)
			return;

		for (i = 0; i < sqrSize; ++i)
		{
			uint32_t value = g_palette[data[i]];
			g_tmpTexture[i] = ((value >> 16) & 0x000000FF) | ((value << 16) & 0x00FF0000) | (value & 0xFF00FF00);
		}

		tex->size = size;
		uploadTextureData(tex, g_tmpTexture);
		tex->palette = g_palette;
	}
}
