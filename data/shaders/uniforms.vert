#version 450
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

#ifndef CLEAR
#ifndef BASIC
#include "common.glsl"
#include "sharedbuffer.glsl"
#endif
#endif

layout(location = 0) in vec4 inWorldPos;
layout(location = 1) in vec4 inUV;
#ifdef DYNAMIC
layout(location = 2) in vec4 inOrientReserved;
#endif

layout (constant_id = 0) const float VOX_SIZE = 0.0f;

#if defined(BASIC)

#if defined(TRANS) && !defined(ROAD) // opacity map not used for roads
layout (binding = 1, r8_snorm) restrict uniform image3D opacityMap;
#elif !defined(ROAD)
layout (binding = 1, r8_snorm) writeonly restrict uniform image3D opacityMap;
#endif

layout (constant_id = 1) const float VolumeDimensions_X = 0.0f;
layout (constant_id = 2) const float VolumeDimensions_Y = 0.0f;
layout (constant_id = 3) const float VolumeDimensions_Z = 0.0f;
#define VolumeDimensions vec3(VolumeDimensions_X, VolumeDimensions_Y, VolumeDimensions_Z)
// corresponding to volume dimensions
const vec3 TransformToIndexScale = vec3(2.0f, -2.0f, 2.0f);
layout (constant_id = 4) const float TransformToIndexBias_X = 0.0f;
layout (constant_id = 5) const float TransformToIndexBias_Y = 0.0f;
layout (constant_id = 6) const float TransformToIndexBias_Z = 0.0f;
#define TransformToIndexBias vec3(TransformToIndexBias_X, TransformToIndexBias_Y, TransformToIndexBias_Z)
layout (constant_id = 7) const float InvToIndex_X = 0.0f;
layout (constant_id = 8) const float InvToIndex_Y = 0.0f;
layout (constant_id = 9) const float InvToIndex_Z = 0.0f;
#define InvToIndex vec3(InvToIndex_X, InvToIndex_Y, InvToIndex_Z)

#elif defined(HEIGHT) || defined(ROAD) // NOT BASIC:

layout (constant_id = 1) const float VolumeDimensions_Y = 0.0f;

#endif // BASIC

#if defined(HEIGHT) // terrain
layout(location = 0) out streamOut
{
	writeonly flat vec3	right, forward; flat vec3 up; // needs r/w
	writeonly flat uint adjacency;
	writeonly flat vec2	world_uv;
#ifndef BASIC
	writeonly flat vec3    ambient;
	writeonly flat float   occlusion;
	writeonly flat float   emission;
#endif
} Out;
#elif defined(ROAD) // road
writeonly layout(location = 0) out streamOut
{
	flat vec3	right, forward, up;
	flat vec4   corners;
	flat vec2	world_uv;
#ifndef BASIC
	flat vec3    ambient;
	flat float   occlusion;
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
	flat vec3    ambient;
	flat vec3	 color;
	flat float   occlusion;
	flat float   emission;
	flat vec4    extra;
	flat float	 passthru;
#endif
} Out;
#endif


#if defined(HEIGHT) || defined(ROAD)
#ifdef BASIC
layout (constant_id = 10) const float INV_MAX_HEIGHT_STEPS = 0.0f;
layout (constant_id = 11) const float HEIGHT_SCALE = 0.0f;
layout (constant_id = 12) const int MINIVOXEL_FACTOR = 1;
#else
layout (constant_id = 2) const float INV_MAX_HEIGHT_STEPS = 0.0f;
layout (constant_id = 3) const float HEIGHT_SCALE = 0.0f;
layout (constant_id = 4) const int MINIVOXEL_FACTOR = 1;
#endif

#define SHIFT_OCCLUSION 5U
#define SHIFT_EMISSION 8U
#define SHIFT_HEIGHTSTEP 12U
const uint MASK_ADJACENCY = 0x1FU;		/*			 0000 0000 0001 1111 */
const uint MASK_OCCLUSION = 0xE0U;		/*           0000 0000 111x xxxx */ 
const uint MASK_EMISSION = 0x100U;		/*           0000 0001 xxxx xxxx */
const uint MASK_HEIGHTSTEP = 0xF000U;	/*			 1111 000x xxxx xxxx */
#if defined(DYNAMIC) && defined(TRANS)  
#define SHIFT_TRANSPARENCY 9U
const uint MASK_TRANSPARENCY = 0x600U;	/*			 xxxx 011x xxxx xxxx */
#endif

#if defined(ROAD)  
#undef MASK_ADJACENCY // not used for roads //
#undef MASK_OCCLUSION // not used for roads //
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

const float ROAD_WIDTH = 11.0f; // should match Iso::ROAD_SEGMENT_WIDTH in iso.h
#endif

#else // not HEIGHT/ROAD

#define SHIFT_OCCLUSION 5U
#define SHIFT_EMISSION 12U
const uint MASK_ADJACENCY =  0x1FU;		/*           0000 0000 0001 1111 */
const uint MASK_OCCLUSION = 0xFE0U;		/*           0000 1111 111x xxxx */ 
const uint MASK_EMISSION = 0x1000U;		/*			 0001 xxxx xxxx xxxx */ 
#if defined(DYNAMIC) && defined(TRANS) 
#define SHIFT_TRANSPARENCY 13U
const uint MASK_TRANSPARENCY = 0x6000U;	/*			 011x xxxx xxxx xxxx */ 
#endif
#endif

#ifndef BASIC

#if defined(DYNAMIC) && defined(TRANS)
const float INV_MAX_TRANSPARENCY = (1.0f / 4.0f);	// 4 levels of transparency (0.25f, 0.5f, 0.75f, 1.0f)
#endif

const uint OCCLUSION_SHADING_CORNER = (1U << 0U),
		   OCCLUSION_SHADING_SIDE_LEFT = (1U << 1U),
		   OCCLUSION_SHADING_SIDE_RIGHT = (1U << 2U);

// excellent ao curve values
const vec4 ao_curve = vec4(0.5f, 0.4233f, 0.415f, 0.4f) * 2.0f;
const float inv_max_occlusion_count = 1.0f / 8.0f; // 0 - 8 inclusive, 8 neighbours checked

// https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
//function vertexAO(side1, side2, corner) {
//  if(side1 && side2) {
//    return 0
//  }
//  return 3 - (side1 + side2 + corner)
//}
														//     *count*  *shading*																
void ao_voxel(const uint occlusion)  // shifted down already :    1111        111
{														//				0b1111111
												   // count mask: 0x78
												   // count shift:   3

	// maximum value 3 in the way this is done, good for index into vec4 (ao_curve)
	const uint truth = uint(
		(OCCLUSION_SHADING_CORNER & occlusion) +
		((OCCLUSION_SHADING_SIDE_LEFT & occlusion) >> 1U) +
		((OCCLUSION_SHADING_SIDE_RIGHT & occlusion) >> 2U)   
	);
	// 1.0f - ((float)OcclusionCount * INV_MAXOCCLUSION_COUNT)
	Out.occlusion = ao_curve[truth] * (1.0f - (float( (0x78U & occlusion) >> 3U ) * inv_max_occlusion_count));
}
#endif

//#ifdef ROAD
//float getRealHeight(in const float heightstep)
//{
//	return fma(heightstep, (INV_MAX_HEIGHT_STEPS * HEIGHT_SCALE * VOX_SIZE * 0.5f), VOX_SIZE * 0.5f);
//}
//#endif // not used more accurate way found

#ifdef DYNAMIC
vec3 v3_rotate_pitch(in const vec3 p)
{
	return( vec3(fma(p.x, inOrientReserved.x, p.y * inOrientReserved.y),
				 fma(p.x, inOrientReserved.y, p.y * inOrientReserved.x),
				 p.z )); //^----- dunno why, but correct is non-negative here otherwise pitch is reversed
}
vec3 v3_rotate_azimuth(in const vec3 p)
{
	return( vec3(fma(p.x, inOrientReserved.z, -p.z * inOrientReserved.w),
				 p.y,
				 fma(p.x, inOrientReserved.w, p.z * inOrientReserved.z) ));
}
#endif

void main() {
  
  vec3 worldPos = inWorldPos.xyz;

  { // orientation output vectors right, forward, up
	const float size = VOX_SIZE;

#ifdef DYNAMIC
	const vec3 right = v3_rotate_azimuth(v3_rotate_pitch(vec3(1.0f, 0.0f, 0.0f)));
	Out.right = right * size;
	const vec3 forward = v3_rotate_azimuth(v3_rotate_pitch(vec3(0.0f, 0.0f, 1.0f)));
	Out.forward	= forward * size;
	Out.up = cross(forward, right) * size;
#else
	Out.right   = vec3(size, 0.0f, 0.0f);
	Out.forward	= vec3(0.0f, 0.0f, size);
	Out.up      = vec3(0.0f, size, 0.0f);
#endif

  }

  const uint hash = floatBitsToUint(inWorldPos.w);
 
#if !(defined(ROAD)) // not road
  Out.adjacency = (hash & MASK_ADJACENCY);
#endif

#if defined(HEIGHT) || defined(ROAD) // terrain/road voxels only
  Out.world_uv.xy = inUV.xy; //  - mousebuffer voxelindex ID
#elif defined(BASIC)
  Out.world_uv.xy = inUV.xy; // only used in BASIC for regular voxels - mousebuffer voxelindex ID
#endif

#if !(defined(HEIGHT) || defined(ROAD) || defined(BASIC)) // voxels only
  Out.passthru = inUV.w; // pass-thru - important as .w component is customizable dependent on fragment shader used (normally a packed color, but for ie.) shockwaves it's uniform distance)
#endif

#ifndef BASIC
  
#ifndef CLEAR

  Out.ambient = b.average_reflection_color.rgb / float(b.average_reflection_count);

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

#if defined(HEIGHT) || defined(ROAD) // terrain/road voxels only

#if defined(HEIGHT) // terrain
  {
	const float heightstep = max(VOX_SIZE, float(((hash & MASK_HEIGHTSTEP) >> SHIFT_HEIGHTSTEP)));  // bugfix: heightstep of 0 was flat and causing strange rendering issues, now has a minimum heightstep of VOX_SIZE (fractional)
	Out.up.y *= heightstep * INV_MAX_HEIGHT_STEPS * HEIGHT_SCALE;
  }
	const float voxel_height = Out.up.y * float(MINIVOXEL_FACTOR);  // range [0.0f ... VolumeDimensions_Y]


#else // road

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
	worldPos.y = (y_max - (y_max - y_min) * 0.5f) - delta_height.y * 0.5f;
  }
#endif

#else // ########## voxels only

  {
#ifndef BASIC
	Out.color = toLinear(unpackColor(inUV.w)); // conversion from SRGB to Linear happens here for all voxels, faster than doing that on the cpu.
#endif
  }

#endif  // else, voxels

#if !defined(BASIC)

	// ambient occlusion calculated once per voxel, permutations used for all quads
	ao_voxel((hash & MASK_OCCLUSION) >> SHIFT_OCCLUSION); // optimization, moved from gs to vs
	
#endif

#ifndef CLEAR
#ifdef BASIC
  const float emissive = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
#else
  Out.emission = float((hash & MASK_EMISSION) >> SHIFT_EMISSION);
#endif
#endif // clear

	// out position //
	gl_Position = vec4(worldPos, 1.0f);

#if defined(BASIC) && !defined(ROAD) // only basic past this point

	// derive the normalized index
	worldPos = fma(TransformToIndexScale, worldPos, TransformToIndexBias) * InvToIndex;

// Opacity Map generation for lighting in volumetric shaders (direct generation saves uploading a seperate 3D texture that is too LARGE to send every frame)
#if !defined(CLEAR) 
	
#if defined(TRANS) // transparent
	float opacity		= fma(emissive, -0.5f, -0.5f);		//  < 0 transparent to -0.5, emissive to -1.0
#else // opaque 
	const float opacity = fma(emissive, 0.5f, 0.5f);		//  > 0 opaque to 0.5, emissive to 1.0
#endif
#endif // if !clear

#if !defined(HEIGHT)
const ivec3 ivoxel = ivec3(floor(worldPos * VolumeDimensions).xzy);

#if defined(TRANS)
const float existing = imageLoad(opacityMap, ivoxel).r;  // already occupied with an opaque or transparent block

if (existing <= 0.0f) 	
	opacity = min(opacity, existing);  // min takes the most emissive transparent voxel
else // already filled with opaque block
	return;
#endif

  // no clamp required, voxels are only rendered if in the clamped range set by voxelModel.h render()
#if defined(CLEAR) // erase
  imageStore(opacityMap, ivoxel, vec4(0));
#else
  imageStore(opacityMap, ivoxel, opacity.rrrr);
#endif

  // hole filling for rotated (xz) voxels (simplified and revised portion of novel oriented rect algorithm)
#if defined(CLEAR) // erase
   imageStore(opacityMap, ivec3(ivoxel.x, ivoxel.y - 1, ivoxel.z), vec4(0));			// this is suprisingly coherent 
#else																					// and works by layer, resulting in bugfix for hole filling.
   imageStore(opacityMap, ivec3(ivoxel.x, ivoxel.y - 1, ivoxel.z), opacity.rrrr);		// reflections on rotated models are no longer distorted.
#endif

#else // terrain only
  const ivec3 ivoxel = ivec3(floor(vec3(worldPos.x, 0.0f, worldPos.z) * VolumeDimensions));
  const int ivoxel_height = int(floor(voxel_height));

  ivec3 iminivoxel;
  
  [[dependency_infinite]] for( iminivoxel.y = ivoxel_height; iminivoxel.y >= 0 ; --iminivoxel.y ) {				// slice

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
