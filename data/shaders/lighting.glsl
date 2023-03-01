#ifndef _LIGHTING_GLSL
#define _LIGHTING_GLSL

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

// FRAGMENT - - - - In = xzy space (matching 3D textures width,depth,height)
//			--------Out = In
// *******************************************************//
// lightmap.glsl must be included before this file
// common.glsl must be included before this file
// with appropriatte defines for its shader
// *******************************************************//
// optional outputs:
// OUT_REFLECTION
// OUT_FRESNEL
// ....

#ifndef ambientLightMap
//#error "ambientLightMap needs to be defined before including lighting.glsl"
#endif

#ifndef ROUGHNESS
//#error "ROUGHNESS needs to be defined before including lighting.glsl"
#endif

#define DownResDimensions (ScreenResDimensions*0.5f)
#define InvDownResDimensions 1.0f / DownResDimensions

#define emission x
#define metallic y
#define roughness z
#define ambient w

// for usage by terrain, road, etc that do not define materials per-voxel but rather the whole surface. Normal voxels *only* have per-voxel materials defined.
vec4 make_material(in const float emission, in const float metallic, in const float roughness) {
	return(vec4(emission, metallic, roughness, In._ambient)); // grabs ambient
}

#ifdef TRANS
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------
// Transparency
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------
// *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change*   *do NOT change* 
//
// Working overlapped translucent rendering - no fighting/flickering for what translucent surface comes first to blend
//
// aaStep provides a nice smooth fresnel affect that depends on overlapped transparency density
// video screen at head-on angle is clear vs frosted at close to parallel angle
// voodoo skull has transparency density differences along the detailed contours of the bone, jaw, head which can be seen zoomed in
//
// roads have working overlapped translucency with no "fighting" at intersections
//
// rain is overall less visible which may be a good or bad thing
//
// do not change any code - this is a HARD problem to solve, infact a known problem in graphics is transparent over transparent rendering
// there is no depth buffer, so which one gets rendered first
//
// this algorithm leverages a mask that is rendered prior defining a pixels density (additive framebuffer of "hits" for only transparent objects)
// the weight is normalized here by the calculated maximum of the entire visible framebuffer
// this weight is carefully applied differently to color and alpha
//
// -----------------------------------------------------------------------------------------------------------------------------------------------------------
vec4 applyTransparency(in vec4 color_alpha, in const float weight)
{ 
	color_alpha.rgb = color_alpha.rgb * color_alpha.a;// * (1.0f - weight);
	color_alpha.a = color_alpha.a * weight;

	return( color_alpha );
}
vec4 applyTransparency(in vec3 color, in float alpha, in const float weight)
{ 
	color = color * alpha;// * (1.0f - weight);
	alpha = alpha * weight;

	return( vec4(color, alpha) );
}

float transparency_weight(in const float clearmask) // normalizes input (sample from screen clearmask) [0.0f...1.0f]
{
	return(clearmask * In._inv_weight_maximum); // order independence is maintained if the result is a.) linear, b.) not inverted - must multiply alpha by this result, order is not dependent on color being multiplied by this weight
}
float refraction_color(out vec3 out_refraction, in const restrict sampler2D grabPassMap, in const float density) // fixes up refraction so objects in front of the current fragment/object are not included.
{
	vec2 uv = (gl_FragCoord.xy * InvScreenResDimensions);
	
	const vec4 backbuffer_color = textureLod(grabPassMap, uv, 0);	// sample the backbuffer (contains correct state, no gui, only opaques-all rendered)
	const float weight = transparency_weight(backbuffer_color.a);

	uv += 0.05f * density;	// nicely offsets texture coordinates following the contour of density (externally calculated - matches roads, shockwave density, etc)
												// *bugfix: "singularity" on transparents only, remove view direction - not neccessary

	const vec4 refracted_backbuffer_color = textureLod(grabPassMap, uv, 0); // sample again with refraction offset
	const float weight_refracted = transparency_weight(refracted_backbuffer_color.a);

// ^^^ alpha contains "clear mask" that refraction is clamped to
// this is it:
	out_refraction = mix(backbuffer_color.rgb, refracted_backbuffer_color.rgb, 1.0f - smoothstep(0.0f, 1.0f, weight_refracted));

	return( weight ); // return transparency composition weight to be applied  **bugfix: gl_FragCoord.z found to cause darker result with no noticable benefit. Also had a very large overhead.
}
#endif

vec3 reflection()
{
	return(subpassLoad(ambientLightMap).rgb);
}
vec3 reflection(inout float emission) 
{
	const vec3 ambient_reflection = reflection();

	emission = clamp(emission + dot(ambient_reflection, LUMA), 0.0f, 1.0f);

	return(ambient_reflection);
}

// NOTE: GGX lobe for specular lighting, took straight from here: http://www.codinglabs.net/article_physically_based_rendering_cook_torrance.aspx
#define POSITIVE_EPSILON 0.000000001f
float chiGGX(in const float v)
{
    //return v > 0. ? 1. : 0.;
	return( step(0.0f, v - POSITIVE_EPSILON) ); // returns 1.0f when v > 0.0f, otherwise 0.0f is returned
}
float GGX_Distribution(in const float NdotH, in const float alpha)
{
    const float alpha2 = alpha * alpha;
    const float NdotH2 = NdotH * NdotH;
    const float den = NdotH2 * alpha2 + (1.0f - NdotH2);
    return (chiGGX(NdotH) * alpha2) / ( PI * den * den );
}

#if defined(FRESNEL_ETA_N1) && defined(FRESNEL_ETA_N2)
float fresnel(in const vec3 N, in const vec3 V)
{
	// 1st quadtrant
	//V = V * 0.5f + 0.5f;
	//N = N * 0.5f + 0.5f;

    const float r = sq((FRESNEL_ETA_N1 - FRESNEL_ETA_N2) / (FRESNEL_ETA_N1 + FRESNEL_ETA_N2));

	// required pow() for refraction indices are defined
    return r + (1.0f - r) * pow(1.0f - max(0.0f, dot(N * 0.5f + 0.5f, V * 0.5f + 0.5f)), 5.0f);
}
#else
float fresnel(in const vec3 N, in const vec3 V)
{
	// Fresnel requires:
	// view space transformed vectors
	// to be in the 1st quadtrant of cartesian space (+X,+Y,+Z)
	// otherwise it is not orientation independent
	// independent of model and view orientation
	
	// 1st quadtrant
	//V = V * 0.5f + 0.5f;
	//N = N * 0.5f + 0.5f;

	// leaving the flexibility of appling pow() to the implementation
	return 1.0f - max(0.0f, dot(N * 0.5f + 0.5f, V * 0.5f + 0.5f));
}
#endif

// Albedo = straight color no shading or artificial lighting (voxel color in)
vec3 lit( in const vec3 albedo, in vec4 material, in const vec3 light_color, in const float occlusion, in const float attenuation,
          in const vec3 L, in const vec3 N, in const vec3 V, // L = light direction, N = normal, V = eye direction   all expected to be normalized
		  in const bool reflection_on
#ifdef OUT_REFLECTION
		  , out vec3 ambient_reflection
#endif
#ifdef OUT_FRESNEL
		  , out float fresnelTerm
#endif
#ifdef INOUT_FRESNEL
		  , inout float fresnelTerm
#endif
		)
{ 
	const float NdotL = 1.0f;//max(0.0f, dot(N, L));
	const float NdotH = 0.1f;//max(0.0f, dot(N, normalize(L + V)));
	
#ifdef OUT_FRESNEL
	fresnelTerm = fresnel(N, V);
#elif defined(INOUT_FRESNEL)
	fresnelTerm = fresnel(N, V) * fresnelTerm;
#else
	const float fresnelTerm = fresnel(N, V);
#endif

	const float luminance = min(1.0f, dot(light_color, LUMA)); // bugfix: light_color sampled can exceed normal [0.0f ... 1.0f] range, cap luminance at 1.0f maximum

	const float specular_reflection_term = GGX_Distribution(NdotH, material.roughness) * fresnelTerm;
	const float diffuse_reflection_term = NdotL * (1.0f - fresnelTerm) * (1.0f - material.metallic);

#ifndef OUT_REFLECTION
	vec3 ambient_reflection = vec3(0);
#endif
    movc(reflection_on, ambient_reflection, reflection(material.emission));

	const float emission_term = (luminance + smoothstep(0.5f, 1.0f, attenuation)) * material.emission; /// emission important formula do not change (see notes below)
	const vec3 ambient_reflection_term = (unpackColor(material.ambient) + ambient_reflection) * occlusion; // chooses diffuse reflection when not metallic, and specular reflection when metallic
	
			// ambient
	return ( ambient_reflection_term +
			  // diffuse color .							// diffuse shading/lighting	// specular shading/lighting					
		     fma( albedo * occlusion, ( diffuse_reflection_term + specular_reflection_term ) * light_color, 
			       // emission		// ^^^^^^ this splits the distribution of light to the albedo color and the actual light color 50/50, it is biased toward to the albedo color in the event the albedo color component is greater than 0.5f. Making bright objects appear brighter. (all modulated by the current occlusion). This makes occulusion emphasized, which looks nice as the effect of ambient occlusion is always very visible. *do not change*
			       albedo * emission_term)
		   );					


		   // emission = emissive color * (emissive light attenuated luminance + smoothstep(0.5, 1.0, emissive light attenuation) * emissive intensity
		   // ########################################################################################################################################
		   // fundamental emission equation 
		   // provides a smooth illumination based on the small change in distance from it's own light position
		   // also illuminates based on its attenuated luminance based off that distance
		   // results in a gradual transition which can be seen as a gradual turn on of the light to fully on
		   // it looks like a gradual illuminance of the area as the lights are turned on
		   // the final color actually looks like an emissive light 
}
#endif // _LIGHTING_GLSL
