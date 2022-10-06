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

#if defined(BASIC)

#if !defined(ROAD) // opacity map not used for roads
layout (binding = 1, r8) writeonly restrict uniform image3D opacityMap;
#endif

layout (constant_id = 1) const float VolumeDimensions = 0.0f;

// corresponding to volume dimensions
const vec3 TransformToIndexScale = vec3(2.0f, -2.0f, 2.0f);
layout (constant_id = 2) const float TransformToIndexBias_X = 0.0f;
layout (constant_id = 3) const float TransformToIndexBias_Y = 0.0f;
layout (constant_id = 4) const float TransformToIndexBias_Z = 0.0f;
#define TransformToIndexBias vec3(TransformToIndexBias_X, TransformToIndexBias_Y, TransformToIndexBias_Z)
layout (constant_id = 5) const float InvToIndex_X = 0.0f;
layout (constant_id = 6) const float InvToIndex_Y = 0.0f;
layout (constant_id = 7) const float InvToIndex_Z = 0.0f;
#define InvToIndex vec3(InvToIndex_X, InvToIndex_Y, InvToIndex_Z)

#elif defined(HEIGHT) || defined(ROAD) // NOT BASIC:

layout (constant_id = 1) const float VolumeDimensions = 0.0f;

#endif // BASIC

#if defined(HEIGHT) // terrain
writeonly layout(location = 0) out streamOut
{
	flat vec3	right, forward, up;
	flat uint   adjacency;
	flat vec2	world_uv;
#ifndef BASIC
	flat float   ambient;
	flat float   color;
	flat float   emission;
#endif
} Out;
#elif defined(ROAD) // road
writeonly layout(location = 0) out streamOut
{
	flat vec3	right, forward, up;
	flat vec4   corners;
	flat vec2	world_uv;
#ifndef BASIC
	flat float   ambient;
	flat float   emission;
	flat vec4    extra;
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


#if defined(HEIGHT) || defined(ROAD)
#ifdef BASIC
layout (constant_id = 8) const float INV_MAX_HEIGHT_STEPS = 0.0f;
layout (constant_id = 9) const float HEIGHT_SCALE = 0.0f;
layout (constant_id = 10) const int MINIVOXEL_FACTOR = 1;
#else
layout (constant_id = 2) const float INV_MAX_HEIGHT_STEPS = 0.0f;
layout (constant_id = 3) const float HEIGHT_SCALE = 0.0f;
layout (constant_id = 4) const int MINIVOXEL_FACTOR = 1;
#endif

#define SHIFT_EMISSION 8U
#define SHIFT_HEIGHTSTEP 12U
const uint MASK_ADJACENCY = 0x3FU;		/*			 0000 0000 0011 1111 */
const uint MASK_RESERVED = 0xC0U;		/*           0000 0000 RRxx xxxx */ // free to use
const uint MASK_EMISSION = 0x100U;		/*           0000 0001 xxxx xxxx */
const uint MASK_HEIGHTSTEP = 0xF000U;	/*			 1111 000x xxxx xxxx */
#if defined(DYNAMIC) && defined(TRANS)  
#define SHIFT_TRANSPARENCY 9U
const uint MASK_TRANSPARENCY = 0x600U;	/*			 xxxx 011x xxxx xxxx */
#endif

#if defined(ROAD)  
#ifdef BASIC
layout (constant_id = 11) const float ROAD_WIDTH = 0.0f;
#else
layout (constant_id = 5) const float ROAD_WIDTH = 0.0f;
#endif

#undef MASK_ADJACENCY // not used for roads //
#undef MASK_EMISSION
#define SHIFT_ROAD_HEIGHTSTEP_BEGIN 9U
#define SHIFT_ROAD_HEIGHTSTEP_END 13U
#define SHIFT_ROAD_DIRECTION 17U
#define SHIFT_ROAD_TILE 19U
const uint MASK_ROAD_HEIGHTSTEP_BEGIN = 0x1E00U;		/*                0001 111x xxxx xxxx */
const uint MASK_ROAD_HEIGHTSTEP_END = 0x1E000U;			/*           0001 111x xxxx xxxx xxxx */
const uint MASK_ROAD_DIRECTION = 0x60000U;				/*           011x xxxx xxxx xxxx xxxx */
const uint MASK_ROAD_TILE = 0x1F80000U;					/* 0001 1111 1xxx xxxx xxxx xxxx xxxx */

const uint  NORTH = 0U,
			SOUTH = 1U,
			EAST = 2U,
			WEST = 3U;

#endif

#else // not HEIGHT/ROAD

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

// trick, the first 3 components x,y,z are sent to vertex shader where the quaternion is then decoded. see uniforms.vert - decode_quaternion() [bandwidth optimization]
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
  
  { // orientation output vectors right, forward, up
	const float size = VOX_SIZE;

#ifdef DYNAMIC
	// only DYNAMIC
	const vec4 quaternion = decode_quaternion(inUV.xyz, sign(inUV.w));

	const vec3 right = v3_rotate(vec3(1.0f, 0.0f, 0.0f), quaternion); //v3_rotate_pitch(v3_rotate_yaw(v3_rotate_roll(vec3(1.0f, 0.0f, 0.0f), sin_angles.z, cos_angles.z), sin_angles.y, cos_angles.y), sin_angles.x, cos_angles.x); // *do not change* extremely sensitive to order
	Out.right = right * size;
	const vec3 forward = v3_rotate(vec3(0.0f, 0.0f, 1.0f), quaternion); //v3_rotate_pitch(v3_rotate_yaw(v3_rotate_roll(vec3(0.0f, 0.0f, 1.0f), sin_angles.z, cos_angles.z), sin_angles.y, cos_angles.y), sin_angles.x, cos_angles.x); // *do not change* extremely sensitive to order
	Out.forward	= forward * size;
	Out.up = cross(forward, right) * size;
#elif !defined(HEIGHT) // not terrain (done below)
	Out.right   = vec3(size, 0.0f, 0.0f);
	Out.forward	= vec3(0.0f, 0.0f, size);
	Out.up      = vec3(0.0f, size, 0.0f);
#endif
  }

  const uint hash = floatBitsToUint(inWorldPos.w);

#if defined(HEIGHT) // terrain only

	Out.right   = vec3(VOX_SIZE * 0.5f, 0.0f, 0.0f);
	Out.forward	= vec3(0.0f, 0.0f, VOX_SIZE * 0.5f); 

  const float heightstep = float(((hash & MASK_HEIGHTSTEP) >> SHIFT_HEIGHTSTEP));  // bugfix: heightstep of 0 was flat and causing strange rendering issues, now has a minimum heightstep of VOX_SIZE (fractional)
  const float real_height = max(VOX_SIZE * 0.5f, heightstep * INV_MAX_HEIGHT_STEPS * HEIGHT_SCALE * VOX_SIZE);
  Out.up      = vec3(0.0f, real_height, 0.0f); // correction - matches up normals computed and sampled so they are aligned on the height axis.
  const float voxel_height = real_height * 0.5f; // range [0.0f ... VolumeDimensions_Y]  *do not change* *bugfix - matches up volumetric computed normals with full resolution normal map.
#endif

#if !(defined(ROAD)) // not road
  Out.adjacency = (hash & MASK_ADJACENCY);
#endif

#if defined(HEIGHT) || defined(ROAD) // terrain/road voxels only
  Out.world_uv.xy = inUV.xy; //  - mousebuffer voxelindex ID
#elif defined(BASIC)
  Out.world_uv.xy = inUV.xy; // only used in BASIC for regular voxels - mousebuffer voxelindex ID
#endif

#ifndef BASIC
  
#ifndef CLEAR

#if defined(DYNAMIC) && defined(TRANS)  // transparent voxels only
#if defined(ROAD)
  Out.extra.z = 0.25f;	// transparent road selection only (does not require configurable level of transparency)
#else
  Out.extra.z = float(((hash & MASK_TRANSPARENCY) >> SHIFT_TRANSPARENCY) + 1) * INV_MAX_TRANSPARENCY;  // 0.25f, 0.5f, 0.75f, 1.0f (4 valid levels of transparency)
#endif
#endif

#endif // !clear

#if defined(TRANS) // all transparents									 // bugfix: this is finally correct *do not change*
	Out.extra.w = 255.0f / float(b.new_image_layer_count_max);  // inverted & normalized maximum "hit" count for entire image, for weighting transparency // note: new_image_layer_count_max is prevented from being zero
#endif

#endif // !basic

   vec3 worldPos = inWorldPos.xyz;

#if defined(ROAD) // road voxels only

#ifndef BASIC
	Out.extra.x = float((hash & MASK_ROAD_TILE) >> SHIFT_ROAD_TILE);
#endif
  {
	const float height_begin = float((hash & MASK_ROAD_HEIGHTSTEP_BEGIN) >> SHIFT_ROAD_HEIGHTSTEP_BEGIN) * VOX_SIZE * INV_MAX_HEIGHT_STEPS * HEIGHT_SCALE;
	const float height_end = float((hash & MASK_ROAD_HEIGHTSTEP_END) >> SHIFT_ROAD_HEIGHTSTEP_END) * VOX_SIZE * INV_MAX_HEIGHT_STEPS * HEIGHT_SCALE;

	const vec2 delta_height = vec2(0, -(height_end - height_begin));
	
	const uint road_direction = (hash & MASK_ROAD_DIRECTION) >> SHIFT_ROAD_DIRECTION;

	[[flatten]] switch(road_direction)
	{
		case NORTH:	// +z
#ifndef BASIC
			Out.extra.y = 0.0f;
#endif
			Out.corners = delta_height.xxyy; // correct
			Out.right *= ROAD_WIDTH;
		break;
		case SOUTH: // -z
#ifndef BASIC
			Out.extra.y = 0.0f;
#endif
			Out.corners = delta_height.yyxx; // correct
			Out.right *= ROAD_WIDTH;
		break;
		case EAST:	// +x
#ifndef BASIC
			Out.extra.y = 1.0f;			  // rotate
#endif
			Out.corners = delta_height.yxyx; // correct
			Out.forward *= ROAD_WIDTH;
		break;
		case WEST:	// -x
#ifndef BASIC
			Out.extra.y = 1.0f;			  // rotate
#endif
			Out.corners = delta_height.xyxy; // correct
			Out.forward *= ROAD_WIDTH;
		break;
	}

	const float road_height = -(height_begin);
	const vec2 corner_height = road_height + delta_height.xy;
	const float y_min = min(corner_height.x, corner_height.y);
	const float y_max = max(corner_height.x, corner_height.y);
	
	// perfectly centered, delta_height removes staircase
	worldPos.y = 0.5f * (y_max + (y_min - delta_height.y)); // (herbie optimized) same as: (y_max - (y_max - y_min) * 0.5f) - delta_height.y * 0.5f
  }
#endif

  // out position //
  gl_Position = vec4(worldPos, 1.0f);

#ifndef CLEAR
#ifdef BASIC
  const float emissive = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
#else // not basic:

#if !defined(HEIGHT) && !defined(ROAD) // voxels only:
  Out.material.emission = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
  Out.material.metallic = float((hash & MASK_METALLIC) >> SHIFT_METALLIC);
  Out.material.roughness = float((hash & MASK_ROUGHNESS) >> SHIFT_ROUGHNESS) / 15.0f; // 4 bits, 16 values maximum value n - 1 
  Out.material.ambient = packColor(b.average_reflection_color.rgb / float(b.average_reflection_count >> 2u));
#else // terrain or road:
  Out.emission = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
  Out.ambient = packColor(b.average_reflection_color.rgb / float(b.average_reflection_count >> 2u));
#endif

#endif
#endif // clear

#ifndef ROAD // applies to ground and regular voxels only
#ifdef BASIC
#if !defined(CLEAR) && !defined(TRANS)
	// Opacity Map generation for lighting in volumetric shaders (direct generation saves uploading a seperate 3D texture that is too LARGE to send every frame)
	const float opacity = fma(emissive, 0.5f, 0.5f);		//  > 0 opaque to 0.5, emissive to 1.0
#endif
#else
	Out.color = packColor(toLinear(unpackColor(abs(inUV.w)))); // conversion from SRGB to Linear happens here for all voxels, faster than doing that on the cpu.
#endif
#endif

#if defined(BASIC) && !defined(ROAD) && !defined(TRANS) // only basic past this point

	// derive the normalized index
	worldPos = fma(TransformToIndexScale, worldPos, TransformToIndexBias) * InvToIndex;

#if !defined(HEIGHT)
  const ivec3 ivoxel = ivec3(floor(worldPos * VolumeDimensions).xzy);

  // no clamp required, voxels are only rendered if in the clamped range set by voxelModel.h render()
#if defined(CLEAR) // erase
  imageStore(opacityMap, ivoxel, vec4(0));
#else
  imageStore(opacityMap, ivoxel, opacity.rrrr);
#endif

#ifdef DYNAMIC // dynamic voxels only
  // hole filling for rotated (xz) voxels (simplified and revised portion of novel oriented rect algorithm)
#if defined(CLEAR) // erase
   imageStore(opacityMap, ivec3(ivoxel.x, ivoxel.y - 1, ivoxel.z), vec4(0));			// this is suprisingly coherent 
#else																					// and works by layer, resulting in bugfix for hole filling.
   imageStore(opacityMap, ivec3(ivoxel.x, ivoxel.y - 1, ivoxel.z), opacity.rrrr);		// reflections on rotated models are no longer distorted.
#endif
#endif // dynamic 

#else // terrain only
  const ivec3 ivoxel = ivec3(floor(vec3(worldPos.x, 0.0f, worldPos.z) * VolumeDimensions));
  const int ivoxel_height = int(floor(voxel_height)) + 1; // *bugfix: +1 yields correct height and the minimum.

  ivec3 iminivoxel;
									
  [[dependency_infinite]] for( iminivoxel.y = ivoxel_height - 1; iminivoxel.y >= 0 ; --iminivoxel.y ) {				// slice

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

#endif // is BASIC & !ROAD
}
