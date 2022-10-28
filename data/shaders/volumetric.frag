#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_quad : enable
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

layout(early_fragment_tests) in;  // required for proper checkerboard stencil buffer usage

#define fragment_shader
#define subgroup_quad_enabled

#include "screendimensions.glsl"
#include "common.glsl"
#include "random.glsl"

// --- defines -----------------------------------------------------------------------------------------------------------------------------------//
#define BOUNCE_EPSILON 0.499f

#define MIN_STEP 0.00005f	// absolute minimum before performance degradation or infinite loop, no artifacts or banding
#define MAX_STEPS VolumeDimensions

float PHASE_FUNCTION(in const float emission) // t = max(0.0f, dot(rd, n))    [0.0f .... 1.0f]
{
	return((1.0f + emission) / (GOLDEN_RATIO * PI)); // Isotropic
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
#define REFLECT 2
#define OPACITY 3

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput inputDepth;	// linear depthmap
layout (binding = 2) uniform sampler2DArray noiseMap;	// bluenoise
layout (binding = 3) uniform sampler2D normalMap;	// accurate view space xzy normal map
layout (binding = 4) uniform sampler3D volumeMap[4];	// LightMap (direction & distance), (light color), (color reflection), (opacity)
layout (binding = 5) uniform samplerCube reflectionCubeMap; // light coming from the sky/space surrounding the volume
layout (binding = 6, rgba8) writeonly restrict uniform image2D outImage[2]; // reflection, volumetric    *writeonly access

//layout (constant_id = 0) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 1) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 2) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"
//layout (constant_id = 3) const float SCREEN_RES_RESERVED see  "screendimensions.glsl"

// "World Visible Volume"			 // xzy (as set in specialization constants for volumetric fragment shader)
layout (constant_id = 4) const float VolumeDimensions = 0.0f; // world volume
layout (constant_id = 5) const float InvVolumeDimensions = 0.0f;
layout (constant_id = 6) const float VolumeLength = 0.0f;

// "Light Volume"
layout (constant_id = 7) const float LightVolumeDimensions = 0.0f; // light volume
layout (constant_id = 8) const float InvLightVolumeDimensions = 0.0f;

// "World Volume"
layout (constant_id = 9) const float WorldDimensions = 0.0f;

layout (constant_id = 10) precise const float ZFar = 0.0f;
layout (constant_id = 11) precise const float ZNear = 0.0f;

#include "lightmap.glsl"

precise vec2 intersect_box(in const vec3 orig, in const vec3 dir) {

	precise const vec3 tmin_tmp = (0.0f - orig) / dir;
	precise const vec3 tmax_tmp = (1.0f - orig) / dir;
	precise const vec3 tmin = min(tmin_tmp, tmax_tmp);
	precise const vec3 tmax = max(tmin_tmp, tmax_tmp);

	return vec2(max(tmin.x, max(tmin.y, max(0.0f, tmin.z))), min(tmax.x, min(tmax.y, max(0.0f, tmax.z)))); // *bugfix - max(0.0f, tmin.z), max(0.0f, tmax.z) additionally limit the interval to the "lowest" ground height. Nothing exists below this height.
}

vec3 offset(in const vec3 uvw)
{
	return(uvw); // linear interpolation (best)
	//return((uvw * (VolumeDimensions + In.fractional_offset / 0.25f)) * InvVolumeDimensions);
	//return((floor(uvw * VolumeDimensions)) * InvVolumeDimensions); // exact to the voxel but has banding
}

float fetch_opacity_emission( in const vec3 uvw) { // interpolates opacity & emission
	
	return( textureLod(volumeMap[OPACITY], offset(uvw), 0).r );
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

bool bounced(in const float opacity)
{
	// simple, prevents volumetrics being intersected with geometry and prevents further steps correctly so that voxels behind
	// an opaque voxel are not marched causing strange issues like emission affecting transmission etc.
	return( (opacity - BOUNCE_EPSILON) >= 0.0f ); // maybe faster to test against zero with fast sign test
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
	return( normalize(textureLod(normalMap, gl_FragCoord.xy * InvScreenResDimensions, 0.0f).xyz) ); // these are world space normals in xzy frag shader form
}

// (intended for reflected light) - returns attenuation and reflection light color
float fetch_light_reflected( out vec3 light_color, in const vec3 uvw, in const vec3 n) { // interpolates light normal/direction & normalized distance
										 
	vec4 Ld;
	getReflectionLightFast(light_color, Ld, offset(uvw));
	Ld.att = getAttenuation(Ld.dist, VolumeLength);
		
	// real dot(N,L)
	Ld.att *= max(0.0f, dot(n, -Ld.dir));

	return(Ld.att);  
}

// (intended for volumetric light) - returns attenuation and light color, uses directional derivatiuves to further shade lighting 
// see: https://iquilezles.org/www/articles/derivative/derivative.htm
float fetch_light_volumetric( out vec3 light_color, in const vec3 uvw, in const float opacity, in const float dt) { // interpolates light normal/direction & normalized distance
		
	vec4 Ld;
	getLightFast(light_color, Ld, uvw);
	Ld.att = getAttenuation(Ld.dist, VolumeLength);

	// directional derivative - equivalent to dot(N,L) operation
	Ld.att *= (1.0f - clamp((abs(extract_opacity(fetch_opacity_emission(uvw + Ld.dir * dt))) - opacity) / dt, 0.0f, 1.0f)); // absolute - sampked opacity can be either opaque or transparent

	return(Ld.att); // returns lightAmount  
}

float fetch_bluenoise(in const vec2 pixel)
{																
	return( textureLod(noiseMap, vec3(pixel * BLUE_NOISE_UV_SCALER, In.slice), 0).r ); // *bluenoise RED channel used* // 
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
void vol_lit(out vec3 light_color, out float emission, out float attenuation, inout float opacity,
		     in const vec3 p, in const float dt)
{
	const float opacity_emission = fetch_opacity_emission(p);

	// setup: 0 = not emissive
	//        1 = emissive
	emission = extract_emission(opacity_emission);

	// setup: 0 = not opaque
	//        1 = opaque
	integrate_opacity(opacity, extract_opacity(opacity_emission), dt);

	// lightAmount = attenuation
	attenuation = fetch_light_volumetric(light_color, p, opacity, dt);
}

void evaluateVolumetric(inout vec4 voxel, inout float opacity, in const vec3 p, in const float dt)
{
	//#########################
	vec3 light_color;
	float emission, attenuation;
	
	vol_lit(light_color, emission, attenuation, opacity, p, dt);  // shadow march tried, kills framerate 

	// ### evaluate volumetric integration step of light
    // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/

	// this is balanced so that sigmaS remains conservative. Only emission can bring the level of sigmaS above 1.0f
	//lightAmount = lightAmount * random_hemi_up.w;

	const float sigmaS = (1.0f - opacity) * attenuation * voxel.tran;
	const float sigmaE = max(EPSILON, sigmaS); // to avoid division by zero extinction

	// Area Light-------------------------------------------------------------------------------
    const vec3 Li = (sigmaS + attenuation * light_color) * PHASE_FUNCTION(emission); // incoming light  *** note this is fine tuned for awesome brightness of volumetric light effects
	const float sigma_dt = exp2(-sigmaE * attenuation);
    const vec3 Sint = (Li - Li * sigma_dt) / sigmaE; // integrate along the current step segment
	voxel.light += voxel.tran * Sint; // accumulate and also`` take into account the transmittance from previous steps

	// Evaluate transmittance -- to view independently (change in transmittance)---------------
	voxel.tran *= sigma_dt;		// *bugfix - dot not multiply emission by dt, square it and add to transmission. emission finaly works right. ***do not change***		// decay or grows with emission
}


// reflected light
void reflect_lit(out vec3 light_color, out float emission, out float attenuation,
				 in const vec3 p, in const vec3 n)
{
	emission = fetch_emission(p);
	attenuation = fetch_light_reflected(light_color, p, n);
}

void reflection(inout vec4 voxel, in const float distance_to_bounce, in const vec3 p, in const vec3 n, in const float opacity)
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
	float emission, attenuation;

	reflect_lit(light_color, emission, attenuation, p, n);
	
	// add ambient light that is reflected																								  
	voxel.light += light_color * attenuation * voxel.tran;
	voxel.a = getAttenuation(distance_to_bounce) * (opacity + emission);
	// upscaling shader uses these output values uniquely.
}

float fetch_opacity_reflection( in const vec3 p ) { // hit test for reflections - note if emissive, is also opaque
	
	//const vec3 uvw = fma(p, VolumeDimensions, In.frac);
	const vec3 uvw = offset(p);
	// texelfetch first to bring into cache the same result trilinear filtered in following textureLod (potentially)
	return( texelFetch(volumeMap[OPACITY], ivec3(floor(uvw * VolumeDimensions + 0.5f)), 0).r + textureLod(volumeMap[OPACITY], uvw, 0).r ); // *bugfix - smoother
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
vec4 traceReflection(in vec4 voxel, in vec3 rd, in vec3 p, in const vec3 n, in const float dt, in const float bn, in float interval_remaining, in const float interval_length)
{								
	rd = normalize(reflect(rd, n));

	interval_remaining = interval_remaining + subgroupQuadSwapDiagonal(interval_remaining); // less divergence with reflections & needs to be 2x anyways (so not an average, rather a sum)
	interval_remaining = min(interval_length, interval_remaining * 3.5f);

	const float fresnel_reflection = pow(abs(dot(rd, n)), 5.0f);
	voxel.rgb = voxel.rgb * (1.0f - voxel.tran) + fresnel_reflection * voxel.tran * bn * BLUE_NOISE_DITHER_SCALAR;

	const vec3 bounce = p; // save bounce position
	p += GOLDEN_RATIO * dt * rd;	// first reflected move to next point - critical to avoid moirre artifacts that this is done with a large enough step (hence scaling by golden ratio) tuned/tweaked.

	float opacity = 0.0f;
	{ //  begin new reflection raymarch to find reflected surface

		// find reflection
		[[dont_unroll]] for( ; interval_remaining >= 0.0f ; interval_remaining -= dt) {  // fast sign test

			p += dt * rd;

			// hit opaque voxel ?
			// extreme loss of detail in reflections if extract_opacity() is used here! which is ok - the opacity here is isolated
			integrate_opacity(opacity, fetch_opacity_reflection(p), dt); // - passes thru transparent voxels recording a reflection, breaks on opaque.  

			[[branch]] if(bounced(opacity)) { 
							
				reflection(voxel, distance(p, bounce), p, n, opacity);
				break;	// hit reflected *opaque surface*
			}
		}

	} // end raymarch
	
	return(voxel);
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
	precise const float dt = max(pre_dt, MIN_STEP);// * GOLDEN_RATIO_ZERO * 0.5f; // fixes infinite loop bug, scaled by the golden ratio instead, placing each slice at intervals of the golden ratio (scaled). (accuracy++) (removes moirre effect in reflections)
	// ----------------------------------- //

	// larger dt = larger step , want largest step for highest depth, smallest step for lowest depth
	//fma(pre_dt, linear_depth, pre_dt * 0.5f); // ***** if you want to see where the artifacts are coming from, or the accuracy of the ray march LOOK at DT *****
	// override = vec3(dt*100.0f);							            // as depth increases so do the "holes" this dynamically adjusts the step size and
																        // gives a good performance to accuracy ratio	

	// Step 4: Starting from the entry ,mk,mkpoint, march the ray through the volume
	// and sample i
	
	// Integration variables		// depth modified transmission - gives depth to volume - clamp is important...
    
	// without modifying interval variables start position
	
	// adjust start position by "bluenoise step"	
	const float bn = fetch_bluenoise(gl_FragCoord.xy);
	const float jittered_interval = dt * bn * GOLDEN_RATIO; // jittered offset should be scaled by golden ratio (hides some noise)

	// ro = eyePos -> volume entry (t_hit.x) + jittered offset
	vec3 p = In.eyePos + (t_hit.x + jittered_interval) * rd; // jittered offset
	
	// -------------------------------------------------------------------- //
	float interval_remaining = interval_length - jittered_interval; // interval remaining must be accurate/exact for best results
	// -------------------------------------------------------------------- //
	// inverted volume height fix (only place this needs to be done!)
	// this is done so its not calculated everytime at texture sampling
	// however it adds confusion, for instance reconstruction of depth 
	p.z = 1.0f - p.z;	// invert slice axis (height of volume)
	rd.z = -rd.z;		// ""		""			""		  ""
	rd = normalize(rd);

	vec4 voxel;
	voxel.rgb = vec3(0.0f);
	voxel.a = 1.0f;

	{ // volumetric scope
		float opacity = 0.0f;

		// Volumetric raymarch 
		[[dont_unroll]] for( ; interval_remaining >= 0.0f ; ) {  // fast sign test
		
			// -------------------------------- part lighting ----------------------------------------------------
			// ## evaluate light
			evaluateVolumetric(voxel, opacity, p, dt);

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

		// last step - sample right at depth removing unclean edges
		if (interval_remaining < 0.0f) { 
			interval_remaining += dt;
			p += interval_remaining * rd;  // this is the last "partial" step!

			evaluateVolumetric(voxel, opacity, p, interval_remaining);
		}

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

		// store volumetrics
		imageStore(outImage[OUT_VOLUME], ivec2(gl_FragCoord.xy), voxel); 

		// store reflection 
		imageStore(outImage[OUT_REFLECT], ivec2(gl_FragCoord.xy), traceReflection(voxel, rd, p, 
																				  getNormal(), dt, bn, interval_remaining, interval_length));

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



