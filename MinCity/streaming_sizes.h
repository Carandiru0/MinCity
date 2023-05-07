#pragma once
#include "betterenums.h"

// ##################### MAJOR THREADING CONSTANTS **************************** //

BETTER_ENUM(eThreadBatchGrainSize, uint32_t const,	// batch (*minimums*) sizes for major tbb parallel loops, representing minimum granularity of tasks in 2D or 1D

	GRID_RENDER_2D = 8U, // <--- this includes ground, is 2 dimensional and should be small (auto-partitioning - load balanced)
	MODEL = 128U,		 // minimum voxels processed in a single task, will be a lot larger as is split uniformly across affinity
	RADIAL = 32U,		 // ""        ""      ""     for a row in a single task, adaptively larger based on row size and is split uniformly across affinity
	GEN_PLOT = 1U		 // minimum unit for auto partitioner - *bugfix: must be equal to one otherwise buildings on plot intersect each other.
);

BETTER_ENUM(eStreamingBatchSize, uint32_t const,		// batch sizes for batched streaming stores

	GROUND = 8U,		// should be a multiple of eThreadBatchGrainSize::GRID_RENDER_2D
	MODEL = 8U,		//   ""    ""  factor   "" eThreadBatchGrainSize::MODEL
	RADIAL = 8U,		//  ""    ""  factor   "" eThreadBatchGrainSize::RADIAL
	LIGHTS = 8U			// equal to maximum number of lights that can be "seeded" on a 2D plane for a single light
);
