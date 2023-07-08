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

#include "light.glsl"

// **must** have defined a :
// layout (constant_id = any) const float VolumeLength = 0.0f; // This length must be scaled by the voxel size. Pass it in getLight()
// layout (binding = any) uniform sampler3D volumeMap[]; // at least array size of 2, with DD at index 0, COLOR at index 1

// FRAGMENT - - - - In = xzy space (matching 3D textures width,depth,height)
//			--------Out = In

// *********************************************** private usage : //     

void lightmap_internal_fetch_fast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 voxel) { 

	const vec3 uvw = voxel * InvLightVolumeDimensions; 

	light_direction_distance = textureLod(volumeMap[DD], uvw, 0);

	light_color = textureLod(volumeMap[COLOR], uvw, 0).rgb;
}
void lightmap_internal_fetch_fast( out float light_distance, out vec3 light_color, in const vec3 voxel) {  
	
	const vec3 uvw = voxel * InvLightVolumeDimensions;

	light_distance = textureLod(volumeMap[DD], uvw, 0).a;

	light_color = textureLod(volumeMap[COLOR], uvw, 0).rgb;
}
void lightmap_internal_fetch_fast( out vec4 light_direction_distance, in const vec3 voxel) {  

	const vec3 uvw = voxel * InvLightVolumeDimensions;

	light_direction_distance = textureLod(volumeMap[DD], uvw, 0);
}
void lightmap_internal_fetch_fast( out float light_distance, in const vec3 voxel) {  
	
	const vec3 uvw = voxel * InvLightVolumeDimensions;

	light_distance = textureLod(volumeMap[DD], uvw, 0).a;
}


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
	
	const vec3 uvw = voxel * InvLightVolumeDimensions; 

	vec4 light_direction_pre_distance = textureLod(volumeMap[DD], uvw, 0);

	light_direction_distance = light_direction_pre_distance * area + light_direction_distance;
	light_color = textureLod(volumeMap[COLOR], uvw, 0).rgb * area + light_color;
}
void lightmap_internal_fetch_nn( inout float light_distance, inout vec3 light_color, in const float area, in const vec3 voxel) { // intended usage with nn sampling
	
	const vec3 uvw = voxel * InvLightVolumeDimensions; 

	float light_pre_distance = textureLod(volumeMap[DD], uvw, 0).a;

	light_distance = light_pre_distance * area + light_distance;
	light_color = textureLod(volumeMap[COLOR], uvw, 0).rgb * area + light_color;
}

void lightmap_internal_sampleNaturalNeighbour(out vec4 light_direction_distance, inout vec3 light_color, in vec3 voxel) {
    
    const vec2 n = floor(voxel.xy);
    const vec2 f = fract(voxel.xy) * 2.0f - (1.0f - 0.000001f);	// fixes NaNs at position 0,0,0 causing black voxels everywhere *compute_area()*, this at least hides in the 1.0f constant (compiler will optimize)
	
	float w = 0.0f;

	light_direction_distance = vec4(0);		// initialize accumulation here
	light_color = vec3(0);

    for (voxel.y = -1.0f; voxel.y <= 1.0f; ++voxel.y) {
        for (voxel.x = -1.0f; voxel.x <= 1.0f; ++voxel.x) {
   
			const float a = lightmap_internal_compute_area(f - voxel.xy * 2.0f);
			    
			lightmap_internal_fetch_nn(light_direction_distance, light_color, a, vec3(n + voxel.xy, voxel.z));

			w += a;
		}
    }

	w = 1.0f / w;
	light_direction_distance = light_direction_distance * w;
	light_color = light_color * w;		
}
void lightmap_internal_sampleNaturalNeighbour(out float light_distance, inout vec3 light_color, in vec3 voxel) {
    
    const vec2 n = floor(voxel.xy);
    const vec2 f = fract(voxel.xy) * 2.0f - (1.0f - 0.000001f);	// fixes NaNs at position 0,0,0 causing black voxels everywhere *compute_area()*, this at least hides in the 1.0f constant (compiler will optimize)
	
	float w = 0.0f;

	light_distance = 0.0f;		// initialize accumulation here
	light_color = vec3(0);

    for (voxel.y = -1.0f; voxel.y <= 1.0f; ++voxel.y) {
        for (voxel.x = -1.0f; voxel.x <= 1.0f; ++voxel.x) {
   
			const float a = lightmap_internal_compute_area(f - voxel.xy * 2.0f);
			    
			lightmap_internal_fetch_nn(light_distance, light_color, a, vec3(n + voxel.xy, voxel.z));

			w += a;
		}
    }

	w = 1.0f / w;
	light_distance = light_distance * w;
	light_color = light_color * w;		
}

// ********************************************* public usage : //

void getLightMapFast( out vec4 light_direction_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_direction_distance, light_color, uvw * LightVolumeDimensions + 0.5f);  // *bugfix - half voxel offset is exact - required
}
void getLightMapFast( out float light_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_distance, light_color, uvw * LightVolumeDimensions + 0.5f);  
}
void getLightMapFast( out vec4 light_direction_distance, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_direction_distance, uvw * LightVolumeDimensions + 0.5f);  
}
void getLightMapFast( out float light_distance, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_distance, uvw * LightVolumeDimensions + 0.5f); 
}

void getLightMap( out vec4 light_direction_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_direction_distance, light_color, uvw * LightVolumeDimensions + 0.5f); 

	// nn sampling
	//lightmap_internal_sampleNaturalNeighbour(light_direction_distance, light_color, uvw * LightVolumeDimensions + 0.5f);  
}
void getLightMap( out float light_distance, out vec3 light_color, in const vec3 uvw ) 
{
	// linear sampling
	lightmap_internal_fetch_fast(light_distance, light_color, uvw * LightVolumeDimensions + 0.5f);

	// nn sampling
	//lightmap_internal_sampleNaturalNeighbour(light_distance, light_color, uvw * LightVolumeDimensions + 0.5f);  
}

#define att a
#define dist a
#define pos xyz
void getLight(out vec3 light_color, out vec4 light_direction_distance, in const vec3 uvw) 
{
	getLightMap(light_direction_distance, light_color, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}
void getLight(out vec3 light_color, out float light_distance, in const vec3 uvw) 
{
	getLightMap(light_distance, light_color, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}

// less preferred, but when required (fast hw-trilinear sampling)
void getLightFast(out vec3 light_color, out vec4 light_direction_distance, in const vec3 uvw) 
{
	getLightMapFast(light_direction_distance, light_color, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}
void getLightFast(out vec3 light_color, out float light_distance, in const vec3 uvw) 
{
	getLightMapFast(light_distance, light_color, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}
void getLightFast(out vec4 light_direction_distance, in const vec3 uvw) 
{
	getLightMapFast(light_direction_distance, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}
void getLightFast(out float light_distance, in const vec3 uvw) 
{
	getLightMapFast(light_distance, uvw); 
	// light_direction_distance.a = normalized [0...1] distance
}

#endif // _LIGHTMAP_GLSL
