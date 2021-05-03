#include "pch.h"
#include "cZoningTool.h"
#include "cVoxelWorld.h"
#include "cBuildingGameObject.h"
#include "MinCity.h"

cZoningTool::cZoningTool()
	: _ActivatedSubTool(eSubTool_Zoning::RESIDENTIAL), _segmentVoxelIndex{}, _activePoint(1) // must be non-zero
{

}
STATIC_INLINE_PURE point2D_t const getHoveredVoxelIndexSnapped()
{
	point2D_t const hoverVoxel(MinCity::VoxelWorld.getHoveredVoxelIndex());

	//point2D_t hoverVoxelSnapped(
	//	SFM::roundToMultipleOf<false>(hoverVoxel.x, (int32_t)Iso::ROAD_SEGMENT_WIDTH),
	//	SFM::roundToMultipleOf<false>(hoverVoxel.y, (int32_t)Iso::ROAD_SEGMENT_WIDTH)
	//);

	//hoverVoxelSnapped = p2D_add(hoverVoxelSnapped, p2D_muls(p2D_sgn(p2D_sub(hoverVoxel, hoverVoxelSnapped)), SEGMENT_SIDE_WIDTH));

	return(hoverVoxel);
}

void cZoningTool::commitZoneHistory() // commits current "road" to grid
{
	_undoHistory.clear();
}

void cZoningTool::clearZoneHistory() // undo's current "road" from grid
{
	// undoing
	// vector is iterated in reverse (newest to oldest) to properly restore the grid voxels
	for (std::vector<sUndoVoxel>::const_reverse_iterator undoVoxel = _undoHistory.crbegin(); undoVoxel != _undoHistory.crend(); ++undoVoxel)
	{
		world::setVoxelAt(undoVoxel->voxelIndex, std::forward<Iso::Voxel const&& __restrict>(undoVoxel->undoVoxel));
	}
	_undoHistory.clear();
}

void cZoningTool::pushZoneHistory(UndoVoxel&& history)
{
	_undoHistory.emplace_back(std::forward<UndoVoxel&&>(history));
}

void __vectorcall cZoningTool::ClickAction(FXMVECTOR const xmMousePos)
{
	static uint32_t lastTool(0),
					lastCount(0);

	world::cBuildingGameObject* pGameObject(nullptr);

	using flags = Volumetric::eVoxelModelInstanceFlags;
	constexpr uint32_t const common_flags(flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING);

	if (lastTool != _ActivatedSubTool) {
		lastCount = 0; // reset 
		lastTool = _ActivatedSubTool;
	}

	switch (_ActivatedSubTool)
	{
	case eSubTool_Zoning::RESIDENTIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld.placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>(MinCity::VoxelWorld.getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	case eSubTool_Zoning::COMMERCIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld.placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>(MinCity::VoxelWorld.getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	case eSubTool_Zoning::INDUSTRIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld.placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>(MinCity::VoxelWorld.getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	default:
		break;
	}	
}

static rect2D_t const __vectorcall orientAreaToRect(point2D_t const start_pt, point2D_t const end_pt)
{
	// minimum = top left, maximum = bottom right
	return(rect2D_t(p2D_min(start_pt, end_pt), p2D_max(start_pt, end_pt)));
}

void __vectorcall cZoningTool::zoneArea(rect2D_t const area)
{
	point2D_t voxelIndex{};

	for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

		for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {

			Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));

			if (pVoxel) {
				Iso::Voxel oVoxel(*pVoxel);

				pushZoneHistory(UndoVoxel(voxelIndex, oVoxel));

				Iso::setEmissive(oVoxel);

				world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
			}
		}

	}
}

void __vectorcall cZoningTool::DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart)
{
	static constexpr milliseconds const LIMIT(20);	// 20ms low-latency response, throttling input dragging updates to acceptable performance level
	static tTime tLast{};

	tTime const tNow(now());

	if (tNow - tLast >= LIMIT) {
		tLast = tNow;

		if (0 != _activePoint) { // have starting index?

			clearZoneHistory();

			point2D_t clampedEndPoint(getHoveredVoxelIndexSnapped().v);

			zoneArea(orientAreaToRect(_segmentVoxelIndex[0], clampedEndPoint));

			_segmentVoxelIndex[_activePoint] = clampedEndPoint;
		}
	}
}

void __vectorcall cZoningTool::PressAction(FXMVECTOR const xmMousePos)
{
	point2D_t origin(getHoveredVoxelIndexSnapped());

	clearZoneHistory();

	if (0 != _activePoint) { // new
		_activePoint = 0;
	}
	_segmentVoxelIndex[_activePoint++].v = origin.v;

	// _segmentVoxelIndex[0] IS ALWAYS EQUAL TO BEGINNING POINT AFTER THIS POINT
}
void __vectorcall cZoningTool::ReleaseAction(FXMVECTOR const xmMousePos)
{
	[[likely]] if (0 != _activePoint) {

		// _segmentVoxelIndex[1] IS ALWAYS EQUAL TO END POINT @ THIS POINT

		commitZoneHistory();
		_activePoint = 0;
	}
}


// these functions should be defined last
void cZoningTool::deactivate()
{
	clearZoneHistory();
}

void cZoningTool::activate()
{

}


