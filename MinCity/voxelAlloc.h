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
	// needs measurement from stress test
	BETTER_ENUM(Allocation, uint32_t const,

		VOXEL_GRID_VISIBLE_XZ = Iso::SCREEN_VOXELS_XZ,
		VOXEL_GRID_VISIBLE_X = Iso::SCREEN_VOXELS_X,
		VOXEL_GRID_VISIBLE_Z = Iso::SCREEN_VOXELS_Z,
		VOXEL_GRID_VISIBLE_TOTAL = VOXEL_GRID_VISIBLE_X * VOXEL_GRID_VISIBLE_Z,

		VOXEL_MINIGRID_VISIBLE_X = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_X,
		VOXEL_MINIGRID_VISIBLE_Y = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Y,
		VOXEL_MINIGRID_VISIBLE_Z = Iso::VOXELS_GRID_SLOT_XZ * Iso::SCREEN_VOXELS_Z,

		// split in half for dynamic + static *** important *** should be less than 256MB total for a dynamic + static combined buffer size.

		                              // 8,388,608 visible mini voxels ( 4,194,304 dynamic + 4,194,304 static )  -  compare to a full/filled 512x512x512 volume 134,217,728 voxels. less than 6.25% of the visible volume can contain a mini voxel - counting on lots of empty space!!!
		                              // this is a 3,628,072,960 bytes (3.6 GB) capacity for the visible voxels gpu buffer.
		                              // this buffer is uploaded to the gpu every frame, however it's size is dynamic being the active/visible voxels for that frame. So it's far less than 3.6 GB uploaded/frame typically.
		                              // If the buffer was completely used, is there enough bandwidth on the PCI Express 3.0 x16 bus?
		                              // PCI Express 3.0 x16 total bandwidth: 16GB/s
		                              // 60 frames/s * 3.6GB = 217,684,377,600 bytes/s - (217 GB/s) ouch.
		                              // So what is the maximum buffer size and corresponding number of voxels that can actually squeeze into the 16 GB/s bandwidth available if that gpu buffer was fully used?    - 266 MB theoretical maximum/frame
		                              // ****** 256 MB maximum/frame --> ~591,000 voxels/frame, so around *****[600k]***** voxels total be used. ******
		                              // --------------------------------------------------------------------------------------------------------------

		VOXEL_MINIGRID_VISIBLE_TOTAL = 600000,	// static voxels
		VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL = VOXEL_MINIGRID_VISIBLE_TOTAL	// dynamic voxels 
	);

	read_only inline XMVECTORF32 const VOXEL_GRID_VISIBLE_XYZ{ (float)Allocation::VOXEL_GRID_VISIBLE_X, 1.0f, (float)Allocation::VOXEL_GRID_VISIBLE_Z };
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
	read_only inline XMVECTORF32 const _xmTransformToIndexBias{ (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f, 0.0f, (float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f };
	read_only inline XMVECTORF32 const _xmTransformToIndexBiasOverScale{ ((float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_X * 0.5f) / 2.0f, 0.0f, ((float)Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_Z * 0.5f) / 2.0f};
	read_only inline XMVECTORF32 const _xmInverseVisible{ Volumetric::INVERSE_MINIGRID_VISIBLE_X, Volumetric::INVERSE_MINIGRID_VISIBLE_Y, Volumetric::INVERSE_MINIGRID_VISIBLE_Z, 1.0f };

}// end ns


