#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable

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
#define FRESNEL_ETA_N2 1.33f	// medium being entered 

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
//-------------

#define LIGHT_EFFECT_SCALE 4.0f  // good value don't change
#define FRESNEL_POWER (PI / 3.0f + 0.1f)

// voxels, w/lighting 
void main() {
  
    vec3 light_color;
	float attenuation;

	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength); 

	vec3 N = normalize(In.N.xyz);
	vec3 V = normalize(In.V.xyz);  
	 
	vec3 reflect_color;
	float fresnelTerm;

	const vec3 lit_color = lit( normalize(unpackColor(In.ambient)), light_color,
						 1.0f, attenuation, 
	                     In._emission, ROUGHNESS,
						 L, N, V, reflect_color, fresnelTerm );
    
	const float density = (1.0f - In._passthru);
	       
	vec3 refract_color;
	const float weight = refraction_color(refract_color, colorMap, density * (1.0f - fresnelTerm) * 0.5f);

	vec3 color = lit_color + mix(refract_color, reflect_color, fresnelTerm) * LIGHT_EFFECT_SCALE + unpackColor(In.ambient) * 10.0f * LIGHT_EFFECT_SCALE;

	outColor = applyTransparency(color, In._transparency * pow(fresnelTerm, FRESNEL_POWER) * LIGHT_EFFECT_SCALE, weight);
}
  

