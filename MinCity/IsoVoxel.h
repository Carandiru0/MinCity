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
#pragma warning( disable : 4804 )	// unsafe "-" on type bool

#ifndef ISOVOXEL_H
#define ISOVOXEL_H
#include <Math/point2D_t.h>
#include <Utility/unroll.h>
#include "Declarations.h"

 // forward decl
struct ImagingMemoryInstance;

namespace Iso
{
	// cONSTANTS/*****************
	static constexpr size_t const
		WORLD_GRID_SIZE_BITS = 14;				// 2n = SIZE, always power of 2 for grid.   maximum size is 2^16 = 65536x65536 which is friggen huge - limited only by resolution of 16bit/component mouse voxelIndex color attachment. *the amount of world space doubles for every bit*  yeaahhh!!!
	static constexpr uint32_t const				//                                          recommend using 2^13 = 8192x8192 or less, map is still extremely large
		WORLD_GRID_WIDTH = (1u << WORLD_GRID_SIZE_BITS),
		WORLD_GRID_HEIGHT = (1u << WORLD_GRID_SIZE_BITS) >> 1u,  // moon maps are 2:1 (width:height)
		WORLD_GRID_HALF_WIDTH = (WORLD_GRID_WIDTH >> 1u),
		WORLD_GRID_HALF_HEIGHT = (WORLD_GRID_HEIGHT >> 1u);

	static constexpr int32_t const
		MIN_VOXEL_COORD_U = -((int32_t)(WORLD_GRID_HALF_WIDTH - 1)),
		MIN_VOXEL_COORD_V = -((int32_t)(WORLD_GRID_HALF_HEIGHT - 1)),
		MAX_VOXEL_COORD_U = (int32_t)(WORLD_GRID_HALF_WIDTH - 1),
		MAX_VOXEL_COORD_V = (int32_t)(WORLD_GRID_HALF_HEIGHT - 1);

	static constexpr float const
		MIN_VOXEL_FCOORD_U = (float)MIN_VOXEL_COORD_U,
		MIN_VOXEL_FCOORD_V = (float)MIN_VOXEL_COORD_V,
		MAX_VOXEL_FCOORD_U = (float)MAX_VOXEL_COORD_U,
		MAX_VOXEL_FCOORD_V = (float)MAX_VOXEL_COORD_V;

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

#define MINIVOXEL_FACTOR (Iso::VOXELS_GRID_SLOT_XZ)
#define MINIVOXEL_FACTORF (Iso::VOXELS_GRID_SLOT_XZ_FP)

	static constexpr uint32_t const
		SCREEN_VOXELS_XZ = 256,
		SCREEN_VOXELS_X = SCREEN_VOXELS_XZ,		// must be be even numbers, maximum zoom out affects this value so screen grid is fully captured in its bounds
		SCREEN_VOXELS_Y = 256,					// this is "up", needs to be measured
		SCREEN_VOXELS_Z = SCREEN_VOXELS_XZ;		// x and z should be equal and divisible by 8

	static constexpr float const
		VOX_SIZE = 0.5f,			// this value and shader value for normal vox size need to always match
		VOX_STEP = VOX_SIZE * 2.0f,

		MINI_VOX_SIZE = VOX_SIZE / MINIVOXEL_FACTORF,	// this value and shader value for mini-vox size need to always match
		MINI_VOX_STEP = MINI_VOX_SIZE * 2.0f,

		WORLD_MAX_HEIGHT = (float)(SCREEN_VOXELS_Y >> 1), // unit: voxels  ** not minivoxels - must always be half the volume height to limit the camera showing void area.
		TERRAIN_MAX_HEIGHT = WORLD_MAX_HEIGHT / MINIVOXEL_FACTORF,
		WORLD_GRID_FWIDTH = (float)WORLD_GRID_WIDTH,
		WORLD_GRID_FHEIGHT = (float)WORLD_GRID_HEIGHT,
		WORLD_GRID_FHALF_WIDTH = (float)WORLD_GRID_HALF_WIDTH,
		WORLD_GRID_FHALF_HEIGHT = (float)WORLD_GRID_HALF_HEIGHT,
		INVERSE_WORLD_GRID_FWIDTH = 1.0f / WORLD_GRID_FWIDTH,
		INVERSE_WORLD_GRID_FHEIGHT = 1.0f / WORLD_GRID_FHEIGHT,
		INVERSE_MAX_VOXEL_COORD_U = 1.0f / (MAX_VOXEL_FCOORD_U),
		INVERSE_MAX_VOXEL_COORD_V = 1.0f / (MAX_VOXEL_FCOORD_V);		// -- good for normalization in range -1.0f...1.0f
      
	static constexpr double const
		VOX_MINZ_SCALAR = ((double)Iso::MINI_VOX_SIZE) * SFM::GOLDEN_RATIO_ZERO;

	read_only inline XMVECTORF32 const WORLD_EXTENTS{ MAX_VOXEL_FCOORD_U, WORLD_MAX_HEIGHT, MAX_VOXEL_FCOORD_V }; // AABB Extents of world, note that Y Axis starts at zero, instead of -WORLD_EXTENT.y

	static constexpr int32_t const
		GRID_VOXELS_START_XZ_OFFSET = SCREEN_VOXELS_XZ >> 1;		// matches the origin of world and volumetric unit cube

	// perfect alignment with 3d texture and volume rendering, finally
	read_only_no_constexpr inline point2D_t const GRID_OFFSET{ -GRID_VOXELS_START_XZ_OFFSET, -GRID_VOXELS_START_XZ_OFFSET };
	read_only inline XMVECTORF32 const
		GRID_OFFSET_X_Z{ -GRID_VOXELS_START_XZ_OFFSET, 0.0f, -GRID_VOXELS_START_XZ_OFFSET };

	static constexpr uint32_t const
		DEFAULT_GROUND_COLOR = 0x00000000;

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

	// 0011 1100 ### UNUSED ###
	static constexpr uint8_t const MASK_UNUSED_BITS = 0b00111100;

	// Height moved to dedicated member
	static constexpr uint32_t const MAX_HEIGHT_STEP = 0xFFFF;	
	static constexpr uint32_t const NUM_HEIGHT_STEPS = MAX_HEIGHT_STEP + 1;	// 256 Values (0-65535 16bits)	
	static constexpr float const    INV_MAX_HEIGHT_STEP = 1.0f / (float)MAX_HEIGHT_STEP;

	// 1100 0000 ### SPECIAL ###
	static constexpr uint8_t const MASK_PENDING_BIT = 0b01000000;			// for temporary status indication (constructing/not commited to grid)
	static constexpr uint8_t const MASK_DISCOVERED_BIT = 0b10000000;		// for graph searching only

	// ######### MaterialDesc bits:

	// ### TYPE_ALL_VOXELS ###
	static constexpr uint8_t const MASK_UNUSED2_BITS = 0b00011111;
	static constexpr uint8_t const MASK_EMISSION_BITS = 0b00100000;
	static constexpr uint8_t const
		EMISSION_SHADING_NONE = 0,
		EMISSION_SHADING = 1;

	// ** Only valid if extended type is type in .Desc ** //
	static constexpr uint8_t const MASK_EXTENDED_TYPE_BITS = 0b11000000;
	static constexpr uint8_t const
		EXTENDED_TYPE_RESERVED0 = 0,
		EXTENDED_TYPE_RESERVED1 = 1,
		EXTENDED_TYPE_RESERVED2 = 2,
		EXTENDED_TYPE_RESERVED3 = 3;

	// ######### Hash bits:

	// ground hash bits //
	static constexpr uint32_t const MASK_ZONING = 0x03;			//									  0011	// Ground/Not Zoned = 0, Residential = 1, Commercial = 2, Industrial = 3 (Treat as a number value - not bit position - after mask is applied)
	static constexpr uint32_t const MASK_RESERVED = 0xFC;		//								 1111 11xx	// ground specific bits not used yet (reserved)
	static constexpr uint32_t const MASK_COLOR = 0xFFFFFF00;	// 1111 1111 1111 1111 1111 1111 xxxx xxxx	// ground coloring BGR

	// "Owner" Voxel & Hashes
	static constexpr uint8_t const
		GROUND_HASH = 0,
		STATIC_HASH = (1 << 0),
		DYNAMIC_HASH = (1 << 1);
	static constexpr uint8_t const
		HASH_COUNT = 7;  // 28 bytes + 3bytes = 31bytes/voxel

	using heightstep = uint16_t;

	class no_vtable alignas(32) Voxel  // 32 bytes / voxel
	{
	public:
		static ImagingMemoryInstance* const& __restrict   HeightMap();
		static heightstep const __vectorcall              HeightMap(point2D_t const voxelIndex);
		static heightstep* const __restrict __vectorcall  HeightMapReference(point2D_t const voxelIndex);

		uint8_t Desc;								// Type of Voxel + Attributes
		uint8_t MaterialDesc;						// Deterministic SubType + Emission
		uint8_t Owner;								// Owner Indices

		uint32_t Hash[HASH_COUNT];
		                                            // Index 0 = Ground / Extended Hash
													// Index 1 = Static Model
													// Index 2 to 7 = Up to 6 Dynamic Model(s)
		constexpr Voxel()
			: Desc{}, MaterialDesc{}, Owner{}, Hash{}
		{}
		Voxel(Voxel const&) = default;
		Voxel& operator=(Voxel const&) = default;  // should not be used as occlusion data will be clobbered only safe in certain init or generation scenarios
		Voxel(Voxel&&) noexcept = default;
		Voxel& operator=(Voxel&&) noexcept = default;
	};
	static_assert(sizeof(Voxel) <= 32); // Ensure Iso::Voxel is correct size @ compile time

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
	STATIC_INLINE void writePending(Voxel& oVoxel, bool const bPending) {

		// Conditionally set or clear bits without branching
		// https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
		oVoxel.Desc = (oVoxel.Desc & ~MASK_PENDING_BIT) | (-bPending & MASK_PENDING_BIT);
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

	// test both dynamic & static
	STATIC_INLINE_PURE bool const isHashEmpty(Voxel const& oVoxel) {  // this helper should *not* be used in conjunction with getNextAvailableHashIndex(), as its reduntantly testing the same thing 
																	  // however can be used isolated from getNextAvailableHashIndex()

		for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {
			if (0 != oVoxel.Hash[i])
				return(false);
		}

		return(true); // empty!
	}

	// test either dynamic xor static
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
			return(0 == oVoxel.Hash[Iso::STATIC_HASH]);
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

	STATIC_INLINE uint32_t const __vectorcall getHeightStep(point2D_t const voxelIndex) // returns 0u .... 65535u
	{
		return(Voxel::HeightMap(voxelIndex));
	}
	STATIC_INLINE void __vectorcall setHeightStep(point2D_t const voxelIndex, uint32_t const HeightStep) // HeightStep must be within the range 0u .... 65535u
	{
		*Voxel::HeightMapReference(voxelIndex) = HeightStep;
	}
	STATIC_INLINE_PURE float const getRealHeight(heightstep const HeightStep)
	{
		static constexpr uint32_t const RANGE_MAX = (UINT16_MAX);
		static constexpr float const INV_RANGE_MAX = 1.0f / (float)RANGE_MAX;

		float const fHeightStep(INV_RANGE_MAX * ((float)HeightStep)); // normalize range 0.0f ... 1.0f
		// scale to maximum world space terrain height....
		// glsl :
		return(SFM::max(MINI_VOX_SIZE, (TERRAIN_MAX_HEIGHT * fHeightStep) * MINI_VOX_SIZE) * MINIVOXEL_FACTORF); // transform by voxel size and minivoxel size, minimum of 1 minivoxel (in world voxel space coordinates) for real height.
	}
	STATIC_INLINE float const __vectorcall getRealHeight(point2D_t const voxelIndex)
	{
		return(getRealHeight(Voxel::HeightMap(voxelIndex))); // returns in world space voxel coordinates the height same as above however directly accessed fron Heightmap automatically for the passed in voxelIndex
	}

	// #defines to ensure one comparison only and compared with zero always (performance)

	// #### All Voxels
	STATIC_INLINE_PURE bool const isEmissive(Voxel const& oVoxel) {	// return emissive (0 or 1)
		return(((MASK_EMISSION_BITS & oVoxel.MaterialDesc) >> 5));
	}
	STATIC_INLINE void clearEmissive(Voxel& oVoxel) {
		// Clear bit
		oVoxel.MaterialDesc &= (~MASK_EMISSION_BITS);
	}
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

	// #### Ground Only 
	STATIC_INLINE_PURE uint32_t const getColor(Voxel const& oVoxel)
	{
		return((MASK_COLOR & oVoxel.Hash[GROUND_HASH]) >> 8);
	}
	STATIC_INLINE void setColor(Voxel& oVoxel, uint32_t const color) {  // bgr, no alpha

		oVoxel.Hash[GROUND_HASH] &= ~MASK_COLOR; // clear ground color bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_COLOR & (color << 8)); // safetly set ground color bits
	}
	STATIC_INLINE void clearColor(Voxel& oVoxel) {

		setColor(oVoxel, DEFAULT_GROUND_COLOR);
	}

	STATIC_INLINE_PURE uint32_t const getZoning(Voxel const& oVoxel)
	{
		return(MASK_ZONING & oVoxel.Hash[GROUND_HASH]);
	}
	STATIC_INLINE void setZoning(Voxel& oVoxel, uint32_t const zoning) {

		oVoxel.Hash[GROUND_HASH] &= ~MASK_ZONING; // clear zoning bits
		oVoxel.Hash[GROUND_HASH] |= (MASK_ZONING & zoning); // safetly set zoning bits
	}
	STATIC_INLINE void clearZoning(Voxel& oVoxel) {

		oVoxel.Hash[GROUND_HASH] &= ~MASK_ZONING; // clear zoning bits
	}

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

