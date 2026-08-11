// SPDX-License-Identifier: MIT
//
// Vulkan port of the OpenGL 2 game fragment shader.
//
// Kept arithmetically identical to the GLSL 110 original so the two backends
// can be diffed pixel for pixel. The discard is Glide's alpha test, which the
// OpenGL backend also folded into the shader rather than using fixed function.

#version 450

layout(location = 0) in vec4 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vFog;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextureSampler;

layout(push_constant) uniform GamePushConstants
{
	mat4 uMatrix;
	vec4 uFogColor;
	float uTextureEnabled;
	float uFogEnabled;
} pc;

void main()
{
	vec4 tex = pc.uTextureEnabled * texture(uTextureSampler, vTexCoord.st / vTexCoord.q);
	vec4 ret = vColor * ((1.0 - pc.uTextureEnabled) + tex);

	if (ret.a <= 16.0 / 255.0)
		discard;

	ret.rgb = mix(pc.uFogColor.rgb, ret.rgb, 1.0 - (1.0 - vFog) * pc.uFogEnabled);

	outColor = ret;
}
