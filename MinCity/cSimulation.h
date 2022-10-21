#pragma once
#include <Utility/class_helper.h>
#include <Math/superfastmath.h>
#include <Utility/bit_row.h>
#include "IsoVoxel.h"
#include "voxelModel.h"

typedef struct properties_patch {

	size_t	tiles[3]{},				// number of tiles for each zone type													   [residential, commercial, industrial]
			tiles_occupied[3]{};	// of the number of tiles actually zoned, the number of tiles that have a structure built, for each zone type.

} properties_patch;

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

	vector<properties_patch> _patch_properties;	// per-patch properties

	struct {

		uint32_t			zoning,
							plot_size;

	} _current_packing;

	struct : properties_patch { // accumulated world scale patch properties -- (all patches)

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

	} _properties;  // world properties 

public:
	cSimulation();
	~cSimulation();
};
