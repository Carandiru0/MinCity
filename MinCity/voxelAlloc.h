#pragma once
#include "globals.h"
#include "IsoVoxel.h"
#include "Math/superfastmath.h"
#include "betterenums.h"

#define VOXEL_DYNAMIC_SHIFT 2
#define VOXEL_ALPHA_SHIFT 1
namespace Volumetric
{
	static constexpr uint32_t const	
		MODEL_MAX_DIMENSION_XYZ = 256,	// supporting 256x256x256 size voxel model
		LEVELSET_MAX_DIMENSIONS_XYZ = 128;

	static constexpr uint32_t const
		VOXEL_MINIMAP_LINES_PER_CHUNK = 2;		// should be an even factor of world size - caution the world size volume will eat memory exponenially,
												// 512x256x512 times VoxelNormal = 12.8 GB !!!! where a "line" of 2x256x512 times VoxelNormal = 50 MB
	
	static constexpr uint32_t const DIRECT_BUFFER_SIZE_MULTIPLIER = 8; // direct buffers require more memory to support direct addressing, where staging buffers do not, as they are sequential and compact.

	// needs measurement from stress test
	BETTER_ENUM(Allocation, uint32_t const,

		VOXEL_GRID_VISIBLE_X = Iso::SCREEN_VOXELS_X,
		VOXEL_GRID_VISIBLE_Y = Iso::SCREEN_VOXELS_Y,
		VOXEL_GRID_VISIBLE_Z = Iso::SCREEN_VOXELS_Z,
		VOXEL_GRID_VISIBLE_TOTAL = VOXEL_GRID_VISIBLE_X * VOXEL_GRID_VISIBLE_Z, // terrainvoxels (must be divisable by 64)

		VOXEL_MINIGRID_VISIBLE_X = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_X,  // minivoxels
		VOXEL_MINIGRID_VISIBLE_Y = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Y,
		VOXEL_MINIGRID_VISIBLE_Z = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Z,

		// split in half for dynamic + static *** important *** should be less than 256MB total for a dynamic + static combined buffer size.

		                              // So what is the maximum buffer size and corresponding number of voxels that can actually squeeze into the 16 GB/s bandwidth available if that gpu buffer was fully used?    - 266 MB theoretical maximum/frame
		                              // ****** 256 MB maximum/frame --> ~591,000 voxels/frame, so around *****[600k]***** voxels total be used. ****** WRONG
		                              // --------------------------------------------------------------------------------------------------------------
		                              // Reserving 1x (1,048,576) - each (static, dynamic)
		VOXEL_MINIGRID_VISIBLE_TOTAL = VOXEL_MINIGRID_VISIBLE_X * VOXEL_MINIGRID_VISIBLE_Z * (VOXEL_MINIGRID_VISIBLE_Y >> 7),	// static voxels (must be divisable by 64)
		VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL = VOXEL_MINIGRID_VISIBLE_TOTAL	// dynamic voxels (must be divisable by 64)
	);
	
	static_assert(0 == (Allocation::VOXEL_GRID_VISIBLE_TOTAL % 64), "terrain voxel visible total not divisable by 64");
	static_assert(0 == (Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL % 64), "static voxel visible total not divisable by 64");
	static_assert(0 == (Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL % 64), "dynamic voxel visible total not divisable by 64");

	read_only inline XMVECTORF32 const VOXEL_GRID_VISIBLE_XYZ{ (float)Allocation::VOXEL_GRID_VISIBLE_X, (float)Allocation::VOXEL_GRID_VISIBLE_Y, (float)Allocation::VOXEL_GRID_VISIBLE_Z };
	read_only inline XMVECTORF32 const VOXEL_MINIGRID_VISIBLE_XYZ{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Y, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z };
	read_only inline XMVECTORF32 const VOXEL_MINIGRID_VISIBLE_XYZ_MINUS_ONE{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X - 1.0f, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Y - 1.0f, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z - 1.0f };
	read_only inline XMVECTORF32 const VOXEL_MINIGRID_VISIBLE_HALF_XYZ{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Y * 0.5f , (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };

	static constexpr float const
		INVERSE_GRID_VISIBLE_X = 1.0f / (float)(Allocation::VOXEL_GRID_VISIBLE_X),
		INVERSE_GRID_VISIBLE_Z = 1.0f / (float)(Allocation::VOXEL_GRID_VISIBLE_Z),
		INVERSE_MINIGRID_VISIBLE_X = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_X),
		INVERSE_MINIGRID_VISIBLE_Y = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_Y),
		INVERSE_MINIGRID_VISIBLE_Z = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_Z);

	// main transform position to uvw constants (works for both normal ground voxels and mini voxels; the scale and bias math is equal)
	read_only inline XMVECTORF32 const _xmTransformToIndexScale{ 2.0f, -2.0f, 2.0f };
	read_only inline XMVECTORF32 const _xmInvTransformToIndexScale{ 0.5f, -0.5f, 0.5f };
	read_only inline XMVECTORF32 const _xmTransformToIndexBias{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, 0.0f, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };
	read_only inline XMVECTORF32 const _xmTransformToIndexBiasOverScale{ ((float)Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f) / 2.0f, 0.0f, ((float)Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f) / 2.0f};
	read_only inline XMVECTORF32 const _xmInverseVisible{ INVERSE_MINIGRID_VISIBLE_X, INVERSE_MINIGRID_VISIBLE_Y, INVERSE_MINIGRID_VISIBLE_Z, 1.0f };

}// end ns 


