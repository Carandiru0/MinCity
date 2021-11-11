#pragma once
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include <Utility/bit_row.h>
#include "IsoVoxel.h"
#include "voxelModel.h"

typedef struct new_properties {

	size_t	tiles[3]{},
			tiles_occupied[3]{};

} new_properties;

class cSimulation : no_copy
{
private:
	bool const __vectorcall generate_zoning(rect2D_t const simArea, Iso::Voxel const* const __restrict theGrid, tbb::affinity_partitioner& __restrict partitioner);
	void __vectorcall process_zoning(rect2D_t const simArea, fp_seconds const& __restrict tDelta, tbb::affinity_partitioner& __restrict partitioner);
public:    
	void run(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
			 Iso::Voxel const* const __restrict theGrid);
private:
	fp_seconds	_tAccumulateLod[2];
	size_t		_run_count;

	vector<uint32_t> _plot_sizes;
	uint32_t		 _plot_size_index;

	vector<rect2D_t> _patches;
	uint32_t		 _patch_index;

	vector<new_properties> _patch_properties;

	struct {

		uint32_t			zoning,
							plot_size;

	} _current_packing;

	struct {

		static inline constexpr size_t const population_per_tile[3] = { 40, 10, 20 };

		// demand
		union {
			XMFLOAT3A				 demand;	// residential, commercial, industrial demand, range [0.0f ... 1.0f]

			struct alignas(16) {
				float	 r, c, i;
			};
		};

		// population
		size_t				 population{},
							 possible_population{};

		// zoning tile counts
		size_t				 tiles[3]{},
							 tiles_occupied[3]{};

	} _properties;

public:
	cSimulation();
	~cSimulation();
};
