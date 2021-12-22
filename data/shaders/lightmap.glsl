#ifndef _LIGHTMAP_GLSL
#define _LIGHTMAP_GLSL

/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#define DD 0
#define COLOR 1
#define REFLECT 2

// define FAST_LIGHTMAP b4 this header to include the faster version in addition to default version
//													  getLightFast()                getLight()
// or define nothing at all b4 this header is included for default highest quality sampling

// **must** have defined a :
// layout (constant_id = any) const float VolumeLength = 0.0f; // This length must be scaled by the voxel size. Pass it in getLight()
// layout (binding = any) uniform sampler3D volumeMap[]; // at least array size of 2, with DD at index 0, COLOR at index 1

// FRAGMENT - - - - In = xzy space (matching 3D textures width,depth,height)
//			--------Out = In

// *********************************************** private usage : //                 

//#ifdef FAST_LIGHTMAP
void lightmap_internal_fetch_fast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 voxel) {  //  intended usage with rndC (no + 0.5f here)
	
	const vec3 voxel_coord = voxel * InvLightVolumeDimensions;

	light_direction_distance = textureLod(volumeMap[DD], voxel_coord, 0);
	light_direction_distance.a = light_direction_distance.a * 0.5f + 0.5f;  // compress distance to [0.0f...1.0f] range (16bit signed texture)

	light_color = textureLod(volumeMap[COLOR], voxel_coord, 0).rgb;
}
#ifdef FAST_LIGHTMAP
void lightmap_internal_fetch_reflection_fast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 voxel) {  //  intended usage with rndC (no + 0.5f here)
	
	const vec3 voxel_coord = voxel * InvLightVolumeDimensions;

	light_direction_distance = textureLod(volumeMap[DD], voxel_coord, 0);
	light_direction_distance.a = light_direction_distance.a * 0.5f + 0.5f;  // compress distance to [0.0f...1.0f] range (16bit signed texture)

	light_color = textureLod(volumeMap[REFLECT], voxel_coord, 0).rgb;
}
#endif

// natural neighbour sampling of light volume

// Natural Neighbour Interpolation for regular grid
// this variant of https://www.shadertoy.com/view/XlSGRR
// interpolates the width and depth axis with Natural Neighbour, passing thru height
// where height is ibterpolated bilinearly via gpu interpolators instead
// saves cost of sampling 3rd dimension (via natural neighbour sampling), is still only a 2D space problem
// looks fantastic
float lightmap_internal_compute_area(in const vec2 uv) {
    const vec2 n = abs(normalize(uv));
    vec4 p = (vec4(n.xy,-n.xy)-length(uv)*0.5f) / n.yxyx;
    const vec4 h = max(vec4(0.0f),sign(1.0f-abs(p)));   

	// NaN fix happens earlier all of the NaN handling below is disabled(not needed) until black NaN voxels , if, come back
    // fix p becoming NaN; unfortunately 0*(1/0) doesn't
    // fix the value   
    //p.x = (h.x < 0.5f)?0.0f:p.x; 
    //p.y = (h.y < 0.5f)?0.0f:p.y; 
    //p.z = (h.z < 0.5f)?0.0f:p.z; 
    //p.w = (h.w < 0.5f)?0.0f:p.w;
	//p = select(p, vec4(0.0f), lessThan(h, vec4(0.5f)));	// optimization verified on shadertoy

    p = (p+1.0f)*0.5f;
    return 0.5f*(h.y*(p.y*p.x*h.x + (p.y+p.w)*h.w) + (p.x+p.z)*h.x*h.z);
}
void lightmap_internal_fetch_nn( inout vec4 light_direction_distance, inout vec3 light_color, in const float area, in const vec3 voxel) { // intended usage with nn sampling
	
	const vec3 voxel_coord = voxel * InvLightVolumeDimensions; // *bugfix 0.5 is not added here, see other related bugfix

	vec4 light_direction_pre_distance = textureLod(volumeMap[DD], voxel_coord, 0);
	light_direction_pre_distance.a = light_direction_pre_distance.a * 0.5f + 0.5f;  // compress distance to [0.0f...1.0f] range (16bit signed texture)

	light_direction_distance = light_direction_pre_distance * area + light_direction_distance;
	light_color = textureLod(volumeMap[COLOR], voxel_coord, 0).rgb * area + light_color;
}

void lightmap_internal_sampleNaturalNeighbour(out vec4 light_direction_distance, inout vec3 light_color, in vec3 uvw) {
     
    const vec2 n = floor(uvw.xy);
    const vec2 f = fract(uvw.xy) * 2.0f - (1.0f - 0.000001f);	// fixes NaNs at position 0,0,0 causing black voxels everywhere *compute_area()*, this at least hides in the 1.0f constant (compiler will optimize)
	
	float w = 0.0f;

	light_direction_distance = vec4(0);		// initialize accumulation here
	light_color = vec3(0);

    for (uvw.y = -1.0f; uvw.y <= 1.0f; ++uvw.y) {
        for (uvw.x = -1.0f; uvw.x <= 1.0f; ++uvw.x) {
   
			const float a = lightmap_internal_compute_area(f - uvw.xy * 2.0f);
			    
			lightmap_internal_fetch_nn(light_direction_distance, light_color, a, vec3(n + uvw.xy, uvw.z));

			w += a;
		}
    }

	w = 1.0f / w;
	light_direction_distance = light_direction_distance * w;
	light_color = light_color * w;		
}

// for sampling specifically direction and distance only.
void lightmap_internal_fetch_direction_distance( out vec4 light_direction_distance, in const vec3 uvw) {
	
	light_direction_distance = textureLod(volumeMap[DD], uvw, 0);
	light_direction_distance.a = light_direction_distance.a * 0.5f + 0.5f;  // compress distance to [0.0f...1.0f] range (16bit signed texture)
}


// ********************************************* public usage : //
#ifdef FAST_LIGHTMAP
// iq's awesome smoothstep based sampling (rndC)
// https://www.shadertoy.com/view/MlS3Dc - ultrasmooth subpixel sampling

void getLightMapFast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// rndC sampling
	lightmap_internal_fetch_fast(light_direction_distance, light_color, rndC(uvw * LightVolumeDimensions + 0.5f));  // *bugfix - half voxel offset is exact - required
}
void getReflectionLightMapFast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// rndC sampling
	lightmap_internal_fetch_reflection_fast(light_direction_distance, light_color, rndC(uvw * LightVolumeDimensions + 0.5f));  // *bugfix - half voxel offset is exact - required
}
#endif

void getLightMap( out vec4 light_direction_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// linear sampling
	//lightmap_internal_fetch_fast(light_direction_distance, light_color, uvw * LightVolumeDimensions + 0.5f);

	// nn sampling
	lightmap_internal_sampleNaturalNeighbour(light_direction_distance, light_color, uvw * LightVolumeDimensions + 0.5f);  // *bugfix - half voxel offset is exact - required
}

// main public functions:

// final inverse square law equation used:
// a = 1.0f / ( (d + 1.0) * (d + 1.0 )
// see : https://www.desmos.com/calculator/hqksaay8ax
float getAttenuation(in float normalized_light_distance, in const float volume_length)
{
	normalized_light_distance = normalized_light_distance * volume_length * 0.5f + 1.0f; // denormalization and scaling to world coordinates (actual world distance) + 1.0

	return( 1.0f / (normalized_light_distance * normalized_light_distance) );
}

#ifdef FAST_LIGHTMAP
vec3 getLightFast(out vec3 light_color, out float attenuation, out float normalized_distance, in const vec3 uvw, in const float volume_length) 
{
	vec4 light_direction_distance; 

	getLightMapFast(light_direction_distance, light_color, uvw); // .zw = xz normalized visible uv coords
	    
	normalized_distance = light_direction_distance.a;

	attenuation = getAttenuation(normalized_distance, volume_length); 

	// light direction is stored in view space natively in xzy format
	return(normalize(light_direction_distance.xyz));
}
vec3 getReflectionLightFast(out vec3 light_color, out float attenuation, in const vec3 uvw, in const float volume_length) 
{
	vec4 light_direction_distance; 

	getReflectionLightMapFast(light_direction_distance, light_color, uvw); // .zw = xz normalized visible uv coords
	
	attenuation = getAttenuation(light_direction_distance.a, volume_length); 

	// light direction is stored in view space natively in xzy format
	return(normalize(light_direction_distance.xyz));
}
#endif

vec3 getLight(out vec3 light_color, out float attenuation, in const vec3 uvw, in const float volume_length) 
{
	vec4 light_direction_distance; 

	getLightMap(light_direction_distance, light_color, uvw); // .zw = xz normalized visible uv coords
	   
	attenuation = getAttenuation(light_direction_distance.a, volume_length); 

	// light direction is stored in view space natively in xzy format
	return(normalize(light_direction_distance.xyz));
}

#endif // _LIGHTMAP_GLSL
