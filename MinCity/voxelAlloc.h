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
		MODEL_MAX_DIMENSION_XYZ = 256;	// supporting 256x256x256 size voxel model

	static constexpr uint32_t const
		VOXEL_MINIMAP_LINES_PER_CHUNK = 2;		// should be an even factor of world size - caution the world size volume will eat memory exponenially,
												// 512x256x512 times VoxelNormal = 12.8 GB !!!! where a "line" of 2x256x512 times VoxelNormal = 50 MB
	// needs measurement from stress test
	BETTER_ENUM(Allocation, uint32_t const,
		VOXEL_RENDER_FACTOR_BITS = 4,
		VOXEL_SCREEN_XYZ = Iso::SCREEN_VOXELS_X + Iso::SCREEN_VOXELS_Y + Iso::SCREEN_VOXELS_Z,

		VOXEL_GRID_VISIBLE_XZ = Iso::SCREEN_VOXELS_XZ,
		VOXEL_GRID_VISIBLE_X = Iso::SCREEN_VOXELS_X,
		VOXEL_GRID_VISIBLE_Z = Iso::SCREEN_VOXELS_Z,
		VOXEL_GRID_VISIBLE_TOTAL = VOXEL_GRID_VISIBLE_X * VOXEL_GRID_VISIBLE_Z * 3,

		VOXEL_MINIGRID_VISIBLE_X = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_X,
		VOXEL_MINIGRID_VISIBLE_Y = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Y,
		VOXEL_MINIGRID_VISIBLE_Z = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Z,
		VOXEL_MINIGRID_VISIBLE_TOTAL = (VOXEL_MINIGRID_VISIBLE_X * VOXEL_MINIGRID_VISIBLE_Y * VOXEL_MINIGRID_VISIBLE_Z) >> Volumetric::Allocation::VOXEL_RENDER_FACTOR_BITS,	// static voxels
		VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL = (VOXEL_MINIGRID_VISIBLE_X * VOXEL_MINIGRID_VISIBLE_Y * VOXEL_MINIGRID_VISIBLE_Z) >> (Volumetric::Allocation::VOXEL_RENDER_FACTOR_BITS + 1)	// dynamic voxels 
	);

	read_only inline XMVECTORF32 const VOXEL_GRID_VISIBLE_XYZ{ (float)Allocation::VOXEL_GRID_VISIBLE_X, 1.0f, (float)Allocation::VOXEL_GRID_VISIBLE_Z };
	read_only inline XMVECTORF32 const VOXEL_MINIGRID_VISIBLE_XYZ{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Y, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z };
	read_only inline XMVECTORF32 const VOXEL_MINIGRID_VISIBLE_HALF_XYZ{ (float)Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, (float)Allocation::VOXEL_MINIGRID_VISIBLE_Y * 0.5f , (float)Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };

	static constexpr float const
		INVERSE_GRID_VISIBLE_X = 1.0f / (float)(Allocation::VOXEL_GRID_VISIBLE_X),
		INVERSE_GRID_VISIBLE_Z = 1.0f / (float)(Allocation::VOXEL_GRID_VISIBLE_Z),
		INVERSE_MINIGRID_VISIBLE_X = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_X),
		INVERSE_MINIGRID_VISIBLE_Y = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_Y),
		INVERSE_MINIGRID_VISIBLE_Z = 1.0f / (float)(Allocation::VOXEL_MINIGRID_VISIBLE_Z);

	// main transform position to uvw constants (works for both normal ground voxels and mini voxels; the scale and bias math is equal)
	read_only inline XMVECTORF32 const _xmTransformToIndexScale{ 2.0f, -2.0f, 2.0f };
	read_only inline XMVECTORF32 const _xmTransformToIndexBias{ (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, 0.0f, (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };
	read_only inline XMVECTORF32 const _xmInverseVisibleXYZ{ Volumetric::INVERSE_MINIGRID_VISIBLE_X, Volumetric::INVERSE_MINIGRID_VISIBLE_Y, Volumetric::INVERSE_MINIGRID_VISIBLE_Z, 1.0f };

}// end ns


