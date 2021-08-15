/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#pragma once
#ifndef ISOVOXEL_H
#define ISOVOXEL_H
#include <Math/point2D_t.h>
#include <Utility/unroll.h>
#include "Declarations.h"

namespace Iso
{

	// cONSTANTS/*****************
	static constexpr size_t const
		WORLD_GRID_SIZE_BITS = 11;				// 2n = SIZE, always power of 2 for grid.   maximum size is 2^16 = 65536x65536 which is friggen huge - limited only by resolution of 16bit/component mouse voxelIndex color attachment. 
	static constexpr uint32_t const				//                                          recommend using 2^13 = 8192x8192 or less, map is still extremely large
		WORLD_GRID_SIZE = (1U << WORLD_GRID_SIZE_BITS),
		WORLD_GRID_HALFSIZE = (WORLD_GRID_SIZE >> 1U);

	static constexpr int32_t const
		MIN_VOXEL_COORD = -((int32_t)WORLD_GRID_HALFSIZE),
		MAX_VOXEL_COORD = (WORLD_GRID_HALFSIZE);

	static constexpr float const
		MIN_VOXEL_FCOORD = (float)MIN_VOXEL_COORD,
		MAX_VOXEL_FCOORD = (float)MAX_VOXEL_COORD;

	// KEY NOTE ON SPACE BETWEEN VOXELS AND SCALE //
	/*
		On the main grid the step, or space between voxels is actually 1.0f

		The voxel size is half that, actually 0.48f so that nice lines show up defining the grid

		therefore,

		minivoxels, where 8 x 8 minivoxels fit into a single voxel on the main grid

		** the step between minivoxels is 1.0f / 8.0f (1/8th) of the space between normal grid voxels
		** the size of a minivoxel is (1.0f/8.0f) * VOX_SIZE (1/8th of a normal grid voxel size)

		// key is to distinguish voxel spacing (distance between voxels) - and the size of the voxel being slightly less than expected
		// for nice seperation lines !
	*/
	// uniforms.vert   ,  isovoxel.h   ,    globals.h
	static constexpr uint32_t const
		VOXELS_GRID_SLOT_XZ_BITS_2N = 1,						 // modify to get desired voxel size, uniforms.vert must equal
		VOXELS_GRID_SLOT_XZ = (1 << VOXELS_GRID_SLOT_XZ_BITS_2N);// max num voxels in x or z direction for a single slot / square on "minigrid" : "grid"
			// 2^3 bits = 8
	static constexpr float const
		VOXELS_GRID_SLOT_XZ_FP = ((float)(VOXELS_GRID_SLOT_XZ));

#define MINIVOXEL_FACTOR Iso::VOXELS_GRID_SLOT_XZ
#define MINIVOXEL_FACTORF Iso::VOXELS_GRID_SLOT_XZ_FP
#define MINIVOXEL_FACTOR_BITS Iso::VOXELS_GRID_SLOT_XZ_BITS_2N

	static constexpr uint32_t const
		SCREEN_VOXELS_XZ = 256,
		SCREEN_VOXELS_X = SCREEN_VOXELS_XZ,		// must be be even numbers, maximum zoom out affects this value so screen grid is fully captured in its bounds
		SCREEN_VOXELS_Y = 256,					// this is "up", needs to be measured
		SCREEN_VOXELS_Z = SCREEN_VOXELS_XZ;		// x and z should be equal and divisible by 8

	static constexpr float const
		HEIGHT_SCALE = 16.0f,		// this value and shader value for height need to always match (uniforms.vert)
		VOX_SIZE = 0.5f,			// this value and shader value for normal vox size need to always match
		VOX_STEP = VOX_SIZE * 2.0f,
		VOX_RADIUS = 866.025403784438646764e-3f, // calculated from usage of 0.5f

		MINI_VOX_SIZE = VOX_SIZE / MINIVOXEL_FACTORF,	// this value and shader value for mini-vox size need to always match
		MINI_VOX_STEP = MINI_VOX_SIZE * 2.0f,
		MINI_VOX_RADIUS = VOX_RADIUS / MINIVOXEL_FACTORF, // calculated "" ""  (currently 4.0f)

		WORLD_GRID_FSIZE = (float)WORLD_GRID_SIZE,
		WORLD_GRID_FHALFSIZE = (float)WORLD_GRID_HALFSIZE,
		INVERSE_WORLD_GRID_FSIZE = 1.0f / WORLD_GRID_FSIZE,
		INVERSE_MAX_VOXEL_COORD = 1.0f / (MAX_VOXEL_FCOORD);		// -- good for normalization in range -1.0f...1.0f

	XMGLOBALCONST inline XMVECTORF32 const WORLD_EXTENTS{ MAX_VOXEL_FCOORD, (float)SCREEN_VOXELS_Y, MAX_VOXEL_FCOORD }; // AABB Extents of world, note that Y Axis starts at zero, instead of -WORLD_EXTENT.y

	static constexpr int32_t const
		VIRTUAL_SCREEN_VOXELS = SCREEN_VOXELS_XZ * MINIVOXEL_FACTOR;

	static constexpr int32_t const
		GRID_VOXELS_START_XZ_OFFSET = (VIRTUAL_SCREEN_VOXELS / MINIVOXEL_FACTOR) >> 1;		// matches the origin of world and volumetric unit cube

	// perfect alignment with 3d texture and volume rendering, finally
	XMGLOBALCONST inline point2D_t const GRID_OFFSET(-GRID_VOXELS_START_XZ_OFFSET, -GRID_VOXELS_START_XZ_OFFSET);
	XMGLOBALCONST inline XMVECTORF32 const
		GRID_OFFSET_X_Z{ -GRID_VOXELS_START_XZ_OFFSET, 0.0f, -GRID_VOXELS_START_XZ_OFFSET };

	// ####### Desc bits:

	// 0000 0001 ### TYPE ###
	static constexpr uint8_t const MASK_TYPE_BIT = 0b00000001;
	static constexpr uint8_t const
		TYPE_GROUND = 0,
		TYPE_EXTENDED = 1;

	// 0000 0010 ### NODE ###
	static constexpr uint8_t const MASK_NODE_BIT = 0b00000010;
	static constexpr uint8_t const
		TYPE_EDGE = 0,
		TYPE_NODE = 1;

	// 0011 1100 ### HEIGHT ###
	static constexpr uint8_t const MASK_HEIGHT_BITS = 0b00111100;
	static constexpr uint8_t const DESC_HEIGHT_STEP_0 = 0;	// 16 Values (0-15 4bits)
	static constexpr uint8_t const MAX_HEIGHT_STEP = 0x0F;	// 16 Values (0-15 4bits)
	static constexpr uint8_t const NUM_HEIGHT_STEPS = MAX_HEIGHT_STEP + 1;	// 16 Values (0-15 4bits)	
	static constexpr float const   INV_MAX_HEIGHT_STEP = 1.0f / (float)MAX_HEIGHT_STEP;

	// 1100 0000 ### SPECIAL ###
	static constexpr uint8_t const MASK_PENDING_BIT = 0b01000000;			// for temporary status indication (constructing/not commited to grid)
	static constexpr uint8_t const MASK_DISCOVERED_BIT = 0b10000000;		// for graph searching only

	// ######### MaterialDesc bits:

	// ### TYPE_ALL_VOXELS ###
	static constexpr uint8_t const MASK_OCCLUSION_BITS = 0b00000111;
	static constexpr uint8_t const MASK_OCCLUSION_RESERVED_BITS = 0b00011000;
	static constexpr uint8_t const
		OCCLUSION_SHADING_NONE = 0,
		OCCLUSION_SHADING_CORNER = (1 << 0),
		OCCLUSION_SHADING_SIDE_LEFT = (1 << 1),
		OCCLUSION_SHADING_SIDE_RIGHT = (1 << 2);
	static constexpr uint8_t const
		OCCLUSION_SHADING_FULL = OCCLUSION_SHADING_CORNER | OCCLUSION_SHADING_SIDE_LEFT | OCCLUSION_SHADING_SIDE_RIGHT;

	static constexpr uint8_t const MASK_EMISSION_BITS = 0b00100000;
	static constexpr uint8_t const
		EMISSION_SHADING_NONE = 0,
		EMISSION_SHADING = 1;

	// ** Only valid if extended type is type in .Desc ** //
	static constexpr uint8_t const MASK_EXTENDED_TYPE_BITS = 0b11000000;
	static constexpr uint8_t const
		EXTENDED_TYPE_ROAD = 0,
		EXTENDED_TYPE_WATER = 1,
		EXTENDED_TYPE_RESERVED0 = 2,
		EXTENDED_TYPE_RESERVED1 = 3;

	// ######### Hash bits:

	// road hash bits //
	static constexpr uint32_t const ROAD_SEGMENT_WIDTH = 11; // shader value should also match this (uniforms.vert) - must be even number
	static constexpr int32_t const  SEGMENT_SIDE_WIDTH(ROAD_SEGMENT_WIDTH >> 1), 
		                            SEGMENT_SNAP_WIDTH(ROAD_SEGMENT_WIDTH + 1);
	static constexpr uint32_t const MASK_ROAD_HEIGHTSTEP_BEGIN = 0x0F;		//	                1111
	static constexpr uint32_t const MASK_ROAD_HEIGHTSTEP_END = 0xF0;		//             1111 xxxx
	static constexpr uint32_t const MASK_ROAD_DIRECTION = 0x300;			//        0011 xxxx xxxx
	static constexpr uint32_t const MASK_ROAD_TILE = 0xFC00;				//   1111 11xx xxxx xxxx
	static constexpr uint32_t const MASK_ROAD_NODE_TYPE = 0xF0000;	 //     1111 xxxx xxxx xxxx xxxx
	static constexpr uint32_t const MASK_ROAD_NODE_CENTER = 0x100000;//0001 xxxx xxxx xxxx xxxx xxxx   

	// road specific //
	namespace ROAD_DIRECTION { // 2 bits - 0 = 0b00 = N,   1 = 0b01 = S,   2 = 0b10 = E,   3 = 0b11 = W
		static constexpr uint32_t const
			N = 0,
			S = 1,
			E = 2,
			W = 3;
	} // end ns
	namespace ROAD_TILE { // 6 bits
		static constexpr uint32_t const
			STRAIGHT = 0,
			XING = 1,
			FLAT = 2,
			SELECT = 3,
			CURVED_0 = 4,			// TL
			CURVED_1 = 5,
			CURVED_2 = 6,
			CURVED_3 = 7,
			CURVED_4 = 8,
			CURVED_5 = 9,
			CURVED_6 = 10,
			CURVED_7 = 11,
			CURVED_8 = 12,
			CURVED_9 = 13,
			CURVED_10 = 14;
		// TR
		// BR
		// BL
	} // end ns
	namespace ROAD_NODE_TYPE {
		static constexpr uint32_t const
			XING_ALL = 0,
			XING_RTL = 1,
			XING_TLB = 2,
			XING_LBR = 3,
			XING_BRT = 4,
			XING = 4, // for testing <= XING = any XING_XXX type

			CORNER_TL = 5,
			CORNER_BL = 6,
			CORNER_BR = 7,
			CORNER_TR = 8,
			CORNER = 8, // for testing <= CORNER = any CORNER_XX type

			INVALID = 9;

	} // end ns

	// "Owner" Voxel
	static constexpr uint8_t const
		GROUND_HASH = 0,
		STATIC_HASH = (1 << 0),
		DYNAMIC_HASH = (1 << 1);
	static constexpr uint8_t const
		HASH_COUNT = 8;

	typedef struct sVoxel
	{
		uint8_t Desc;								// Type of Voxel + Attributes
		uint8_t MaterialDesc;						// Deterministic SubType
		uint8_t Owner;								// Owner Indices
		uint32_t Hash[HASH_COUNT];					// Index 0 = Ground / Extended Hash
													// Index 1 = Static Model
													// Index 2 to 7 = Up to 6 Dynamic Model(s)
		sVoxel()
			: Desc{}, MaterialDesc{}, Owner{}, Hash{}
		{}
		sVoxel(sVoxel const&) = default;
		sVoxel& operator=(sVoxel const&) = default;  // should not be used as occlusion data will be clobbered only safe in certain init or generation scenarios
		sVoxel(sVoxel&&) noexcept = default;
		sVoxel& operator=(sVoxel&&) noexcept = default;
	} Voxel;

	// Special case leveraging more of the beginning bits
	STATIC_INLINE_PURE bool const isGroundOnly(Voxel const& oVoxel) { return(TYPE_GROUND == (MASK_TYPE_BIT & oVoxel.Desc)); }
	STATIC_INLINE_PURE bool const isExtended(Voxel const& oVoxel) { return(TYPE_EXTENDED == (MASK_TYPE_BIT & oVoxel.Desc)); }

	// for indicating voxel is temporary / constructing
	STATIC_INLINE_PURE bool const isPending(Voxel const& oVoxel) { return(MASK_PENDING_BIT & oVoxel.Desc); }
	STATIC_INLINE void clearPending(Voxel& oVoxel) {	
		// Clear bit
		oVoxel.Desc &= (~MASK_PENDING_BIT);
	}
	STATIC_INLINE void setPending(Voxel& oVoxel) {	
		// Set bit
		oVoxel.Desc |= MASK_PENDING_BIT;
	}

	// only for search algorithms
	STATIC_INLINE_PURE bool const isDiscovered(Voxel const& oVoxel) { return(MASK_DISCOVERED_BIT & oVoxel.Desc); }
	STATIC_INLINE void clearDiscovered(Voxel& oVoxel) {	
		// Clear bit
		oVoxel.Desc &= (~MASK_DISCOVERED_BIT);
	}
	STATIC_INLINE void setDiscovered(Voxel& oVoxel) {	
		// Set bit
		oVoxel.Desc |= MASK_DISCOVERED_BIT;
	}

	// hasXXX hash - whether voxel is owner or not
	STATIC_INLINE_PURE bool const hasStatic(Voxel const& oVoxel) { return(0 != oVoxel.Hash[STATIC_HASH]); }
	STATIC_INLINE_PURE bool const hasDynamic(Voxel const& oVoxel) {
		for (uint32_t i = DYNAMIC_HASH; i < HASH_COUNT; ++i) {
			if (0 != oVoxel.Hash[i])
				return(true);
		}
		return(false);
	}

	STATIC_INLINE void clearExtendedType(Voxel& oVoxel) {
		// Clear type bits
		oVoxel.MaterialDesc &= (~MASK_EXTENDED_TYPE_BITS);
	}
	STATIC_INLINE_PURE uint8_t const getExtendedType(Voxel const& oVoxel) { return(MASK_EXTENDED_TYPE_BITS & oVoxel.MaterialDesc); }
	STATIC_INLINE void setExtendedType(Voxel& oVoxel, uint8_t const Type)
	{
		uint32_t const CurType = (MASK_EXTENDED_TYPE_BITS & oVoxel.MaterialDesc);

		// if different only...
		if (Type != CurType) {
			// Clear type bits
			oVoxel.MaterialDesc &= (~MASK_EXTENDED_TYPE_BITS);
			// Set new type bits safetly
			oVoxel.MaterialDesc |= (MASK_EXTENDED_TYPE_BITS & (Type));
		}
	}

	STATIC_INLINE_PURE uint8_t const getType(Voxel const& oVoxel) { return(MASK_TYPE_BIT & oVoxel.Desc); }
	STATIC_INLINE void setType(Voxel& oVoxel, uint8_t const Type)
	{
		uint32_t const CurType = (MASK_TYPE_BIT & oVoxel.Desc);

		// if different only...
		if (Type != CurType) {
			// Clear type bits
			oVoxel.Desc &= (~MASK_TYPE_BIT);
			// Set new type bits safetly
			oVoxel.Desc |= (MASK_TYPE_BIT & (Type));
		}
	}

	// *note: index *can NOT* be combination of hashes STATIC_HASH|DYNAMIC_HASH
	STATIC_INLINE_PURE bool const isOwnerAny(Voxel const& oVoxel, uint8_t const start = 0) {
		return(oVoxel.Owner >> start);
	}
	STATIC_INLINE_PURE bool const isOwner(Voxel const& oVoxel, uint8_t const index) {
		return(1 & (oVoxel.Owner >> index));
	}
	STATIC_INLINE void clearAsOwner(Voxel& oVoxel, uint8_t const index)
	{
		// Clear root bits corresponding to hashType selected
		oVoxel.Owner &= ~(1 << index);
		// *** not changing hash to zero, clearAsOwner is used in some cases to temporarily disable Owner status
	}
	STATIC_INLINE void setAsOwner(Voxel& oVoxel, uint8_t const index)
	{
		// Set new root bit safetly
		oVoxel.Owner |= (1 << index);
	}

	template<bool const Dynamic>
	STATIC_INLINE_PURE bool const isHashEmpty(Voxel const& oVoxel) {  // this helper should *not* be used in conjunction with getNextAvailableHashIndex(), as its reduntantly testing the same thing 
																	  // however can be used isolated from getNextAvailableHashIndex()
		if constexpr (Dynamic) {
			for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
				if (0 != oVoxel.Hash[i])
					return(false);
			}
		}
		else {
			if (0 != oVoxel.Hash[Iso::STATIC_HASH])
				return(false);
		}

		return(true); // empty!
	}

	template<bool const Dynamic>
	STATIC_INLINE_PURE uint32_t const getNextAvailableHashIndex(Voxel const& oVoxel) {

		if constexpr (Dynamic) {
			for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
				if (0 == oVoxel.Hash[i])
					return(i);
			}
		}
		else {
			if (0 == oVoxel.Hash[Iso::STATIC_HASH])
				return(Iso::STATIC_HASH);
		}

		return(Iso::GROUND_HASH); // 0 == not available (0 corresponds to invalid index for Iso::STATIC_HASH / Iso::DYNAMIC_HASH
	}

	STATIC_INLINE_PURE uint32_t const getHash(Voxel const& oVoxel, uint32_t const index) { return(oVoxel.Hash[index]); }
	STATIC_INLINE void setHash(Voxel& oVoxel, uint32_t const index, uint32_t const hash) { oVoxel.Hash[index] = hash; }
	STATIC_INLINE void resetHash(Voxel& oVoxel, uint32_t const index) {
		oVoxel.Hash[index] = 0;
		clearAsOwner(oVoxel, index); // *** also remove owner status if it exists, applies to any/all cases
	}

	STATIC_INLINE void clearAsNode(Voxel& oVoxel)
	{
		// Clear node bit
		oVoxel.Desc &= (~MASK_NODE_BIT);
	}
	STATIC_INLINE void setAsNode(Voxel& oVoxel)
	{
		// Set new node bit safetly
		oVoxel.Desc |= MASK_NODE_BIT;
	}

	// This completely resets back to ground
	STATIC_INLINE void resetAsGroundOnly(Voxel& oVoxel) {

		for (uint32_t i = 0; i < HASH_COUNT; ++i) {
			resetHash(oVoxel, i); // also clears owner state
		}						  // also resets ground hash which isn't used if ground only, only for roads / modifications to ground
		clearAsNode(oVoxel);
	
		setType(oVoxel, Iso::TYPE_GROUND);   // default change type to ground
	}

	STATIC_INLINE_PURE uint32_t const getHeightStep(Voxel const& oVoxel)
	{
		// Mask off height bits
		// Shift to realize number
		return((MASK_HEIGHT_BITS & oVoxel.Desc) >> 2);
	}
	STATIC_INLINE void setHeightStep(Voxel& oVoxel, uint8_t const HeightStep)
	{
		// Clear height bits
		oVoxel.Desc &= (~MASK_HEIGHT_BITS);
		// Set new height bits safetly truncating passed parameter HeightStep
		oVoxel.Desc |= (MASK_HEIGHT_BITS & (HeightStep << 2));
	}
	STATIC_INLINE_PURE float const getRealHeight(float const fHeightStep)
	{
		static constexpr float const INV_MAX_HEIGHT_STEP = 1.0f / (float)MAX_HEIGHT_STEP;

		return(SFM::__fma(fHeightStep, (INV_MAX_HEIGHT_STEP * HEIGHT_SCALE * VOX_SIZE), VOX_SIZE * 0.5f));  // half-voxel offset is exact
	}
	STATIC_INLINE_PURE float const getRealHeight(Voxel const& oVoxel)
	{
		return(getRealHeight((float)getHeightStep(oVoxel)));  // half-voxel offset is exact
	}
	
	// #defines to ensure one comparison only and compared with zero always (performance)

	// #### All Voxels
	STATIC_INLINE_PURE uint8_t const getOcclusion(Voxel const& oVoxel) {
		return(MASK_OCCLUSION_BITS & oVoxel.MaterialDesc);
	}
	STATIC_INLINE void clearOcclusion(Voxel& oVoxel) {
		// Clear occlusion bits
		oVoxel.MaterialDesc &= (~MASK_OCCLUSION_BITS);
	}
	STATIC_INLINE void setOcclusion(Voxel& oVoxel, uint8_t const occlusionshading) {
		clearOcclusion(oVoxel);
		if (OCCLUSION_SHADING_NONE != occlusionshading) {
			// Set new occlusion bits
			oVoxel.MaterialDesc |= (MASK_OCCLUSION_BITS & occlusionshading);
		}
	}

	STATIC_INLINE_PURE bool const isEmissive(Voxel const& oVoxel) {	// return emissive (0 or 1)
		return(((MASK_EMISSION_BITS & oVoxel.MaterialDesc) >> 5));
	}
	STATIC_INLINE void clearEmissive(Voxel& oVoxel) {
		// Clear occlusion bits
		oVoxel.MaterialDesc &= (~MASK_EMISSION_BITS);
	}
	/*STATIC_INLINE void setEmissive(Voxel& oVoxel) { // set emissive (0 or 1)

		// Set new occlusion bits
		oVoxel.MaterialDesc |= (MASK_EMISSION_BITS & (EMISSION_SHADING << 5));
	}*/
	template<bool const emissive = true>
	STATIC_INLINE void setEmissive(Voxel& oVoxel) { // set emissive (0 or 1)

		if constexpr (emissive) {
			// Set new occlusion bits
			oVoxel.MaterialDesc |= (MASK_EMISSION_BITS & (EMISSION_SHADING << 5));
		}
		else {
			clearEmissive(oVoxel);
		}

	}

	// #### Roads
	STATIC_INLINE_PURE bool const isRoadNode(Voxel const& oVoxel) { return((MASK_NODE_BIT & oVoxel.Desc)); } // Only used if current voxel is of extended type road
	STATIC_INLINE_PURE bool const isRoadEdge(Voxel const& oVoxel) { return(!(MASK_NODE_BIT & oVoxel.Desc)); } // Only used if current voxel is of extended type road
	STATIC_INLINE_PURE bool const isRoadNodeCenter(Voxel const& oVoxel) { return(MASK_ROAD_NODE_CENTER == (oVoxel.Hash[GROUND_HASH] & MASK_ROAD_NODE_CENTER)); } // Only used if current voxel is of extended type road and is a node

	STATIC_INLINE_PURE uint32_t const getRoadNodeType(Voxel const& oVoxel) // returns Iso::ROAD_NODE_TYPE::INVALID if current voxel is not a node
	{																	   // Only use if current voxel is of extended type road
		// Mask off road node type bits
		// Shift to realize number
		return((MASK_ROAD_NODE_TYPE & oVoxel.Hash[GROUND_HASH]) >> 16);
	}
	STATIC_INLINE void setAsRoadEdge(Voxel& oVoxel) // Only use if current voxel is of extended type road
	{
		// Clear node bit
		clearAsNode(oVoxel);
		// clear node center
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_NODE_CENTER);
		// clear node type
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_NODE_TYPE);
		// set road type bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_NODE_TYPE & (Iso::ROAD_NODE_TYPE::INVALID << 16));
	}
	STATIC_INLINE void setAsRoadNode(Voxel& oVoxel, uint32_t const type, bool const centered) // Only use if current voxel is of extended type road
	{
		// clear node type
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_NODE_TYPE);
		// set road type bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_NODE_TYPE & (type << 16));

		// clear node center
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_NODE_CENTER);
		// set road node center bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_NODE_CENTER & (((uint32_t)centered) << 20));

		// Set new node bit safetly
		setAsNode(oVoxel);
	}

	STATIC_INLINE_PURE uint32_t const getRoadDirection(Voxel const& oVoxel) { // Iso::ROAD_DIRECTION:: N,S,E,W (2 bits - 0 = 0b00 = N,   1 = 0b01 = S,   2 = 0b10 = E,   3 = 0b11 = W)
		return(((MASK_ROAD_DIRECTION & oVoxel.Hash[GROUND_HASH]) >> 8));
	}

	STATIC_INLINE void setRoadDirection(Voxel& oVoxel, uint32_t const direction) { // Iso::ROAD_DIRECTION:: N,S,E,W (2 bits - 0 = 0b00 = N,   1 = 0b01 = S,   2 = 0b10 = E,   3 = 0b11 = W)
		// clear road direction bits
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_DIRECTION);
		// set road direction bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_DIRECTION & (direction << 8));
	}

	STATIC_INLINE_PURE uint32_t const getRoadTile(Voxel const& oVoxel) {
		return(((MASK_ROAD_TILE & oVoxel.Hash[GROUND_HASH]) >> 10));
	}

	STATIC_INLINE void setRoadTile(Voxel& oVoxel, uint32_t const tile_index) {
		// clear road direction bits
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_TILE);
		// set road direction bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_TILE & (tile_index << 10));
	}

	STATIC_INLINE_PURE uint32_t const getRoadHeightStepBegin(Voxel const& oVoxel) {
		return(MASK_ROAD_HEIGHTSTEP_BEGIN & oVoxel.Hash[GROUND_HASH]);
	}
	STATIC_INLINE_PURE uint32_t const getRoadHeightStepEnd(Voxel const& oVoxel) {
		return((MASK_ROAD_HEIGHTSTEP_END & oVoxel.Hash[GROUND_HASH]) >> 4);
	}

	STATIC_INLINE void setRoadHeightStepBegin(Voxel& oVoxel, uint32_t const heightstep) {
		// clear road heightstep begin bits
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_HEIGHTSTEP_BEGIN);
		// set road heightstep begin bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_HEIGHTSTEP_BEGIN & heightstep);
	}
	STATIC_INLINE void setRoadHeightStepEnd(Voxel& oVoxel, uint32_t const heightstep) {
		// clear road heightstep end bits
		oVoxel.Hash[GROUND_HASH] &= (~MASK_ROAD_HEIGHTSTEP_END);
		// set road heightstep end bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ROAD_HEIGHTSTEP_END & (heightstep << 4));
	}

	STATIC_INLINE_PURE float const getRealRoadHeight(Voxel const& oVoxel) // *** if called when there is infact no road, results are undefined. Validate road exists b4 calling this function.
	{
		uint32_t const uiMaxHeightStep(SFM::max(Iso::getRoadHeightStepBegin(oVoxel), Iso::getRoadHeightStepEnd(oVoxel)));

		return(getRealHeight((float)uiMaxHeightStep));  // half-voxel offset is exact
	}

	// for voxel painting //
	namespace mini
	{
		static constexpr uint32_t const
			no_flags = 0,
			emissive = (1 << 0),
			transparent = (1 << 1);

	} // end ns
	typedef struct sMiniVoxel
	{
		VertexDecl::VoxelDynamic data;

		uint32_t color;
		uint32_t flags;

		sMiniVoxel const* __restrict next;

		sMiniVoxel()
			: data{}, next{ nullptr }, color{}, flags{}
		{}
		__forceinline explicit __vectorcall sMiniVoxel(FXMVECTOR worldPos_, FXMVECTOR uv_vr_, FXMVECTOR orient_reserved_, uint32_t const hash, uint32_t const color_, uint32_t const flags_, sMiniVoxel const* __restrict next_) noexcept
			: data(worldPos_, uv_vr_, orient_reserved_, hash), color(color_), flags(flags_), next(next_)
		{}
		sMiniVoxel(sMiniVoxel const&) = default;
		sMiniVoxel& operator=(sMiniVoxel const&) = default;
		sMiniVoxel(sMiniVoxel&&) noexcept = default;
		sMiniVoxel& operator=(sMiniVoxel&&) noexcept = default;
	} miniVoxel;


	// helper struct (last) //
	typedef struct sVoxelIndexHashPair // for general usage of associating a voxel (index) with an instance (hash)
	{
		point2D_t	voxelIndex;
		uint32_t	hash;

		sVoxelIndexHashPair()
			: voxelIndex{}, hash{}
		{}
		sVoxelIndexHashPair(point2D_t const voxelIndex_, uint32_t const hash_)
			: voxelIndex(voxelIndex_), hash(hash_)
		{}
		sVoxelIndexHashPair(sVoxelIndexHashPair const&) = delete;
		sVoxelIndexHashPair& operator=(sVoxelIndexHashPair const&) = delete;
		sVoxelIndexHashPair(sVoxelIndexHashPair&&) noexcept = default;
		sVoxelIndexHashPair& operator=(sVoxelIndexHashPair&&) noexcept = default;
	} voxelIndexHashPair;
	

// iSOMETRIC hELPER fUNCTIONS

// Grid Space to World Isometric Coordinates Only, Grid Spae should be pushed from
// range (-x,-y)=>(X,Y) to (0, 0)=>(X, Y) units first if in that coordinate system
// **** this is also Isometric to screenspace
	//STATIC_INLINE_PURE point2D_t const p2D_GridToIso(point2D_t const gridSpace)
	//{
	//	static constexpr int32_t const GRID_X = (Iso::GRID_RADII_X2 >> 1),
	//								   GRID_Y = (Iso::GRID_RADII >> 1);
		/*
			_x = (_col * tile_width * .5) + (_row * tile_width * .5);
			_y = (_row * tile_hieght * .5) - ( _col * tile_hieght * .5);
		*/
	//	point2D_t const vHalfGrid(GRID_X, GRID_Y);

	//	return(p2D_mul(point2D_t(gridSpace.x + gridSpace.y,
	//					         gridSpace.x - gridSpace.y), vHalfGrid));
	//}
///#define p2D_IsoToScreen p2D_GridToIso
	
	// Grid Space to World Isometric Coordinates Only, Grid Spae should be pushed from
	// range (-x,-y)=>(X,Y) to (0, 0)=>(X, Y) units first if in that coordinate system
	// **** this is also Isometric to screenspace (barebones, see world.h v2_gridtoscreen)
	STATIC_INLINE_PURE XMVECTOR const XM_CALLCONV v2_GridToIso(XMVECTOR const gridSpace)
	{
		static constexpr float const GRID_FX = (Iso::VOX_SIZE),
									 GRID_FY = (Iso::VOX_SIZE);
		/*
			_x = (_col * tile_width * .5) + (_row * tile_width * .5);
			_y = (_row * tile_hieght * .5) - ( _col * tile_hieght * .5);
		*/
		XMVECTORF32 const xmHalfGrid{ GRID_FX, GRID_FY, 0.0f, 0.0f };

		XMVECTOR xmIso = _mm_addsub_ps(XMVectorSplatX(gridSpace), XMVectorSplatY(gridSpace));

		xmIso = XMVectorMultiply(XMVectorSwizzle<XM_SWIZZLE_Y, XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_W>(xmIso), xmHalfGrid);

		return(xmIso);
	}
#define v2_IsoToScreen v2_GridToIso

	STATIC_INLINE_PURE XMVECTOR const XM_CALLCONV v2_ScreenToIso(FXMVECTOR const screenSpace)
	{
		static constexpr float const INV_GRID_FX = -1.0f / (Iso::VOX_SIZE),
									 INV_GRID_FY = 1.0f / (Iso::VOX_SIZE);
		/*
		// factored out the constant divide-by-two
		// only if we're doing floating-point division!
		map.x = screen.y / TILE_HEIGHT - screen.x / TILE_WIDTH;
		map.y = screen.y / TILE_HEIGHT + screen.x / TILE_WIDTH;
		*/
		XMVECTORF32 const xmInvGrid{ INV_GRID_FX, INV_GRID_FY, 0.0f, 0.0f };

		XMVECTOR xmScreen = XMVectorMultiply(screenSpace, xmInvGrid);
		
		xmScreen = _mm_addsub_ps(XMVectorSplatY(xmScreen), XMVectorSplatX(xmScreen) );
		/*
		XMFLOAT2A vScreen;
		XMStoreFloat2A(&vScreen, xmScreen);
		vScreen.x = vScreen.y - vScreen.x;
		vScreen.y = vScreen.y + vScreen.x;
		xmScreen = XMLoadFloat2A(&vScreen);
		*/
		return(xmScreen);
	}

	STATIC_INLINE_PURE point2D_t const p2D_ScreenToIso(point2D_t const screenSpace)
	{
		// avoid division, leverage other function for floats
		// 4 conversions = 4 cycles
		// integer division = 14 cycles minimum, and there would be 4 of them (ouch)
		return(v2_to_p2D(v2_ScreenToIso(p2D_to_v2(screenSpace))));
	}

} // endnamespace

#endif

