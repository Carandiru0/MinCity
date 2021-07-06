#version 450
#extension GL_GOOGLE_include_directive : enable
//#extension GL_KHR_shader_subgroup_vote: enable
#extension GL_EXT_control_flow_attributes :enable

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

#define fragment_shader
//#define subgroup_quad_enabled

#include "screendimensions.glsl"
#include "common.glsl"
#include "random.glsl"


// --- defines -----------------------------------------------------------------------------------------------------------------------------------//
#define MIN_STEP 0.00005f	// absolute minimum before performance degradation or infinite loop, no artifacts or banding
#define MAX_STEPS 512.0f

#define BOUNCE_INTERVAL (GOLDEN_RATIO_ZERO * 0.5f) // optimized when less, 0.25f is too short, 0.5f is to long
#define BOUNCE_EPSILON 0.5f

const float INV_MAX_STEPS = 1.0f/MAX_STEPS;
const float SQRT_MAX_STEPS = -2.0f * sqrt(MAX_STEPS); // must be negative

#define EPSILON 0.000000001f
#define LIGHT_ABSORBTION 0.25f // how bright the volumetric light appears, % of light reflected by air/fog
#define FOG_STRENGTH -0.08f // must be negative
#define FOG_MAX_HEIGHT 80.0f
// -----------------------------------------------------------------------------------------------------------------------------------------------//

//#define DEBUG_VOLUMETRIC
#ifdef DEBUG_VOLUMETRIC
#define DEBUG
#include "text.glsl"

void debug_out(inout vec4 outColor);

layout (binding = 10) restrict buffer DebugStorageBuffer {
  vec4		numbers;
  bvec4		toggles;
  float		history[1024][1024];
} b;

#endif

readonly layout(location = 0) in streamIn
{   // must all be xzy
	vec3				rd;
	flat vec3			eyePos;
	flat vec3			eyeDir;
} In;

#define OUT_REFLECT 0
#define OUT_VOLUME 1

#define DD 0
#define LIGHT 1
#define REFLECT 2
#define OPACITY 3

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput inputDepth;	// linear depthmap
layout (binding = 2) uniform sampler2D noiseMap;	// bluenoise
layout (binding = 3) uniform sampler2D fogMap;	// dynamic fog
layout (binding = 4) uniform sampler3D volumeMap[4];	// LightMap (direction & distance), (light color), (color reflection), OpacityMap
layout (binding = 5, rgba8) writeonly restrict uniform image2D outImage[2]; // reflection & volumetric writeonly access

//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"

layout (constant_id = 4) const float VolumeDimensions_X = 0.0f;
layout (constant_id = 5) const float VolumeDimensions_Y = 0.0f;
layout (constant_id = 6) const float VolumeDimensions_Z = 0.0f;
layout (constant_id = 7) const float InvVolumeDimensions_X = 0.0f;
layout (constant_id = 8) const float InvVolumeDimensions_Y = 0.0f;
layout (constant_id = 9) const float InvVolumeDimensions_Z = 0.0f;
#define VolumeDimensions vec3(VolumeDimensions_X, VolumeDimensions_Y, VolumeDimensions_Z)
#define InvVolumeDimensions vec3(InvVolumeDimensions_X, InvVolumeDimensions_Y, InvVolumeDimensions_Z)

layout (constant_id = 10) const float LightVolumeDimensions_X = 0.0f;
layout (constant_id = 11) const float LightVolumeDimensions_Y = 0.0f;
layout (constant_id = 12) const float LightVolumeDimensions_Z = 0.0f;
layout (constant_id = 13) const float InvLightVolumeDimensions_X = 0.0f;
layout (constant_id = 14) const float InvLightVolumeDimensions_Y = 0.0f;
layout (constant_id = 15) const float InvLightVolumeDimensions_Z = 0.0f;
#define LightVolumeDimensions vec3(LightVolumeDimensions_X, LightVolumeDimensions_Y, LightVolumeDimensions_Z)
#define InvLightVolumeDimensions vec3(InvLightVolumeDimensions_X, InvLightVolumeDimensions_Y, InvLightVolumeDimensions_Z)

layout (constant_id = 16) const float ZFar = 0.0f;
layout (constant_id = 17) const float ZNear = 0.0f;

#define FAST_LIGHTMAP
#include "lightmap.glsl"

vec2 intersect_box(in const vec3 orig, in const vec3 dir) {

	const vec3 inv_dir = 1.0f / dir;
	const vec3 tmin_tmp = (vec3(0.0f) - orig) * inv_dir;
	const vec3 tmax_tmp = (vec3(1.0f) - orig) * inv_dir;
	const vec3 tmin = min(tmin_tmp, tmax_tmp);
	const vec3 tmax = max(tmin_tmp, tmax_tmp);

	return vec2(max(tmin.x, max(tmin.y, tmin.z)), min(tmax.x, min(tmax.y, tmax.z)));
}

float fetch_opacity_emission( in const vec3 uvw) { // interpolates opacity & emission
	
	return( textureLod(volumeMap[OPACITY], uvw, 0).r );
}
float extract_opacity( in const float sampling ) // this includes transparent voxels, however result is negative if transparent
{
	return( clamp(sampling * 2.0f, -1.0f, 1.0f) ); // if opaque greatest value is 0.5f, want a value no greater than 1.0f - this is affected by emission
}
float fetch_opacity( in const vec3 uvw ) { // interpolates opacity - note if emissive, is also opaque
	return( extract_opacity(fetch_opacity_emission(uvw) ) );  
}
float extract_emission( in const float sampling ) // this includes transparent voxels that are emissive, result is positive either opaque or transparent
{
	return( max( 0.0f, (abs(sampling) - 0.5f) * 2.0f ) );  // if greater than 0.5f is emissive, want value no greater than 1.0f and only the emissive part
}
float fetch_emission( in const vec3 uvw ) { // interpolates emission
	return( extract_emission(fetch_opacity_emission(uvw)) ); 
}

bool bounced(in const float opacity)
{
	// simple, prevents volumetrics being intersected with geometry and prevents further steps correctly so that voxels behind
	// an opaque voxel are not marched causing strange issues like emission affecting transmission etc.
	return( (opacity - BOUNCE_EPSILON) >= 0.0f ); // maybe faster to test against zero with fast sign test
}
float fetch_opacity_texel( in const ivec3 p_scaled ) { // hit test for reflections - note if emissive, is also opaque
							  
	return (texelFetch(volumeMap[OPACITY], p_scaled, 0).r);
}


/* https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch01.html
Compute normal based on density from GPU Gems book, chapter 1 !
based on central differences

float d = 1.0/(float)voxels_per_block;
float3 grad;
grad.x = density_vol.Sample(TrilinearClamp, uvw + float3( d, 0, 0)) -
         density_vol.Sample(TrilinearClamp, uvw + float3(-d, 0, 0));
grad.y = density_vol.Sample(TrilinearClamp, uvw + float3( 0, d, 0)) -
         density_vol.Sample(TrilinearClamp, uvw + float3( 0,-d, 0));
grad.z = density_vol.Sample(TrilinearClamp, uvw + float3( 0, 0, d)) -
         density_vol.Sample(TrilinearClamp, uvw + float3( 0, 0,-d));
output.wsNormal = -normalize(grad);
*/
 
vec3 computeNormal(in const vec3 uvw)
{
	const vec2 half_texel_offset = vec2(1.0f, 0.0f) * InvVolumeDimensions_X;   // *** optimization - all dimension axis are equal so this simplifies to this, 
																			   // will NOT work properly with a non unit cube / dimensions x=y=z

	vec3 gradient;	
	// trilinear sampling - more accurate interpolated result then using texelFetch
	gradient.x = fetch_opacity(uvw - half_texel_offset.xyy) - fetch_opacity(uvw + half_texel_offset.xyy);
	gradient.y = fetch_opacity(uvw - half_texel_offset.yxy) - fetch_opacity(uvw + half_texel_offset.yxy);
	gradient.z = fetch_opacity(uvw - half_texel_offset.yyx) - fetch_opacity(uvw + half_texel_offset.yyx);

	return( normalize(gradient) ); // normal from central differences (gradient) 
}

// (intended for reflected light) - returns attenuation and reflection light color
float fetch_light_reflected( out vec3 light_color, in const vec3 uvw, in const float opacity, in const float dt) { // interpolates light normal/direction & normalized distance
										 
	float attenuation;
	const float volume_length = length(VolumeDimensions);
	const vec3 light_direction = getReflectionLightFast(light_color, attenuation, uvw, volume_length);

	// directional derivative - equivalent to dot(N,L) operation
	attenuation *= (1.0f - clamp((abs(extract_opacity(fetch_opacity_emission(uvw + light_direction.xyz * dt))) - opacity) / dt, 0.0f, 1.0f)); // absolute - sampked opacity can be either opaque or transparent

	return(attenuation);  
}

// (intended for volumetric light) - returns attenuation and light color, uses directional derivatiuves to further shade lighting 
// see: https://iquilezles.org/www/articles/derivative/derivative.htm
float fetch_light_volumetric( out vec3 light_color, out float scattering, inout float transparency, in const vec3 uvw, in const float opacity, in const float dt) { // interpolates light normal/direction & normalized distance
		
	float attenuation, normalized_distance;		//   ____FAST____
	const float volume_length = length(VolumeDimensions);
	const vec3 light_direction = getLightFast(light_color, attenuation, normalized_distance, uvw, volume_length);

	// directional derivative - equivalent to dot(N,L) operation
	attenuation *= (1.0f - clamp((abs(extract_opacity(fetch_opacity_emission(uvw + light_direction.xyz * dt))) - opacity) / dt, 0.0f, 1.0f)); // absolute - sampked opacity can be either opaque or transparent

	// dynamic fog
	vec4 fog = textureLod(fogMap, uvw.xy, 0.0f);
	fog.xyz = fog.xyz * 2.0f - 1.0f;
	fog.xyz = normalize(fog.xyz);

	const float fog_max_height = InvVolumeDimensions_Z * FOG_MAX_HEIGHT;
	const float fog_height = max(0.0f, InvVolumeDimensions_Z * FOG_MAX_HEIGHT * fog.w - uvw.z);
	
	transparency += fog_height;

	// scattering lit fog
	const float NdotL = max(0.0f, dot(-light_direction, fog.xyz));
	const float opacity_light = fetch_opacity(uvw + light_direction * normalized_distance + fog.xyz * InvVolumeDimensions_Z);
	scattering = max(0.0f, dot(-light_direction, normalize(vec3(opacity_light))));
	scattering = fog_height * scattering;
	scattering = scattering * (1.0f + NdotL);

	//scattering = max(0.0f, dot(-light_direction, normalize(opacity_light * vec3(f))));

	//
	//scattering = mix(scattering * fog_height, scattering, f);
	//scattering = mix(scattering, scattering * NdotL, fog.w);

	return(attenuation);  
}

float fetch_bluenoise(in const vec2 pixel)
{
	return( textureLod(noiseMap, pixel * BLUE_NOISE_UV_SCALER, 0).r ); // *bluenoise RED channel used* //
}
float fetch_depth()
{
	// sample & convert to linear depth
	const float ZLength = ZFar - ZNear;

	return( fma(subpassLoad(inputDepth).r, ZLength, ZNear) / ZLength );
}
vec3 reconstruct_depth(in const vec3 rd, in const float linear_depth)	// https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
{	
	// Project the view ray onto the camera's z-axis (eyeDirection)    xyz -> xzy
	const float viewZDist = abs(dot(In.eyeDir, rd));		

	// Scale the view ray by the ratio of the linear z value to the projected view ray
	return( /*In.eyePos +*/ rd * (linear_depth/viewZDist) );	// eye relative position (skipping redundant add and subtraction of eyePos to obtain yltimately interval //
}
/*
vec3 reconstruct_normal(in const vec3 eye_relative_depth) // this function can only be called in regular program flow, not inside a branch or condition
{														  // too low resolution but works
	// xyz -> xzy
	return( normalize(cross(dFdx(eye_relative_depth.xzy), dFdy(eye_relative_depth.xzy))).xzy );
}
vec3 reconstruct_normal_fine(in const vec3 rd)
{
	const vec2 InvDepthResDimensions = 2.0f / ScreenResDimensions; // depth is available at double the resolution of current color attribute
	const vec2 uv = gl_FragCoord.xy * InvScreenResDimensions;

	// xyz -> xzy
	const vec3 dFdxPos = reconstruct_depth(rd, uv + vec2(1.0f, 0.0f) * InvDepthResDimensions).xzy - reconstruct_depth(rd, uv + vec2(-1.0f, 0.0f) * InvDepthResDimensions).xzy;
	const vec3 dFdyPos = reconstruct_depth(rd, uv + vec2(0.0f, 1.0f) * InvDepthResDimensions).xzy - reconstruct_depth(rd, uv + vec2( 0.0f,-1.0f) * InvDepthResDimensions).xzy;

	return( normalize(cross(dFdxPos, dFdyPos)).xzy );
}
*/

#define light rgb
#define tran a

// steps:
// sample   -  fetch_opacity_emission(p)
// extract  -  extract_opacity(opacity_emission)
// isolate  -  max(0.0f, opacity_transparency)
//
//					   previous opacity     extracted & isolated opacity only 
void integrate_opacity(inout float opacity, in const float new_opacity, in const float dt)
{
	opacity = mix(opacity, new_opacity, smoothstep(0.5f, 1.0f, abs(new_opacity - opacity) / dt));
}

// volumetric light
void vol_lit(out vec3 light_color, out float intensity, out float scattering, out float attenuation, out float emission, out float transparency, inout float opacity,
		     in const vec3 p, in const float dt)
{
	const float opacity_emission = fetch_opacity_emission(p);

	// setup: 0 = not emissive
	//        1 = emissive
	emission = extract_emission(opacity_emission);

	const float opacity_transparency = extract_opacity(opacity_emission);
	
	// setup: 0 = not transparent
	//        1 = transparent
	transparency = smoothstep(0.0f, abs(min(0.0f, opacity_transparency)), emission); // seems to highlight edges better when emission and transparency mix in smoothstep here()
																					 // also removes some aliasing do not touch
	// setup: 0 = not opaque
	//        1 = opaque
	integrate_opacity(opacity, max(0.0f, opacity_transparency), dt);
	
	attenuation = fetch_light_volumetric(light_color, scattering, transparency, p, opacity, dt);

	// checked makes sense
	intensity = attenuation * emission * (transparency + opacity);

}

//const float phaseFunction = 1.0f / (4.0f * PI);

// rgb(0.222f,0.222f,1.0f) (far depth color) - royal blue (cooler)
// rgb(0.80f,0.65f,1.0f) (near depth color) - royal purle (warmer) 
//const vec3 royal_blue_purple = vec3(0.1776f,0.1443f,1.0f); // components are maximized (above combined)

void evaluateVolumetric(inout vec4 voxel, inout float opacity, in const vec3 p, in const float dt)
{
	//#########################
	vec3 light_color;
	float intensity;
	float scattering;
	float attenuation;
	float emission;
	float transparency;
	
	vol_lit(light_color, intensity, scattering, attenuation, emission, transparency, opacity, p, dt);  // shadow march tried, kills framerate

	
	// ### evaluate volumetric integration step of light
    // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
	const float fogAmount = (voxel.tran) * (1.0f - exp2(((transparency + emission) * 0.5f + scattering * 100.0f) * FOG_STRENGTH));
	const float lightAmount = (1.0f - opacity) * attenuation + emission;  // this is what brings out a volumetric glow *do not change*

	// this is balanced so that sigmaS remains conservative. Only emission or intensity can bring the level of sigmaS above 1.0f
	const float sigmaS = fogAmount * lightAmount + fogAmount + intensity;
	const float inv_sigmaE = 1.0f / max(EPSILON, sigmaS); // to avoid division by zero extinction

	// Area Light-------------------------------------------------------------------------------
    const vec3 Li = light_color * sigmaS;// incoming light
	const float sigma_dt = exp2(sigmaS * SQRT_MAX_STEPS * dt);
    const vec3 Sint = (Li - Li * sigma_dt) * inv_sigmaE; // integrate along the current step segment

	voxel.light += voxel.tran * Sint; // accumulate and also take into account the transmittance from previous steps

	// Evaluate transmittance -- to view independently (change in transmittance)---------------
	voxel.tran *= sigma_dt;				// decay
}


// reflected light
void reflect_lit(out vec3 light_color, out float attenuation, in const float opacity,
				 in const vec3 p, in const float dt)
{
	attenuation = fetch_light_reflected(light_color, p, opacity, dt);
}

vec4 reflection(in vec4 voxel, in const float bounce_scatter, in const float opacity, in const vec3 p, in const float dt)
{
 	// opacity at this point may be equal to zero
	// which means a "surface" was not hit after the initial bounce
	// so there is no reflected surface
	// however, there is still reflection of volumetric light
	// so we clamp it to the histogram value of opacity that provides
	// enough reflection of the light (looks nice and does not alias)
	
	// calculate lighting
	//#########################
	vec3 light_color;
	float attenuation;
	
	reflect_lit(light_color, attenuation, opacity, p, dt);
	
	// NdotV clamped at 1.0f so that shading with NdotV doesn't disobey laws of conservation
	// add ambient light that is reflected																								  // combat moire patterns with blue noise
	voxel.light = opacity * mix(voxel.light, light_color * attenuation, voxel.tran);
	voxel.a = bounce_scatter;

	return(voxel);
}

// all in parameters is important
void traceReflection(in const vec4 voxel, in const vec3 rd, in vec3 p, in const float dt, in float interval_remaining, in float interval_length)
{
	// interval_remaining currently equals the bounce interval location
	// allow reflections visible at same distance that was travelled from eye to bounce
	interval_length = interval_remaining = min(dt * MAX_STEPS, interval_length - interval_remaining) * BOUNCE_INTERVAL;

	p += 2.0f * dt * rd; // first reflected move to next point

	float opacity = 0.0f;
	vec4 stored_reflection = voxel;
	{ //  begin new reflection raymarch to find reflected surface
		// transform to world volume scale (using integer based coord because of texelFetch usage in test_hit() optimization)
		p *= VolumeDimensions;

		float reflection_avg = 1.0f;

		// find reflection
		for( ; interval_remaining >= 0.0f ; interval_remaining -= dt ) {  // fast sign test

			// hit opaque/transparent voxel ?
			const float opacity_sample = fetch_opacity_texel(ivec3(p));  // extreme loss of detail in reflections if extract_opacity() is used here! which is ok - the opacity here is isolated
			integrate_opacity(opacity, abs(opacity_sample), dt); // - passes thru transparent voxels recording a reflection, breaks on opaque.  
			[[branch]] if(bounced(opacity)) { 
				
				stored_reflection = mix(stored_reflection, reflection(voxel, 1.0f - smoothstep(0.0f, interval_length, interval_remaining), opacity, p * InvVolumeDimensions, dt),
										reflection_avg);
				reflection_avg *= 0.5f; // averaging reflections if passing thru transparent voxels.

				if (opacity_sample >= 0.0f) {
					break;	// hit reflected *opaque surface*
				}
			}

			p += rd;	// jump by full direction step (in worlld volume coordinates now)
		}
		// transform back to uv scale
		// not needed anymore p *= InvVolumeDimensions;
	} // end raymarch

	imageStore(outImage[OUT_REFLECT], ivec2(gl_FragCoord.xy), stored_reflection);
}

void main() {
  
	// Step 1: Normalize the view ray
	/*const*/ vec3 rd = normalize(In.rd);

	// Step 2: Intersect the ray with the volume bounds to find the interval
	// along the ray overlapped by the volume.
	vec2 t_hit = intersect_box(In.eyePos.xyz, rd);
	if (t_hit.x > t_hit.y) {
		return;
	}

	// We don't want to sample voxels behind the eye if it's
	// inside the volume, so keep the starting point at or in front
	// of the eye
	t_hit.x = max(t_hit.x, 0.0f);

	// Step 3: Compute the step size to march through the volume grid
	//const vec3 dt_vec = 1.0f / (VolumeDimensions * abs(rd));
	//const float dt = min(dt_vec.x, min(dt_vec.y, dt_vec.z));
	
	// interval and dt
	{
		// depth_eye_relative_position = reconstruct_depth(rd, linear_depth)/*- In.eyePos*/;	// see reconstruct_depth, eyePos redundancy removed, higher precision results
		const float t_depth = length(reconstruct_depth(rd, fetch_depth()));

#ifdef DEBUG_VOLUMETRIC
	b.numbers.x = t_hit.x;
	b.numbers.y = t_hit.y;
	b.numbers.z = t_depth;
#endif
		// Modify Interval end to be min of either volume end or scene depth
		// **** this clips the volume against any geometry in the scene
		t_hit.y = min(t_hit.y, t_hit.x + t_depth);
#ifdef DEBUG_VOLUMETRIC
	b.numbers.w = t_hit.y;
#endif
	}

	float pre_interval_length = (t_hit.y - t_hit.x);
	const float inv_num_steps = 1.0f / length(VolumeDimensions * rd); // number of steps for full volume
	float pre_dt = pre_interval_length * inv_num_steps;	// dt calculated @ what would be the full volume interval w/o depth clipping	
	pre_dt = max(pre_dt, pre_interval_length*INV_MAX_STEPS);
	// ----------------------------------- //
	const float dt = max(pre_dt, MIN_STEP);// fixes infinite loop bug, *do not change*
	// ----------------------------------- //

	// larger dt = larger step , want largest step for highest depth, smallest step for lowest depth
	//fma(pre_dt, linear_depth, pre_dt * 0.5f); // ***** if you want to see where the artifacts are coming from, or the accuracy of the ray march LOOK at DT *****
	// override = vec3(dt*100.0f);							            // as depth increases so do the "holes" this dynamically adjusts the step size and
																        // gives a good performance to accuracy ratio	

	// Step 4: Starting from the entry ,mk,mkpoint, march the ray through the volume
	// and sample i
	
	// Integration variables		// depth modified transmission - gives depth to volume - clamp is important...
    
	// without modifying interval variables start position
	// done so the interval frame by frame is deterministic per pixel
	// which reflection cache optimizations rely on ---
	const float blue_noise = fetch_bluenoise(gl_FragCoord.xy);
	// adjust interval_length and start position by "bluenoise step"
	// -------------------------------------------------------------------- //
	const float interval_length = pre_interval_length - (dt * blue_noise);
	float interval_remaining = interval_length;
	// -------------------------------------------------------------------- //

	vec3 p = In.eyePos.xyz + fma(dt, blue_noise, t_hit.x) * rd; // jittered offset
		
	// inverted volume height fix (only place this needs to be done!)
	// this is done so its not calculated everytime at texture sampling
	// however it adds confusion, for instance reconstruction of depth 
	p.z = 1.0f - p.z;	// invert slice axis (height of volume)
	rd.z = -rd.z;		// ""		""			""		  ""

	vec4 voxel = vec4(vec3(0.0f),1.0f);		// accumulated light color w/scattering, // transmittance
	
	{ // volumetric scope
		float opacity = 0.0f;

		// Volumetric raymarch 
		for( ; interval_remaining >= 0.0f ; ) {  // fast sign test
		
			// -------------------------------- part lighting ----------------------------------------------------
			// ## evaluate light
			evaluateVolumetric(voxel, opacity, p, dt * 2.0f); // evaluated at 2x dt because of skipped light evaluation (only every second step)

			// ## test hit voxel
			[[branch]] if( bounced(opacity) ) {	
				break;	// stop raymarch, note: can't do lod here, causes aliasing
			}

			// ## step
			p += dt * rd;
			interval_remaining -= dt;

			// ---------------------------------- end one step -----------------------------------------------------

			// ---------------------------------- part test only ---------------------------------------------------
			// ## evalute opacity only
			integrate_opacity(opacity, max(0.0f, extract_opacity(fetch_opacity_emission(p))), dt);
			
			// ## test hit voxel
			[[branch]] if( bounced(opacity) ) {		
				break;	// stop raymarch, note: can't do lod here, causes aliasing
			}

			// ## step
			p += dt * rd;
			interval_remaining -= dt;

			// ------------------------------------ end two steps ---------------------------------------------------
		} // end for
	
		traceReflection(voxel, reflect(rd, computeNormal(p)), 
						p, dt, interval_remaining, interval_length);

		// refine volumetric raymarch //
		for( ; interval_remaining >= 0.0f ; ) {  // fast sign test
			
			// ## step
			p += dt * rd;
			interval_remaining -= dt;

			vec4 refine_voxel = voxel;
			float refine_opacity = opacity;

			evaluateVolumetric(refine_voxel, refine_opacity, p, dt);

			[[branch]] if (refine_opacity > opacity) { // is next opacity closer?
				// take refinement
				voxel = refine_voxel;
				opacity = refine_opacity;
			}
			else {
				// refinement complete
				break;
			}

		} // end for

		// last step - sample right at depth removing unclean edges
		if (interval_remaining < 0.0f) { 
			interval_remaining += dt;
			p += interval_remaining * rd;  // this is the last "partial" step!

			evaluateVolumetric(voxel, opacity, p, interval_remaining);
		}
	} // end volumetric scope

//#ifdef DEBUG_VOLUMETRIC
	//debug_out(voxel);
	// check normals:
	//imageStore(outImage[VOLUME], ivec2(gl_FragCoord.xy), vec4(computeNormal(p) * 0.5f + 0.5f, 1.0f));
	//imageStore(outImage[VOLUME], ivec2(gl_FragCoord.xy), vec4((rd * 0.5f + 0.5f) * interval_length, 1.0f));
	
	//return;
//#endif

	// output volumetric light
	imageStore(outImage[OUT_VOLUME], ivec2(gl_FragCoord.xy), vec4(voxel.light, (1.0f - voxel.tran))); // <-- this is correct blending of light, don't change it. opacity = 1.0f - transmission
						
	// - done!
	//vec4 test = textureLod(fogMap, gl_FragCoord.xy * InvScreenResDimensions, 0.0f);

	//imageStore(outImage[OUT_VOLUME], ivec2(gl_FragCoord.xy), vec4(test.aaa, 1.0f)); // <-- this is correct blending of light, don't change it. opacity = 1.0f - transmission
	
}

#ifdef DEBUG_VOLUMETRIC
void debug_out(inout vec4 outColor) {

	// view sampling density
	//outColor = vec4(mix(vec3(0,0,1), vec3(1,0,0), (1.0f/dt) / min(length(VolumeDimensions * abs(rd)), VolumeDimensions.z)), 1.0f);

	const vec2 uv = gl_FragCoord.xy / ScreenResDimensions;
	// debug view //
	//DistanceMarched.y = mix( ((t_hit.y - t_hit.x) * DistanceMarched.y * 100.0f), DistanceMarched.x, DistanceMarched.y * (1.0 - DistanceMarched.x));
	//outColor /= vec4( mix(vec3(0.0f, 0.0f, 1.0f), vec3(1.0f, 0.0f, 0.0f), DistanceMarched.x /* length( VolumeDimensions.xzy * rd)*/) * (1.0f - DistanceMarched.y), (1.0f - DistanceMarched.x));
	//
	
	//b.numbers.z = max(b.numbers.z, DistanceMarched.x - DistanceMarched.y);
	//b.numbers.w = min(b.numbers.w, DistanceMarched.x - DistanceMarched.y);
	
	//if ( uv.x < 0.5f ) 
	//{
		
		//outColor = vec4(DistanceMarched.xxx, 1.0f);

		//if (!bounced) {
		//	outColor = vec4(at_opacity.xxx, 1.0f);
		//}
		/*
		const ivec2 index = ivec2(gl_FragCoord.xy);

		const float prev_at_opacity = b.history[index.y][index.x];

		outColor = vec4(mix(outColor.rgb, ceil(prev_at_opacity.xxx), 0.5f), 1.0f);

		if ( 0 == index.y ) {
			b.history[index.y + 1][index.x] = at_opacity;
		}
		else {
			b.history[index.y + 1][index.x] = prev_at_opacity;
		}

		//outColor = vec4(mix(outColor.x, at_opacity, clamp(index.y, 0.0f, 1.0f)).xxx, 1.0f);
		
		//b.numbers.z = max(b.numbers.z, at_opacity);
		//b.numbers.w = min(b.numbers.w, at_opacity);
		//outColor = vec4(DistanceMarched.xxx, 1.0f);
		*/
	//}
	//else {

		
		//outColor = vec4(vec3(0), 1);
		//outColor = vec4(normal.xzy * 0.5f + 0.5f, 1.0f);
		//outColor = vec4(abs(DistanceMarched.x - DistanceMarched.y).xxx, 1.0f);
	//}
	
	//outColor = vec4(vec3(DistanceMarched.x, voxel.light.g, DistanceMarched.x), 1.0f);
	//

	//outColor = vec4(1.0f - exp2(-DistanceMarched.xxx), 1.0f);
	//outColor = vec4( mix(vec3(0,0,1), vec3(1,0,0), float(MAX_STEPS-steps_remaining) / float(MAX_STEPS)), 1.0f);




	const float ROW_START = 0.67f;
	const float SPACING = 0.025f;
	// Set a general character size...
    vec2 charSize = vec2(.05, .07) * 0.5f;
    // and a starting position.
    vec2 charPos = vec2(ROW_START, 0.6f);
    // Draw some text!
    float chr = 0.0;

    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_H, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_I, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_X, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_COLN, charPos, charSize, uv); charPos.x += SPACING;

    charPos.x += .15;
    chr += drawFixed( b.numbers.x, 3, charPos, charSize, uv);

	charPos.x = ROW_START;
	charPos.y += SPACING * 2.0f;

	chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_H, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_I, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_Y, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_COLN, charPos, charSize, uv); charPos.x += SPACING;

    charPos.x += .15;
    chr += drawFixed(b.numbers.y, 3, charPos, charSize, uv);

	charPos.x = ROW_START;
	charPos.y += SPACING * 4.0f;

	charPos.x += .1;
    chr += drawFixed( b.numbers.z, 5, charPos, charSize, uv);

	charPos.x = ROW_START;
	charPos.y += SPACING * 4.0f;

	charPos.x += .1;
    chr += drawFixed( b.numbers.w, 5, charPos, charSize, uv);

	outColor.rgb = outColor.rgb + (chr);
	outColor.a -= chr;

}
#endif



