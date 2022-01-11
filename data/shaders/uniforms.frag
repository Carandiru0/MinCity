#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#define subgroup_quad_enabled
#define fragment_shader
#if !defined(BASIC)
#include "screendimensions.glsl"           
#include "common.glsl"     
#endif

layout(early_fragment_tests) in;              

#if defined(TRANS)
layout(location = 0) out vec4 outColor;
#else
layout(location = 0) out vec3 outColor;
#ifdef BASIC
layout(location = 1) out vec2 outMouse;
#endif
#endif

#include "voxel_fragment.glsl"

#if defined(BASIC)
void main() {
	outMouse.rg = In._voxelIndex;
}

#else

#if (defined(ROAD) && !defined(TRANS))
#define INOUT_FRESNEL
#define OUT_REFLECTION
#endif

#ifdef TRANS 
#define OUT_FRESNEL
#endif

//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"

layout (constant_id = 4) const float VolumeLength = 0.0f; // <--- beware this is scaled by voxel size, for lighting only
layout (constant_id = 5) const float LightVolumeDimensions_X = 0.0f;
layout (constant_id = 6) const float LightVolumeDimensions_Y = 0.0f;
layout (constant_id = 7) const float LightVolumeDimensions_Z = 0.0f; 
layout (constant_id = 8) const float InvLightVolumeDimensions_X = 0.0f;
layout (constant_id = 9) const float InvLightVolumeDimensions_Y = 0.0f;
layout (constant_id = 10) const float InvLightVolumeDimensions_Z = 0.0f; 
#define LightVolumeDimensions vec3(LightVolumeDimensions_X, LightVolumeDimensions_Y, LightVolumeDimensions_Z)
#define InvLightVolumeDimensions vec3(InvLightVolumeDimensions_X, InvLightVolumeDimensions_Y, InvLightVolumeDimensions_Z)

#if defined(TRANS) && !defined(ROAD)
layout (constant_id = 11) const float VolumeDimensions_Z = 0.0f;
layout (constant_id = 12) const float InvVolumeDimensions_Z = 0.0f;
#endif

#define DD 0
#define COLOR 1
layout (binding = 3) uniform sampler3D volumeMap[2];
#if defined(TRANS)
layout (binding = 6) uniform sampler2D colorMap;
#endif
layout (input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput ambientLightMap;

// roughness = inverse of specular reflectivity
// so higher value = less specular reflection
// "" lower  ""    = more specular reflection
#if defined(T2D)	// terrain only
const float ROUGHNESS = 0.9f;  
#else  
#if defined(TRANS) /// ---- transparent
#if defined(ROAD) // road only
const float ROUGHNESS = 0.5f;     
#else	            // voxels only 
const float ROUGHNESS = 0.11f;
#endif
#else              /// ---- opaque
#if defined(ROAD) // road only
const float ROUGHNESS = 0.7f;     
#else	            // voxels only 
const float ROUGHNESS = 0.5f;
#endif
#endif 
#endif

#include "lightmap.glsl"
#include "lighting.glsl"
    

// terrain, w/lighting 
#if defined(T2D) 

// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
#include "texturearray.glsl"

// iq - https://www.iquilezles.org/www/articles/filterableprocedurals/filterableprocedurals.htm
float filteredGrid( in vec2 uv, in const float scale, in const float thin )    // thin - greater, thick - less
{
	float grid = 0.0f;

	// grid lines
	uv = mod(uv * scale, 0.5f);

	vec2 p;

	p = uv - 0.3f; // bl

	{
		const vec2 dp = rndC(p);
		vec2 w = max(abs(dFdx(dp)), abs(dFdy(dp)));
		vec2 a = dp + 0.5*w;                        
		vec2 b = dp - 0.5*w;           
		vec2 i = (floor(a)+min(fract(a)*thin,1.0)-
				  floor(b)-min(fract(b)*thin,1.0))/(thin*w);
		grid += fma(i.x, (1.0 - i.y), i.y); // (herbie optimized) same as 1.0f - (1.0-i.x)*(1.0-i.y)
	}

	p = (1.0 - uv) - 0.8f; // tr

	{
		const vec2 dp = rndC(p);
		vec2 w = max(abs(dFdx(dp)), abs(dFdy(dp)));
		vec2 a = dp + 0.5*w;                        
		vec2 b = dp - 0.5*w;           
		vec2 i = (floor(a)+min(fract(a)*thin,1.0)-
				  floor(b)-min(fract(b)*thin,1.0))/(thin*w);
		grid += fma(i.x, (1.0 - i.y), i.y); // (herbie optimized) same as 1.0f - (1.0-i.x)*(1.0-i.y)
	}

	return(min(1.0f, grid));
}

/*
float antialiasedGrid(in vec2 uv, in const float scale)
{
	// grid lines	
	uv = mod(uv * scale, 0.5f);

	// first way ----------------------------------	
	vec2 sidesBL = abs(0.008f / (uv - 0.333f));
	vec2 sidesTR = abs(0.008f / ((1.0 - uv) - 0.825f));
	sidesTR = smoothstep(vec2(0), vec2(1), clamp(sidesBL + sidesTR, 0.0f, 1.0f));
	float grid = sidesTR.x + sidesTR.y;
	
	return(smoothstep(0.0f, 1.0f, grid * GOLDEN_RATIO));	// final smoothing
}
*/

// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space
void main() {
  
	vec3 light_color;
	float attenuation; 

	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength);
																   // bugfix: y is flipped. simplest to correct here.
	const float terrainHeight = textureLod(_texArray[TEX_TERRAIN], vec3(vec2(In.world_uv.x, 1.0f - In.world_uv.y), 0), 0).r; // since its dark, terrain color doesnt't have a huge impact - but lighting does on terrain

	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz);
	
	float grid = filteredGrid(In.world_uv.xy, 1024.0f, 16.0f);

	grid *= max(0.0f, dot(N.xzy, vec3(0,-1,0))); // only top faces

	const vec3 grid_color = unpackColor(In._color) * grid;

	vec3 color = lit( terrainHeight + grid_color, light_color,				// regular terrain lighting
					  In._occlusion, attenuation,
					  grid * In._emission, ROUGHNESS,
					  L, N, V);
	
	const float luma = dot(color, LUMA);
	color += grid_color * (1.0f - attenuation) * (1.0f - terrainHeight) * (1.0f - luma) * (1.0f - In._emission);

	outColor.rgb = color;
	//outColor.rgb = vec3(terrainHeight * In._occlusion);
}
#elif defined(ROAD)  

// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space

// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
#include "texturearray.glsl"
#include "common.glsl"
#ifdef TRANS
const vec3 gui_bleed = vec3(619.607e-3f, 1.0f, 792.156e-3f);
#endif

void main() {
  
#ifndef TRANS

	float fresnelTerm;

	//const float terrainHeight = textureLod(_texArray[TEX_TERRAIN], vec3(In._uv_texture.xy, 0), 0).r; // since its dark, terrain color doesnt't have a huge impact - but lighting does on terrain
	const float bump = texture(_texArray[TEX_NOISE], vec3(In.world_uv.xy * 8.0f, 0))._perlin;
	fresnelTerm = smoothstep(0.0f, 1.0f, min(1.0f, bump /* (1.0f - terrainHeight)*/ + pow(bump, 5.0f)));

	// smoother
	vec4 road_segment = texture(_texArray[TEX_ROAD], In.road_uv.xyz); // anisotropoic filtering enabled, cannot use textureLod, must use texture
	road_segment.rgb *= road_segment.a; // important
	// or ** more sharp pixelated but without any aliasing hmmm...
	//vec4 road_segment = textureGrad(_texArray[TEX_ROAD], vec3(magnify(In.uv_local.xy, vec2(64.0f, 16.0f)),In.uv_local.z), dFdx(In.uv_local.xy), dFdy(In.uv_local.xy)); 
	
	vec3 light_color;
	float attenuation;

	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength);

	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz); 

	const float decal_luminance = min(1.0f, road_segment.g * attenuation * attenuation * 2.0f);
	vec3 reflection;
	vec3 color = lit( road_segment.rgb, light_color,
					  1.0f, attenuation,
	                  decal_luminance, mix(ROUGHNESS, 0.1f, min(1.0f, fresnelTerm + road_segment.g)),
					  L, N, V, reflection, fresnelTerm );

	color = mix(color, unpackColor(In.ambient) + color * road_segment.g + color * decal_luminance * dot(reflection, LUMA) + reflection, fresnelTerm); 

	const vec3 shineCol = 0.333333f * vec3(0.5f, 0.05f, 1.0f);

	color += road_segment.b * shineCol * (1.0f - exp2(-1000.0f*road_segment.b*fresnelTerm));

	outColor.rgb = color;

#else  // roads, "transparent selection"

#define SELECTION 3.0f

	vec3 light_color;
	float attenuation;

	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength);

	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz); 

	// smoother
	vec4 road_segment = texture(_texArray[TEX_ROAD], In.road_uv.xyz); // anisotropoic filtering enabled, cannot use textureLod, must use texture
	road_segment.rgb *= road_segment.a; // important
	const float road_tile_luma = dot(road_segment.rgb, LUMA);
	//const vec4 dFdUV = vec4( dFdx(In.uv_local.xy), dFdy(In.uv_local.xy) );
	//const float road_tile_luma = dot(textureGrad(_texArray[TEX_ROAD], vec3(magnify(In.uv_local.xy, vec2(64.0f, 16.0f)), In.uv_local.z), dFdUV.xy, dFdUV.zw).rgb, LUMA);

	const vec2 uv = In.road_uv.xy + vec2(0.0f, In._time*0.5f);

	road_segment.a = texture(_texArray[TEX_ROAD], vec3(uv, SELECTION)).a;
	road_segment.rgb = road_tile_luma * gui_bleed * road_segment.a;
	// or ** more sharp pixelated but without any aliasing hmmm...
	//vec4 road_segment = textureGrad(_texArray[TEX_ROAD], vec3(magnify(uv, vec2(64.0f, 16.0f)), SELECTION), dFdUV.xy, dFdUV.zw); 

	vec3 color;

	const float decal_luminance = road_segment.a * attenuation * attenuation;
	float fresnelTerm;  // feedback from lit
	color.rgb = lit( road_segment.rgb, light_color,	// todo roads need actual street lights for proper lighting or else too dark
						1.0f, // occlusion
						min(1.0f, attenuation + decal_luminance),
						road_segment.a * 20.0f, ROUGHNESS, // emission, roughness   
						L, N, V, fresnelTerm);
						    
	vec3 refract_color;
	const float weight = refraction_color(refract_color, colorMap, decal_luminance);
	color.rgb = road_segment.rgb + mix(color.rgb + color.rgb * decal_luminance, color.rgb + refract_color * road_segment.a, fresnelTerm);

	outColor = applyTransparency(color, road_segment.a, weight);
	// outColor = vec4(weight.xxx, 1.0f); 
#endif

}
#else // voxels, w/lighting 
              
// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space  
void main() {        
    
	vec3 light_color;    
	float attenuation;   
	 
	const vec3 L = getLight(light_color, attenuation, In.uv.xyz, VolumeLength);
	
	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz);                            
			        
#ifndef TRANS              
    
	outColor.rgb = lit( unpackColor(In._color), light_color,
						    In._occlusion, attenuation,
	                        In._emission, ROUGHNESS,
						    L, N, V );

#else     
#define SCROLL_SPEED GOLDEN_RATIO
	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	
	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	
	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	

	float fresnelTerm;  // feedback from lit      
	const vec3 lit_color = lit( unpackColor(In._color), light_color,
						    In._occlusion, attenuation,
	                        In._emission, ROUGHNESS,
						    L, N, V, fresnelTerm );
							             
	// Apply specific transparecy effect for MinCity //

	// using occlusion for approx density to add some dynamics to refraction
	const float density = 1.0f - In._occlusion;

	vec3 refract_color;  
	const float weight = refraction_color(refract_color, colorMap, density);                                                    
         
	                     
	const float accurate = InvVolumeDimensions_Z * (128.0f);                                                              
	const float scanline = aaStep( accurate, mod(In.uv.z * VolumeDimensions_Z + mod(SCROLL_SPEED * In._time, VolumeDimensions_Z), accurate * 1.5f * 1.5f)) * (N.z * 0.5f + 0.5f);
	
	vec3 color = mix(lit_color * (1.0f - density), refract_color + lit_color, fresnelTerm * 2.0f);      

	color *= min(1.0f, scanline + 0.5f);
	  
	outColor = applyTransparency( color, ( In._transparency ) + (scanline * 0.5f) * density*density, weight );

	//outColor = vec4(weight.xxx, 1.0f);                                   
#endif

}
#endif

#endif // else basic

