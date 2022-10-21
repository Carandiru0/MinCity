#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad: enable
#define subgroup_quad_enabled
#define fragment_shader

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

layout(early_fragment_tests) in;  

#if !defined(BASIC)
#include "screendimensions.glsl"           
#include "common.glsl"     
#endif            

#if defined(TRANS)
layout(location = 0) out vec4 outColor;
#else
layout(location = 0) out vec3 outColor;
#ifdef BASIC
layout(location = 1) out vec2 outMouse;
layout(location = 2) out vec4 outNormal;
#endif
#endif

#include "voxel_fragment.glsl"

#if defined(BASIC)

void main() {
	outMouse.rg = In.voxelIndex;
	outNormal.rgb = normalize(In.N); // signed output
	outNormal.a = 0.0f; // unused - how to use....
}

#else

#ifdef TRANS 
#define OUT_FRESNEL
#endif

//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
layout (constant_id = 4) const float VolumeDimensions = 0.0f;	// world volume //
layout (constant_id = 5) const float InvVolumeDimensions = 0.0f;
layout (constant_id = 6) const float VolumeLength = 0.0f; // <---- only for this constant ** length is scaled by voxel_size

layout (constant_id = 7) const float LightVolumeDimensions = 0.0f; // light volume //
layout (constant_id = 8) const float InvLightVolumeDimensions = 0.0f;

#define DD 0
#define COLOR 1
#define OPACITY 2
layout (binding = 3) uniform sampler3D volumeMap[3];
layout (input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput ambientLightMap;
// binding 5 is the shared common image array bundle
#if defined(TRANS)
layout (binding = 6) uniform sampler2D colorMap;
#endif

// binding 5:
// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
#include "texturearray.glsl"

#include "lightmap.glsl"
#include "lighting.glsl"
    
float extract_opacity( in const float sampling ) // this includes transparent voxels, however result is negative if transparent
{
	return( clamp(sampling * 2.0f, 0.0f, 1.0f) ); // if opaque greatest value is 0.5f, want a value no greater than 1.0f - this is affected by emission
}
float extract_emission( in const float sampling ) // this includes transparent voxels that are emissive, result is positive either opaque or transparent
{
	return( max( 0.0f, sampling - 0.5f) * 2.0f );  // if greater than 0.5f is emissive, want value no greater than 1.0f and only the emissive part
}
float opacity( in const float sampling ) {

	return( extract_opacity(sampling) ); // only opaques
}

// iq - voxel occlusion //
// https://www.iquilezles.org/www/articles/voxellines/voxellines.htm
float getOcclusion(in vec3 uvw) 
{
	vec4 vc;
	// sides
	vc.x = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3( 0, 1,-1)).r ); // front
	vc.y = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3( 0,-1,-1)).r ); // back
	vc.z = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3(-1, 0,-1)).r ); // left
	vc.w = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3( 1, 0,-1)).r ); // right

	vec4 vd;
	// corners
	vd.x = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3(-1, 1,-1)).r ); // down
	vd.y = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3(-1,-1,-1)).r ); // left
	vd.z = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3( 1,-1,-1)).r ); // up
	vd.w = opacity( textureLodOffset(volumeMap[OPACITY], uvw, 0.0f, ivec3( 1, 1,-1)).r ); // right

	const vec2 st = 1.0f - uvw.xy;

	// sides
	const vec4 wa = vec4( uvw.x, st.x, uvw.y, st.y ) * vc;

	// corners
	const vec4 wb = vec4(uvw.x * uvw.y,
						 st.x  * uvw.y,
						 st.x  * st.y,
						 uvw.x * st.y) * vd * (1.0f - vc.xzyw) * (1.0f - vc.zywx);
	
	const float occlusion = 1.0f - (wa.x + wa.y + wa.z + wa.w + wb.x + wb.y + wb.z + wb.w) / 8.0f;

	return(occlusion*occlusion*occlusion);
}


// terrain, w/lighting 
#if defined(T2D) 
/*
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

float antialiasedGrid( in const vec2 uv, in float scale, in float thin )
{
	vec2 tile_uv, p;

	p = uv - 0.3f; // bl

	tile_uv = fract(p * scale);
	tile_uv = -abs(tile_uv*2.-1.);

	float tile0 = min(tile_uv.x, tile_uv.y);

	p = (1.0f - uv) - 0.8f; // tr

	tile_uv = fract(p * scale);
	tile_uv = -abs(tile_uv*2.-1.);

	float tile1 = min(tile_uv.x, tile_uv.y);
    
    float d = abs(min(tile0,tile1));

    //d = smoothstep(1.0f - thin, 1.0f, d);
	d = 1.0f - exp2(-2.7f * (d - thin) * (d - thin));

    //
    //
	//d = smoothstep(1.0f - thin, 1.0f, d);
	return d;
}

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
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz);
	Ld.att = getAttenuation(Ld.dist, VolumeLength);
	
																						// bugfix: y is flipped. simplest to correct here.
	const vec2 terrainDetail = texture(_texArray[TEX_TERRAIN], vec3(vec2(In.world_uv.x, 1.0f - In.world_uv.y), 0)).rg; // since its dark, terrain color doesnt't have a huge impact - but lighting does on terrain
	const vec3 grid_segment = texture(_texArray[TEX_GRID], vec3(VolumeDimensions * 4.0f * vec2(In.world_uv.x, In.world_uv.y), 0)).rgb;
	const float grid = 1.0f - dot(grid_segment.rgb, LUMA);

	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz);

	vec3 color = vec3(0);
					   // twilight reflection
	color.rgb += lit( terrainDetail.yyy, make_material(terrainDetail.y, terrainDetail.y, 1.0f - terrainDetail.y), vec3(1),				// regular terrain lighting
					      terrainDetail.y, abs(dot(N,V)),
					      vec3(0,0,1), N, V);

						// only emissive can have color
	color.rgb += lit( grid * mix(terrainDetail.xxx, unpackColor(In._color), In._emission), make_material(In._emission, 0.0f, 1.0f - terrainDetail.y), light_color,				// regular terrain lighting
					    grid * getOcclusion(In.uv.xyz), Ld.att,
					    -Ld.dir, N, V);
	
	outColor.rgb = color;
	/*
	const vec3 N = normalize(In.N.xyz);
	
	float s = abs(step(0.0f, -N.y) * N.y);
	
	outColor.rgb = vec3(s);*/
	
	//outColor = vec3(attenuation);
	//outColor.rgb = vec3(visibility);
	//outColor.rgb = vec3(getOcclusion(In.uv.xyz));//vec3(terrainHeight * In._occlusion);
}
#elif defined(ROAD)  

void main() { 
}

/*
// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space

#ifdef TRANS
const vec3 gui_bleed = vec3(619.607e-3f, 1.0f, 792.156e-3f);
#endif

void main() {
  
#ifndef TRANS

	vec3 light_color;
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz);
	Ld.att = getAttenuation(Ld.dist, VolumeLength);

	// smoother
	vec4 road_segment = texture(_texArray[TEX_ROAD], In.road_uv.xyz); // anisotropoic filtering enabled, cannot use textureLod, must use texture
	
	road_segment.rgb *= road_segment.a; // important
	
	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz); 

	const float decal_luminance = min(1.0f, road_segment.g * Ld.att * Ld.att * 2.0f);

	outColor.rgb = lit( road_segment.rgb, make_material(decal_luminance, 0.0f, mix(ROUGHNESS, 0.1f, min(1.0f, road_segment.g))), light_color,
					    getOcclusion(In.uv.xyz), Ld.att,
					    -Ld.dir, N, V );	
	//outColor.rgb = vec3(attenuation);

#else  // roads, "transparent selection"

#define SELECTION 3.0f

	vec3 light_color;
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz);
	Ld.att = getAttenuation(Ld.dist, VolumeLength);

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

	const float decal_luminance = road_segment.a * Ld.att * Ld.att;
	float fresnelTerm;  // feedback from lit
	color.rgb = lit( road_segment.rgb, make_material(road_segment.a * 20.0f, 0.0f, ROUGHNESS), light_color,	// todo roads need actual street lights for proper lighting or else too dark
						1.0f, // occlusion
						min(1.0f, Ld.att + decal_luminance),
						-Ld.dir, N, V, fresnelTerm);
						    
	vec3 refract_color;
	const float weight = refraction_color(refract_color, colorMap, decal_luminance);
	color.rgb = road_segment.rgb + mix(color.rgb + color.rgb * decal_luminance, color.rgb + refract_color * road_segment.a, fresnelTerm);

	outColor = applyTransparency(vec3(1,0,0) * color, road_segment.a, weight);
	// outColor = vec4(weight.xxx, 1.0f); 
#endif

}
*/

#else // voxels, w/lighting 
        
// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space  
void main() {        
    
	vec3 light_color;    
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz);

	const vec3 N = normalize(In.N.xyz);
	const vec3 V = normalize(In.V.xyz);                            
	
#ifndef TRANS              
    
	outColor.rgb = lit( unpackColor(In._color), In.material, light_color,
						getOcclusion(In.uv.xyz), getAttenuation(Ld.dist, VolumeLength),
						-Ld.dir, N, V);

	//outColor.rgb = vec3(attenuation);
	//outColor.xyz = vec3(getOcclusion(In.uv.xyz));
#else     
#define SCROLL_SPEED GOLDEN_RATIO

	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	
	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	
	// ##### FINAL HOLOGRAPHIC TRANSPARENCY MIX - DO *NOT* MODIFY UNDER ANY CIRCUMSTANCE - HARD TO FIND, LOTS OF ITERATIONS  #########################################	

	float fresnelTerm;  // feedback from lit      
	const vec3 lit_color = lit( unpackColor(In._color), In.material, light_color,
						    getOcclusion(In.uv.xyz), getAttenuation(Ld.dist, VolumeLength),
						    -Ld.dir, N, V, fresnelTerm );
							             
	// Apply specific transparecy effect for MinCity //

	// using occlusion for approx density to add some dynamics to refraction
	const float density = 1.0f - fresnelTerm;

	vec3 refract_color;  
	const float weight = refraction_color(refract_color, colorMap, density);                                                    
         
	vec3 color = mix(refract_color * lit_color, lit_color, In._transparency + In._transparency * In.material.emission);

	outColor = applyTransparency( color, In._transparency + In._transparency*density*density, weight );
	
	/*1
	const float accurate = InvVolumeDimensions * (128.0f);                                                              
	float scanline = aaStep( accurate, triangle_wave(mod(In.uv.z * VolumeDimensions + mod(SCROLL_SPEED * In._time, VolumeDimensions), accurate * 1.5f * 1.5f))) * (N.z * 0.5f + 0.5f);
	//scanline = triangle_wave(scanline);

	vec3 color = mix(lit_color * (1.0f - density), refract_color + lit_color, fresnelTerm * 2.0f);      

	color *= min(1.0f, scanline + 0.5f);
	  
	outColor = applyTransparency( color, ( In._transparency ) + (scanline * 0.5f) * density*density, weight );
	*/
	//outColor = vec4(weight.xxx, 1.0f);                                   
#endif

}
#endif

#endif // else basic

