#pragma once
#include "betterenums.h"
#include "IsoVoxel.h"

BETTER_ENUM(eDirection, uint32_t const,
	
	N = Iso::ROAD_DIRECTION::N,		// 0
	S = Iso::ROAD_DIRECTION::S,		// 1
	E = Iso::ROAD_DIRECTION::E,		// 2
	W = Iso::ROAD_DIRECTION::W,		// 3

	NE,	// 4
	NW, // 5
	SE, // 6
	SW  // 7
);