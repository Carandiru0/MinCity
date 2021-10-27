#include "pch.h"
#include "globals.h"
#include "cSimulation.h"
#include <Random/superrandom.hpp>
#include <Math/vec4_t.h>
#include "eVoxelModels.h"
#include "world.h"
#include "cBuildingGameObject.h"
#include "cBlueNoise.h"
#include "betterenums.h"

static constexpr float const MINIMUM_INCREMENT = 0.01f,
							 MAXIMUM_RANGE_SCALE = (float)INT32_MAX - 65;	// *bugfix: rollover to INT32_MAX + 1 if converted to float. precision requires offset of 65 or previous floating point digit that is actually <= INT32_MAX

// spacing = minimum + (+-1 * minimum) + 1   (calculation of spacing used around buildings where +-1 is randomized but in the range of +-1)																			// spacing = minimum + (+-1 * minimum) + 1
static constexpr int32_t const MINIMUM_SPACING = 2,
							   MAXIMUM_SPACING = MINIMUM_SPACING + MINIMUM_SPACING + 1;	

namespace // private to this file (anonymous)
{
	namespace packing
	{
		static constexpr uint32_t const
			PATCH_SIZE = Iso::WORLD_GRID_SIZE,
			MINIMUM_PLOT_SIZE = 4;

		constinit static inline alignas(CACHE_LINE_BYTES) bit_row<Iso::WORLD_GRID_SIZE>* theZone[3]{ nullptr, nullptr, nullptr };
		constinit static inline alignas(CACHE_LINE_BYTES) bit_row<Iso::WORLD_GRID_SIZE>* theRoad{ nullptr };

	} // end ns
} // end ns

cSimulation::cSimulation()
	: _demand{}, _tAccumulateLod{}, _run_count(0), _plot_size_index(0), _current_packing{}
{
	for (uint32_t i = 0; i < 3; ++i) {
		packing::theZone[i] = bit_row<Iso::WORLD_GRID_SIZE>::create(Iso::WORLD_GRID_SIZE);
	}
	packing::theRoad = bit_row<Iso::WORLD_GRID_SIZE>::create(Iso::WORLD_GRID_SIZE);

	_plot_sizes.reserve(10);
	for (uint32_t i = 256; i > (packing::MINIMUM_PLOT_SIZE << 1); i >>= 1) {
		_plot_sizes.emplace_back(i);
	}
	for (uint32_t i = 192; i > (packing::MINIMUM_PLOT_SIZE << 1); i >>= 1) {
		_plot_sizes.emplace_back(i);
	}
	random_shuffle(_plot_sizes.begin(), _plot_sizes.end());

 	_current_packing.plot_size = _plot_sizes[_plot_size_index];
}

template <int32_t const model_group_id>
static Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const* const __vectorcall getRandomVoxelModel(point2D_t const available_width_height, uint32_t const minimum_height = 0)
{
	vector<Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const*> models;

	uint32_t const model_count( Volumetric::getVoxelModelCount<model_group_id>() );

	models.reserve(model_count);

	for (uint32_t model_index = 0; model_index < model_count; ++model_index) {

		auto const* const __restrict model(Volumetric::getVoxelModel<model_group_id>(model_index));

		point2D_t const model_width_height(model->_LocalArea.width_height());

		if (model->_maxDimensions.y >= minimum_height) {
			if (model_width_height.x < available_width_height.x && model_width_height.x >= (available_width_height.x >> 1)
				&& model_width_height.y < available_width_height.y && model_width_height.y >= (available_width_height.y >> 1))
			{
				models.emplace_back(model);
			}
		}
	}

	if (!models.empty()) {
		return(models[PsuedoRandomNumber32(0, ((int32_t)models.size()) - 1)]);
	}

	return(nullptr);

}

// generating -------------------------------------------------------------------------------------------------------------------------------------------------------------------//

// constant, does not write to global memory.						// this function assumes y is less than Iso::WORLD_GRID_SIZE, no bounds check required.
__declspec(safebuffers) STATIC_INLINE_PURE void __vectorcall genRow(point2D_t const simRowRange, uint32_t const y, Iso::Voxel const* const __restrict theGrid)
{
	bit_row<Iso::WORLD_GRID_SIZE>* const __restrict rowZone[3]{ &packing::theZone[world::RESIDENTIAL][y], &packing::theZone[world::COMMERCIAL][y], &packing::theZone[world::INDUSTRIAL][y] };
	bit_row<Iso::WORLD_GRID_SIZE>& __restrict rowRoad{ packing::theRoad[y] };

	// (optimization) raw grid access is ok as it will always be read within the bounds //
	Iso::Voxel const* __restrict pVoxel(theGrid + y * Iso::WORLD_GRID_SIZE + ((uint32_t)simRowRange.x)); // beginning of row (read-only)

	for (uint32_t x = ((uint32_t)simRowRange.x); x < ((uint32_t)simRowRange.y); ++x) {

		Iso::Voxel const oVoxel(*pVoxel);

		if (!Iso::isPending(oVoxel)) {

			if (Iso::isGroundOnly(oVoxel)) {

				if (!Iso::hasStatic(oVoxel)) {

					uint32_t const hash(Iso::getHash(oVoxel, Iso::GROUND_HASH));
					uint32_t const voxelZoning(Iso::MASK_ZONING & hash);

					if (0 != voxelZoning) {
						rowZone[voxelZoning - 1]->set_bit(x); // tile is zoned and empty
					}


				}
			}
			else if (Iso::isRoad<false>(oVoxel)) {
				rowRoad.set_bit(x);
			}
		}
		++pVoxel; // next voxel in row
	}
}

static void __vectorcall generate(rect2D_t const simArea, Iso::Voxel const* const __restrict theGrid, tbb::affinity_partitioner& __restrict partitioner)
{
	typedef struct no_vtable generation {

	private:
		point2D_t const					   simRowRange;
		Iso::Voxel const* const __restrict theGrid;
	public:
		__forceinline generation(point2D_t const simRowRange_, Iso::Voxel const* const __restrict theGrid_)
			: simRowRange(simRowRange_), theGrid(theGrid_)
		{}

		void operator()(tbb::blocked_range<uint32_t> const& rows) const {

			uint32_t const // pull out into registers from memory
				row_begin(rows.begin()),
				row_end(rows.end());

			for (uint32_t iDy = row_begin; iDy < row_end; ++iDy) {

				genRow(simRowRange, iDy, theGrid);
			}
		}

	} generation;
	
	for (uint32_t i = 0; i < 3; ++i) {
		__memclr_stream<CACHE_LINE_BYTES>(packing::theZone[i], Iso::WORLD_GRID_SIZE * sizeof(bit_row<Iso::WORLD_GRID_SIZE>)); // reset
	}
	__memclr_stream<CACHE_LINE_BYTES>(packing::theRoad, Iso::WORLD_GRID_SIZE * sizeof(bit_row<Iso::WORLD_GRID_SIZE>)); // reset

	//for (uint32_t y = simArea.top; y < simArea.bottom; ++y) {
	//
	//	tbb::blocked_range<uint32_t> const rows{ y, y + 1 };
	//	generation(point2D_t(simArea.left, simArea.right), theGrid, zoning)(rows);
	//}
	tbb::parallel_for(tbb::blocked_range<uint32_t>(simArea.top, simArea.bottom, eThreadBatchGrainSize::GEN_PLOT), // parallel rows
		generation(point2D_t(simArea.left, simArea.right), theGrid), partitioner
	);
}

bool const __vectorcall cSimulation::generate_zoning(rect2D_t const simArea, Iso::Voxel const* const __restrict theGrid, tbb::affinity_partitioner& __restrict partitioner)
{
	static constexpr float const EPSILON = 0.01f;

	uvec4_v const uvMinimum(SFM::floor_to_u32(XMVectorScale(XMLoadFloat3A(&_demand), MAXIMUM_RANGE_SCALE)));

	uvec4_v const uvRandom(PsuedoRandomNumber32(), PsuedoRandomNumber32(), PsuedoRandomNumber32());

	uint32_t const masked(uvec4_v::result<3>(uvRandom < uvMinimum));

	if (masked) {  // only continue if at least 1 succeeded, allows for skipping simulation this frame

		// allow one once/frame either residential, commercial, or industrial exclusively based on demand
		bool bPending[3]{ (bool)(masked & (1 << world::RESIDENTIAL)), (bool)(masked & (1 << world::COMMERCIAL)), (bool)(masked & (1 << world::INDUSTRIAL)) };

		alignas(16) float* const demand(reinterpret_cast<float* const>(&_demand)); // indexable local reference

		if (bPending[world::RESIDENTIAL]) {

			if (bPending[world::COMMERCIAL]) {
				bPending[world::RESIDENTIAL] = !((demand[world::COMMERCIAL] - demand[world::RESIDENTIAL]) >= EPSILON); // disable (not pending) residential if demand is less
			}
			if (bPending[world::INDUSTRIAL]) {
				bPending[world::RESIDENTIAL] = !((demand[world::INDUSTRIAL] - demand[world::RESIDENTIAL]) >= EPSILON); // disable (not pending) residential if demand is less
			}
			// otherwise is still pending
		}

		if (bPending[world::COMMERCIAL]) {

			if (bPending[world::RESIDENTIAL]) {
				bPending[world::COMMERCIAL] = !((demand[world::RESIDENTIAL] - demand[world::COMMERCIAL]) >= EPSILON); // disable (not pending) commercial if demand is less
			}
			if (bPending[world::INDUSTRIAL]) {
				bPending[world::COMMERCIAL] = !((demand[world::INDUSTRIAL] - demand[world::COMMERCIAL]) >= EPSILON); // disable (not pending) commercial if demand is less
			}
			// otherwise is still pending
		}

		if (bPending[world::INDUSTRIAL]) {

			if (bPending[world::COMMERCIAL]) {
				bPending[world::INDUSTRIAL] = !((demand[world::COMMERCIAL] - demand[world::INDUSTRIAL]) >= EPSILON); // disable (not pending) industrial if demand is less
			}
			if (bPending[world::RESIDENTIAL]) {
				bPending[world::INDUSTRIAL] = !((demand[world::RESIDENTIAL] - demand[world::INDUSTRIAL]) >= EPSILON); // disable (not pending) industrial if demand is less
			}
			// otherwise is still pending
		}

		// final output should be exactly one of residential, commercial or industrial exclusively
		// verify, if not the case then randomly select one
		uint32_t active(0);
		uint32_t pending_min(999), pending_max(0);
		for (uint32_t i = 0; i < 3; ++i) {
			pending_min = SFM::min(pending_min, bPending[i] ? i : 999);
			pending_max = SFM::max(pending_max, bPending[i] ? i : 0);
		}
		if (pending_min != pending_max) {

			// randomly select between only the pending range
			active = PsuedoRandomNumber32(pending_min, pending_max);
		}
		else {
			active = pending_min;
		}

		_current_packing.zoning = active;

		// random shuffle once all sizes have been used consecutively and are about to repeat
		// ensures that a plot size is never missed, and randomizes the plot distribution to
		// the entire map. larger plot sizes are rarer than small plot sizes.
		if (_plot_sizes.size() == ++_plot_size_index) {
			_plot_size_index = 0;
			random_shuffle(_plot_sizes.begin(), _plot_sizes.end());
		}
		_current_packing.plot_size = _plot_sizes[_plot_size_index]; // update currently used plot size

		//rect2D_t simOverlappingArea = r2D_grow(simArea, point2D_t(MAXIMUM_SPACING));
		// clamp to local/minmax coords
		//simOverlappingArea = r2D_clamp(simOverlappingArea, 0, Iso::WORLD_GRID_SIZE);

		generate(simArea, theGrid, partitioner);

		return(true);
	}

	return(false);
}

// processing -------------------------------------------------------------------------------------------------------------------------------------------------------------------// 
BETTER_ENUM(requirement, uint32_t const,  // exclusive states, do not combine
	none = 0,
	adjacent_road = 1,
	adjacent_building_adjacent_to_road = 2
);

typedef struct 
{
	bool const success;
	point2D_t  voxelIndex;

} result;

STATIC_INLINE_PURE result const __vectorcall test_adjacent_road(rect2D_t const perimeter)
{
	// check perimeter exclusively for roads
	point2D_t voxelIndex[2];

	{ // top -- bottom

		// first row
		voxelIndex[0] = perimeter.left_top();

		// last row
		voxelIndex[1] = perimeter.left_bottom();

		for (uint32_t i = 0; i < 2; ++i) {

			for (; voxelIndex[i].x < perimeter.right; ++voxelIndex[i].x) {

				if (packing::theRoad[voxelIndex[i].y].read_bit(voxelIndex[i].x)) {
					return{ true, voxelIndex[i] };
				}
			}
		}
	}

	{ // left -- right

		// first column
		voxelIndex[0] = perimeter.left_top();

		// last column
		voxelIndex[1] = perimeter.right_top();

		for (uint32_t i = 0; i < 2; ++i) {

			for (; voxelIndex[i].y < perimeter.bottom; ++voxelIndex[i].y) {

				if (packing::theRoad[voxelIndex[i].y].read_bit(voxelIndex[i].x)) {
					return{ true, voxelIndex[i] };
				}
			}
		}
	}

	return{ false, point2D_t() };
}

/*
STATIC_INLINE_PURE bool const __vectorcall require_building_adjacent_to_road(Iso::Voxel const& __restrict oVoxel)
{
	if (Iso::hasStatic(oVoxel)) { // part of building?

		uint32_t const hash(Iso::getHash(oVoxel, Iso::STATIC_HASH));

		auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<false>(hash);

		if (instance) {

			auto const identity = instance->getModel().identity();
			if (Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL == identity._modelGroup
				|| Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL == identity._modelGroup
				|| Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL == identity._modelGroup) {

				point2D_t const instance_origin(instance->getVoxelIndex());

				// transform local area to world coordinates relative to the center origin of the model instance, then increase rect size by one for perimeter.
				rect2D_t const building_perimeter(r2D_grow(r2D_add(instance->getModel()._LocalArea, instance_origin), point2D_t(1)));

				return(test_adjacent_road(building_perimeter));
			}
		}
	}

	return(false);
}

STATIC_INLINE_PURE result const __vectorcall require_adjacent_building_adjacent_to_road(rect2D_t const perimeter, point2D_t const origin)
{
	// perimeter area with random spacing is used, to result in nice spacing between adjacent buildings.
	point2D_t voxelIndex[2];

	// second, check perimeter with additional offset up to MAXIMUM_SPACING for existing buildings
	rect2D_t growing_perimeter;

	for (int32_t offset = 0; offset <= MAXIMUM_SPACING; ++offset) 
	{
		growing_perimeter = r2D_grow(perimeter, point2D_t(offset));

		{ // top -- bottom

			// setup
			uint32_t const start = (uint32_t const)PsuedoRandom5050();

			// first row
			voxelIndex[start] = growing_perimeter.left_top();

			// last row
			voxelIndex[!start] = growing_perimeter.left_bottom();

			for (uint32_t i = 0; i < 2; ++i) {

				for (; voxelIndex[i].x < growing_perimeter.right; ++voxelIndex[i].x) {

					Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex[i]));

					if (pVoxel) {

						if (require_building_adjacent_to_road(*pVoxel)) { // part of building?

							// at least one voxel of the area defined touches building that touches road - done.
							return{ true, origin };
						}
					}
				}
			}
		}

		{ // left -- right

			// setup
			uint32_t const start = (uint32_t const)PsuedoRandom5050();

			// first column
			voxelIndex[start] = growing_perimeter.left_top();

			// last column
			voxelIndex[!start] = growing_perimeter.right_top();

			for (uint32_t i = 0; i < 2; ++i) {

				for (; voxelIndex[i].y < growing_perimeter.bottom; ++voxelIndex[i].y) {

					Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex[i]));

					if (pVoxel) {

						if (require_building_adjacent_to_road(*pVoxel)) { // part of building?

							// at least one voxel of the area defined touches building that touches road - done.
							return{ true, origin };
						}
					}
				}
			}
		}
	}

	return{ false, point2D_t() };
}

STATIC_INLINE_PURE result const __vectorcall require_adjacent_road(rect2D_t const perimeter, point2D_t const origin, int32_t const spacing)
{
	result returned{ false, origin };	// initialized to original center of area - see notes below.

	// check perimeter exclusively for roads
	point2D_t voxelIndex[2], voxelOffset[2];

	{ // top -- bottom

		bool bMoved(false);

		// setup
		uint32_t const start = (uint32_t const)PsuedoRandom5050();

		// first row
		voxelIndex[start] = perimeter.left_top();
		voxelOffset[start] = point2D_t(0, -spacing); // translate up
		// last row
		voxelIndex[!start] = perimeter.left_bottom();
		voxelOffset[!start] = point2D_t(0, spacing); // translate down

		for (uint32_t i = 0; (!bMoved && i < 2); ++i) {

			for (; voxelIndex[i].x < perimeter.right; ++voxelIndex[i].x) {

				if (packing::theRoad[voxelIndex[i].y].read_bit(voxelIndex[i].x)) {

					returned.voxelIndex = p2D_add(returned.voxelIndex, voxelOffset[i]);
					bMoved = true;
					break;
				}
			}
		}

		returned.complete |= bMoved;
	}

	{ // left -- right

		bool bMoved(false);

		// setup
		uint32_t const start = (uint32_t const)PsuedoRandom5050();

		// first column
		voxelIndex[start] = perimeter.left_top();
		voxelOffset[start] = point2D_t(-spacing, 0); // translate left
		// last column
		voxelIndex[!start] = perimeter.right_top();
		voxelOffset[!start] = point2D_t(spacing, 0); // translate right

		for (uint32_t i = 0; (!bMoved && i < 2); ++i) {

			for (; voxelIndex[i].y < perimeter.bottom; ++voxelIndex[i].y) {

				if (packing::theRoad[voxelIndex[i].y].read_bit(voxelIndex[i].x)) {

					returned.voxelIndex = p2D_add(returned.voxelIndex, voxelOffset[i]);
					bMoved = true;
					break;
				}
			}
		}

		returned.complete |= bMoved; 
	}

	return(returned);
}

template<uint32_t const requirement>
STATIC_INLINE_PURE result const __vectorcall checkRequirement(rect2D_t const area, int32_t const spacing) {

	// check surrounding voxels (perimeter) for:
	// 1.) road
	// 2.) adjacent building (which would only be placed previously if 1.) had been met)
	
	// *note: if the new area is adjacent to a road, it should be within 1 space/voxel. The area passed in includes a random amount of spacing around the building.
	//		  since the area with the extra spacing is always larger than the area without the extra spacing, the building could be moved within the larger area.
	//		  this is a new "center" that is returned for the origin of the building to be placed.
	// *note: the unique spacing around the new potential building is included in the area passed in to check. so we don't have to worry about it when looking for adjacent buildings.
	//        however, the spacing that *was* used on an adjacent existing building is unknown. so a search needs to iterate of the maximum spacing the building could have.
	rect2D_t const perimeter(r2D_grow(area, point2D_t(1)));

	if constexpr (requirement::adjacent_road == requirement) { 

		return(require_adjacent_road(perimeter, area.center(), spacing));
	}
	else if constexpr (requirement::adjacent_building_adjacent_to_road == requirement) {

		return(require_adjacent_building_adjacent_to_road(perimeter, area.center()));
	}

	return{ false, point2D_t() };
}
*/

typedef struct {
	rect2D_t 	area;
	point2D_t	adjacentIndex;
} zone;

template<uint32_t const requirement>
static zone const __vectorcall process(rect2D_t const simArea, uint32_t const zoning, uint32_t const plot_size, tbb::affinity_partitioner& __restrict partitioner)
{
	using Areas = tbb::enumerable_thread_specific<
		vector<zone>,
		tbb::cache_aligned_allocator<vector<zone>>,
		tbb::ets_key_per_instance>;

	typedef struct no_vtable processing {

	private:
		bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict		theZone;
		Areas& __restrict											areas;
		uint32_t const												plot_size;
	public:
		__forceinline processing(bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict& __restrict theZone_, Areas& __restrict areas_, uint32_t const plot_size_)
			: theZone(theZone_), areas(areas_), plot_size(plot_size_)
		{}

		void operator()(tbb::blocked_range<uint32_t> const& rows) const {

			uint32_t const // pull out into registers from memory
				row_begin(rows.begin()),
				row_end(rows.end());

			Areas::reference local_areas(areas.local());
			
			uint32_t size(0);

			for (uint32_t iDy = row_begin; iDy < row_end; ++iDy) {

				// Test Number of Consecutive Bits //
				for (uint32_t iDx = 0; iDx < Iso::WORLD_GRID_SIZE; ++iDx)
				{
					if (theZone[iDy].read_bit(iDx)) {

						if (++size == plot_size)
							break;
					}
					else
						size = 0;
				}

				if (plot_size == size) {

					bit_row<Iso::WORLD_GRID_SIZE> adjacency(theZone[iDy]);
					for (uint32_t iDyTest = 1; iDyTest < plot_size; ++iDyTest) {

						bit_row<Iso::WORLD_GRID_SIZE>::and_bits(adjacency, theZone[iDy + iDyTest]);
					}

					uint32_t offset(0);
					size = 0; // reset

					// Test Number of Consecutive Bits //
					for (uint32_t iDx = 0; iDx < Iso::WORLD_GRID_SIZE; ++iDx)
					{
						if (adjacency.read_bit(iDx)) {

							if (++size == plot_size)
								break;
						}
						else {
							size = 0;
							offset = iDx + 1;
						}
					}

					// Do we have enough Matching Rows ?
					if (size >= plot_size) {

						point2D_t const beginPoint(offset, iDy);
						point2D_t const endPoint(p2D_add(beginPoint, size));

						// convert to area
						rect2D_t localArea(beginPoint, endPoint);

						rect2D_t perimeter(r2D_grow(localArea, point2D_t(1)));
						perimeter = r2D_clamp(perimeter, 0, Iso::WORLD_GRID_SIZE);

						auto const [success, voxelIndex] = test_adjacent_road(perimeter);

						if (success) {

							// Change from (0,0) => (x,y) to (-x,-y) => (x,y)
							local_areas.emplace_back(r2D_sub(localArea, point2D_t(Iso::WORLD_GRID_HALFSIZE)), p2D_subs(voxelIndex, Iso::WORLD_GRID_HALFSIZE));
						}
						else { // within 1 extra tile/cell
							perimeter = r2D_grow(localArea, point2D_t(1));
							perimeter = r2D_clamp(perimeter, 0, Iso::WORLD_GRID_SIZE);

							auto const [success, voxelIndex] = test_adjacent_road(perimeter);

							if (success) {

								// Change from (0,0) => (x,y) to (-x,-y) => (x,y)
								local_areas.emplace_back(r2D_sub(localArea, point2D_t(Iso::WORLD_GRID_HALFSIZE)), p2D_subs(voxelIndex, Iso::WORLD_GRID_HALFSIZE));
							}

						}
					}

					size = 0; // reset
				}				
			}
		}
	} processing;


	Areas areas;

	//for (uint32_t y = simArea.top; y < (simArea.bottom - ((uint32_t)plot_size.y)); ++y) {
	//
	//	tbb::blocked_range<uint32_t> const rows{ y, y + 1 };
	//	processing(locations, point2D_t(simArea.left, simArea.right), plot_size, spacing)(rows);
	//}
	tbb::parallel_for(tbb::blocked_range<uint32_t>(simArea.top, simArea.bottom - 1 - plot_size, plot_size/*eThreadBatchGrainSize::GEN_PLOT*//* (simArea.bottom - 1) - simArea.top*/), // parallel rows
		processing(packing::theZone[zoning], areas, plot_size), partitioner
	);

	tbb::flattened2d<Areas> const flat_view(tbb::flatten2d(areas));
	uint32_t const count((uint32_t const)flat_view.size());

	zone selected{};

	if (count) {
		
		if (PsuedoRandom5050()) {
			   
			uint32_t const random_index(PsuedoRandomNumber32(0, count - 1));

			uint32_t index(0);

			for (tbb::flattened2d<Areas>::const_iterator
				i = flat_view.begin(); i != flat_view.end(); ++i) {

				if (random_index == index) {
					selected = *i;
					break;
				}
				++index;
			}
		}
		else {

			float min_square_ratio(9999999.0f), max_area(0.0f);

			for (tbb::flattened2d<Areas>::const_iterator
				i = flat_view.begin(); i != flat_view.end(); ++i) {

				point2D_t const width_height(i->area.width_height());

				//float square_ratio(SFM::abs(((float)width_height.x) / ((float)width_height.y)));
				//square_ratio = SFM::abs(square_ratio - 1.0f); // distance from ideal ratio of 1.0f

				//if (square_ratio < min_square_ratio)
				{
					uint32_t const area(((uint32_t const)width_height.x) * ((uint32_t const)width_height.y));

					if (area > max_area) {

						max_area = area;
					//	min_square_ratio = square_ratio;
						selected = *i;
					}
				}
			}
		}

		point2D_t start = selected.area.left_top();
		point2D_t end = selected.area.right_bottom();

		XMVECTOR const xmWorldOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOrigin()));

		for (; start.x < end.x; ++start.x) {

			Iso::Voxel const* const pVoxelStart(world::getVoxelAt(start));
			Iso::Voxel const* const pVoxelEnd(world::getVoxelAt(point2D_t(start.x, end.y)));

			if (pVoxelStart && pVoxelEnd) {

				Iso::Voxel const oVoxelStart(*pVoxelStart);
				Iso::Voxel const oVoxelEnd(*pVoxelStart);

				XMVECTOR xmOriginStart = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(start));
				xmOriginStart = XMVectorSetY(xmOriginStart, -Iso::getRealHeight(oVoxelStart) - 0.5f);
				xmOriginStart = XMVectorSubtract(xmOriginStart, xmWorldOrigin);

				XMVECTOR xmOriginEnd = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(point2D_t(start.x, end.y)));
				xmOriginEnd = XMVectorSetY(xmOriginEnd, -Iso::getRealHeight(oVoxelEnd) - 0.5f);
				xmOriginEnd = XMVectorSubtract(xmOriginEnd, xmWorldOrigin);

				world::addVoxel(xmOriginStart, start, 0x00ffffff, Iso::mini::emissive);
				world::addVoxel(xmOriginEnd, point2D_t(start.x, end.y), 0x00ffffff, Iso::mini::emissive);
			}
		}

		start = selected.area.left_top();
		end = selected.area.right_bottom();

		for (; start.y < end.y; ++start.y) {

			Iso::Voxel const* const pVoxelStart(world::getVoxelAt(start));
			Iso::Voxel const* const pVoxelEnd(world::getVoxelAt(point2D_t(end.x, start.y)));

			if (pVoxelStart && pVoxelEnd) {

				Iso::Voxel const oVoxelStart(*pVoxelStart);
				Iso::Voxel const oVoxelEnd(*pVoxelStart);

				XMVECTOR xmOriginStart = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(start));
				xmOriginStart = XMVectorSetY(xmOriginStart, -Iso::getRealHeight(oVoxelStart) - 0.5f);
				xmOriginStart = XMVectorSubtract(xmOriginStart, xmWorldOrigin);

				XMVECTOR xmOriginEnd = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(point2D_t(end.x, start.y)));
				xmOriginEnd = XMVectorSetY(xmOriginEnd, -Iso::getRealHeight(oVoxelEnd) - 0.5f);
				xmOriginEnd = XMVectorSubtract(xmOriginEnd, xmWorldOrigin);

				world::addVoxel(xmOriginStart, start, 0xff00ff00, Iso::mini::emissive);
				world::addVoxel(xmOriginEnd, point2D_t(end.x, start.y), 0xff00ff00, Iso::mini::emissive);
			}
		}
	}
	/*
	Iso::Voxel const* const pVoxel(world::getVoxelAt(select_area.center()));

	if (pVoxel) {
		XMVECTOR const xmWorldOrigin(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOrigin()));

		Iso::Voxel const oVoxel(*pVoxel);

		XMVECTOR xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(p2D_to_v2(select_area.center()));
		xmOrigin = XMVectorSetY(xmOrigin, -Iso::getRealHeight(oVoxel) - 0.5f);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		world::addVoxel(xmOrigin, select_area.center(), 0xff00ff00, Iso::mini::emissive);
	}*/


	// Grid Space (-x,-y) to (X, Y) Coordinates Only Returned
	return(selected);
}

void __vectorcall cSimulation::process_zoning(rect2D_t const simArea, fp_seconds const& __restrict tDelta, tbb::affinity_partitioner& __restrict partitioner)
{
	/*
	float const bn = supernoise::blue.get2D(current_packing.model->_LocalArea.right_bottom()) * 2.0f - 1.0f;	// bluenoise can make spacing smaller or larger

	// plot spacing range after adjust by bluenoise is *minimum* of 1 to *maximum* MINIMUM_SPACING + MINIMUM_SPACING + 1
	int32_t const unique_spacing(MINIMUM_SPACING + SFM::round_to_i32(bn * float(MINIMUM_SPACING)) + 1);	// bluenoise scaled

	point2D_t const plot_size(r2D_grow(current_packing.model->_LocalArea, point2D_t(unique_spacing)).width_height());

	rect2D_t simOverlappingArea = r2D_grow(simArea, plot_size);
	// clamp to local/minmax coords
	simOverlappingArea = r2D_clamp(simOverlappingArea, 0, Iso::WORLD_GRID_SIZE);
	*/

	zone const returned( process<requirement::adjacent_road>(simArea, _current_packing.zoning, _current_packing.plot_size, partitioner) );
		
	if (!returned.area.isZero()) {

		point2D_t plot_size(_current_packing.plot_size);

		//float const bn = supernoise::blue.get2D(returned.center()) * 2.0f - 1.0f;	// bluenoise can make spacing smaller or larger

		// plot spacing range after adjust by bluenoise is *minimum* of 1 to *maximum* MINIMUM_SPACING + MINIMUM_SPACING + 1
		//int32_t const unique_spacing(MINIMUM_SPACING + SFM::round_to_i32(bn * float(MINIMUM_SPACING)) + 1);	// bluenoise scaled

		// finally this makes sure only one is pending in the end
		Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const* pendingModel(nullptr);

		if (world::RESIDENTIAL == _current_packing.zoning) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>(plot_size);
		}
		else if (world::COMMERCIAL == _current_packing.zoning) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>(plot_size);
		}
		else if (world::INDUSTRIAL == _current_packing.zoning) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>(plot_size);
		}

		if (pendingModel) {

			using flags = Volumetric::eVoxelModelInstanceFlags;
			constexpr uint32_t const common_flags(flags::GROUND_CONDITIONING);

			// move center to have the models local area be adjacent to the adjacent index (road/building) in the zone.
			// this translation is still inside the available area for the zone.
			point2D_t const spacing(p2D_half(p2D_sub(returned.area.width_height(), pendingModel->_LocalArea.width_height())));
			point2D_t distance(p2D_sub(returned.adjacentIndex, returned.area.center()));
			point2D_t const direction(p2D_sgn(distance));

			distance = p2D_abs(distance);
			distance = p2D_clamp(distance, point2D_t(), spacing);

			point2D_t const voxelOffset(p2D_mul(direction, distance));

			point2D_t const origin( p2D_add(returned.area.center(), voxelOffset) );

			if (nullptr != MinCity::VoxelWorld.placeNonUpdateableInstanceAt<world::cBuildingGameObject, false>(origin, pendingModel, common_flags)) {

				alignas(16) float* const demand(reinterpret_cast<float* const>(&_demand)); // indexable local reference

				// reduce demand
				demand[_current_packing.zoning] -= 10.0f * MINIMUM_INCREMENT * tDelta.count();

				FMT_LOG(GAME_LOG, "spawned {:s} @ ({:d},{:d})", (_current_packing.zoning == 0 ? "residential" : (_current_packing.zoning == 1 ? "commercial" : "industrial")), origin.x, origin.y);
			}
		}
	}
}

namespace // private to this file (anonymous)
{
	namespace packing
	{
		static constexpr fp_seconds const // should be multiples increasing
			interval[4]{ fp_seconds(milliseconds(150)),	// visible (256)
						 fp_seconds(milliseconds(300)),	// 512
						 fp_seconds(milliseconds(600)),	// 1024
						 fp_seconds(milliseconds(1200))	// 2048
		};

		static constexpr int32_t const
			size[4]{ Iso::WORLD_GRID_SIZE >> 3,	// visible (256)
					 Iso::WORLD_GRID_SIZE >> 2,	// 512
					 Iso::WORLD_GRID_SIZE >> 1,	// 1024
					 Iso::WORLD_GRID_SIZE		// 2048
		};

	} // end ns
} // end ns

void cSimulation::run(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
	                  Iso::Voxel const* const __restrict theGrid)
{
	static constexpr uint32_t const
		SUBDIVIDE_SIZE = 256,
		SUBDIVISIONS = Iso::WORLD_GRID_SIZE / 256;

	// age demand
	XMVECTOR xmDemand(XMLoadFloat3A(&_demand));
	xmDemand = SFM::__fma(XMVectorMultiply( XMVectorSubtract(_mm_set_ps1(1.0f), xmDemand),
						  XMVectorScale(XMVectorSet(PsuedoRandomFloat(), PsuedoRandomFloat(), PsuedoRandomFloat(), 0.0f), MINIMUM_INCREMENT)), _mm_set_ps1(tDelta.count()), xmDemand);
	xmDemand = SFM::saturate(xmDemand);
	XMStoreFloat3A(&_demand, xmDemand);

	// level of detail zone generation & processing //
	for (uint32_t i = 0; i < 4; ++i) {
		_tAccumulateLod[i] += tDelta;
	}

	//if (0 == (_run_count & 1)) { // only run on even frames to balance loading

		rect2D_t simArea;

		// LOD 0
		if (_tAccumulateLod[0] > packing::interval[0]) {

			_tAccumulateLod[0] -= packing::interval[0];

			simArea = MinCity::VoxelWorld.getVisibleGridBoundsClamped();
		}
		/*
		// LOD 1, 2, 3
		for (uint32_t lod = 1; lod < 4; ++lod) {

			if (_tAccumulateLod[lod] > packing::interval[lod]) {

				_tAccumulateLod[lod] -= packing::interval[lod];

				int32_t const extent(packing::size[lod] >> 1);

				simArea = rect2D_t(point2D_t(-extent), point2D_t(extent));
				//simArea = r2D_add(simArea, MinCity::VoxelWorld.getVisibleGridCenter());
			}
		}
		*/
		//if (!simArea.isZero()) {

			// Change from(-x,-y) => (x,y)  to (0,0) => (x,y)
			simArea = r2D_add(simArea, point2D_t(Iso::WORLD_GRID_HALFSIZE));

			simArea = rect2D_t(point2D_t(), point2D_t(Iso::WORLD_GRID_SIZE));
			
			// clamp to local/minmax coords
			simArea = r2D_clamp(simArea, 0, Iso::WORLD_GRID_SIZE);

			static constexpr uint32_t const num_samples(16);

			constinit static microseconds sum{}, avg{}, maxi{}, mini{9999999999999999};
			constinit static uint32_t samples(0);

			tTime const tStart(high_resolution_clock::now());

			tbb::affinity_partitioner partitioner;

			if (generate_zoning(simArea, theGrid, partitioner)) {

				process_zoning(simArea, tDelta, partitioner);
			}

			microseconds const tElapsed(duration_cast<microseconds>(high_resolution_clock::now() - tStart));
			sum += tElapsed;
			maxi = std::max(maxi, tElapsed);
			mini = std::min(mini, tElapsed);

			if (num_samples == ++samples) {
				avg = sum / num_samples;
				sum = microseconds(0);
				samples = 0;
			}

			//FMT_NUKLEAR_DEBUG(true, "packing: avg({:d}us)  max({:d}us)  min({:d}us)  frame({:d}us)", avg.count(), maxi.count(), mini.count(), tElapsed.count());
		//}
	//}


	{
		//alignas(16) float* const demand(reinterpret_cast<float* const>(&_demand)); // indexable local reference
		//FMT_NUKLEAR_DEBUG(false, "residential [ {:.1f} ]  commercial [ {:.1f} ]  industrial [ {:.1f} ]", (demand[world::RESIDENTIAL] * 100.0f), (demand[world::COMMERCIAL] * 100.0f), (demand[world::INDUSTRIAL] * 100.0f));
	}

	++_run_count;
}

cSimulation::~cSimulation()
{
	for (uint32_t i = 0; i < 3; ++i) {
		if (packing::theZone[i]) {
			bit_row<Iso::WORLD_GRID_SIZE>::destroy(packing::theZone[i]);
			packing::theZone[i] = nullptr;
		}
	}
	if (packing::theRoad) {
		bit_row<Iso::WORLD_GRID_SIZE>::destroy(packing::theRoad);
		packing::theRoad = nullptr;
	}
}