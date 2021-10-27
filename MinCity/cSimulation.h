#pragma once
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include <Utility/bit_row.h>
#include "IsoVoxel.h"
#include "voxelModel.h"

class cSimulation : no_copy
{
private:
	bool const __vectorcall generate_zoning(rect2D_t const simArea, Iso::Voxel const* const __restrict theGrid, tbb::affinity_partitioner& __restrict partitioner);
	void __vectorcall process_zoning(rect2D_t const simArea, fp_seconds const& __restrict tDelta, tbb::affinity_partitioner& __restrict partitioner);
public:    
	void run(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
			 Iso::Voxel const* const __restrict theGrid);
private:
	XMFLOAT3A	_demand; // residential, commercial, industrial demand, range [0.0f ... 1.0f]
	fp_seconds	_tAccumulateLod[4];
	size_t		_run_count;

	vector<uint32_t> _plot_sizes;
	uint32_t		 _plot_size_index;

	struct {

		uint32_t			zoning,
							plot_size;

	} _current_packing;

public:
	cSimulation();
	~cSimulation();
};
