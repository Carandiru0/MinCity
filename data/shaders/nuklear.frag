#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_vote: enable
#extension GL_EXT_control_flow_attributes : enable

#define fragment_shader

#include "screendimensions.glsl"
#include "common.glsl"

layout (constant_id = 4) const int array_size = 1;

layout (push_constant) restrict readonly uniform PushConstant {
	layout(offset=0) uint		array_index;
	layout(offset=4) uint		type;
} pc;

layout(binding = 1) uniform sampler2DArray sdfTexture[array_size];	// RG8 texture containing shade, distance

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec2 fragGrey;

layout(location = 0) out vec4 outColor;

#define shade r
#define dist g
#define shadedist rg
#define alpha g
#define shadealpha rg

#define SDF_IMAGE 0
#define RGBA_IMAGE 1
#define ARRAY_IMAGE 2

vec2 textureRGBA(in const vec3 uvw) // returns greyscale + alpha
{
	const vec4 color = textureLod(sdfTexture[pc.array_index], uvw, 0);

	return( vec2(dot(color.rgb, LUMA), color.a) );
}

vec2 textureRGBA(in const vec2 uv)
{
	vec3 uvw;
	uvw.xy = uv;
	// if pc.type is RGBA_IMAGE, this will always result in layer 0
	// ""   ""   "" ARRAY_IMAGE, this will result in layer corresponding to current sequence frame
	uvw.z = max(0.0f, float(int(pc.type) - ARRAY_IMAGE));

	// *first
	vec2 last = textureRGBA(uvw);
	last.shade = last.shade * last.alpha;

	return(last);
}

vec2 textureSDF(in const vec3 uvw) // returns greyscale + distance
{
	// .r = shade , .g = distance
	return( textureLod(sdfTexture[pc.array_index], uvw, 0).shadedist );
}

vec2 textureSDF(in const vec2 uv)
{
	vec3 uvw;
	uvw.xy = uv;
	uvw.z = 0.0f;

	// *first
	vec2 last = textureSDF(uvw);
	last.shade = last.shade * last.dist;

	// *layers
	for (uvw.z = 1.0f ; uvw.z < 8.0f ; ++uvw.z )
	{
		vec2 cur = textureSDF(uvw);
		cur.shade = mix(last.shade, cur.shade, cur.dist);

		// for the next layer //
		last.dist = last.dist + cur.dist * (1.0f - last.dist);
	
		last.shade = mix(last.shade, cur.shade, last.dist);		
	}

	const float half_distance_dt = fwidth(last.dist) * 0.5f;
	
	last.alpha = smoothstep(-half_distance_dt, half_distance_dt, last.dist - 0.5f ); // conversion of signed distance to alpha

	return(last);
}

void main() {    
	
	vec2 grey; // all gui pixels reduce to greyscale and alpha regardless of input texture by design
				 // post processing is used to colorize the gui pixels.
	
	[[flatten]] if(subgroupAll(SDF_IMAGE == pc.type)) {	// this is evaluated as a scalar subgroup operation, conditional on const readonly push constant
											// so it has minimal implications on performance
		grey = textureSDF(fragUv);
	}
	else /*RGBA_IMAGE or ARRAY_IMAGE*/ {
		
		grey = textureRGBA(fragUv);
	}

	// greyscale
	grey.shade = 2.0f * grey.alpha * fragGrey.shade * grey.shade;

	// alpha
	grey.alpha = grey.alpha * fragGrey.alpha * (1.0f - grey.alpha);
	
	outColor = grey.xyxy;  // grey-alpha-grey-alpha = r-g-b-s
}