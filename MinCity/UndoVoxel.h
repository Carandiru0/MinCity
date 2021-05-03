#pragma once
#include "IsoVoxel.h"

typedef struct sUndoVoxel
{
	point2D_t const voxelIndex;
	Iso::Voxel const undoVoxel;

	sUndoVoxel(point2D_t const voxelIndex_, Iso::Voxel const& undoVoxel_) noexcept
		: voxelIndex(voxelIndex_), undoVoxel(undoVoxel_)
	{}

	bool const operator<(sUndoVoxel const& rhs) const {

		return(voxelIndex < rhs.voxelIndex);
	}
} UndoVoxel;


