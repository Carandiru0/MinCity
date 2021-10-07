#pragma once
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include <Utility/bit_row.h>
#include "IsoVoxel.h"

class cSimulation : no_copy
{

private:
public:
	void run(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
			 bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict residential, 
		     bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict commercial, 
		     bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict industrial);
private:
	XMFLOAT3A	_demand; // residential, commercial, industrial demand, range [0.0f ... 1.0f]
public:
	cSimulation();
	~cSimulation();
};
