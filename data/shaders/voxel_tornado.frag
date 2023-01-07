#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable
#extension GL_KHR_shader_subgroup_arithmetic: enable

layout(early_fragment_tests) in;

#define subgroup_quad_enabled
#define fragment_shader
#include "screendimensions.glsl"
#include "common.glsl"

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
layout (constant_id = 4) const float VolumeDimensions = 0.0f;
layout (constant_id = 5) const float InvVolumeDimensions = 0.0f;
layout (constant_id = 6) const float VolumeLength = 0.0f; // <--- beware this is scaled by voxel size, for lighting only
layout (constant_id = 7) const float InvVolumeLength = 0.0f; // <---- is ok, not scaled by voxel size.
layout (constant_id = 8) const float LightVolumeDimensions_X = 0.0f;
layout (constant_id = 9) const float LightVolumeDimensions_Y = 0.0f;
layout (constant_id = 10) const float LightVolumeDimensions_Z = 0.0f; 
layout (constant_id = 11) const float InvLightVolumeDimensions_X = 0.0f;
layout (constant_id = 12) const float InvLightVolumeDimensions_Y = 0.0f;
layout (constant_id = 13) const float InvLightVolumeDimensions_Z = 0.0f; 
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
//-------------

#define SHOCKWAVE_BASE_COLOR (vec3(0.98f))

vec3 haze(in vec3 color, in const vec3 reflect_color, in const float fresnelTerm, in const float density, in const float w)
{
	color = mix(color + In._passthru * density, reflect_color, fresnelTerm);
	color = mix(color, color + In._passthru, density); 
	return(color);
}  

// voxels, w/lighting 
void main() {
  
    vec3 light_color;
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz); 
	 
	vec3 N = normalize(vec3(2.0f * In._passthru, -In._passthru, 2.0f * In._passthru));//vec3(0.0f, 1.0f, 0.0f);//normalize(In.normal);//vec3(0.0f, 1.0f, 0.0f);
	vec3 V = normalize(In.V.xyz);  
	 
	vec3 reflect_color;
	float fresnelTerm;
	vec3 lit_color = lit( SHOCKWAVE_BASE_COLOR * light_color, make_material(In._emission, 0.0f, ROUGHNESS), light_color,
						 1.0f, getAttenuation(Ld.dist, VolumeLength, MINI_VOX_SIZE),
						 -Ld.dir, N, V, reflect_color, fresnelTerm );
    
	// shockwave dynamic density - working do not change // In._extra_data = distance
	const float density = 1.0f - (In._passthru + normalize(subgroupInclusiveAdd(In._passthru).xxx)).x * 0.5f;
	       
	vec3 color;
	const float weight = refraction_color(color, colorMap, density);
	
	lit_color = mix(lit_color, color.rgb, 1.0f - density);

	float luminance = dot(lit_color, LUMA);

	color.rgb = haze(color.rgb, reflect_color, fresnelTerm, density * 0.9f * aaStep(density, luminance), In._passthru * 0.5f) * 0.77f;
	color.rgb = haze(color.rgb, reflect_color, fresnelTerm, density * luminance, In._passthru * 0.5f);

	luminance = dot(lit_color, LUMA);
	color.rgb = mix(color + aaStep(0.75f, density * (1.0f - luminance)), lit_color, luminance);

	luminance = dot(color.rgb, LUMA);        
	                                           
	outColor = applyTransparency(color * 0.15f, aaStep(0.1f, luminance), weight);                             
	// outColor = vec4(weight.xxx, 1.0f);                                                                   
}
  

