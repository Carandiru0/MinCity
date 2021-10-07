#include "pch.h"
#include "globals.h"
#include "cSimulation.h"
#include <Random/superrandom.hpp>
#include <Math/vec4_t.h>
#include "eVoxelModels.h"
#include "world.h"
#include "cBuildingGameObject.h"
#include "cBlueNoise.h"

static constexpr float const MINIMUM_INCREMENT = 0.01f,
							 MAXIMUM_RANGE_SCALE = (float)INT32_MAX - 65;	// *bugfix: rollover to INT32_MAX + 1 if converted to float. precision requires offset of 65 or previous floating point digit that is actually <= INT32_MAX
static constexpr int32_t const MINIMUM_SPACING = 2;

cSimulation::cSimulation()
	: _demand{}
{
}

template <int32_t const model_group_id>
static Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const* const getRandomVoxelModel(uint32_t const minimum_height = 0)
{
	vector<Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const*> models;

	uint32_t const model_count( Volumetric::getVoxelModelCount<model_group_id>() );

	models.reserve(model_count);

	for (uint32_t model_index = 0; model_index < model_count; ++model_index) {

		auto const* const __restrict model(Volumetric::getVoxelModel<model_group_id>(model_index));

		if (model->_maxDimensions.y >= minimum_height) {
			models.emplace_back(model);
		}
	}

	if (!models.empty()) {
		return(models[PsuedoRandomNumber32(0, ((int32_t)models.size()) - 1)]);
	}

	return(nullptr);

}

static auto const __vectorcall genPlot(rect2D_t const area, bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict zone)
{
	float const bn = supernoise::blue.get2D(p2D_abs(area.center())) * 2.0f - 1.0f;	// bluenoise can make spacing smaller or larger
	int32_t const unique_spacing(SFM::round_to_i32(bn * float(MINIMUM_SPACING)));	// bluenoise scaled

	// plot spacing range after adjust by bluenoise is *minimum* of 1 to *maximum* MINIMUM_SPACING + MINIMUM_SPACING + 1
	point2D_t const plot_size(r2D_grow(area, point2D_t(MINIMUM_SPACING + unique_spacing + 1)).width_height());

	using Locations = tbb::enumerable_thread_specific<
		vector<point2D_t>,
		tbb::cache_aligned_allocator<vector<point2D_t>>,
		tbb::ets_key_per_instance>;
	Locations locations;

	tbb::parallel_for(tbb::blocked_range<uint32_t>(0, Iso::WORLD_GRID_SIZE - (((uint32_t)plot_size.y) - 1), eThreadBatchGrainSize::GEN_PLOT), // parallel rows
		[&](tbb::blocked_range<uint32_t> const& rows) {

			uint32_t const // pull out into registers from memory
				row_begin(rows.begin()),
				row_end(rows.end());
			uint32_t row_size(0);

			Locations::reference local_locations = locations.local();
			
			for (uint32_t iDy = row_begin; iDy != row_end; ++iDy) {

				// maybe the fastest rectangle packig algorithm in the world.

				{
					bit_row<Iso::WORLD_GRID_SIZE> multi_row(zone[iDy]);

					// And Next n Rows
					for (uint32_t iDyTest = 1; iDyTest < ((uint32_t)plot_size.y); ++iDyTest)
					{
						bit_row<Iso::WORLD_GRID_SIZE>::and_bits(multi_row, zone[iDy + iDyTest]);
					}

					{
						uint32_t begin(0);

						for (uint32_t iDx = 0; iDx < Iso::WORLD_GRID_SIZE; ++iDx)
						{
							if (multi_row.read_bit(iDx))
							{
								if (plot_size.x == ++row_size)
									break;

							}
							else
							{
								if (plot_size.x == row_size)
									break;
								else
								{
									begin = iDx + 1;
									row_size = 0;
								}
							}
						}

						// Do we have enough Matching Rows ?
						if (row_size >= plot_size.x) {
							point2D_t const beginPoint(begin, iDy),
											endPoint(p2D_add(beginPoint, plot_size));

							rect2D_t const voxelArea(beginPoint, endPoint);

							local_locations.emplace_back(voxelArea.center());
							break; // found area of zoned type that is completely empty.
						}
						
						// otherwise continue to next row of the outermost loop
					}
				}
			}

		});

	struct {
		bool found;
		point2D_t voxelIndex;
	} returned{};

	tbb::flattened2d<Locations> flat_view = tbb::flatten2d(locations);
	uint32_t const location_count((uint32_t)flat_view.size());
	if (location_count) {

		// select found location randomly / by location demand / land value / etc.
		uint32_t offset(PsuedoRandomNumber32(0, location_count - 1));

		for (tbb::flattened2d<Locations>::const_iterator
			i = flat_view.begin(); i != flat_view.end(); ++i) {

			if (0 == offset) {
				returned.found = true;
				
				// Change from (0,0) => (x,y) to (-x,-y) => (x,y)
				returned.voxelIndex = p2D_sub(*i, point2D_t(Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE));
				break;
			}
			--offset;
		}
	}
	// Grid Space (-x,-y) to (X, Y) Coordinates Only Returned
	return(returned);
}


void cSimulation::run(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta,
	bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict residential,
	bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict commercial,
	bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict industrial)
{
	// age demand
	XMVECTOR xmDemand(XMLoadFloat3A(&_demand));
	xmDemand = SFM::__fma(XMVectorMultiply( XMVectorSubtract(_mm_set_ps1(1.0f), xmDemand),
						  XMVectorScale(XMVectorSet(PsuedoRandomFloat(), PsuedoRandomFloat(), PsuedoRandomFloat(), 0.0f), MINIMUM_INCREMENT)), _mm_set_ps1(tDelta.count()), xmDemand);
	xmDemand = SFM::saturate(xmDemand);
	XMStoreFloat3A(&_demand, xmDemand);
	alignas(16) float* const demand(reinterpret_cast<float* const>(&_demand)); // indexable local reference

	uvec4_v const uvMinimum(SFM::floor_to_u32(XMVectorScale(xmDemand, MAXIMUM_RANGE_SCALE)));

	uvec4_v const uvRandom(PsuedoRandomNumber32(), PsuedoRandomNumber32(), PsuedoRandomNumber32());

	uint32_t const masked(uvec4_v::result<3>(uvRandom < uvMinimum));

	if (masked) {  // only continue if at least 1 succeeded, allows for skipping simulation this frame

	// allow one once/frame either residential, commercial, or industrial exclusively based on demand
		bool bPending[3]{ (bool)(masked & (1 << world::RESIDENTIAL)), (bool)(masked & (1 << world::COMMERCIAL)), (bool)(masked & (1 << world::INDUSTRIAL)) };

		if (bPending[world::RESIDENTIAL]) {

			if (bPending[world::COMMERCIAL]) {
				bPending[world::RESIDENTIAL] = !(demand[world::RESIDENTIAL] < demand[world::COMMERCIAL]); // disable (not pending) residential if demand is less
			}
			if (bPending[world::INDUSTRIAL]) {
				bPending[world::RESIDENTIAL] = !(demand[world::RESIDENTIAL] < demand[world::INDUSTRIAL]); // disable (not pending) residential if demand is less
			}
			// otherwise is still pending
		}

		if (bPending[world::COMMERCIAL]) {

			if (bPending[world::RESIDENTIAL]) {
				bPending[world::COMMERCIAL] = !(demand[world::COMMERCIAL] < demand[world::RESIDENTIAL]); // disable (not pending) commercial if demand is less
			}
			if (bPending[world::INDUSTRIAL]) {
				bPending[world::COMMERCIAL] = !(demand[world::COMMERCIAL] < demand[world::INDUSTRIAL]); // disable (not pending) commercial if demand is less
			}
			// otherwise is still pending
		}

		if (bPending[world::INDUSTRIAL]) {

			if (bPending[world::COMMERCIAL]) {
				bPending[world::INDUSTRIAL] = !(demand[world::INDUSTRIAL] < demand[world::COMMERCIAL]); // disable (not pending) industrial if demand is less
			}
			if (bPending[world::RESIDENTIAL]) {
				bPending[world::INDUSTRIAL] = !(demand[world::INDUSTRIAL] < demand[world::RESIDENTIAL]); // disable (not pending) industrial if demand is less
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
			for (uint32_t i = pending_min; i < (pending_max + 1); ++i) {
				bPending[i] = false;	// disable all
			}
			// randomly select between only the pending range
			active = PsuedoRandomNumber32(pending_min, pending_max);
			bPending[active] = true; // enable
		}
		else {
			active = pending_min;
		}

		// finally this makes sure only one is pending in the end
		Volumetric::voxB::voxelModel<Volumetric::voxB::STATIC> const* pendingModel(nullptr);

		if (bPending[world::RESIDENTIAL]) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>();
		}
		else if (bPending[world::COMMERCIAL]) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>();
		}
		else if (bPending[world::INDUSTRIAL]) {
			pendingModel = getRandomVoxelModel<Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>();
		}

		if (pendingModel) { // also allows for skipping simulation this frame

			bit_row<Iso::WORLD_GRID_SIZE> const* const __restrict zone[3]{ residential, commercial, industrial };

			auto const [found, voxelIndex] = genPlot(pendingModel->_LocalArea, zone[active]);

			if (found) {

				using flags = Volumetric::eVoxelModelInstanceFlags;
				constexpr uint32_t const common_flags(flags::GROUND_CONDITIONING);

				if (nullptr != MinCity::VoxelWorld.placeNonUpdateableInstanceAt<world::cBuildingGameObject, false>(voxelIndex, pendingModel, common_flags)) {

					rect2D_t worldArea(pendingModel->_LocalArea);
					worldArea = r2D_add(worldArea, voxelIndex);
					world::zoning::clearArea(worldArea, active);
					// reduce demand
					demand[active] -= 10.0f * MINIMUM_INCREMENT * tDelta.count();

					FMT_LOG(GAME_LOG, "spawned {:s} @ ({:d},{:d})", (active == 0 ? "residential" : (active == 1 ? "commercial" : "industrial")), voxelIndex.x, voxelIndex.y);
				}
			}
		}
	}

	FMT_NUKLEAR_DEBUG(false, "residential [ {:.1f} ]  commercial [ {:.1f} ]  industrial [ {:.1f} ]", (demand[world::RESIDENTIAL] * 100.0f), (demand[world::COMMERCIAL] * 100.0f), (demand[world::INDUSTRIAL] * 100.0f));
}

cSimulation::~cSimulation()
{

}