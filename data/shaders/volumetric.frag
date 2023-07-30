#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes :enable
#extension GL_KHR_shader_subgroup_quad : enable

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

layout(early_fragment_tests) in;  // required for proper checkerboard stencil buffer usage

#ifdef HDR
	#define output_format rgba16
#else 
    #define output_format rgba8
#endif

#define fragment_shader

#include "screendimensions.glsl"
#include "common.glsl"
#include "random.glsl"

// --- defines -----------------------------------------------------------------------------------------------------------------------------------//
#define LIGHT_INTENSITY (20.0f)  // *bugfix: important that applied to light color only
#define BOUNCE_EPSILON (0.5f)
#define REFLECTION_BOUNCE_DISTANCE_SCALAR (1.33f)
#define BACK_FORWARD_SCATTER_SCALAR (0.35f) // <-- adjust scattering scale & direction, backward (-1.0f) to forward (1.0f)
#define REFLECTION_STRENGTH (1.0f) // <--- adjust reflection brightness / intensity *tweakable*
#define REFLECTION_FADE (20.0f) // <-- adjust reflections fading away at distance more with larger values. *tweakable*
#define MIN_STEP (0.00005f)	// absolute minimum before performance degradation or infinite loop, no artifacts or banding
#define MAX_STEPS (VolumeLength) // this touches just inside the orthographic frustum

// HenyeyGreenstein Phase Function - Light Scattering
// https://pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions
// 
// * g *  [-1.0 ... 1.0] backward ... forward scattering   0 equals isotropic
float PHASE_FUNCTION(in const vec3 wo, in const vec3 wi, in const float att, in const float g)
{
	const float inv_pi = 1.0f / (4.0f * PI);
	const float denom = 1.0f + g * g + 2.0f * g * dot(wo, wi);
	
	return(inv_pi * (1.0f - g * g) / (denom * sqrt(denom))); 
}

#define EPSILON 0.000000001f
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

#endif // DEBUG_VOLUMETRIC

layout(location = 0) in streamIn
{   // must all be xzy
	readonly noperspective vec3	rd;
	readonly noperspective vec3	eyePos;
	readonly flat vec3			eyeDir;
	readonly flat float			slice;
} In;

#define OUT_REFLECT 0
#define OUT_VOLUME 1

#define DD 0
#define LIGHT 1
#define OPACITY 2

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput inputDepth;	// linear depthmap
layout (binding = 2) uniform sampler2DArray noiseMap;	// bluenoise
layout (binding = 3) uniform sampler2D normalMap;	// accurate view space xzy normal map
layout (binding = 4) uniform sampler3D volumeMap[3];	// LightMap (direction & distance), (light color), (opacity)
layout (binding = 5, output_format) writeonly restrict uniform image2D outImage[2]; // reflection, volumetric    *writeonly access
 
//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"

// "World Visible Volume"			 // xzy (as set in specialization constants for volumetric fragment shader)
layout (constant_id = 4) const float VolumeDimensions = 0.0f; // world volume
layout (constant_id = 5) const float InvVolumeDimensions = 0.0f;
layout (constant_id = 6) const float VolumeLength = 0.0f; // <-- scaled by minivoxel size

// "Light Volume"
layout (constant_id = 7) const float LightVolumeDimensions = 0.0f; // light volume
layout (constant_id = 8) const float InvLightVolumeDimensions = 0.0f;

layout (constant_id = 9) precise const float ZFar = 0.0f;
layout (constant_id = 10) precise const float ZNear = 0.0f;

#include "lightmap.glsl"

precise vec2 intersect_box(in const vec3 orig, in const vec3 dir) {

	precise const vec3 tmin_tmp = (0.0f - orig) / dir;
	precise const vec3 tmax_tmp = (1.0f - orig) / dir;
	precise const vec3 tmin = min(tmin_tmp, tmax_tmp);
	precise const vec3 tmax = max(tmin_tmp, tmax_tmp);

	return vec2(max(tmin.x, max(tmin.y, max(0.0f, tmin.z))), min(tmax.x, min(tmax.y, max(0.0f, tmax.z)))); // *bugfix - max(0.0f, tmin.z), max(0.0f, tmax.z) additionally limit the interval to the "lowest" ground height. Nothing exists below this height.
}

float fetch_opacity_emission( in const vec3 uvw) { // interpolates opacity & emission
	
	return( textureLod(volumeMap[OPACITY], (uvw * VolumeDimensions) * InvVolumeDimensions, 0).r );  // @todo - offset determined, but why??
}
float extract_opacity( in const float sampling ) // this includes transparent voxels, however result is negative if transparent
{
	return( clamp(sampling * 2.0f, 0.0f, 1.0f) ); // if opaque greatest value is 0.5f, want a value no greater than 1.0f - this is affected by emission
}
float fetch_opacity( in const vec3 uvw ) { // interpolates opacity - note if emissive, is also opaque
	return( extract_opacity(fetch_opacity_emission(uvw) ) );  
}
float extract_emission( in const float sampling ) // this includes transparent voxels that are emissive, result is positive either opaque or transparent
{
	return( max( 0.0f, sampling - 0.5f) * 2.0f );  // if greater than 0.5f is emissive, want value no greater than 1.0f and only the emissive part
}
float fetch_emission( in const vec3 uvw ) { // interpolates emission
	return( extract_emission(fetch_opacity_emission(uvw)) ); 
}

bool bounced(in const float opacity, in const float epsilon)
{
	// simple, prevents volumetrics being intersected with geometry and prevents further steps correctly so that voxels behind
	// an opaque voxel are not marched causing strange issues like emission affecting transmission etc.
	return( (opacity - epsilon) >= 0.0f ); // maybe faster to test against zero with fast sign test
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
// [deprecated] - full resolution normals are now used
vec3 computeNormal(in const vec3 uvw)
{
	const vec4 voxel_offset = vec4(InvVolumeDimensions.xxx, 0.0f);

	vec3 gradient;	
	// trilinear sampling + centered differences
	gradient.x =   fetch_opacity(uvw + voxel_offset.xww) - fetch_opacity(uvw - voxel_offset.xww);
	gradient.y =   fetch_opacity(uvw + voxel_offset.wyw) - fetch_opacity(uvw - voxel_offset.wyw);
	gradient.z = -(fetch_opacity(uvw + voxel_offset.wwz) - fetch_opacity(uvw - voxel_offset.wwz)); // *bugfix - Vulkan Up Axis negative

	return( normalize(gradient) ); // normal from central differences (gradient) 
}

vec3 getNormal()
{	
	const vec3 n = textureLod(normalMap, gl_FragCoord.xy * InvScreenResDimensions, 0.0f).xyz;

	return( normalize(-n) ); // these are world space normals in xzy frag shader form, negated to match raymarch direction (correction) *do not change*
}

// returns normalized light distance, light position and light color - uses trilinear sampling on both color and position + distance, intended for *volumetric accumulation*
float fetch_light_fast( out vec3 light_color, out vec3 light_direction, in const vec3 uvw) {
										 
	vec4 Ld;
	getLightFast(light_color, Ld, uvw);

	Ld.pos = Ld.pos - uvw;
	float d = length(Ld.pos);
	light_direction = Ld.pos / d; // normalized light direction
	d = getAttenuation(d);
	Ld.dist = d - abs(d - Ld.dist);
	//Ld.z = -Ld.z; // vulkan: relative positions are both positive, but to match N & V, the z axis (up) must be flipped for L

	return(Ld.dist);  
}

float fetch_bluenoise(in const vec2 pixel, in const float slice)
{																
	return( textureLod(noiseMap, vec3(pixel * BLUE_NOISE_UV_SCALAR, slice), 0).g ); // *bluenoise GREEN channel used* *do not change*
}
precise float fetch_depth()
{
	// sample & convert to linear depth
	precise const float ZLength = ZFar - ZNear;

	return( fma(subpassLoad(inputDepth).r, ZLength, ZNear) / ZLength );
}
precise vec3 reconstruct_depth(in const vec3 rd, in const float linear_depth)	// https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
{	
	// Project the view ray onto the camera's z-axis (eyeDirection)    xyz -> xzy
	precise const float viewZDist = abs(dot(normalize(In.eyeDir), rd));		

	// Scale the view ray by the ratio of the linear z value to the projected view ray
	return( /*In.eyePos +*/ rd * (linear_depth/viewZDist) );	// eye relative position (skipping redundant add and subtraction of eyePos to obtain yltimately interval //
}

/*
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

// generalization:
//   - opacity
//   - emission
//   - any integration, only requires a single variable to keep track of the accumulated integration.
//
// integration:        previous, result                     new               step                        
void integrate(inout float previous_out, in const float current, in const float dt)
{                                                                    // change in opacity per step interval
	previous_out = mix(previous_out, current, clamp(abs(current - previous_out) / dt, 0.0f, 1.0f) * 0.5f + 0.5f );  
}

// volumetric light
void vol_lit(out vec4 light_color, out vec3 light_direction, inout float opacity, inout float emission,
		     in const vec3 p, in const float dt)
{
	const float opacity_emission = fetch_opacity_emission(p);

	// setup: 0 = not emissive
	//        1 = emissive
	integrate(emission, extract_emission(opacity_emission), dt);

	// setup: 0 = not opaque
	//        1 = opaque
	integrate(opacity, extract_opacity(opacity_emission), dt);

	// lightAmount = attenuation
	light_color.a = fetch_light_fast(light_color.rgb, light_direction, p);
}

float evaluateVolumetric(inout vec4 voxel, inout float opacity, inout float emission, in const vec3 p, in const float dt)
{
	//#########################
	vec4 light_color; vec3 light_direction;  // "attenuation = light_color.a"
	
	vol_lit(light_color, light_direction, opacity, emission, p, dt);  // shadow march tried, kills framerate 

	const float bn = fetch_bluenoise(p.xy, p.z * BLUE_NOISE_SLICE_SCALAR) * 2.0f - 1.0f; // best for random vector here, no visible noise after accumulation of the ray march light color for this pixel. slice cannot be dynamicly changed every frame, light intensity visibly pulses.
	float scatter = abs(dot(normalize(light_direction + bn), light_direction)); // random direction in the sphere and the cosine (angle) between light direction (accumulated over raymarch)
	scatter = pow(scatter, 5.0f);

	light_color.a = PHASE_FUNCTION(In.eyeDir.xyz, light_direction, light_color.a, scatter * BACK_FORWARD_SCATTER_SCALAR); // skew the phasefunction to back-scatter (-1.0f) thru forward-scattering based on normalize(random vector + light_direction) - half angle

	{ // Russian roulette based on blue noise
		const bvec3 weight = lessThan(bn.xxx, vec3(0.25f, 0.5f, 0.75f)); // last choice is simply local unchanged (1.0f)
		light_color = mix( light_color, subgroupQuadSwapHorizontal(light_color), weight.xxxx );
		light_color = mix( light_color, subgroupQuadSwapVertical(light_color), weight.yyyy );
		light_color = mix( light_color, subgroupQuadSwapDiagonal(light_color), weight.zzzz );
	}

	// ### evaluate volumetric integration step of light
    // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/

	// this is balanced so that sigmaS remains conservative. Only emission can bring the level of sigmaS above 1.0f

	const float sigmaS = (1.0f - opacity) * dt;
	const float sigmaE = max(EPSILON, sigmaS); // to avoid division by zero extinction

	// Area Light----------------------------------------------------------------------------------------------------------------                                 
    // |-------------------- light amount ----------------------- free space * dt + constant fog amount --- color and intensity-|
	const vec3 Li = fma( fma(light_color.a, emission, light_color.a), sigmaS, sigmaS) * light_color.rgb * LIGHT_INTENSITY; // incoming light  *** note this is fine tuned for awesome brightness of volumetric light effects
	const float sigma_dt = exp2(-sigmaS * light_color.a);
    const vec3 Sint = (Li - Li * sigma_dt) / sigmaE; // integrate along the current step segment
	voxel.light += (voxel.tran + emission * sigma_dt) * Sint; // accumulate and also`` take into account the transmittance from previous steps
	                                                                                                                                                                                                                                                                                                                                                 
	// Evaluate transmittance -- to view independently (change in transmittance)---------------
	voxel.tran = voxel.tran * exp2(-sigmaS * light_color.a * 5.0f);  // *bugfix - transmission sigma_dt needs to be scaled exponentially 4x - 5x for decent precision so unfortuanetly it must be calculated again 
	// bugfix - adding emission*sigma_dt directly to transmission results in "transparent" sections around emissive light sources. The checkerboard pattern is exposed, and some times the ground is showing thru opaque voxels - no good
	//          instead of accumulating emission into the transmission, it is applied above to the light momentarily (workaround) - prevents increase in precision_dt below which would slow the raymarch (see below)
	return(voxel.tran * 0.5f + 0.5f);  // *only* modifies precision_dt on return to a minimum of 50% of its value or greater depending on the current integrated transmission. blue noise tried here, however the accumulation results in noticable white noise / light distortion so its removed.
}


// reflected light
void reflect_lit(out vec3 light_color, out vec3 light_direction, out float attenuation,
				 in const vec3 p)
{
	attenuation = fetch_light_fast(light_color, light_direction, p);
}

vec4 reflection(in const float distance_to_bounce, in const vec3 p, in const vec3 n, in const float emission, in const float dt)
{
 	// opacity at this point may be equal to zero
	// which means a "surface" was not hit after the initial bounce
	// so there is no reflected surface
	// however, there is still reflection of volumetric light
	// so we clamp it to the histogram value of opacity that provides
	// enough reflection of the light (looks nice and does not alias)
	
	// calculate lighting
	//#########################
	vec3 light_color, light_direction;
	float attenuation;

	reflect_lit(light_color, light_direction, attenuation, p + n * dt);
	
	// account for light direction *important* - removes reflections that should not be present
	light_color *= 0.5f * max(0.0f, dot(n, light_direction)); // *bugfix - should be half of the light color here, as below it could again double depending on current emission*attenuation (see below). This also darkens reflections more when emission is not present giving a more realistic, not overbrite reflection.

	// add ambient light that is reflected	
	vec4 voxel;
	voxel.light = clamp(light_color, vec3(0), vec3(1)) * (REFLECTION_STRENGTH + attenuation*emission);  // *do not change*
	voxel.a = getAttenuation(distance_to_bounce * REFLECTION_FADE);   // upsampling uses directly as "fading", inversely as "blur"  // *do not change*

	// upscaling shader uses these output values uniquely.
	return voxel;
}

///////////////////////////////////////////////////////////////////////////////////////////
/*
float pixel_stars(in const vec3 st, in const float t) {
  float r = rand(round(st));
  return 0.000001 + smoothstep(0.995*t, 1.0, r);
}

float stars(in vec3 st, in float t) {
  return(pixel_stars(st, t));
}

// background with stars
float background(in const vec3 dir, in float d, in const float dt)
{
    d = 1.0f / (1.0f + d*d);
    
    float den = abs(dir.y); den = 1.0-den; den=den*den*den*den; den*=.1;
    
    float n = stars(dt * (dir*(1.0f - d)*ScreenResDimensions.y), 1.0f - den);
    	
	return d * 1000000000.0f * pow(n*0.95f+den,22.0);
}
*/
///////////////////////////////////////////////////////////////////////////////////////////

// all in parameters is important
vec4 traceReflection(in vec3 rd, in vec3 p, in const vec3 n, in const float dt, in float interval_remaining, in const float interval_length)
{								
	rd = normalize(reflect(rd, n));

	interval_remaining = interval_remaining * 2.0f; // must be 2x
	interval_remaining = min(interval_length, interval_remaining * REFLECTION_BOUNCE_DISTANCE_SCALAR); // extend range for reflection bounce

	const vec3 bounce = p; // save bounce position
	p += dt * rd;	// first reflected move to next point - critical to avoid moirre artifacts that this is done with a large enough step (hence scaling by golden ratio) tuned/tweaked.

	int early_hits = 1;

	float opacity = 0.0f;

	{ //  begin new reflection raymarch to find reflected surface

		// find reflection
		[[dont_unroll]] for( ; interval_remaining >= 0.0f ; ) {  // fast sign test

			const float precision_dt = max(MIN_STEP, (1.0f - opacity) * dt); 

			// hit opaque voxel ?
			const float opacity_emission = fetch_opacity_emission(p);
			integrate(opacity, extract_opacity(opacity_emission), precision_dt); // - passes thru transparent voxels recording a reflection, breaks on opaque.  
			
			--early_hits; // if distance is travelled from the start position, this will be less than zero and the bounce is not an early "self-reflection error"
			[[branch]] if(early_hits < 0 && bounced(opacity, BOUNCE_EPSILON * precision_dt)) { 
					
				// * reflection bounced * //
				return(reflection(distance(p, bounce), p, n, extract_emission(opacity_emission), precision_dt));
			}

			p += precision_dt * rd;
			interval_remaining -= precision_dt;
		}

	} // end raymarch
	
	// * no reflection bounce * //

	// no reflection (rgb), set for [maximum blur=0.0f] or [maximum fade=1.0f] (a)
	return(vec4(0)); 
}

void main() {
  
	// Step 1: Normalize the view ray
	/*const*/ precise vec3 rd = normalize(In.rd);

	// Step 2: Intersect the ray with the volume bounds to find the interval
	// along the ray overlapped by the volume.
	precise vec2 t_hit = intersect_box(In.eyePos, rd);
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
		precise const float t_depth = length(reconstruct_depth(rd, fetch_depth())/* - In.eyePos*/);

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

	precise const float interval_length = (t_hit.y - t_hit.x);

	precise const float inv_num_steps = InvVolumeDimensions; // number of steps for full volume
	precise float pre_dt = interval_length * inv_num_steps;	// dt calculated @ what would be the full volume interval w/o depth clipping	
	pre_dt = max(pre_dt, interval_length/MAX_STEPS);
	// ----------------------------------- //
	precise const float dt = max(MIN_STEP, pre_dt * 0.5f); // *bugfix - final step size is scaled 0.5 resulting in greater precision (better sampling) - 0.25 is overkill adding 3-4ms overhead, max() fixes infinite loop bug
	// ----------------------------------- //

	// larger dt = larger step , want largest step for highest depth, smallest step for lowest depth
	//fma(pre_dt, linear_depth, pre_dt * 0.5f); // ***** if you want to see where the artifacts are coming from, or the accuracy of the ray march LOOK at DT *****


	// Step 4: Starting from the entry ,mk,mkpoint, march the ray through the volume
	// and sample i
	
	// Integration variables		
    
	// without modifying interval variables start position
	
	// ro = eyePos + volume entry (t_hit.x)
	float interval_remaining = interval_length;
	vec3 p = In.eyePos + t_hit.x * rd; 
	
	// inverted volume height fix (only place this needs to be done!)
	// this is done so its not calculated everytime at texture sampling
	// however it adds confusion, for instance reconstruction of depth 
	p.z = 1.0f - p.z;	// invert slice axis (height of volume)
	rd.z = -rd.z;		// ""		""			""		  ""
	rd = normalize(rd);

	{
		// adjust start position by "bluenoise step"	
		const float bn = fetch_bluenoise(gl_FragCoord.xy, In.slice); // very first jitter step is changed every frame, so that the start position for every pixel varies over time.
		const float jittered_interval = dt * bn; // //  must scale and bias to prevent really small dt stepsizes. this is strickly for modify the step size so that the samples are jittered, using the green channel is safe and does not mix with the red channel before or after the raymarch as it's isolated due to the accumulation of multiple samples in the volumetric or reflection raymarches. (blurs the bluenoise)

		p += jittered_interval * rd; // + jittered offset
		interval_remaining -= jittered_interval; // interval remaining must be accurate/exact for best results
	}

	vec4 voxel;
	voxel.rgb = vec3(0.0f);
	voxel.a = 1.0f;

	{ // volumetric scope
		float opacity = 0.0f, emission = 0.0f;

		// Volumetric raymarch 	
		[[dont_unroll]] for( ; interval_remaining >= 0.0f ; ) {  // fast sign test
		
			// -------------------------------- part lighting ----------------------------------------------------
			float precision_dt = max(MIN_STEP, (1.0f - opacity) * dt);   // dt is smaller (smaller step) when opacity is higher == smaller steps when encountering opaque voxels to close in as close as possible by decreasing step size as opacity increases...

			// ## evaluate light
			precision_dt *= evaluateVolumetric(voxel, opacity, emission, p, precision_dt);

			// ## test hit voxel
			[[branch]] if( bounced(opacity, BOUNCE_EPSILON * precision_dt) ) {	
				break;	// stop raymarch, note: can't do lod here, causes aliasing
			}

			// ## step
			p += precision_dt * rd;
			interval_remaining -= precision_dt;
			
			// ---------------------------------- end one step -----------------------------------------------------

			// *bugfix - skipping a step inserts gaps into the current integrated opacity
			// visually looks like a on/off opacity repeating

		} // end for

		// last step - sample right at depth removing unclean edges
		if (interval_remaining < 0.0f) { 
			interval_remaining += dt;
			p += interval_remaining * rd;  // this is the last "partial" step!

			evaluateVolumetric(voxel, opacity, emission, p, interval_remaining);
		}

		// trace shadow ray //
		

#ifdef DEBUG_VOLUMETRIC
		debug_out(voxel);
#endif
		// test normals:
		//voxel.rgb = mix(computeNormal(p), -getNormal(), 0.5f) * 0.5f + 0.5f; // test normals
		//voxel.rgb = -getNormal() * 0.5f + 0.5f;
		//voxel.rgb = normalize( computeNormal(p) + -getNormal() ) * 0.5f + 0.5f; // test normals
		//voxel.rgb = vec3(NdotV);
		//voxel.a = 0.5f;

		//voxel = vec4(vec3(0),1); // turn-off volumetrics
		//voxel.rgb = getNormal() * 0.5f + 0.5f;
		//voxel.a = 1.0f; // make fully opaque

		// store volumetrics
		imageStore(outImage[OUT_VOLUME], ivec2(gl_FragCoord.xy), voxel); 

		// store reflection 
		vec4 reflected = traceReflection(rd, p, getNormal(), dt, interval_remaining, interval_length);
		reflected.rgb = reflected.rgb * voxel.a;
		imageStore(outImage[OUT_REFLECT], ivec2(gl_FragCoord.xy), reflected);

	} // end volumetric scope
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



	#define SCALE_FONT 0.75f
	
	const float ROW_START = 0.52f;
	const float SPACING = 0.025f * SCALE_FONT * 2.0f;
	// Set a general character size...
    vec2 charSize = vec2(.04, .05) * SCALE_FONT;
    // and a starting position.
    vec2 charPos = vec2(ROW_START, 0.6f);
    // Draw some text!
    float chr = 0.0;

	// first row
    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_H, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_I, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_X, charPos, charSize, uv); charPos.x += SPACING;
    charPos.x += SPACING * 2.0f;

    charPos.x += .15;
    chr += drawFixed( b.numbers.x, 3, charPos, charSize, uv);

	/// second row
	charPos.x = ROW_START;
	charPos.y += SPACING * 2.0f;

	chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_H, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_I, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_T, charPos, charSize, uv); charPos.x += SPACING;
    chr += drawChar( CH_Y, charPos, charSize, uv); charPos.x += SPACING;
    charPos.x += SPACING * 2.0f;

    charPos.x += .15;
    chr += drawFixed(b.numbers.y, 3, charPos, charSize, uv);

	// third row
	charPos.x = ROW_START;
	charPos.y += SPACING * 2.0f;

	charPos.x += SPACING * 2.0f * 2.0f;
	charPos.x += SPACING * 2.0f;

	charPos.x += .15;
    chr += drawFixed( b.numbers.z, 5, charPos, charSize, uv);

	// fourth row
	charPos.x = ROW_START;
	charPos.y += SPACING * 2.0f;

	charPos.x += SPACING * 2.0f * 2.0f;
	charPos.x += SPACING * 2.0f;

	charPos.x += .15;
    chr += drawFixed( b.numbers.w, 5, charPos, charSize, uv);

	outColor.rgb = outColor.rgb + (chr);
	outColor.a += chr;

}
#endif



