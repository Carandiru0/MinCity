#include "pch.h"
#include "globals.h"
#include "cAISkyMover.h"
#include "cVoxelWorld.h"
#include "MinCity.h"

cAISkyMover::cAISkyMover()
	: _fMaxElevation(64.0f), _fClearance(0.0f), _fCurrent(0.0f), _fStart(0.0f), _fTarget(0.0f)
{

}

int32_t const cAISkyMover::conditionOfRouteVoxel(point2D_t const voxelIndex)
{
	int32_t condition(eRouteCondition::CLEAR);
	/*
	Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(voxelIndex));

	if (pVoxel) {

		float fElevation(0.0f);

		Iso::Voxel const oVoxel(*pVoxel);

		// dynamic check 
		if (hasDynamic(oVoxel) && 0 == getNextAvailableHashIndex<true>(oVoxel)) { // 0 == getNextAvailableHashIndex<true>() means voxel is full, no more dynamic instances can be layered (max 6)
			return(eRouteCondition::CANCEL); // blocked by dynamic, cancel the route
		}

		float const groundHeight(Iso::getRealHeight(oVoxel));

		{ // ground check
			float const fNewElevation(groundHeight + _fClearance);
			if (_fCurrent < fNewElevation) {

				fElevation = fNewElevation;
				condition = eRouteCondition::BLOCKED;
			}
		}

		// static check
		if (hasStatic(oVoxel)) {

			// get height of model
			auto const ModelInstanceHash = Iso::getHash(oVoxel, Iso::STATIC_HASH);
			auto const FoundModelInstance = MinCity::VoxelWorld->lookupVoxelModelInstance<false>(ModelInstanceHash);

			if (FoundModelInstance) {

				float const fNewElevation(groundHeight + SFM::max(_fClearance, FoundModelInstance->getModel()._Extents.y));
				if (fNewElevation <= _fMaxElevation) {

					fElevation = fNewElevation;
					condition = eRouteCondition::BLOCKED;
				}
				else {
					return(eRouteCondition::CANCEL); // blocked by static, cancel the route
				}
			}
		}

		// no change in elevation if clear route
		if (eRouteCondition::CLEAR != condition) {
			_fStart = _fCurrent;
			_fTarget = SFM::min(_fMaxElevation, fElevation);
		}

		return(condition); // by default returning CLEAR (not blocked) otherwise BLOCKED
	}
	*/
	return(eRouteCondition::CANCEL); // out of bounds or general error if this is reached
}

int32_t const cAISkyMover::conditionOfRoute(FXMVECTOR const xmLocation)
{
	point2D_t const voxelIndex(v2_to_p2D(xmLocation));
	rect2D_t const rectArea(r2D_add(_rectLocalArea, voxelIndex));

	// area:
	point2D_t voxelIterate(rectArea.left_top());
	point2D_t const voxelEnd(rectArea.right_bottom());

	while (voxelIterate.y <= voxelEnd.y) {

		voxelIterate.x = rectArea.left;
		while (voxelIterate.x <= voxelEnd.x) {

			// voxel:
			int32_t const condition = conditionOfRouteVoxel(voxelIterate);

			if (eRouteCondition::CLEAR != condition)
				return(condition);

			++voxelIterate.x;
		}

		++voxelIterate.y;
	}

	return(eRouteCondition::CLEAR);
}

void cAISkyMover::updateRoute(float const fTDeltaNormalized)
{
	_fCurrent = SFM::lerp(_fStart, _fTarget, fTDeltaNormalized);
}
