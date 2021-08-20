#pragma once
#include "globals.h"
#include "IsoVoxel.h"
#include "Math/superfastmath.h"
#include "betterenums.h"

#define VOXEL_DYNAMIC_SHIFT 2
#define VOXEL_ALPHA_SHIFT 1
namespace Volumetric
{
	static uint32_t const	
		MODEL_MAX_DIMENSION_XYZ = 256;	// supporting 256x256x256 size voxel model

	static constexpr uint32_t const
		VOXEL_MINIMAP_LINES_PER_CHUNK = 2;		// should be an even factor of world size - caution the world size volume will eat memory exponenially,
												// 512x256x512 times VoxelNormal = 12.8 GB !!!! where a "line" of 2x256x512 times VoxelNormal = 50 MB
	// needs measurement from stress test
	BETTER_ENUM(Allocation, uint32_t const,
		VOXEL_RENDER_FACTOR_BITS = 5,
		VOXEL_SCREEN_XYZ = Iso::SCREEN_VOXELS_X + Iso::SCREEN_VOXELS_Y + Iso::SCREEN_VOXELS_Z,

		VOXEL_GRID_VISIBLE_XZ = Iso::SCREEN_VOXELS_XZ,
		VOXEL_GRID_VISIBLE_X = Iso::SCREEN_VOXELS_X,
		VOXEL_GRID_VISIBLE_Z = Iso::SCREEN_VOXELS_Z,
		VOXEL_GRID_VISIBLE_TOTAL = VOXEL_GRID_VISIBLE_X * VOXEL_GRID_VISIBLE_Z,

		VOXEL_MINIGRID_VISIBLE_X = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_X,
		VOXEL_MINIGRID_VISIBLE_Y = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Y,
		VOXEL_MINIGRID_VISIBLE_Z = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Z,
		VOXEL_MINIGRID_VISIBLE_TOTAL = (VOXEL_MINIGRID_VISIBLE_X * VOXEL_MINIGRID_VISIBLE_Y * VOXEL_MINIGRID_VISIBLE_Z) >> Volumetric::Allocation::VOXEL_RENDER_FACTOR_BITS,	// static voxels
		
		VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL = (VOXEL_MINIGRID_VISIBLE_X * VOXEL_MINIGRID_VISIBLE_Y * VOXEL_MINIGRID_VISIBLE_Z) >> (Volumetric::Allocation::VOXEL_RENDER_FACTOR_BITS + 1),	// dynamic voxels 

		VOXEL_ATOMIC_STATE_RESERVE_SIZE = VOXEL_GRID_VISIBLE_TOTAL
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

	// main transform position to uvw constants
	read_only inline XMVECTORF32 const _xmTransformToIndexScale{ 2.0f, -2.0f, 2.0f };
	read_only inline XMVECTORF32 const _xmTransformToIndexBias{ (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, 0.0f, (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };
	read_only inline XMVECTORF32 const _xmInverseVisibleXYZ{ Volumetric::INVERSE_MINIGRID_VISIBLE_X, Volumetric::INVERSE_MINIGRID_VISIBLE_Y, Volumetric::INVERSE_MINIGRID_VISIBLE_Z, 1.0f };

	STATIC_INLINE_PURE XMVECTOR const XM_CALLCONV worldToNormalized(FXMVECTOR const voxel)
	{
		// change from -1 ... 1  >  0 ... 1, clamp to 0 ... 1
		XMVECTOR const xmPointFive(_mm_set1_ps(0.5f));
		return(SFM::saturate(SFM::__fma(XMVectorScale(voxel, Iso::INVERSE_MAX_VOXEL_COORD), xmPointFive, xmPointFive)));					
	}

	STATIC_INLINE_PURE XMVECTOR const XM_CALLCONV worldToVisibleGrid(FXMVECTOR const voxel)
	{
		return(XMVectorMultiply(worldToNormalized(voxel), VOXEL_GRID_VISIBLE_XYZ));					// scale by extents of visible volume
	}

	STATIC_INLINE_PURE XMVECTOR const XM_CALLCONV worldToVisibleMiniGrid(FXMVECTOR const voxel)
	{
		return(XMVectorMultiply(worldToNormalized(voxel), VOXEL_MINIGRID_VISIBLE_XYZ));				// scale by extents of visible volume
	}
}// end ns
