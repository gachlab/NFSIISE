// SPDX-License-Identifier: MIT
//
// Vulkan port of the OpenGL 2 game vertex shader.
//
// The uniforms that OpenGL set with glUniform* live in a single push constant
// block (88 bytes, comfortably below the 128 byte guaranteed minimum), shared
// verbatim with game.frag -- Vulkan requires the block to be declared
// identically in every stage that consumes it.

#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aFog;

layout(location = 0) out vec4 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vFog;

layout(push_constant) uniform GamePushConstants
{
	mat4 uMatrix;
	vec4 uFogColor;
	float uTextureEnabled;
	float uFogEnabled;
} pc;

void main()
{
	vTexCoord = aTexCoord;
	vColor = aColor;
	vFog = aFog;

	// OpenGL declared aPosition as vec4 and fed it three floats, so w defaulted
	// to 1.0. Vulkan has no such fixup, hence the explicit vec4().
	gl_Position = pc.uMatrix * vec4(aPosition, 1.0);
}
