#include "pch.h"
#include "cRoadTool.h"
#include "eVoxelModels.h"
#include "cVoxelWorld.h"
#include "MinCity.h"
#include "Adjacency.h"
#include "cSignageGameObject.h"
#include "cTrafficSignGameObject.h"
#include "cTrafficControlGameObject.h"
#include "gui.h"
#include "prices.h"

#include "cNuklear.h"

#ifndef NDEBUG

//#define DEBUG_AUTOTILE
//#define DEBUG_ROAD_SEGMENTS // SLOWS IT RIGHT DOWN TOO MUCH LOGGING

#endif

static constexpr int32_t const ROAD_SIGNAGE_CHANCE = 10;

cRoadTool::cRoadTool()
	: _segmentVoxelIndex{}, _selection_start{}, _selected{}, _activePoint(1), // must be non-zero
	_seed_traffic_sign(PsuedoRandomNumber64()),
	_seed_signage(PsuedoRandomNumber64())
{

}

STATIC_INLINE_PURE point2D_t const getHoveredVoxelIndexSnapped()
{
	point2D_t const hoverVoxel(MinCity::VoxelWorld->getHoveredVoxelIndex());

	/*
	point2D_t hoverVoxelSnapped(
		SFM::roundToMultipleOf<false>(hoverVoxel.x, (int32_t)Iso::ROAD_SEGMENT_WIDTH),
		SFM::roundToMultipleOf<false>(hoverVoxel.y, (int32_t)Iso::ROAD_SEGMENT_WIDTH)
	);

	hoverVoxelSnapped = p2D_add(hoverVoxelSnapped, p2D_muls(p2D_sgn(p2D_sub(hoverVoxel, hoverVoxelSnapped)), Iso::SEGMENT_SIDE_WIDTH));
	*/

	return(hoverVoxel);
}

template<bool const perpendicular_side = true> // otherwise in-line with current direction
static bool const __vectorcall getAdjacentSides(point2D_t const currentPoint, uint32_t const currentDirection, int32_t const offset,
	point2D_t* const __restrict sidePoint, Iso::Voxel const** const __restrict pSideVoxel)  // both are maximum 2 elements in size
{
	// returns true if N or S, false if E or W
	
	if constexpr (perpendicular_side) {

		switch (currentDirection)
		{
		case Iso::ROAD_DIRECTION::N:
		case Iso::ROAD_DIRECTION::S:
			sidePoint[0] = point2D_t(currentPoint.x - offset, currentPoint.y);
			pSideVoxel[0] = world::getVoxelAt(sidePoint[0]);

			sidePoint[1] = point2D_t(currentPoint.x + offset, currentPoint.y);
			pSideVoxel[1] = world::getVoxelAt(sidePoint[1]);

			return(true);
			break;
		case Iso::ROAD_DIRECTION::E:
		case Iso::ROAD_DIRECTION::W:
			sidePoint[0] = point2D_t(currentPoint.x, currentPoint.y - offset);
			pSideVoxel[0] = world::getVoxelAt(sidePoint[0]);

			sidePoint[1] = point2D_t(currentPoint.x, currentPoint.y + offset);
			pSideVoxel[1] = world::getVoxelAt(sidePoint[1]);

			return(false);
			break;
		} // end switch
	}
	else { // in-line with current road direction

		switch (currentDirection)
		{
		case Iso::ROAD_DIRECTION::N:
		case Iso::ROAD_DIRECTION::S:
			sidePoint[0] = point2D_t(currentPoint.x, currentPoint.y - offset);
			pSideVoxel[0] = world::getVoxelAt(sidePoint[0]);

			sidePoint[1] = point2D_t(currentPoint.x, currentPoint.y + offset);
			pSideVoxel[1] = world::getVoxelAt(sidePoint[1]);

			return(true);
			break;
		case Iso::ROAD_DIRECTION::E:
		case Iso::ROAD_DIRECTION::W:
			sidePoint[0] = point2D_t(currentPoint.x - offset, currentPoint.y);
			pSideVoxel[0] = world::getVoxelAt(sidePoint[0]);

			sidePoint[1] = point2D_t(currentPoint.x + offset, currentPoint.y);
			pSideVoxel[1] = world::getVoxelAt(sidePoint[1]);

			return(false);
			break;
		} // end switch
	}

	return(false);
}

void __vectorcall cRoadTool::PressAction(FXMVECTOR const xmMousePos)
{
	point2D_t origin(getHoveredVoxelIndexSnapped());

	undoRoadHistory();

	_seed_traffic_sign = PsuedoRandomNumber64();
	_seed_signage = PsuedoRandomNumber64();

	_selection_start = _selected; // save the road start if existing road has been selected - otherwise selection start is zero

	bool const bSnapped(!_selected.origin.isZero());
	if (bSnapped) { // mousemove search found road block
		//snap to nearby road segment
		origin.v = _selected.origin.v;
	}
	
	if (0 != _activePoint) { // new
		_activePoint = 0;
	}
	_segmentVoxelIndex[_activePoint++].v = origin.v;

	// _segmentVoxelIndex[0] IS ALWAYS EQUAL TO BEGINNING POINT AFTER THIS POINT

#if defined(DEBUG_AUTOTILE) || defined(DEBUG_ROAD_SEGMENTS)
	static int32_t last_key(0);
	if (popup_text_at_voxelindex(_segmentVoxelIndex[0], fmt::format(FMT_STRING("BEGIN {:s}  ({:d},{:d})"), (bSnapped ? "SNAP":""), origin.x, origin.y), last_key)) {
		FMT_LOG_DEBUG("BEGIN {:s}  ({:d},{:d})", (bSnapped ? "SNAP" : ""), origin.x, origin.y);
	}
#endif
}

void __vectorcall cRoadTool::ReleaseAction(FXMVECTOR const xmMousePos)
{
	[[likely]] if (0 != _activePoint) {

		// _segmentVoxelIndex[1] IS ALWAYS EQUAL TO END POINT @ THIS POINT

#if defined(DEBUG_AUTOTILE) || defined(DEBUG_ROAD_SEGMENTS)
		static int32_t last_key(0);
		if (popup_text_at_voxelindex(_segmentVoxelIndex[1], fmt::format(FMT_STRING("RELEASED  ({:d},{:d})"), _segmentVoxelIndex[1].x, _segmentVoxelIndex[1].y), last_key)) {
			FMT_LOG_DEBUG("RELEASED  ({:d},{:d})", _segmentVoxelIndex[1].x, _segmentVoxelIndex[1].y);
		}
#endif

		commitRoadHistory();
		_activePoint = 0;
	}
}

void cRoadTool::commitRoadHistory() // commits current "road" to grid
{
	// using history (only voxel indices) to acquire the voxel from the grid, unset the pending bit, then update the voxel in the grid
	for (vector<sUndoVoxel>::const_iterator commitVoxel = getHistory().cbegin(); commitVoxel != getHistory().cend(); ++commitVoxel)
	{
		point2D_t const voxelIndex(commitVoxel->voxelIndex);
		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);
		if (pVoxel) {

			Iso::Voxel oVoxel(*pVoxel);
			Iso::clearPending(oVoxel);
			Iso::clearEmissive(oVoxel);
			world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		}
	}

	// destroy all instances of signage that were hidden
	for (vector<Iso::voxelIndexHashPair>::const_iterator destroyInstance = _undoExistingSignage.cbegin(); destroyInstance != _undoExistingSignage.cend(); ++destroyInstance)
	{
		MinCity::VoxelWorld->destroyVoxelModelInstance(destroyInstance->hash); // doesn't need to be immediate
	}
	
	// clear all history, all changes to grid committed
	_undoExistingSignage.clear();
	_undoSignage.clear();
	clearHistory();
}

void cRoadTool::undoRoadHistory() // undo's current "road" from grid
{
	// restore hidden signage
	for (auto const& undoVoxel : _undoExistingSignage)
	{
		Iso::Voxel const* const pVoxel = world::getVoxelAt(undoVoxel.voxelIndex);
		if (pVoxel) {

			Iso::Voxel oVoxel(*pVoxel);
			uint32_t existing(0);

			for (uint32_t i = Iso::STATIC_HASH; i < Iso::HASH_COUNT; ++i) {

				if (Iso::getHash(oVoxel, i) == undoVoxel.hash) {
					existing = i;
					break;
				}
			}

			if (existing) {
				Iso::setAsOwner(oVoxel, existing);
				world::setVoxelAt(undoVoxel.voxelIndex, std::forward<Iso::Voxel const&&>(oVoxel));
			}
		}
	}
	_undoExistingSignage.clear();

	for (auto const& undoVoxel : _undoSignage)
	{
		MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(undoVoxel);
	}
	_undoSignage.clear();

	undoHistory();
}

static uint32_t const __vectorcall getRoadNodeType(point2D_t const origin, int32_t const self_direction = -1)	// (query actual road node block type) inherently assumes origin points to a voxel that is : "road" & "CENTERED root/node"
{
	uint32_t neighbour_count(0);

	static constexpr int32_t const SEARCH_OFFSET(Iso::SEGMENT_SIDE_WIDTH + 1);

	point2D_t const axis[4]{ point2D_t(0, SEARCH_OFFSET)/*N*/, point2D_t(0, -SEARCH_OFFSET)/*S*/, point2D_t(SEARCH_OFFSET/*E*/, 0), point2D_t(-SEARCH_OFFSET/*W*/, 0),  };
	bool road_adjacency[4]{}; // in order NSEW
	
	// search adjacent segments for width of a road block
	for (uint32_t direction = 0; direction < 4; ++direction) {

		road_adjacency[direction] = world::roads::search_neighbour_for_road(origin, axis[direction]);
		neighbour_count += (uint32_t)road_adjacency[direction];
	}

	if (self_direction >= 0 && !road_adjacency[self_direction]) {
		road_adjacency[self_direction] = true;
		++neighbour_count;
	}

	/*

								NBR_TR
					NBR_T				    NBR_R
		NBR_TL					VOXEL					NBR_BR
					NBR_L					NBR_B
								NBR_BL
	*/
	
	// ###################################
	// neighbour_count = 2 : Corner Truth Table:
	// **counter clockwise** //

	// CORNER_TL 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   *this   |   E		 |
	// |          |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// CORNER_BL 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |	      |   *this   |   E		 |
	// |          |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// CORNER_BR 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |          |
	// | (edge)   |   (node)  |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// +----------+-----------+----------+
	
	// CORNER_TR 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |          |
	// | (edge)   |   (node)  |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	if (2 == neighbour_count) {

		// note: counter-clockwise winding order
		if (road_adjacency[Iso::ROAD_DIRECTION::E]) {

			if (road_adjacency[Iso::ROAD_DIRECTION::S]) {
				return(Iso::ROAD_NODE_TYPE::CORNER_TL);
			}
			else if (road_adjacency[Iso::ROAD_DIRECTION::N]) {
				return(Iso::ROAD_NODE_TYPE::CORNER_BL);
			}	
		}
		else if (road_adjacency[Iso::ROAD_DIRECTION::W]) {

			if (road_adjacency[Iso::ROAD_DIRECTION::N]) {
				return(Iso::ROAD_NODE_TYPE::CORNER_BR);
			}
			else if (road_adjacency[Iso::ROAD_DIRECTION::S]) {
				return(Iso::ROAD_NODE_TYPE::CORNER_TR);
			}
		}
	}

	// ###################################
	// neighbour_count > 2 : Intersection Truth Table:
	// **counter clockwise** //

	// XING_RTL 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |   E		 |
	// | (edge)   |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |           |          |
	// |          |		      |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// XING_TLB 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |			 |
	// | (edge)   |   (node)  |			 |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// XING_LBR 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |		      |          |
	// |          |           |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |   E		 |
	// | (edge)   |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// XING_BRT 
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |	      |   *this   |   E		 |
	// |	      |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	// XING_ALL
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   N		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |  W		  |   *this   |   E		 |
	// | (edge)   |   (node)  |   (edge) |
	// |          |           |          |
	// +----------+-----------+----------+
	// |          |           |          |
	// |          |   S		  |          |
	// |          |  (edge)   |          |
	// |          |           |          |
	// +----------+-----------+----------+

	else if (neighbour_count > 2) {

		if (road_adjacency[Iso::ROAD_DIRECTION::E] & road_adjacency[Iso::ROAD_DIRECTION::N] & road_adjacency[Iso::ROAD_DIRECTION::W] & road_adjacency[Iso::ROAD_DIRECTION::S])
			return(Iso::ROAD_NODE_TYPE::XING_ALL);
		else if (road_adjacency[Iso::ROAD_DIRECTION::E] & road_adjacency[Iso::ROAD_DIRECTION::N] & road_adjacency[Iso::ROAD_DIRECTION::W])
			return(Iso::ROAD_NODE_TYPE::XING_RTL);
		else if (road_adjacency[Iso::ROAD_DIRECTION::N] & road_adjacency[Iso::ROAD_DIRECTION::W] & road_adjacency[Iso::ROAD_DIRECTION::S])
			return(Iso::ROAD_NODE_TYPE::XING_TLB);
		else if (road_adjacency[Iso::ROAD_DIRECTION::W] & road_adjacency[Iso::ROAD_DIRECTION::S] & road_adjacency[Iso::ROAD_DIRECTION::E])
			return(Iso::ROAD_NODE_TYPE::XING_LBR);
		else if (road_adjacency[Iso::ROAD_DIRECTION::S] & road_adjacency[Iso::ROAD_DIRECTION::E] & road_adjacency[Iso::ROAD_DIRECTION::N])
			return(Iso::ROAD_NODE_TYPE::XING_BRT);
	}

	// ###################################
	// neighbour_count <= 1 : It's NOT a Node:
	//

	return(Iso::ROAD_NODE_TYPE::INVALID);
}

#ifndef NDEBUG
static std::string const getNodeTypeText(uint32_t const nodeType)
{
	switch (nodeType)
	{
	case Iso::ROAD_NODE_TYPE::CORNER_TL:
		return("Corner TL");
	case Iso::ROAD_NODE_TYPE::CORNER_BL:
		return("Corner BL");
	case Iso::ROAD_NODE_TYPE::CORNER_TR:
		return("Corner TR");
	case Iso::ROAD_NODE_TYPE::CORNER_BR:
		return("Corner BR");
	case Iso::ROAD_NODE_TYPE::XING_ALL:
		return("Xing ALL");
	case Iso::ROAD_NODE_TYPE::XING_RTL:
		return("Xing RTL");
	case Iso::ROAD_NODE_TYPE::XING_TLB:
		return("Xing TLB");
	case Iso::ROAD_NODE_TYPE::XING_LBR:
		return("Xing LBR");
	case Iso::ROAD_NODE_TYPE::XING_BRT:
		return("Xing BRT");
	}

	return("Invalid");
}
#endif // !NDEBUG

static inline bool const __vectorcall isRoadNodeType(point2D_t const origin, int32_t const self_direction = -1)	// (query actual road node block type) inherently assumes origin points to a voxel that is : "road" & "CENTERED root/node"
{
	return(Iso::ROAD_NODE_TYPE::INVALID != getRoadNodeType(origin, self_direction));
}

// snaps to end of road if passed in origin is a road and not of a node road type (straight segment)
static point2D_t const __vectorcall snap_to_nearest_end(point2D_t const origin, uint32_t const segment_distance = 0 )
{
	Iso::Voxel const* const pVoxel = world::getVoxelAt(origin);
	if (nullptr != pVoxel) {

		Iso::Voxel const oVoxel(*pVoxel);

		// ### Only if Not Already center of node road block && Is a road block (STRAIGHT SECTION)
		if (Iso::isRoad(oVoxel) && !Iso::isRoadNode(oVoxel)) {

			// adjacent in "inline" direction?
			uint32_t const encoded_direction(Iso::getRoadDirection(oVoxel));

			// search adjacent sides
			int32_t const search_width(Iso::ROAD_SEGMENT_WIDTH + segment_distance);

			point2D_t lastRoadPoint[2]{origin, origin}; // last known road segment
			for (int32_t offset = 1; offset < search_width; ++offset) {

				point2D_t sidePoint[2]{};
				Iso::Voxel const* pSideVoxel[2]{ nullptr };

				getAdjacentSides<false>(origin, encoded_direction, offset, sidePoint, pSideVoxel); // inline

				for (uint32_t side = 0; side < 2; ++side) { // both directions inline with road

					if (pSideVoxel[side]) {
						Iso::Voxel const oSideVoxel(*pSideVoxel[side]);

						// looking for absence of road start
						if (!Iso::isRoad(oSideVoxel)) {

							// needs to ensure that within Iso::SEGMENT_SIDE_WIDTH that there are no nodes in the current direction inline (there could be a gap in space between a straight segment and road corner equal to half the Iso::ROAD_SEGMENT_WIDTH before the road node is there, due to the way its laid out)
							point2D_t const current_direction(p2D_sub(sidePoint[side],lastRoadPoint[side])); // maximum [-1...1] on one axis difference possible here

							point2D_t const possible_node_point(p2D_add(sidePoint[side], p2D_muls(current_direction, Iso::SEGMENT_SIDE_WIDTH)));

							Iso::Voxel const* const pVoxelNode = world::getVoxelAt(possible_node_point);
							if (nullptr != pVoxelNode) {

								Iso::Voxel const oVoxelNode(*pVoxelNode);
								if (Iso::isRoad(oVoxelNode) && Iso::isRoadNode(oVoxelNode)) {
									// found the node - return unchanged origin point
									return(origin);
								}
							}
							// current point is not road, since last point recorded for this direction (including origin)
							// return the last known road point - the end....
							return(lastRoadPoint[side]);
						}
						else {
							lastRoadPoint[side] = sidePoint[side];
						}
					}
				} // for side
			}//for offset
		}
	}

	// return unmodified point, no node found
	return(origin);
}

// snaps to nearest node if it exists, assumes origin passed in is already some kind of road, returns modified origin thats is snapped to node middle if found
// this search is special in that it is not dependent on the origin voxels road direction
static point2D_t const __vectorcall snap_to_nearest_node(point2D_t const origin, uint32_t const segment_distance = 0, uint32_t* const pNodeType = nullptr) // addional distance offset
{
	Iso::Voxel const* const pVoxel = world::getVoxelAt(origin);
	if (nullptr != pVoxel) {

		Iso::Voxel const oVoxel(*pVoxel);

		// ### Already center of node road block?
		if (Iso::isRoadNode(oVoxel)) {

			uint32_t const nodeType(getRoadNodeType(origin));

			if (Iso::ROAD_NODE_TYPE::INVALID != nodeType) {
				// center on both axis, directly in the middle of a node already
				if (nullptr != pNodeType) {
					*pNodeType = nodeType;
				}
				return(origin);
			}
		}

		// adjacent in "inline" direction?
		uint32_t const encoded_direction(Iso::getRoadDirection(oVoxel));

		// search adjacent sides
		int32_t const search_width(Iso::ROAD_SEGMENT_WIDTH + segment_distance);

		for (int32_t offset = 1; offset < search_width; ++offset) {

			point2D_t sidePoint[2]{};
			Iso::Voxel const* pSideVoxel[2]{ nullptr };

			getAdjacentSides<false>(origin, encoded_direction, offset, sidePoint, pSideVoxel);

			for (uint32_t side = 0; side < 2; ++side) {

				if (pSideVoxel[side]) {
					Iso::Voxel const oSideVoxel(*pSideVoxel[side]);
					if (Iso::isRoad(oSideVoxel) && Iso::isRoadNode(oSideVoxel)) {

						uint32_t const nodeType(getRoadNodeType(sidePoint[side]));

						if (Iso::ROAD_NODE_TYPE::INVALID != nodeType) {
							// center on both axis, directly in the middle of a node now
							if (nullptr != pNodeType) {
								*pNodeType = nodeType;
							}
							return(sidePoint[side]);
						}
					}
				}
			} // for side
		}//for offset
	}

	// return unmodified point, no node found
	return(origin);
}

// do not call directly, use class methods deselect_road_intersect & select_road_intersect below this function
template<bool const bSelected>
static bool const __vectorcall select_road_intersect(point2D_t const origin, uint32_t const roadNodeType, SelectedRoad& __restrict outSelectedRoad)
{
	// center intersect
	Iso::Voxel const* const pVoxel = world::getVoxelAt(origin);
	if (nullptr != pVoxel) {

		Iso::Voxel oVoxel(*pVoxel);
		// "selections" do not use road history !!!, must be deselected when neccessary

		if constexpr (bSelected) {
			Iso::setEmissive(oVoxel);
		}
		else {
			Iso::clearEmissive(oVoxel);
		}
		world::setVoxelAt(origin, std::forward<Iso::Voxel const&& __restrict>(oVoxel));

		uint32_t const encoded_direction(Iso::getRoadDirection(oVoxel));

		// for intersections, addionally select the xing (crosswalk) segment parts
		bool const bXing(roadNodeType <= Iso::ROAD_NODE_TYPE::XING);
		int32_t const roadBlockWidth(Iso::SEGMENT_SIDE_WIDTH + int32_t(bXing));

		// select adjacent sides
		for (int32_t offset = 1; offset <= roadBlockWidth; ++offset) {

			point2D_t sidePoint[2]{};
			Iso::Voxel const* pSideVoxel[2]{ nullptr };

			getAdjacentSides<false>(origin, encoded_direction, offset, sidePoint, pSideVoxel);

			for (uint32_t side = 0; side < 2; ++side) {

				if (pSideVoxel[side]) {
					Iso::Voxel oSideVoxel(*pSideVoxel[side]);
					if (Iso::isRoad(oSideVoxel)) {
						// "selections" do not use road history !!!, must be deselected when neccessary

						if constexpr (bSelected) {
							Iso::setEmissive(oSideVoxel);
						}
						else {
							Iso::clearEmissive(oSideVoxel);
						}
						world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));
					}
				}
			} // for side
		}//for offset

		if (bXing) {  // addionally select xing (crosswalk) segment parts perpindicular to this road block if an intersection

			point2D_t sidePoint[2]{};
			Iso::Voxel const* pSideVoxel[2]{ nullptr };

			getAdjacentSides<true>(origin, encoded_direction, Iso::SEGMENT_SIDE_WIDTH + 1, sidePoint, pSideVoxel);

			for (uint32_t side = 0; side < 2; ++side) {

				if (pSideVoxel[side]) {
					Iso::Voxel oSideVoxel(*pSideVoxel[side]);
					if (Iso::isRoad(oSideVoxel)) {
						// "selections" do not use road history !!!, must be deselected when neccessary

						if constexpr (bSelected) {
							Iso::setEmissive(oSideVoxel);
						}
						else {
							Iso::clearEmissive(oSideVoxel);
						}
						world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));
					}
				}
			} // for side
		}

		if constexpr (bSelected) {
			outSelectedRoad.origin = origin;		// selecting
			outSelectedRoad.roadNodeType = roadNodeType;
		}
		else {
			outSelectedRoad = SelectedRoad{};		// de-selecting
		}

		return(true);
	}

	outSelectedRoad = SelectedRoad{};

	return(false);
}

void __vectorcall cRoadTool::deselect_road_intersect()
{
	if (!_selected.origin.isZero()) {
		
		::select_road_intersect<false>(_selected.origin, _selected.roadNodeType, _selected);
	}
}
bool const __vectorcall cRoadTool::select_road_intersect(point2D_t const origin, uint32_t const roadNodeType)
{
	// important to deselect previous selection if still selected somehow
	deselect_road_intersect();

	return(::select_road_intersect<true>(origin, roadNodeType, _selected));
}

bool const __vectorcall cRoadTool::search_and_select_closest_road(point2D_t const origin, int32_t const additional_search_width)
{
	static constexpr int32_t const SHORT_SEARCH_RADIUS(Iso::SEGMENT_SIDE_WIDTH);
	int32_t const LONG_SEARCH_RADIUS(Iso::SEGMENT_SNAP_WIDTH + additional_search_width);

	point2D_t const axis[4]{ point2D_t(1, 0), point2D_t(-1, 0), point2D_t(0, 1), point2D_t(0, -1) };

	point2D_t closest_intersect(0);
	int32_t closest_distance(INT32_MAX);

	uint32_t roadNodeType(Iso::ROAD_NODE_TYPE::INVALID);
	bool ok(false);

	deselect_road_intersect(); // reset

	// short search, ie.) cursor is hovering road

	// 0, 0 //
	if (world::roads::search_point_for_road(origin)) { // directly over road root voxel
		closest_intersect.v = origin.v;
	}
	else { // search adjacent segments for width of a road block

		for (uint32_t direction = 0; direction < 4; ++direction) {

			point2D_t const intersect = world::roads::search_road_intersect(origin, axis[direction], 1, SHORT_SEARCH_RADIUS);
			if (!intersect.isZero()) {

				int32_t const distance = p2D_distanceSquared(origin, intersect);
				if (distance < closest_distance) {
					closest_distance = distance;
					closest_intersect.v = intersect.v;
				}
			}
		}
	}

	if (!closest_intersect.isZero()) { // short search found road block

		closest_intersect = snap_to_nearest_node(closest_intersect, 1, &roadNodeType);		// **** snap to (node) happens here
		closest_intersect = snap_to_nearest_end(closest_intersect, 1); // this works giving road nodes priority to be selected first, which if that did happen this ends up behaving like a nop.
		ok = select_road_intersect(closest_intersect, roadNodeType);

#ifndef NDEBUG
		//FMT_NUKLEAR_DEBUG(false, "                        *road*  ({:d},{:d})", closest_intersect.x, closest_intersect.y);
#endif
	}
	
	if (!ok)
	{ // long search, ie.) cursor over ground, nearby roads are selected if inline with cursor horizontally/vertically

		closest_distance = INT32_MAX; //reset

		uint32_t intersect_count(0);

		for (uint32_t direction = 0; direction < 4; ++direction) {

			point2D_t const intersect = world::roads::search_road_intersect(origin, axis[direction], SHORT_SEARCH_RADIUS, LONG_SEARCH_RADIUS);
			if (!intersect.isZero()) {

				int32_t const distance = p2D_distanceSquared(origin, intersect);
				if (distance < closest_distance) {
					closest_distance = distance;
					closest_intersect.v = intersect.v;
					++intersect_count;
				}
			}
		}

		if (!closest_intersect.isZero()) { // short search found road block

			closest_intersect = snap_to_nearest_node(closest_intersect, 1, &roadNodeType);	// **** snap to (node) happens here
			closest_intersect = snap_to_nearest_end(closest_intersect, 1); // this works giving road nodes priority to be selected first, which if that did happen this ends up behaving like a nop.
			ok = select_road_intersect(closest_intersect, roadNodeType);
		}
	}

	return(ok);
}

void __vectorcall cRoadTool::MouseMoveAction(FXMVECTOR const xmMousePos)
{
	static constexpr uint32_t const color(gui::color);  // abgr - rgba backwards

	static point2D_t last_origin;
	point2D_t origin(getHoveredVoxelIndexSnapped());

	if (origin != last_origin) {
		last_origin = origin;

		undoRoadHistory();

		search_and_select_closest_road(origin); // while moving mouse and road tool is activated

		point2D_t const selectedEndPoint(_selected.origin);
		if (!selectedEndPoint.isZero()) { // snapped to nearby road?
			origin = selectedEndPoint;	// this prevents diagonal road as the axis are clamped next
												// and the axis max will still be the same
		}

		highlightArea(rect2D_t(p2D_subs(origin, Iso::SEGMENT_SIDE_WIDTH), p2D_adds(origin, Iso::SEGMENT_SIDE_WIDTH + 1)), color);

	}
}

typedef struct ROAD_SEGMENT {
	
	point2D_t				origin;
	bool					node = false;
	uint8_t					h0, h1;		// 0 .. Iso::MAX_HEIGHTSTEP
	
} ROAD_SEGMENT;

static vector<ROAD_SEGMENT>::const_iterator const findTargetRoadSegment(vector<ROAD_SEGMENT>::const_iterator const start, vector<ROAD_SEGMENT>::const_iterator const end,
												                        int32_t const currentHeightStep, int32_t& __restrict targetHeightStep)
{
	static constexpr int32_t const SAMPLE_COUNT(Iso::ROAD_SEGMENT_WIDTH * 3);

	vector<ROAD_SEGMENT>::const_iterator target(end);

	// first priority target are nodes
	for (vector<ROAD_SEGMENT>::const_iterator i = start; i != end; ++i) {
		if (i->node) {
			target = i;
			targetHeightStep = target->h0;
			return(target);
		}
	}

	// second priority target are smoothed average deviations
	int32_t sum(0), counter(0);
	for (vector<ROAD_SEGMENT>::const_iterator i = start; i != end; ++i) {
		
		// 2x sampling, more sampling = higher accuracy //
		sum += i->h0;
		sum += i->h1;
		++counter;

		if (counter > SAMPLE_COUNT) {
			int32_t const average(sum / (counter << 1));

			if (average != currentHeightStep) {
				target = i;
				targetHeightStep = average;
				break;
			}
			// otherwise keep sampling
		}		
	} // for

	return(target);
}

static uint32_t const __vectorcall getSmoothHeightStep(point2D_t const currentPoint, uint32_t const currentDirection)
{
	point2D_t sidePoint[2]{};
	Iso::Voxel const* pSideVoxel[2]{ nullptr };

	// inline
	getAdjacentSides<false>(currentPoint, currentDirection, 1, sidePoint, pSideVoxel);

	uint32_t uiHeightSum(0);
	float fNumSamples(0.0f);

	for (uint32_t side = 0; side < 2; ++side) {
		if (nullptr != pSideVoxel[side]) {
			uiHeightSum += Iso::getHeightStep(*pSideVoxel[side]); ++fNumSamples;
		}
	}

	// add in current voxel
	Iso::Voxel const* const __restrict pVoxel(world::getVoxelAt(currentPoint));
	if (nullptr != pVoxel) {
		uiHeightSum += Iso::getHeightStep(*pVoxel); ++fNumSamples;
	}

	return(SFM::round_to_u32(((float)uiHeightSum) / fNumSamples));
}

void __vectorcall cRoadTool::ConditionRoadGround(
	point2D_t const currentPoint, uint32_t const currentDirection,
	int32_t const groundHeightStep, Iso::Voxel& __restrict oVoxel)
{
	point2D_t mini(INT32_MAX), maxi(INT32_MIN);

	// move ground under road up to match
	{
		Iso::setHeightStep(oVoxel, groundHeightStep);
		Iso::setPending(oVoxel);

		mini = p2D_min(mini, currentPoint);
		maxi = p2D_max(maxi, currentPoint);
	}
	// surrounding ground under road - ground under road adjacent to the center/middle of the road.
	for (int32_t offset = 1; offset <= Iso::SEGMENT_SIDE_WIDTH; ++offset) {

		point2D_t sidePoint[2]{};
		Iso::Voxel const* pSideVoxel[2]{ nullptr };

		getAdjacentSides(currentPoint, currentDirection, offset, sidePoint, pSideVoxel);

		for (uint32_t side = 0; side < 2; ++side) {

			if (pSideVoxel[side]) {
				Iso::Voxel oSideVoxel(*pSideVoxel[side]);
				pushHistory(UndoVoxel(sidePoint[side], oSideVoxel));
				
				// define as road, but not an owner. (not the middle of the road)
				Iso::setType(oSideVoxel, Iso::TYPE_EXTENDED);
				Iso::setExtendedType(oSideVoxel, Iso::EXTENDED_TYPE_ROAD);
				Iso::clearAsOwner(oSideVoxel, Iso::GROUND_HASH);
				Iso::clearZoning(oSideVoxel);
				Iso::clearColor(oSideVoxel);
				Iso::setHeightStep(oSideVoxel, groundHeightStep);
				Iso::setPending(oSideVoxel); // in "constructing" state, not fully committed to grid

				world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));

				mini = p2D_min(mini, sidePoint[side]);
				maxi = p2D_max(maxi, sidePoint[side]);
			}
		} // for side
	}//for offset

	// ** required for any changes in terrain height ** //
	world::recomputeGroundAdjacencyOcclusion(rect2D(mini, maxi));
}

bool const __vectorcall cRoadTool::CreateRoadSegments(point2D_t currentPoint, point2D_t const endPoint)
{
	point2D_t const direction(p2D_sgn(p2D_sub(endPoint, currentPoint)));
	uint32_t encoded_direction(0);

	if (direction.y > 0) {
		encoded_direction = Iso::ROAD_DIRECTION::N;
#if defined(DEBUG_ROAD_SEGMENTS)
		FMT_LOG_DEBUG("### New Road, Heading {:s}", "North");
#endif
	}
	else if (direction.y < 0) {
		encoded_direction = Iso::ROAD_DIRECTION::S;
#if defined(DEBUG_ROAD_SEGMENTS)
		FMT_LOG_DEBUG("### New Road, Heading {:s}", "South");
#endif
	}
	else if (direction.x > 0) {
		encoded_direction = Iso::ROAD_DIRECTION::E;
#if defined(DEBUG_ROAD_SEGMENTS)
		FMT_LOG_DEBUG("### New Road, Heading {:s}", "East");
#endif
	}
	else if (direction.x < 0) {
		encoded_direction = Iso::ROAD_DIRECTION::W;
#if defined(DEBUG_ROAD_SEGMENTS)
		FMT_LOG_DEBUG("### New Road, Heading {:s}", "West");
#endif
	}
	else
		return(false);

	vector<ROAD_SEGMENT> segments;

	ROAD_SEGMENT currentSegment{};
	int32_t intersection_index(-1), intersection_remaining(0);

	{ // start
		Iso::Voxel const* const pVoxel = world::getVoxelAt(currentPoint);
		if (nullptr == pVoxel)
			return(false);

		currentSegment.h0 = getSmoothHeightStep(currentPoint, encoded_direction);
		currentSegment.origin.v = currentPoint.v;
	}

	// elevation 
	while (!p2D_sub(endPoint, currentPoint).isZero())  //(endPoint.x != currentPoint.x || endPoint.y != currentPoint.y)
	{
		bool bExistingCancel(false);

		Iso::Voxel const* const pVoxel = world::getVoxelAt(currentPoint);
		if (nullptr == pVoxel)
			break;

		Iso::Voxel const oVoxel(*pVoxel);
		
		// ** note currentSegment.h1 is only updated when it needs to change ** //
		if (Iso::isGroundOnly(oVoxel) || intersection_index >= 0  // new road *or* is road but not the middle of road (allows xing).
 			|| (Iso::isExtended(oVoxel) && (Iso::EXTENDED_TYPE_ROAD == Iso::getExtendedType(oVoxel)) && !Iso::isOwner(oVoxel, Iso::GROUND_HASH))) {  

			pushHistory(UndoVoxel(currentPoint, oVoxel));

			if (intersection_index < 0) {
#if defined(DEBUG_ROAD_SEGMENTS)
				FMT_LOG_DEBUG("Edge Segment");
#endif
				currentSegment.node = false;
				currentSegment.h1 = getSmoothHeightStep(currentPoint, encoded_direction);
				
			}
			else {
#if defined(DEBUG_ROAD_SEGMENTS)
				FMT_LOG_DEBUG("Intersect Segment, node({:s}), intersection remaining({:d})", (currentSegment.node ? "true" : "false"), intersection_remaining);
#endif
				--intersection_remaining;

				int32_t const mirrored_index(intersection_index - (Iso::SEGMENT_SIDE_WIDTH - intersection_remaining));
				if (mirrored_index >= 0) { // backtracking half a road width (mirrored)

					ROAD_SEGMENT& mirrored = segments[mirrored_index];

					if (intersection_remaining >= 0) {
						// make flat (inside intersection)
						mirrored.h0 = mirrored.h1 = currentSegment.h1;
					}
					else {
						currentSegment.node = false;
						// make flat (begin/end intersection)
						mirrored.h1 = currentSegment.h1;

						intersection_index = -1;
					}
					mirrored.node = currentSegment.node;
				}
				else { // started at root node instead

					if (intersection_remaining < 0) {
						currentSegment.node = false;

						intersection_index = -1;
					}
				}
			}
		}
		else if (Iso::isRoad(oVoxel)) {  // existing road

			// same axis for existing road being drawn now? // Prevent drawing road over road.
			// bugfix: **only** applies to straight segments of roads, nodes need to be bypassed on this check!

			if (!Iso::isRoadNode(oVoxel)) { // is existing road not a node?
				uint32_t const existingDirection(Iso::getRoadDirection(oVoxel));
				switch (existingDirection)
				{
				case Iso::ROAD_DIRECTION::N:
				case Iso::ROAD_DIRECTION::S:
					bExistingCancel = ((encoded_direction == Iso::ROAD_DIRECTION::N) | (encoded_direction == Iso::ROAD_DIRECTION::S));
					break;
				case Iso::ROAD_DIRECTION::E:
				case Iso::ROAD_DIRECTION::W:
					bExistingCancel = ((encoded_direction == Iso::ROAD_DIRECTION::E) | (encoded_direction == Iso::ROAD_DIRECTION::W));
					break;
				}

				if (bExistingCancel) {
					// bugfix: update the beginning height of the segment (h0 would be used by next segment)
					if (existingDirection == encoded_direction) {
						currentSegment.h0 = Iso::getRoadHeightStepEnd(oVoxel);

						if (!segments.empty()) {
							segments.back().h1 = Iso::getRoadHeightStepBegin(oVoxel);
						}
					}
					else {
						currentSegment.h0 = Iso::getRoadHeightStepBegin(oVoxel);

						if (!segments.empty()) {
							segments.back().h1 = Iso::getRoadHeightStepEnd(oVoxel);
						}
					}
				}

			}

			if (!bExistingCancel)
			{ // all nodes no straights apply below!
				bool const bCenterNode(isRoadNodeType(currentPoint, encoded_direction));

#if defined(DEBUG_ROAD_SEGMENTS)
				FMT_LOG_DEBUG("Existing Segment, intersection remaining({:d})", intersection_remaining);
#endif

				if (bCenterNode) {

					if (0 == segments.size()) {  // starting at a node?

						// add xing (crosswalk) to history for required autotiling that would be missed for this edge segment
						{
							point2D_t const patchPoint(p2D_sub(currentPoint, p2D_muls(direction, Iso::SEGMENT_SIDE_WIDTH + 1)));

							Iso::Voxel const* const pVoxelPatch = world::getVoxelAt(patchPoint);
							if (nullptr == pVoxelPatch)
								break;

							Iso::Voxel const oVoxelPatch(*pVoxelPatch);

							pushHistory(UndoVoxel(patchPoint, oVoxelPatch)); // need auto-tiling
						}

						// add road segments to fill empty half
						// patch required for halfwidth of road in opposite direction, as it starts in the center
						for (int32_t i = Iso::SEGMENT_SIDE_WIDTH; i > 0; --i)
						{
							point2D_t const patchPoint(p2D_sub(currentPoint, p2D_muls(direction, i)));

							Iso::Voxel const* const pVoxelPatch = world::getVoxelAt(patchPoint);
							if (nullptr == pVoxelPatch)
								break;

							Iso::Voxel const oVoxelPatch(*pVoxelPatch);

							pushHistory(UndoVoxel(patchPoint, oVoxelPatch)); // need auto-tiling

							ROAD_SEGMENT patchSegment(currentSegment);

							patchSegment.origin = patchPoint;

							segments.push_back(patchSegment);
						}
					}
				}

				// order is important between previous "patching" and this
				pushHistory(UndoVoxel(currentPoint, oVoxel));

				currentSegment.node = true;
				currentSegment.h1 = getSmoothHeightStep(currentPoint, encoded_direction);

				intersection_index = int32_t(segments.size()); // push_back is pending below, this is the index for *this segment
				intersection_remaining = Iso::SEGMENT_SIDE_WIDTH;

				if (bCenterNode) { // only neccesarry that all of this is done when center node is reached

					{ // set adjacent (perpindicular) sides elevation to be flat
						static constexpr int32_t const SIDE_OFFSET = Iso::SEGMENT_SIDE_WIDTH + 1;

						point2D_t sidePoint[2]{};
						Iso::Voxel const* pSideVoxel[2]{ nullptr };

						getAdjacentSides(currentPoint, encoded_direction, SIDE_OFFSET, sidePoint, pSideVoxel);

						// apply flatness to adjacent sides
						for (uint32_t side = 0; side < 2; ++side) {
							if (pSideVoxel[side]) {
								Iso::Voxel oSideVoxel(*pSideVoxel[side]);

								if (Iso::isRoad(oSideVoxel)) {
									pushHistory(UndoVoxel(sidePoint[side], oSideVoxel));

									uint32_t const side_direction(Iso::getRoadDirection(oSideVoxel));

									// always make xing flat
									Iso::setRoadHeightStepBegin(oSideVoxel, currentSegment.h1);
									Iso::setRoadHeightStepEnd(oSideVoxel, currentSegment.h1);

									// move ground under road up to match
									int32_t const groundHeightStep = SFM::max(0, SFM::min(currentSegment.h0, currentSegment.h1));
									ConditionRoadGround(sidePoint[side], side_direction, groundHeightStep, oSideVoxel);

									// done.
									world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));
								}
							}
						}
					}


					// set straight road connected to node to match elevation *bugfix - now properly interpolates accounting for nodes (corners and xing) glitch free.

					// for each perpindicular side, find the next node 
					{
						int32_t stepsaved[2]{ 0, 0 };
						bool searching[2]{ true,true }; // will contain false if node was found
						Iso::Voxel oSideVoxelSaved[2]{}; // will contain the last voxel for each side (node or end of road)

						int32_t step(1);

						do
						{
							point2D_t sidePoint[2]{};
							Iso::Voxel const* pSideVoxel[2]{ nullptr };

							getAdjacentSides(currentPoint, encoded_direction, Iso::SEGMENT_SIDE_WIDTH + step, sidePoint, pSideVoxel);

							for (uint32_t side = 0; side < 2; ++side) {
								if (searching[side]) { // skips search when finished side.

									if (pSideVoxel[side]) {
										oSideVoxelSaved[side] = *pSideVoxel[side];

										if (Iso::isRoad(oSideVoxelSaved[side])) {
											searching[side] = !Iso::isRoadNode(oSideVoxelSaved[side]); // xing or corner
											stepsaved[side] = step;
										}
										else {
											searching[side] = false; // end of straight road
										}
									}
									else {
										searching[side] = false; // end of world
									}
								}
							}

							++step;

						} while (searching[0] | searching[1]);

						// then interpolate from current to the result of the search for each perpindicular road.
						int32_t const startHeightStep(currentSegment.h1);
						int32_t currentSideHeightStep[2]{ startHeightStep, startHeightStep };
						bool grading[2]{ stepsaved[0] > 0, stepsaved[1] > 0 };

						int32_t const total_steps[2]{ stepsaved[0], stepsaved[1] };

						step = 1; // reset
						
						do
						{
							grading[0] &= step <= total_steps[0];
							grading[1] &= step <= total_steps[1];

							if (grading[0] | grading[1]) {

								point2D_t sidePoint[2]{};
								Iso::Voxel const* pSideVoxel[2]{ nullptr };

								getAdjacentSides(currentPoint, encoded_direction, Iso::SEGMENT_SIDE_WIDTH + step, sidePoint, pSideVoxel);

								for (uint32_t side = 0; side < 2; ++side) {
									if (grading[side]) { // skips grading when finished/side

										if (pSideVoxel[side]) {
											Iso::Voxel oSideVoxel(*pSideVoxel[side]);

											if (Iso::isRoad(oSideVoxel)) {

												if (Iso::isRoadEdge(oSideVoxel)) {
													pushHistory(UndoVoxel(sidePoint[side], oSideVoxel));

													uint32_t const side_direction(Iso::getRoadDirection(oSideVoxel));

													bool begin_start;

													switch (side_direction)
													{
													case Iso::ROAD_DIRECTION::N:
													case Iso::ROAD_DIRECTION::E:

														begin_start = side;
														break;
													case Iso::ROAD_DIRECTION::S:
													case Iso::ROAD_DIRECTION::W:

														begin_start = !side;
														break;
													} // end switch

													int32_t grade_heightstep;

													if (begin_start) {

														Iso::setRoadHeightStepBegin(oSideVoxel, currentSideHeightStep[side]);

														int32_t const targetHeightStep(Iso::getRoadHeightStepEnd(oSideVoxelSaved[side]));
														float const current = SFM::smoothstep(float(startHeightStep), float(targetHeightStep), float(step - 1) / float(total_steps[side]));

														grade_heightstep = SFM::round_to_i32(current);

														Iso::setRoadHeightStepEnd(oSideVoxel, grade_heightstep);

													}
													else {

														Iso::setRoadHeightStepEnd(oSideVoxel, currentSideHeightStep[side]);

														int32_t const targetHeightStep(Iso::getRoadHeightStepBegin(oSideVoxelSaved[side]));
														float const current = SFM::smoothstep(float(startHeightStep), float(targetHeightStep), float(step - 1) / float(total_steps[side]));

														grade_heightstep = SFM::round_to_i32(current);

														Iso::setRoadHeightStepBegin(oSideVoxel, grade_heightstep);
													}

													// move ground under road up to match
													int32_t const groundHeightStep = SFM::max(0, SFM::min(grade_heightstep, currentSideHeightStep[side]) - SFM::max(0, SFM::abs((int32_t)Iso::getRoadHeightStepBegin(oSideVoxel) - (int32_t)Iso::getRoadHeightStepEnd(oSideVoxel) - 1)));
													ConditionRoadGround(sidePoint[side], side_direction, groundHeightStep, oSideVoxel);

													// done.
													world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));

													currentSideHeightStep[side] = grade_heightstep;
												}
												else {
													grading[side] = false; // node
												}
											}
											else {
												grading[side] = false; // end of road
											}
										}
										else {
											grading[side] = false; // end of world
										}
									}
								}

								++step;
							}

						} while (grading[0] | grading[1]);
					} 


				} // centernode
				// done.
			} // !existing
		}
		else
			break;

		if (!bExistingCancel) {
			segments.push_back(currentSegment);
			currentSegment.h0 = currentSegment.h1; // this is the only place h0 should ever be updated
		}

		currentPoint = p2D_add(currentPoint, direction); // next
		currentSegment.origin.v = currentPoint.v;

	} // end while (elevation)

	if (segments.empty()) {
#if defined(DEBUG_ROAD_SEGMENTS)
		FMT_LOG_DEBUG("No Segments, aborting...");
#endif
		return(false);
	}

#if defined(DEBUG_ROAD_SEGMENTS)
	FMT_LOG_DEBUG("Segment Elevation Complete, segments({:d})", segments.size());
#endif

#if defined(DEBUG_ROAD_SEGMENTS)
	uint32_t target_count(0);
#endif

	vector<ROAD_SEGMENT>::const_iterator iter(segments.cbegin());
	vector<ROAD_SEGMENT>::const_iterator target(segments.cend());

	ptrdiff_t target_to_start(0);

	int32_t prevHeightStep(iter->h0), currentHeightStep(prevHeightStep);
	int32_t targetHeightStep(currentHeightStep), startHeightStep(currentHeightStep);

	// everything but auto tiling
	for (; iter != segments.cend(); ++iter)
	{
		currentPoint.v = iter->origin.v;

		if (segments.cend() == target) {

#if defined(DEBUG_ROAD_SEGMENTS)
			FMT_LOG_DEBUG("Target is Empty, finding...");
#endif

			int32_t pending_targetHeightStep(0);
			vector<ROAD_SEGMENT>::const_iterator const pending_target = findTargetRoadSegment(iter, segments.cend(), currentHeightStep, pending_targetHeightStep);

			if (segments.cend() != pending_target) {

				// intersection always becomes target //
				target = pending_target;
				targetHeightStep = pending_targetHeightStep;
				// common //
				target_to_start = target - iter; startHeightStep = currentHeightStep;
			}
		}

		if (iter == target) {
#if defined(DEBUG_ROAD_SEGMENTS)
			FMT_LOG_DEBUG("Current Segment is Target!");
#endif

			// this allows next target to be calculated
			currentHeightStep = targetHeightStep;
			target = segments.cend();
#if defined(DEBUG_ROAD_SEGMENTS)
			++target_count;
#endif
		}


		// apply  
		if (segments.cend() != target)
		{
			if (currentHeightStep != targetHeightStep) {

				ptrdiff_t const target_to_current = target - iter;
				float const current = SFM::smoothstep(float(startHeightStep), float(targetHeightStep), 1.0f - (float(target_to_current) / float(target_to_start)));

				currentHeightStep = SFM::round_to_i32(current);

			}
		}

		currentHeightStep = SFM::max(0, SFM::min(int32_t(Iso::MAX_HEIGHT_STEP), currentHeightStep));

		bool const isNodeSegment(iter->node);

		Iso::Voxel oVoxel(*world::getVoxelAt(currentPoint));  // already known good point has voxel 

		// Send to GRID begins, all state is set //
		Iso::setType(oVoxel, Iso::TYPE_EXTENDED);
		Iso::setExtendedType(oVoxel, Iso::EXTENDED_TYPE_ROAD);
		Iso::resetHash(oVoxel, Iso::GROUND_HASH); // reset hash

		Iso::setRoadHeightStepBegin(oVoxel, prevHeightStep);
		Iso::setRoadHeightStepEnd(oVoxel, currentHeightStep);
		Iso::setRoadDirection(oVoxel, encoded_direction);
#ifndef NDEBUG // for debugging purposes only
		Iso::setRoadTile(oVoxel, Iso::ROAD_TILE::SELECT);
#endif
		if (isNodeSegment) {
			Iso::setAsRoadNode(oVoxel, Iso::ROAD_NODE_TYPE::INVALID, false); // set to invalid, autotile returns the proper type later...
		}
		else { // edge
			Iso::setAsRoadEdge(oVoxel);
		}

		Iso::setPending(oVoxel); // in "constructing" state, not fully committed to grid

		// move ground under road up to match
		int32_t const groundHeightStep = SFM::min(prevHeightStep, currentHeightStep);
		ConditionRoadGround(currentPoint, encoded_direction, groundHeightStep, oVoxel);

		Iso::setAsOwner(oVoxel, Iso::GROUND_HASH);  // *important* set here *only*
		
		Iso::setEmissive(oVoxel);

		// done!
		world::setVoxelAt(currentPoint, std::forward<Iso::Voxel const&& __restrict>(oVoxel));

		prevHeightStep = currentHeightStep;

	} // end for (all segments)
	
#if defined(DEBUG_ROAD_SEGMENTS)
	FMT_NUKLEAR_DEBUG(false, "                        {:d} targets", target_count);
#endif

	return(true);
}

static auto const __vectorcall autotile(point2D_t const currentPoint, uint32_t const encoded_direction, bool const node)
{
	typedef struct
	{
		uint32_t const road_tile,
					   road_node_type;
		bool		   center;

	} const Returned;

	uint32_t roadNodeType(Iso::ROAD_NODE_TYPE::INVALID);

	point2D_t const centerPoint(snap_to_nearest_node(currentPoint, 1, &roadNodeType));

	point2D_t sidePoint[2]{};
	Iso::Voxel const* pSideVoxel[2]{ nullptr };

	if (!node) { // edges

		if (roadNodeType <= Iso::ROAD_NODE_TYPE::XING) {  // intersections only have xing tiles

			point2D_t const absDiff(p2D_abs(p2D_sub(centerPoint, currentPoint)));

			if ((Iso::SEGMENT_SIDE_WIDTH + 1) == absDiff.x || (Iso::SEGMENT_SIDE_WIDTH + 1) == absDiff.y) {
#if defined(DEBUG_AUTOTILE)
				FMT_LOG_DEBUG("### Edge Xing");
#endif
				return(Returned{ Iso::ROAD_TILE::XING, Iso::ROAD_NODE_TYPE::INVALID, false });
			}
		}
	}
	else if (Iso::ROAD_NODE_TYPE::INVALID != roadNodeType) { // nodes

#if defined(DEBUG_AUTOTILE)
		FMT_LOG_DEBUG("### Node type({:s})", getNodeTypeText(roadNodeType));
#endif

		if (roadNodeType <= Iso::ROAD_NODE_TYPE::XING) {  // xing 

#if defined(DEBUG_AUTOTILE)
			FMT_LOG_DEBUG("crossing/intersection");
#endif

			return(Returned{ Iso::ROAD_TILE::FLAT, roadNodeType, (centerPoint == currentPoint)});
		}
		else if (roadNodeType <= Iso::ROAD_NODE_TYPE::CORNER) { // corner

			bool bValid(false);
			uint32_t curve_index(Iso::ROAD_SEGMENT_WIDTH);

			// determine where inline what curved tile index we are.
			for (int32_t i = 0; i < Iso::ROAD_SEGMENT_WIDTH; ++i) {
				
				getAdjacentSides<false>(currentPoint, encoded_direction, i, sidePoint, pSideVoxel); 
				
				// index into sidePoint & pSideVoxel that would be the next point in the same direction/heading
				uint32_t next_index; // next index determination
				switch (encoded_direction)
				{
				case Iso::ROAD_DIRECTION::N:
				case Iso::ROAD_DIRECTION::S:
					next_index = uint32_t(Iso::ROAD_DIRECTION::S == encoded_direction);
					break;
				case Iso::ROAD_DIRECTION::E:
				case Iso::ROAD_DIRECTION::W:
					next_index = uint32_t(Iso::ROAD_DIRECTION::W == encoded_direction);
					break;
				} // end switch

				if (pSideVoxel[next_index]) {

					Iso::Voxel const oSideVoxel(*pSideVoxel[next_index]);

					if (Iso::isRoad(oSideVoxel)) {

						bValid = true;

						if (0 == --curve_index)
							break;
					}
					else
						break; // road ended in the inline direction taken
				}
			}

			if (bValid) {

#if defined(DEBUG_AUTOTILE)
				FMT_LOG_DEBUG("corner valid index({:d})", curve_index);
#endif

				switch (roadNodeType)
				{
				case Iso::ROAD_NODE_TYPE::CORNER_BL:
					return(Returned{ curve_index + Iso::ROAD_TILE::CURVED_0, roadNodeType, false });
				case Iso::ROAD_NODE_TYPE::CORNER_TR:
					return(Returned{ ((Iso::ROAD_SEGMENT_WIDTH - 1) - curve_index) + Iso::ROAD_TILE::CURVED_0 + Iso::ROAD_SEGMENT_WIDTH + Iso::ROAD_SEGMENT_WIDTH, roadNodeType, false });

				case Iso::ROAD_NODE_TYPE::CORNER_TL:
				{ // orientation bug fix:
					Iso::Voxel const* const pVoxel = world::getVoxelAt(currentPoint);
					if (nullptr != pVoxel) {

						Iso::Voxel const oVoxel(*pVoxel);
						switch (Iso::getRoadDirection(oVoxel))
						{
						case Iso::ROAD_DIRECTION::N:
						case Iso::ROAD_DIRECTION::S:
							return(Returned{ ((Iso::ROAD_SEGMENT_WIDTH - 1) - curve_index) + Iso::ROAD_TILE::CURVED_0 + Iso::ROAD_SEGMENT_WIDTH + Iso::ROAD_SEGMENT_WIDTH + Iso::ROAD_SEGMENT_WIDTH, roadNodeType, false });
						case Iso::ROAD_DIRECTION::E:
						case Iso::ROAD_DIRECTION::W:
							return(Returned{ curve_index + Iso::ROAD_TILE::CURVED_0 + Iso::ROAD_SEGMENT_WIDTH, roadNodeType, false });
						}
					}
				}
				break;
				case Iso::ROAD_NODE_TYPE::CORNER_BR:
				{ // orientation bug fix:
					Iso::Voxel const* const pVoxel = world::getVoxelAt(currentPoint);
					if (nullptr != pVoxel) {

						Iso::Voxel const oVoxel(*pVoxel);
						switch (Iso::getRoadDirection(oVoxel))
						{
						case Iso::ROAD_DIRECTION::N:
						case Iso::ROAD_DIRECTION::S:
							return(Returned{ curve_index + Iso::ROAD_TILE::CURVED_0 + Iso::ROAD_SEGMENT_WIDTH, roadNodeType, false });
						case Iso::ROAD_DIRECTION::E:
						case Iso::ROAD_DIRECTION::W:
							return(Returned{ ((Iso::ROAD_SEGMENT_WIDTH - 1) - curve_index) + Iso::ROAD_TILE::CURVED_0 + Iso::ROAD_SEGMENT_WIDTH + Iso::ROAD_SEGMENT_WIDTH + Iso::ROAD_SEGMENT_WIDTH, roadNodeType, false });
						}
					}
				}
				break;
				}

			}
		}
	}

	// always returning straight as default
#if defined(DEBUG_AUTOTILE)
	FMT_LOG_DEBUG("### Edge");
#endif
	return(Returned{ Iso::ROAD_TILE::STRAIGHT, Iso::ROAD_NODE_TYPE::INVALID, false });
}

void cRoadTool::autotileRoadHistory()
{
	for (vector<sUndoVoxel>::const_reverse_iterator iter = getHistory().crbegin(); iter != getHistory().crend(); ++iter)
	{
		point2D_t const voxelIndex(iter->voxelIndex);

		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {
			Iso::Voxel oVoxel(*pVoxel);

			if (Iso::isRoad(oVoxel)) {

				auto const [road_tile, road_node_type, center] = autotile(voxelIndex, Iso::getRoadDirection(oVoxel), Iso::isRoadNode(oVoxel));

				Iso::setRoadTile(oVoxel, road_tile);

				// bugfix: force all straight segments as edges
				// straight to straight can sometimes be a node if user was drawing them overlapped
				if (Iso::ROAD_TILE::STRAIGHT == Iso::getRoadTile(oVoxel)) {
					Iso::setAsRoadEdge(oVoxel);
				}
				
				if (Iso::isRoadNode(oVoxel)) {
					Iso::setAsRoadNode(oVoxel, road_node_type, center); // here the proper type is set
				}

				// done!
				world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
			}
		}
	}
}

static void __vectorcall decorate_xing(point2D_t const currentPoint, Iso::Voxel const& __restrict oVoxel,
								       vector<sUndoVoxel>& __restrict undoHistory,
								       vector<uint32_t>& __restrict undoSignage,
									   int64_t const seed)
{
	static constexpr int32_t const XING_OFFSET = Iso::SEGMENT_SIDE_WIDTH + 1;

	uint32_t const roadNodeType(Iso::getRoadNodeType(oVoxel));

	// only proceed if node is the center of a xing
	if (roadNodeType <= Iso::ROAD_NODE_TYPE::XING) {

		v2_rotation_t xingRotation[4];
		point2D_t xingPoint[4]{};
		uint32_t xingCount(3);

		switch (roadNodeType)
		{
		case Iso::ROAD_NODE_TYPE::XING_ALL:
			xingPoint[0] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, -XING_OFFSET));	// TOP
			xingPoint[1] = p2D_add(currentPoint, point2D_t(XING_OFFSET, -XING_OFFSET));		// LEFT
			xingPoint[2] = p2D_add(currentPoint, point2D_t(XING_OFFSET, XING_OFFSET));		// BOTTOM
			xingPoint[3] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, XING_OFFSET));		// RIGHT

			xingRotation[0] = -v2_rotation_constants::v180;		// TOP
			xingRotation[1] = -v2_rotation_constants::v90;		// LEFT
			xingRotation[2] = v2_rotation_t{};					// BOTTOM
			xingRotation[3] = v2_rotation_constants::v90;		// RIGHT

			++xingCount;
			break;
		case Iso::ROAD_NODE_TYPE::XING_RTL:
			xingPoint[0] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, XING_OFFSET));		// RIGHT
			xingPoint[1] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, -XING_OFFSET));	// TOP
			xingPoint[2] = p2D_add(currentPoint, point2D_t(XING_OFFSET, -XING_OFFSET));		// LEFT	

			xingRotation[0] = v2_rotation_constants::v90;		// RIGHT
			xingRotation[1] = -v2_rotation_constants::v180;		// TOP
			xingRotation[2] = -v2_rotation_constants::v90;		// LEFT
			
			break;
		case Iso::ROAD_NODE_TYPE::XING_TLB:
			xingPoint[0] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, -XING_OFFSET));	// TOP
			xingPoint[1] = p2D_add(currentPoint, point2D_t(XING_OFFSET, -XING_OFFSET));		// LEFT
			xingPoint[2] = p2D_add(currentPoint, point2D_t(XING_OFFSET, XING_OFFSET));		// BOTTOM

			xingRotation[0] = -v2_rotation_constants::v180;		// TOP
			xingRotation[1] = -v2_rotation_constants::v90;		// LEFT
			xingRotation[2] = v2_rotation_t{};					// BOTTOM
			break;
		case Iso::ROAD_NODE_TYPE::XING_LBR:
			xingPoint[0] = p2D_add(currentPoint, point2D_t(XING_OFFSET, -XING_OFFSET));		// LEFT
			xingPoint[1] = p2D_add(currentPoint, point2D_t(XING_OFFSET, XING_OFFSET));		// BOTTOM
			xingPoint[2] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, XING_OFFSET));		// RIGHT

			xingRotation[0] = -v2_rotation_constants::v90;		// LEFT
			xingRotation[1] = v2_rotation_t{};					// BOTTOM
			xingRotation[2] = v2_rotation_constants::v90;		// RIGHT
			break;
		case Iso::ROAD_NODE_TYPE::XING_BRT:
			xingPoint[0] = p2D_add(currentPoint, point2D_t(XING_OFFSET, XING_OFFSET));		// BOTTOM
			xingPoint[1] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, XING_OFFSET));		// RIGHT
			xingPoint[2] = p2D_add(currentPoint, point2D_t(-XING_OFFSET, -XING_OFFSET));	// TOP		

			xingRotation[0] = v2_rotation_t{};					// BOTTOM
			xingRotation[1] = v2_rotation_constants::v90;		// RIGHT
			xingRotation[2] = -v2_rotation_constants::v180;		// TOP
			break;
		default:
			return; // invalid node?
		}

		using flags = Volumetric::eVoxelModelInstanceFlags;
		world::cTrafficSignGameObject* pTrafficSignGameObject[4]{ nullptr };
		uint32_t traffic_signs(0);

		SetSeed(seed); // so each traffic sign has different chance of having extra signage, this is placed here

		bool bExistingXing(false);

		// place the traffic signs for the traffic control
		for (uint32_t xing = 0; xing < xingCount; ++xing) {

			Iso::Voxel const* pVoxel = world::getVoxelAt(xingPoint[xing]);
			if (pVoxel) {

				Iso::Voxel oXingVoxel(*pVoxel);

				bool bExistingTrafficSign(false);

				// ensure that it does not already exist
				if (Iso::isOwnerAny(oXingVoxel, Iso::DYNAMIC_HASH)) {
					for (uint32_t i = Iso::DYNAMIC_HASH; i < Iso::HASH_COUNT; ++i) {
						if (0 != oXingVoxel.Hash[i]) {
							auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<true>(oXingVoxel.Hash[i]);
							if (instance) {
								auto const identity = instance->getModel().identity();
								if (Volumetric::eVoxelModels_Dynamic::MISC == identity._modelGroup
									&& Volumetric::eVoxelModels_Indices::TRAFFIC_SIGN == identity._index) {

									// already has traffic sign in this location, user is modifying / extending an existing xing
									bExistingTrafficSign = true;
									break;
								}
							}
						}
					}
				}

				if (!bExistingTrafficSign) {
					undoHistory.emplace_back(xingPoint[xing], oXingVoxel);

					// make ground match height of road where traffic sign will be placed
					Iso::setHeightStep(oXingVoxel, Iso::getHeightStep(oVoxel));
					world::setVoxelAt(xingPoint[xing], std::forward<Iso::Voxel const&& __restrict>(oXingVoxel));
					world::recomputeGroundAdjacencyOcclusion(xingPoint[xing]);

					world::cTrafficSignGameObject* const pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cTrafficSignGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(
						xingPoint[xing],
						Volumetric::eVoxelModels_Indices::TRAFFIC_SIGN,
						flags::INSTANT_CREATION);

					if (pGameObject) {
						pGameObject->getModelInstance()->setAzimuth(xingRotation[xing]);
						undoSignage.emplace_back(pGameObject->getModelInstance()->getHash());

						pTrafficSignGameObject[traffic_signs] = pGameObject;
						++traffic_signs;
					}
				}

				bExistingXing |= bExistingTrafficSign;
			}
		}

		// controller //
		if (traffic_signs > 0) {

			world::cTrafficControlGameObject* pGameObject(nullptr);
			if (!bExistingXing && traffic_signs > 2) {

				pGameObject = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cTrafficControlGameObject, Volumetric::eVoxelModels_Static::EMPTY>(
					currentPoint,
					Volumetric::eVoxelModels_Indices::EMPTY,
					flags::INSTANT_CREATION | flags::EMPTY_INSTANCE);



				if (pGameObject) {
					undoSignage.emplace_back(pGameObject->getModelInstance()->getHash());
				}
			}
			else {

				// find the "empty" instance which is the faux static render instance to also get its set gameobject, which is a cTrafficControlGameObject
				// static "empty" instances are never an owner for the voxel
				uint32_t const hash = oVoxel.Hash[Iso::STATIC_HASH];
				if (0 != hash) {
					auto const instance = MinCity::VoxelWorld->lookupVoxelModelInstance<false>(hash);
					if (instance) {
						auto const identity = instance->getModel().identity();
						if (Volumetric::eVoxelModels_Static::EMPTY == identity._modelGroup
							&& Volumetric::eVoxelModels_Indices::EMPTY == identity._index) {

							pGameObject = instance->getOwnerGameObject<world::cTrafficControlGameObject>();
						}
					}
				}
			}

			// add new signs to existing, or adds all signs to new. controller.
			if (pGameObject) {

				for (uint32_t i = 0; i < traffic_signs; ++i) {

					pGameObject->Add(pTrafficSignGameObject[i]);
				}
			}
		}
	}
}

static uint32_t __vectorcall decorate_lamppost(point2D_t const currentPoint, Iso::Voxel const& __restrict oVoxel, 
											   bool const lamp_post_side, bool& lamp_post_swapped_for_sign,
											   int64_t const seed,
											   vector<sUndoVoxel>& __restrict undoHistory)
{
	static constexpr int32_t const LAMP_POST_OFFSET = Iso::SEGMENT_SIDE_WIDTH + 1;

	uint32_t const encoded_direction(Iso::getRoadDirection(oVoxel));

	// *** rotation applies to lamppost and road signs
	point2D_t sidePoint[2]{};
	Iso::Voxel const* pSideVoxel[2]{ nullptr };

	bool const bNSEW = getAdjacentSides(currentPoint, encoded_direction, LAMP_POST_OFFSET, sidePoint, pSideVoxel);

	v2_rotation_t rotation;
	uint32_t const side(lamp_post_side);

	if (pSideVoxel[side]) {

		Iso::Voxel oSideVoxel(*pSideVoxel[side]);
		undoHistory.emplace_back(sidePoint[side], oSideVoxel);

		// make ground match height of road where lamp/sign will be placed
		Iso::setHeightStep(oSideVoxel, Iso::getHeightStep(oVoxel));
		world::setVoxelAt(sidePoint[side], std::forward<Iso::Voxel const&& __restrict>(oSideVoxel));
		world::recomputeGroundAdjacencyOcclusion(sidePoint[side]);

		if (bNSEW) {
			rotation = lamp_post_side ? rotation : v2_rotation_constants::v180;
		}
		else { 
			rotation = lamp_post_side ? v2_rotation_constants::v90 : v2_rotation_constants::v270;
		}

		using flags = Volumetric::eVoxelModelInstanceFlags;

		if (!lamp_post_swapped_for_sign) {

			SetSeed(seed);
			if (PsuedoRandomNumber32(0, 100) < ROAD_SIGNAGE_CHANCE) {

				// place road sign - uses rotation only, want centered on road
				world::cSignageGameObject* const pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cSignageGameObject, Volumetric::eVoxelModels_Dynamic::MISC>(
					currentPoint,
					Volumetric::eVoxelModels_Indices::ROAD_SIGN,
					flags::INSTANT_CREATION);

				if (pGameObject) {
					pGameObject->getModelInstance()->setAzimuth(rotation);
					lamp_post_swapped_for_sign = true;
					return(pGameObject->getModelInstance()->getHash());
				}
			}
		}
		
		// place lamp post - uses rotation & offset from center of road sidePoint[side]
		auto const [hash, instance] = MinCity::VoxelWorld->placeVoxelModelInstanceAt<Volumetric::eVoxelModels_Dynamic::MISC>(sidePoint[side], Volumetric::eVoxelModels_Indices::LAMP_POST,
			flags::INSTANT_CREATION);
		if (instance) {
			instance->setAzimuth(rotation);
			return(hash);
		}
	}

	return(0);
}

void cRoadTool::decorateRoadHistory()
{
	static constexpr uint32_t const LAMP_POST_INTERVAL(Iso::ROAD_SEGMENT_WIDTH << 1);

	vector<sUndoVoxel>	undoHistory; // any modifications to voxel grid ground while decorating get's appended to main _undoHistory
									 // *after* iterations of current road history elements

	bool lamp_post_side(false),
		 lamp_post_swapped_for_sign(false);

	uint32_t pendingSign(0);
	uint32_t segment_count(0);
	uint32_t intersection_count(0);

	for (vector<sUndoVoxel>::const_iterator iter = getHistory().cbegin(); iter != getHistory().cend(); ++iter)
	{
		point2D_t const voxelIndex(iter->voxelIndex);

		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {
			Iso::Voxel oVoxel(*pVoxel);

			if (Iso::isRoad(oVoxel)) {

				if (Iso::isRoadNode(oVoxel)) {
					
					// "remove" any lamp posts / signage in area of xing
					rect2D_t area(0, 0, Iso::ROAD_SEGMENT_WIDTH << 1, Iso::ROAD_SEGMENT_WIDTH << 1);
					area = r2D_add(area, voxelIndex);
					area = r2D_sub(area, point2D_t(Iso::ROAD_SEGMENT_WIDTH, Iso::ROAD_SEGMENT_WIDTH));

					MinCity::VoxelWorld->hideVoxelModelInstancesAt(area, Volumetric::eVoxelModels_Dynamic::MISC, Volumetric::eVoxelModels_Indices::LAMP_POST, &_undoExistingSignage);
					MinCity::VoxelWorld->hideVoxelModelInstancesAt(area, Volumetric::eVoxelModels_Dynamic::MISC, Volumetric::eVoxelModels_Indices::ROAD_SIGN, &_undoExistingSignage);

					if (Iso::isRoadNodeCenter(oVoxel)) {

						if (0 != pendingSign) { // cancel last lamppost
							MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(pendingSign);
							pendingSign = 0;
						}
						lamp_post_side = !lamp_post_side;
						segment_count = 0;

						decorate_xing(voxelIndex, oVoxel, undoHistory, _undoSignage, _seed_traffic_sign);
						++intersection_count;
					}
				}
				else {

					if (++segment_count >= LAMP_POST_INTERVAL) {
						
						if (0 != pendingSign) { // commit last lamppost
							_undoSignage.push_back(pendingSign);
						}

						// test area for existing lamppost or road signs, if the exist, skip.
						rect2D_t area(0, 0, LAMP_POST_INTERVAL << 1, LAMP_POST_INTERVAL << 1);
						area = r2D_add(area, voxelIndex);
						area = r2D_sub(area, point2D_t(LAMP_POST_INTERVAL, LAMP_POST_INTERVAL));

						uint32_t const hashLamp = MinCity::VoxelWorld->hasVoxelModelInstanceAt(area, Volumetric::eVoxelModels_Dynamic::MISC, Volumetric::eVoxelModels_Indices::LAMP_POST);
						if (0 == hashLamp || hashLamp == pendingSign) {

							uint32_t const hashSign = MinCity::VoxelWorld->hasVoxelModelInstanceAt(area, Volumetric::eVoxelModels_Dynamic::MISC, Volumetric::eVoxelModels_Indices::ROAD_SIGN);
							if (0 == hashSign || hashSign == pendingSign) {

								pendingSign = decorate_lamppost(voxelIndex, oVoxel, lamp_post_side, lamp_post_swapped_for_sign, _seed_signage, undoHistory);
							}
						}

						lamp_post_side = !lamp_post_side;
						segment_count = 0;
					}
				}
			}

		}

	}

	if (0 != pendingSign) { // commit last lamppost
		_undoSignage.push_back(pendingSign);
	}

	// move any changed voxel grid history
	pushHistory(std::forward<vector<sUndoVoxel>&&>(undoHistory));
}

void __vectorcall cRoadTool::DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart)
{
	static constexpr uint32_t const color(gui::color);  // abgr - rgba backwards

	if (0 != _activePoint) { // have starting index?

		static point2D_t lastEndPoint;
		point2D_t clampedEndPoint(getHoveredVoxelIndexSnapped().v);

		if (clampedEndPoint != lastEndPoint) {
			lastEndPoint = clampedEndPoint;

			undoRoadHistory();

			search_and_select_closest_road(clampedEndPoint);  // while dragging..... done b4 any new road is placed to exclude it from being selected

			point2D_t const selectedEndPoint(_selected.origin);
			if (!selectedEndPoint.isZero()) { // snapped to nearby road?
				clampedEndPoint = selectedEndPoint;	// this prevents diagonal road as the axis are clamped next
													// and the axis max will still be the same
			}

			highlightArea(rect2D_t(p2D_subs(clampedEndPoint, Iso::SEGMENT_SIDE_WIDTH), p2D_adds(clampedEndPoint, Iso::SEGMENT_SIDE_WIDTH + 1)), color);

			{
				point2D_t const abs_difference = p2D_abs(p2D_sub(clampedEndPoint, _segmentVoxelIndex[0]));

				if (abs_difference.isZero())	// filter out changes with no length
					return;

				if (abs_difference.x > abs_difference.y) {

					// lock on xaxis
					clampedEndPoint.y = _segmentVoxelIndex[0].y;
				}
				else if (abs_difference.x < abs_difference.y) {

					// lock on yaxis
					clampedEndPoint.x = _segmentVoxelIndex[0].x;
				}
				else {
					return;
				}
			}

#if defined(DEBUG_AUTOTILE) || defined(DEBUG_ROAD_SEGMENTS)
			FMT_LOG_DEBUG("-----------------------------------------------------------------\n NEW ENDPOINT:  ({:d},{:d})", clampedEndPoint.x, clampedEndPoint.y);
#endif

			if (CreateRoadSegments(_segmentVoxelIndex[0], clampedEndPoint)) {

				if (!selectedEndPoint.isZero()) { // snapped to nearby road?

					if (selectedEndPoint != clampedEndPoint) {  // there is another road section too draw, this should always be different

						// no clamping of new enpoint required as previous clamped endpoint was limited on what ever axis locked, to the corresponding value in _selectedRoadIndex
						// Only need to draw out the axis from that previous endpoint to the selected / snapped to endpoint selected by user
						// this results in a L - road shape
						point2D_t direction, newEndPoint;

						// do overlap to produce the node (xing/corner)

						// extend original road segment, so it will overlap the new road segment
						// *bugfix - this must be last so original road direction is the last direction assigned to any voxels
						// this overlap causes. Fixes jumpy behaviour when an intersection exists and retains correct road direction
						direction = p2D_sgn(p2D_sub(clampedEndPoint, _segmentVoxelIndex[0]));
						newEndPoint = p2D_add(clampedEndPoint, p2D_muls(direction, Iso::SEGMENT_SIDE_WIDTH + 1));

						CreateRoadSegments(clampedEndPoint, newEndPoint);

						// new road segment
						direction = p2D_sgn(p2D_sub(clampedEndPoint, selectedEndPoint));
						newEndPoint = p2D_sub(selectedEndPoint, p2D_muls(direction, Iso::SEGMENT_SIDE_WIDTH + 1));

						CreateRoadSegments(clampedEndPoint, newEndPoint);
					}
				}

				// autotiling for any modified tiles, this leaves the undohistory unmodified (constant)
				// but can change the grid's "road tile" with no further undo history needed for these changes
				autotileRoadHistory();

				// Add things to road, like road signs, lamp posts, and traffic control
				decorateRoadHistory();

				_segmentVoxelIndex[_activePoint].v = clampedEndPoint.v;
			}
			else {
				undoRoadHistory();
			}
		}
	}
}

void cRoadTool::paint()
{
	cAbstractToolMethods::paint();

	// make relative to world origin (gridspace to worldspace transform)
	XMVECTOR const xmWorldOrigin(world::getOrigin());
}

// these functions should be defined last
void cRoadTool::deactivate()
{
	deselect_road_intersect(); // reset
	undoRoadHistory();

#if defined(DEBUG_AUTOTILE) || defined(DEBUG_ROAD_SEGMENTS)
	MinCity::Nuklear->clearAllPopupText();
#endif

}

void cRoadTool::activate()
{
}

