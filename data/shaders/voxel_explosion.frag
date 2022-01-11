#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable
#extension GL_KHR_shader_subgroup_arithmetic: enable

#define subgroup_quad_enabled
#define fragment_shader
#include "screendimensions.glsl"
#include "common.glsl"

layout(early_fragment_tests) in;

#define TRANS

layout(location = 0) out vec4 outColor;

#include "voxel_fragment.glsl"


//------------ lighting
#ifdef TRANS 

#define OUT_REFLECTION
#define OUT_FRESNEL

#define FRESNEL_ETA_N1 1.00f	// medium being left
#define FRESNEL_ETA_N2 1.88f	// medium being entered 

#endif

//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"

layout (constant_id = 4) const float VolumeLength = 0.0f;
layout (constant_id = 5) const float LightVolumeDimensions_X = 0.0f;
layout (constant_id = 6) const float LightVolumeDimensions_Y = 0.0f;
layout (constant_id = 7) const float LightVolumeDimensions_Z = 0.0f; 
layout (constant_id = 8) const float InvLightVolumeDimensions_X = 0.0f;
layout (constant_id = 9) const float InvLightVolumeDimensions_Y = 0.0f;
layout (constant_id = 10) const float InvLightVolumeDimensions_Z = 0.0f; 
#define LightVolumeDimensions vec3(LightVolumeDimensions_X, LightVolumeDimensions_Y, LightVolumeDimensions_Z)
#define InvLightVolumeDimensions vec3(InvLightVolumeDimensions_X, InvLightVolumeDimensions_Y, InvLightVolumeDimensions_Z)  

layout (binding = 3) uniform sampler3D volumeMap[2];
#if defined(TRANS)
layout (binding = 6) uniform sampler2D colorMap;
#endif
layout (input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput ambientLightMap;

const float ROUGHNESS = 0.11f;

#include "lightmap.glsl"
#include "lighting.glsl"

// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
#include "texturearray.glsl"
//-------------

#define SHOCKWAVE_BASE_COLOR (vec3(0.98f))

vec3 blackbody(in const float norm)
{
	return(textureLod(_texArray[TEX_BLACKBODY], vec3(norm, 0.0f, 0.0f), 0).rgb);
}

vec3 haze(in vec3 color, in const vec3 reflect_color, in const float fresnelTerm, in const float density, in const float w)
{
	color = mix(color + In._passthru * density, reflect_color, fresnelTerm);
	color = mix(color, color + In._passthru, density); 
	return(color);
}  

// voxels, w/lighting 
void main() {
  
    vec3 light_color;
	float attenuation;

	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength); 
	 
	vec3 N = normalize(In.N.xyz);
	vec3 V = normalize(In.V.xyz);  
	 
	float luminance = dot(light_color, LUMA);
	
	vec3 reflect_color;
	float fresnelTerm;
	vec3 lit_color = lit( light_color, light_color,
						 1.0f, attenuation,
	                     In._emission, ROUGHNESS,
						 L, N, V, reflect_color, fresnelTerm );

	//float luminance = 1.0f - dot(lit_color.rgb, LUMA) * lit_color.r;
	// shockwave dynamic density - working do not change // In._extra_data = distance
	//const float density = 1.0f - (In._extra_data + normalize(subgroupInclusiveAdd(In._extra_data).xxx)).x * 0.5f;
	
	vec3 color;
	const float weight = refraction_color(color, colorMap, 1.0f);

	/*
	lit_color = mix(lit_color, color.rgb, 1.0f - density);

	float luminance = dot(lit_color, LUMA);

	color.rgb = haze(color.rgb, reflect_color, fresnelTerm, density * 0.9f * aaStep(density, luminance), In._extra_data * 0.5f) * 0.77f;
	color.rgb = haze(color.rgb, reflect_color, fresnelTerm, density * luminance, In._extra_data * 0.5f);

	luminance = dot(lit_color, LUMA);
	color.rgb = mix(color + aaStep(0.75f, density * (1.0f - luminance)), lit_color, luminance);

	luminance = dot(color.rgb, LUMA);        
	  */                                       
	outColor = applyTransparency(vec3(luminance), 1.0f, weight);                             
	// outColor = vec4(weight.xxx, 1.0f);                                                                   
}
  

