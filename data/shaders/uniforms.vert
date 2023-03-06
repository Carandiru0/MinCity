#version 460
#extension GL_GOOGLE_include_directive : enable
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

// for shared uniform access, use geometry shader instead
#if defined(DYNAMIC) || !defined(CLEAR)
#include "common.glsl"
#endif

#if !defined(BASIC) && !defined(CLEAR)
#include "sharedbuffer.glsl"
#endif

#define emission x
#define metallic y
#define roughness z
#define ambient w

layout(location = 0) in vec4 inWorldPos;
layout(location = 1) in vec4 inUV;

layout (constant_id = 0) const float VOX_SIZE = 0.0f;
layout (constant_id = 1) const float VOX_STEP = 0.0f;

#if defined(BASIC)

layout (binding = 1, r8) writeonly restrict uniform image3D opacityMap;

layout (constant_id = 2) const float VolumeDimensions = 0.0f;

// corresponding to volume dimensions
const vec3 TransformToIndexScale = vec3(2.0f, -2.0f, 2.0f);
layout (constant_id = 3) const float TransformToIndexBias_X = 0.0f;
layout (constant_id = 4) const float TransformToIndexBias_Y = 0.0f;
layout (constant_id = 5) const float TransformToIndexBias_Z = 0.0f;
#define TransformToIndexBias vec3(TransformToIndexBias_X, TransformToIndexBias_Y, TransformToIndexBias_Z)
layout (constant_id = 6) const float InvToIndex_X = 0.0f;
layout (constant_id = 7) const float InvToIndex_Y = 0.0f;
layout (constant_id = 8) const float InvToIndex_Z = 0.0f;
#define InvToIndex vec3(InvToIndex_X, InvToIndex_Y, InvToIndex_Z)

#elif defined(HEIGHT) // NOT BASIC:

layout (constant_id = 2) const float VolumeDimensions = 0.0f;

#endif // BASIC

#if defined(HEIGHT) // terrain
writeonly layout(location = 0) out streamOut
{
	flat vec3	right, forward, up;
	flat uint   adjacency;
	flat vec4	world_uvw;
#ifndef BASIC
	flat float   ambient;
	flat float   color;
	flat float   emission;
#endif
} Out;
#else  // voxels only
writeonly layout(location = 0) out streamOut
{
	flat vec3	right, forward, up;
	flat uint	adjacency;
#ifdef BASIC
	flat vec2	world_uv;
#endif
#ifndef BASIC
	flat float	 color;
	flat vec4    material;
	flat vec4    extra;
#endif
} Out;
#endif


#if defined(HEIGHT)

#ifdef BASIC
layout (constant_id = 9) const int MINIVOXEL_FACTOR = 0;
layout (constant_id = 10) const float TERRAIN_MAX_HEIGHT = 0;
#else
layout (constant_id = 3) const int MINIVOXEL_FACTOR = 0;
layout (constant_id = 4) const float TERRAIN_MAX_HEIGHT = 0;
#endif

#define SHIFT_EMISSION 6U
#define SHIFT_HEIGHTSTEP 8U
#define SHIFT_MINHEIGHTSTEP 24U
const uint MASK_ADJACENCY = 0x3FU;		         /*			  0000 0000 0011 1111 */
const uint MASK_EMISSION = 0x40U;		         /*           0000 0000 01xx xxxx */    
const uint MASK_HEIGHTSTEP = 0x00FFFF00U;	     /* 1111 1111 1111 1111 Rxxx xxxx */   // free to use R
const uint MASK_MINHEIGHTSTEP = 0xFF000000U;     /* remaining high bits (8)       */

#else // not HEIGHT

#define SHIFT_EMISSION 6U
#define SHIFT_METALLIC 7U
#define SHIFT_ROUGHNESS 8U
const uint MASK_ADJACENCY =  0x3FU;		/*           0000 0000 0011 1111 */
const uint MASK_EMISSION = 0x40U;		/*           0000 0000 01xx xxxx */
const uint MASK_METALLIC = 0x80U;		/*           0000 0000 1xxx xxxx */
const uint MASK_ROUGHNESS = 0xF00U;		/*			 0000 1111 xxxx xxxx */ 

#if defined(DYNAMIC) && defined(TRANS) 
#define SHIFT_TRANSPARENCY 13U
const uint MASK_TRANSPARENCY = 0x6000U;	/*			 011x xxxx xxxx xxxx */ 
#endif
#endif

#ifndef BASIC

#if defined(DYNAMIC) && defined(TRANS)
const float INV_MAX_TRANSPARENCY = (1.0f / 4.0f);	// 4 levels of transparency (0.25f, 0.5f, 0.75f, 1.0f)
#endif

#endif

#if defined(DYNAMIC)

// trick, the first 3 components x,y,z are sent to vertex shader where the quaternion is then decoded. w is color, however the sign is used for decoding the quaternion. see uniforms.vert - decode_quaternion() [bandwidth optimization]
vec4 decode_quaternion(in const vec3 xyz, in const float sgn)
{
	// 1.0 = x^2 + y^2 + z^2 + w^2
	// w^2 = 1.0 - x^2 + y^2 + z^2
	// w = sqrt(1.0 - x^2 + y^2 + z^2)

	const float norm = dot(xyz,xyz); // *bugfix - y must be flipped for vulkan coord system
	return(normalize(vec4(vec3(xyz.x,-xyz.y,xyz.z), sgn * sqrt(1.0f - abs(norm)))));
}

#endif

void main() {
  
#define size VOX_SIZE
  { // orientation output vectors right, forward, up

#ifdef DYNAMIC
	// only DYNAMIC
	const vec4 quaternion = decode_quaternion(inUV.xyz, sign(inUV.w));

	const vec3 right = v3_rotate(vec3(1.0f, 0.0f, 0.0f), quaternion); //v3_rotate_pitch(v3_rotate_yaw(v3_rotate_roll(vec3(1.0f, 0.0f, 0.0f), sin_angles.z, cos_angles.z), sin_angles.y, cos_angles.y), sin_angles.x, cos_angles.x); // *do not change* extremely sensitive to order
	Out.right = right * size;
	const vec3 forward = v3_rotate(vec3(0.0f, 0.0f, 1.0f), quaternion); //v3_rotate_pitch(v3_rotate_yaw(v3_rotate_roll(vec3(0.0f, 0.0f, 1.0f), sin_angles.z, cos_angles.z), sin_angles.y, cos_angles.y), sin_angles.x, cos_angles.x); // *do not change* extremely sensitive to order
	Out.forward	= forward * size;
	Out.up = cross(forward, right) * size;
	
#elif !defined(HEIGHT) // STATIC
	Out.up      = vec3(0.0f, size, 0.0f);
	Out.right   = vec3(size, 0.0f, 0.0f);
	Out.forward	= vec3(0.0f, 0.0f, size);

#endif
  }

  const uint hash = floatBitsToUint(inWorldPos.w);
  Out.adjacency = (hash & MASK_ADJACENCY);

#if defined(HEIGHT) // terrain only                                                                             // heightstep column:
  const float heightstep = float(0xffffu & (((hash & MASK_HEIGHTSTEP) >> SHIFT_HEIGHTSTEP))) / 65535.0f;        // 16bit heightstep (top)
  const float minheightstep = float(0xffu & (((hash & MASK_MINHEIGHTSTEP) >> SHIFT_MINHEIGHTSTEP))) / 255.0f;   //  8bit minimum heightstep (bottom)

  const float real_height = max(1.0f, TERRAIN_MAX_HEIGHT * heightstep) * size;
  Out.up      = vec3(0.0f, real_height, 0.0f); // correction - matches up normals computed and sampled so they are aligned on the height axis.
  Out.right   = vec3(size, 0.0f, 0.0f);
  Out.forward = vec3(0.0f, 0.0f, size);
#endif

#if defined(HEIGHT) // terrain voxels only
  Out.world_uvw.xyzw = vec4(inUV.xy, heightstep, -TERRAIN_MAX_HEIGHT * minheightstep * size * 0.75f); //  - world scale uv's, range [-1.0f ... 1.0f] // ** must be in this order for shader variations to compile.
#elif defined(BASIC)
  Out.world_uv.xy = inUV.xy; // only used in BASIC for regular voxels - mousebuffer voxelindex ID
#endif

#ifndef BASIC
  
#ifndef CLEAR

#if defined(DYNAMIC) && defined(TRANS)  // transparent voxels only
  Out.extra.z = float(((hash & MASK_TRANSPARENCY) >> SHIFT_TRANSPARENCY) + 1) * INV_MAX_TRANSPARENCY;  // 0.25f, 0.5f, 0.75f, 1.0f (4 valid levels of transparency)
#endif

#endif // !clear

#if defined(TRANS) // all transparents									 // bugfix: this is finally correct *do not change*
	Out.extra.w = 255.0f / float(b.new_image_layer_count_max);  // inverted & normalized maximum "hit" count for entire image, for weighting transparency // note: new_image_layer_count_max is prevented from being zero
#endif

#endif // !basic

#ifndef CLEAR
#ifdef BASIC
  const float emissive = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
#else // not basic:

#if !defined(HEIGHT) // voxels only:
  Out.material.emission = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
  Out.material.metallic = float((hash & MASK_METALLIC) >> SHIFT_METALLIC);
  Out.material.roughness = float((hash & MASK_ROUGHNESS) >> SHIFT_ROUGHNESS) / 15.0f; // 4 bits, 16 values maximum value n - 1 
  Out.material.ambient = packColorHDR(normalize(b.average_reflection_color.rgb / float(b.average_reflection_count)));
#else // terrain 
  Out.emission = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
  Out.ambient = packColorHDR(normalize(b.average_reflection_color.rgb / float(b.average_reflection_count)));
#endif

#endif
#endif // clear
 

  vec3 worldPos = inWorldPos.xyz;

  // out position //
  gl_Position = vec4(worldPos * VOX_STEP, 1.0f);  // *bugfix - was only 1.0 for all voxel sizes. Scaling must occur as the last transformation, this solves the problem for of RenderGrid() incrementing by only 1 for each voxel, rather than by VOX_STEP which is based off of the VOX_SIZE. VOX_SIZE is a "radius", so it would take 2.0f * VOX_SIZE to translate between any 2 voxel origins, accounting for the size thwy are. VOX_STEP is only applied here to keep the scale as it was before (1.0) before this point in the entire program. This is visual only.  
#ifndef BASIC
    Out.color = packColorHDR(toLinear(unpackColor(abs(inUV.w)))); // conversion from SRGB to Linear happens here for all voxels, faster than doing that on the cpu.
#endif                                                            // 

// applies to ground and regular voxels
#ifdef BASIC
#if !defined(CLEAR) && !defined(TRANS)
	// Opacity Map generation for lighting in volumetric shaders (direct generation saves uploading a seperate 3D texture that is too LARGE to send every frame)
	const float opacity = fma(emissive, 0.5f, 0.5f);		//  > 0 opaque to 0.5, emissive to 1.0
#endif
#endif

#if defined(BASIC) && !defined(TRANS) // only basic past this point

	// derive the normalized index
	// clamping provides opaque borders for voxel models that intersect the bounds of the visible volume. Makes it so they are not see-thru to the insides of the voxel model.
	worldPos = clamp(fma(TransformToIndexScale, worldPos, TransformToIndexBias) * InvToIndex, 0.0f, 1.0f);

  // *** never write to slice 0 (slice 0 contains the always on and filled world ground plane) ***

#if !defined(HEIGHT)
                             
  ivec3 ivoxel = ivec3(floor(worldPos * VolumeDimensions + 0.5f)); // *bugfix - must offset by 0.5f to store to center of voxel properly.
  ivoxel.y = max(1, ivoxel.y); // never write to slice 0 (slice 0 contains the always on and filled world ground plane)

  // no clamp required, voxels are only rendered if in the clamped range set by voxelModel.h render()
  // **clamp required** for black occulder voxels ! provides occlusion in raymarched volumetrics, so the volumetrics don't overlay the black occulder voxels and make it look like you can still see voxel model interiors!
#if defined(CLEAR) // erase
  imageStore(opacityMap, ivoxel.xzy, vec4(0));
#else
  imageStore(opacityMap, ivoxel.xzy, opacity.rrrr);
#endif

#ifdef DYNAMIC // dynamic voxels only
  // hole filling for rotated voxels on each axis(xy, xz, zy) (simplified and revised portion of novel oriented rect algorithm)
#if defined(CLEAR) // erase - if the gpu has an out of bounds store that is +-1 in the xy direction past the beginning or end, it will discard the store silently. Discarding is required for this algorithm to work properly (hole filling)
   imageStore(opacityMap, ivec3(ivoxel.x - 1, ivoxel.y,             ivoxel.z).xzy,     vec4(0));			// this is suprisingly coherent 	
   imageStore(opacityMap, ivec3(ivoxel.x,     max(1, ivoxel.y - 1), ivoxel.z).xzy,     vec4(0));			 
   imageStore(opacityMap, ivec3(ivoxel.x,     ivoxel.y,             ivoxel.z - 1).xzy, vec4(0));			
#else																					            // and works by layer, resulting in bugfix for hole filling.
   imageStore(opacityMap, ivec3(ivoxel.x - 1, ivoxel.y,             ivoxel.z).xzy,     opacity.rrrr);
   imageStore(opacityMap, ivec3(ivoxel.x,     max(1, ivoxel.y - 1), ivoxel.z).xzy,     opacity.rrrr);		// reflections on rotated models are no longer distorted.
   imageStore(opacityMap, ivec3(ivoxel.x,     ivoxel.y,             ivoxel.z - 1).xzy, opacity.rrrr);
#endif
#endif // dynamic 

#else // terrain only
  const ivec3 ivoxel = ivec3(floor(vec3(worldPos.x, 0.0f, worldPos.z) * VolumeDimensions + 0.5f)); // *bugfix - must offset by 0.5f to store to center of voxel properly.
  const int minimum_height = max(0, int(floor(minheightstep * TERRAIN_MAX_HEIGHT * MINIVOXEL_FACTOR * size * 0.75f)) - 1);  // use of > rather than >= below, to always avoid writing to slice 0

  ivec3 iminivoxel;
									                                                                                // never write to slice 0 (slice 0 contains the always on and filled world ground plane)
  [[dependency_infinite]] for( iminivoxel.y = int(floor(heightstep * TERRAIN_MAX_HEIGHT * MINIVOXEL_FACTOR * size)) - 1; iminivoxel.y > minimum_height ; --iminivoxel.y ) { // slice

	[[dependency_infinite]] for( iminivoxel.z = MINIVOXEL_FACTOR - 1; iminivoxel.z >= 0 ; --iminivoxel.z ) {		// depth

	  [[dependency_infinite]] for( iminivoxel.x = MINIVOXEL_FACTOR - 1; iminivoxel.x >= 0 ; --iminivoxel.x ) {		// width - optimal cache order
	    
#if defined(CLEAR) // erase
		imageStore(opacityMap, (ivoxel + iminivoxel).xzy, vec4(0));
#else
		imageStore(opacityMap, (ivoxel + iminivoxel).xzy, opacity.rrrr);
#endif
	  }
	}
  }
#endif // HEIGHT

#endif // is BASIC
}
