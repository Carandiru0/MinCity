#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

#define fragment_shader

#include "screendimensions.glsl"
#include "common.glsl"

layout (constant_id = 4) const int array_size = 1;

layout (push_constant) restrict readonly uniform PushConstant {
	layout(offset=0) uint		array_index;
	layout(offset=4) uint		type;
} pc;

layout(binding = 1) uniform sampler2DArray sdfTexture[array_size];

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

#define RGBA_IMAGE 0
#define ARRAY_IMAGE 1

vec4 textureRGBA(in const vec2 uv)
{
	vec3 uvw;

	const vec2 texture_resolution = vec2(textureSize(sdfTexture[pc.array_index], 0).xy);

	uvw.xy = subpixel_filter(uv * texture_resolution) / texture_resolution; // anti-aliasing

	// if pc.type is RGBA_IMAGE, this will always result in layer 0
	// ""   ""   "" ARRAY_IMAGE, this will result in layer corresponding to current sequence frame
	uvw.z = max(0.0f, float(int(pc.type) - ARRAY_IMAGE));

	return( textureLod(sdfTexture[pc.array_index], uvw, 0) );
}


void main() {    
	
	vec4 color = textureRGBA(fragUv);

	// pre-multiply alpha
	color.rgb = color.rgb * color.a;

	// composite - note this works best and is simplest.
	outColor = fragColor * color;
}