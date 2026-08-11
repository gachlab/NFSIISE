// SPDX-License-Identifier: MIT
//
// Vulkan port of the OpenGL 2 display fragment shader (gamma correction).
//
// The swapchain is deliberately created with a UNORM format rather than SRGB:
// the OpenGL backend renders to a plain non-sRGB default framebuffer, so an
// sRGB swapchain would apply a second, silent encode and wash the image out.

#version 450

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextureSampler;

layout(push_constant) uniform DisplayPushConstants
{
	float uGamma;
} pc;

void main()
{
	vec4 tex = texture(uTextureSampler, vTexCoord);
	// The OpenGL backend's offscreen target was GL_RGB, so sampling it always
	// yielded alpha = 1. The Vulkan target is RGBA8, so force it to match.
	outColor = vec4(pow(tex.rgb, vec3(1.0 / pc.uGamma)), 1.0);
}
