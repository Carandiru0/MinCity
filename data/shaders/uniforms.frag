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
	outNormal.rgb = normalize(In.N.xyz); // signed output
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

#if defined(T2D) 
layout (constant_id = 9) const float  TextureDimensionsU = 0.0f; // terrain texture u //
layout (constant_id = 10) const float TextureDimensionsV = 0.0f; // terrain texture v //
#define TextureDimensions vec2(TextureDimensionsU, TextureDimensionsV)

#include "terrain.glsl"

#endif

#define DD 0
#define COLOR 1
#define OPACITY 2
layout (binding = 3) uniform sampler3D volumeMap[3];
layout (input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput ambientLightMap;
// binding 5 - is the shared common image array bundle
// for 2D textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], vec3(uv.xy,0), 0); // only one layer
// for 2D Array textures use : textureLod(_texArray[TEX_YOURTEXTURENAMEHERE], uv.xyz, 0); // z defines layer index (there is no interpolation between layers for array textures so don't bother)
#include "texturearray.glsl"

#if defined(TRANS) // only for transparent voxels
layout (binding = 6) uniform sampler2D colorMap;
#endif
#if defined(T2D) // only for terrain
layout (binding = 7) uniform sampler3D detailDerivativeMap;

//#define DEBUG_SHADER

#endif



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
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DERIVATIVE MAPPING - Mikkelsen2020BumP.pdf - https://jcgt.org/published/0009/03/04/

#define FLT_EPSILON 0.000000001f // avoid division by zero

// texture uv's in, screen space surface gradient out (st)
vec2 derivatives(in const vec2 uv, 
			     in const vec2 texture_dimensions, 
				 in const vec2 duvdx, in const vec2 duvdy,
				 in const vec2 derivative_map_sample) 
{
	const vec2 dhdST = texture_dimensions * (derivative_map_sample * 2.0f - 1.0f);

	// chain rule - uv space to screen space
	return( vec2( dot(dhdST.xy, duvdx.xy), dot(dhdST.xy, duvdy.xy) ) );
}

// It was shown in the paper how Blinn's perturbed normal is independent of
// the underlying parametrization. Thus this function allows us to perturb the normal on
// an arbitrary domain (not restricted to screen-space)
vec3 surface_gradient(in const vec3 vSigmaU, in const vec3 vSigmaV, in const vec3 lastNormal, in const vec2 dHduv)   // generalized, domainless surface gradient
{
	// surf_norm must be normalized
	const vec3 R1 = cross(vSigmaV,lastNormal);
	const vec3 R2 = cross(lastNormal,vSigmaU);
	
	const float fDet = dot(vSigmaU, R1);
	
	return ( dHduv.x * R1 + dHduv.y * R2 ) / dot(vSigmaU, R1);
}

// usage:

// obtain derivatives()

// use surface_gradient() where:

// vSigmaU = ddx(world_position)
// vSigmaV = ddy(world_position)
// surf_normal, normalized
// dHduv = derivatives() obtained previously

// mix surface gradients, not normals perturbed by surface gradients

// perturb normal by surface gradient

// repeat ...


// resolve function to establish the final perturbed normal:
vec3 perturb_normal(in const vec3 lastNormal, in const vec3 surfGrad) // perturbs a normal by a surface gradient
{
	return normalize(lastNormal - surfGrad);
}

// conversion of object/world space normal to surface gradient:

// Surface gradient from a known "normal" such as from an object-
// space normal map. This allows us to mix the contribution with
// others, including from tangent-space normals. The vector v
// doesn’t need to be unit length so long as it establishes
// the direction. It must also be in the same space as the normal.
vec3 surface_gradient_normal(in const vec3 lastNormal, in const vec3 perturbedNormal) // acquires the *surface-gradient* of the original normal from a perturbed normal
{
	// If k is negative then we have an inward facing vector v,
	// so we negate the surface gradient to perturb toward v.
	const vec3 n = lastNormal;
	const float k = dot(n, perturbedNormal);
	return (k*n - perturbedNormal)/max(FLT_EPSILON, abs(k));
}

// conversion of volume gradient to surface gradient:

// Used to produce a surface gradient from the gradient of a volume
// bump function such as 3D Perlin noise. Equation 2 in [Mik10].
vec3 surface_gradient_volumetric(in const vec3 lastNormal, in const vec3 volumetric_gradient) // volumetric position to surface gradient
{
	return volumetric_gradient - dot(lastNormal, volumetric_gradient)*lastNormal;
}

vec3 triplanar_weights(in const vec3 lastNormal, in const float k) // default = 3.0
{
	vec3 weights = abs(lastNormal) - 0.2f;
	weights = max(vec3(0), weights);
	weights = pow(weights, k.xxx);
	weights /= (weights.x + weights.y + weights.z);
	return weights;
}

// Triplanar projection is considered a special case of volume
// bump map. Weights are obtained using DetermineTriplanarWeights()
// and derivatives using TspaceNormalToDerivative().
vec3 surface_gradient_triplanar(in const vec3 lastNormal, in const vec3 triplanarWeights, in const vec2 dHduv_x, in const vec2 dHduv_y, in const vec2 dHduv_z) // tri-planar surface gradient
{
	const float w0 = triplanarWeights.x;
	const float w1 = triplanarWeights.y;
	const float w2 = triplanarWeights.z;

	// Assume deriv xplane, deriv yplane, and deriv zplane are
	// sampled using (z,y), (x,z), and (x,y), respectively.
	// Positive scales of the lookup coordinate will work
	// as well, but for negative scales the derivative components
	// will need to be negated accordingly.
	const vec3 grad = vec3(w2*dHduv_z.x + w1*dHduv_y.x, // x = z,y
					       w0*dHduv_x.y + w2*dHduv_z.y, // y = x,z
					       w0*dHduv_x.x + w1*dHduv_y.y);// z = x,y

	return surface_gradient_volumetric(lastNormal, grad);
}

float height_triplanar(in const vec3 triplanarWeights, in const float x, in const float y, in const float z) // tri-planar surface gradient
{
	const float w0 = triplanarWeights.x;
	const float w1 = triplanarWeights.y;
	const float w2 = triplanarWeights.z;

	// Assume deriv xplane, deriv yplane, and deriv zplane are
	// sampled using (z,y), (x,z), and (x,y), respectively.
	// Positive scales of the lookup coordinate will work
	// as well, but for negative scales the derivative components
	// will need to be negated accordingly.
	//const vec3 grad = vec3(w2*z + w1*y, // x = z,y
	//				       w2*z + w0*x, // y = z,x
	//				       w0*x + w1*y);// z = x,y

	return(x * w0 + y * w1 + z * w2);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// https://www.shadertoy.com/view/Xtl3zf - Texture Repitition Removal (iq)
float sum( vec3 v ) { return v.x+v.y+v.z; }

vec3 textureNoTile( in const vec2 uv, in const float height, in const vec2 duvdx, in const vec2 duvdy, in const float t )
{
    float k = texture( _texArray[TEX_NOISE], vec3(0.005f*uv, 0.0f) )._simplex; // cheap (cache friendly) lookup
        
    float l = k*8.0f;
    float f = fract(l);
    
#if 0
    float ia = floor(l); // iq's method
    float ib = ia + 1.0f;
#else
    float ia = floor(l+0.5f); // suslik's method (see comments)
    float ib = floor(l);
    f = min(f, 1.0f-f)*2.0f;
#endif    
    
    vec2 offa = sin(vec2(3.0,7.0)*ia); // can replace with any other hash
    vec2 offb = sin(vec2(3.0,7.0)*ib); // can replace with any other hash

    vec3 cola = textureGrad( detailDerivativeMap, vec3(uv + t*offa, height), vec3(duvdx, 0.0f), vec3(duvdy, 0.0f) ).xyz;
    vec3 colb = textureGrad( detailDerivativeMap, vec3(uv + t*offb, height), vec3(duvdx, 0.0f), vec3(duvdy, 0.0f) ).xyz;
    
    return mix( cola, colb, smoothstep(0.2f,0.8f,f-0.1f*sum(cola-colb)) );
}

// FRAGMENT - - - - In = xzy view space
//					all calculation in this shader remain in xzy view space, 3d textures are all in xzy space
//			--------Out = screen space
void main() {

	vec3 N, gradient;
	float height;

	// **swizzle to Y Is Up**	        // <---
	const vec3 uvw = In.world_uvw.xzy; // make it xyz
	const vec3 duvdx = dFdxFine(uvw),
	           duvdy = dFdyFine(uvw);

	// **swizzle to Y Is Up**	        // <---
	N = normalize(In.N.xyz).xzy; // the derivative map is derived from a normal map that has Y Axis As Up, input normal to fragment shader uses Z As Up - must swizzle input normal. 

#ifdef DEBUG_SHADER
	outColor = unpackColor(In._color); // debug mode - color output of normal direction (xyz order)
	return;
#endif

	// temporarily invert Y (negative->positive) vulkan
	//N.y = -N.y;

	// apply surface gradient
	//gradient = surface_gradient(dFdxFine(world_uvw), dFdyFine(world_uvw), N, derivative); 

	// common triplanar weights
	const vec3 weights = triplanar_weights(N, TRIPLANAR_BLEND_WEIGHT);

	// -------------------------16K SURFACE //
	{
		// xz (y axis)
		const vec3 height_derivative_xz = texture(_texArray[TEX_TERRAIN], vec3(uvw.xz, 0)).rgb; // repeated on all axis

		height = height_triplanar(weights, height_derivative_xz.r, height_derivative_xz.r, height_derivative_xz.r);
		const vec2 derivative = derivatives(uvw.xz, TextureDimensions, duvdx.xz, duvdy.xz, height_derivative_xz.gb);  // 11585.0

		// apply triplanar surface gradient
		gradient = surface_gradient_triplanar(N, weights, derivative, derivative, derivative); // tricks for "triplanar projection of the same axis"
		N = perturb_normal(N, gradient);
	}

	//outColor = height.xxx;
	//return;

	// -------------------------DETAIL SURFACE //
	{
		const float normalized_height = abs(uvw.y);
		vec2 scale, uv, dvdx, dvdy;

		// zy (x axis)
		scale = vec2(DETAIL_SCALE, 1.0f);
		uv = uvw.zy * scale;
		dvdx = duvdx.zy * scale;
		dvdy = duvdy.zy * scale;

		const vec3 detail_derivative_zy = textureNoTile(uv, normalized_height, dvdx, dvdy, 0.5f);
		const vec2 derivative_zy = derivatives(uv, vec2(DETAIL_DIMENSIONS, 1.0f), dvdx, dvdy, detail_derivative_zy.gb);

		// xz (y axis)
		scale = vec2(DETAIL_SCALE);
		uv = uvw.xz * scale;
		dvdx = duvdx.xz * scale;
		dvdy = duvdy.xz * scale;

		const vec3 detail_derivative_xz = textureNoTile(uv, normalized_height, dvdx, dvdy, 0.5f);
		const vec2 derivative_xz = derivatives(uv, vec2(DETAIL_DIMENSIONS), dvdx, dvdy, detail_derivative_xz.gb);

		// xy (z axis)
		scale = vec2(DETAIL_SCALE, 1.0f);
		uv = uvw.xy * scale;
		dvdx = duvdx.xy * scale;
		dvdy = duvdy.xy * scale;

		const vec3 detail_derivative_xy = textureNoTile(uv, normalized_height, dvdx, dvdy, 0.5f);
		const vec2 derivative_xy = derivatives(uv, vec2(DETAIL_DIMENSIONS, 1.0f), dvdx, dvdy, detail_derivative_xy.gb);

		height *= height_triplanar(weights, detail_derivative_zy.r, detail_derivative_xz.r, detail_derivative_xy.r);

		// apply triplanar surface gradient
		gradient = surface_gradient_triplanar(N, weights, derivative_zy, derivative_xz, derivative_xy);
		N = perturb_normal(N, gradient).xzy;  // <---
		// **swizzled back to Z Is Up**
		// restore invert Y (positive->negative) vulkan
		//N.z = -N.z;
	}

	outColor = pow(height, 1.0f/2.2f).xxx;
	return;

	//outColor = N.xzy * 0.5f + 0.5f;
	//return;

	// lighting
	const vec3 V = normalize(In.V.xyz);
	const vec2 albedo_ao = texture(_texArray[TEX_TERRAIN2], vec3(In.world_uvw.xy, 0)).rg;

	vec3 color = vec3(0);
	
	const float roughness = 0.5f;//1.0f - sq(albedo_ao.x*height);
	//const vec3 grid_segment = texture(_texArray[TEX_GRID], vec3(VolumeDimensions * 3.0f * vec2(In.world_uv.x, In.world_uv.y), 0)).rgb;
	//const float grid = (1.0f - dot(grid_segment.rgb, LUMA)) * max(0.0f, dot(N, vec3(0,0,-1)));

	const float albedo = pow(albedo_ao.x * height, 1.0f/2.2f);

	// twilight/starlight terrain lighting
	color.rgb += lit( albedo.xxx, make_material(0.75f, 0.0f, roughness), vec3(1),				 
				      albedo_ao.y, getAttenuation((1.0f - height) * InvVolumeDimensions, VolumeDimensions), // on a single voxel
				      vec3(0,0,1), N, V, false); // don't want to double-add reflections

	// regular terrain lighting
	vec3 light_color;
	vec4 Ld;

	getLight(light_color, Ld, In.uv.xyz);
	Ld.att = getAttenuation(Ld.dist, VolumeLength);

						// only emissive can have color
	color.rgb += lit( mix(albedo.xxx, unpackColor(In._color), In._emission), make_material(In._emission, 0.0f, roughness), light_color,		
					  getOcclusion(In.uv.xyz), Ld.att,
					  -Ld.dir, N, V, true);

	outColor.rgb = color;
}

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
						-Ld.dir, N, V, true);

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
						    -Ld.dir, N, V, true, fresnelTerm );
							             
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

