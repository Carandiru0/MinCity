#include "pch.h"
#include "cZoningTool.h"
#include "cVoxelWorld.h"
#include "cBuildingGameObject.h"
#include "MinCity.h"
#include "prices.h"
#include "gui.h"

static constexpr float const MIN_VISIBILITY = 0.95f;
static constexpr float const GUI_HEIGHT = -100.0f;
static inline v2_rotation_t const _offsetAngle{ v2_rotation_constants::v15 }; // starting offset for text alignment

cZoningTool::cZoningTool()
	: _ActivatedSubTool(eSubTool_Zoning::RESIDENTIAL), _segmentVoxelIndex{}, _activePoint(1)  // must be non-zero
{

}

void cZoningTool::setActivatedSubTool(uint32_t const subtool)
{ 
	_ActivatedSubTool = subtool; 

	switch (_ActivatedSubTool)
	{
	case eSubTool_Zoning::RESIDENTIAL:
		setCost(ePrices::RESIDENTIAL);
		break;
	case eSubTool_Zoning::COMMERCIAL:
		setCost(ePrices::COMMERCIAL);
		break;
	case eSubTool_Zoning::INDUSTRIAL:
		setCost(ePrices::INDUSTRIAL);
		break;
	default:
		setCost(ePrices::ZERO);
		break;
	}
}
void cZoningTool::setCost(int64_t const cost)
{
	_cost = cost;
}

void cZoningTool::paint()
{
	cAbstractToolMethods::paint();
	/*
	static constexpr uint32_t const color(gui::color);  // abgr - rgba backwards

	constinit static bool bFindMaxVisibility(false);

	// make relative to world origin (gridspace to worldspace transform)
	XMVECTOR const xmWorldOrigin(world::getOrigin());

	if (_activePoint > 1) { // have starting & ending index?

		rect2D_t const area(orientAreaToRect(_segmentVoxelIndex[0], _segmentVoxelIndex[1]));

		//draw_grid(area, Iso::SEGMENT_SIDE_WIDTH, 120);

		// calculate area cost
		money_t const total_cost(_cost.amount * area.width() * area.height());

		XMVECTOR xmOrigin;
		point2D_t origin;
		uint32_t length(0);

		// line from tl to tr (x axis)
		origin = area.left_top();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		length = (area.right - area.left) << 1; // needs to be in minivoxels not voxels, hence the doubling of length todo the conversion.
		//gui::draw_line(gui::axis::x, xmOrigin, origin, color, length, gui::flags::emissive);

		// line from bl to br (x axis)
		origin = area.left_bottom();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		// same length = (area.right - area.left) << 1;
		//gui::draw_line(gui::axis::x, xmOrigin, origin, color, length + 1, gui::flags::emissive);

		// line from tl to bl (z axis)
		origin = area.left_top();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		length = (area.bottom - area.top) << 1; // needs to be in minivoxels not voxels, hence the doubling of length todo the conversion.
		//gui::draw_line(gui::axis::z, xmOrigin, origin, color, length, gui::flags::emissive);

		// line from tr to br (z axis)
		origin = area.right_top();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		// same length = (area.bottom - area.top) << 1;
		//gui::draw_line(gui::axis::z, xmOrigin, origin, color, length, gui::flags::emissive);

		v2_rotation_t const view(world::getYaw() + _offsetAngle); // range is -XM_PI to XM_PI or -180 to 180 // offset angle represents the most optimal "angle" offset to switch the gui text.
		point2D_t best_origin;
		uint32_t axis;
		/*
		if (view.angle() < -v2_rotation_constants::v90.angle()) {  // -180 to -90
			best_origin = area.right_top();
			axis = gui::axis::xn;
		}
		else if (view.angle() < 0.0f) { // -90 to 0
			best_origin = area.right_bottom();
			axis = gui::axis::zn;
		}
		else if (view.angle() < v2_rotation_constants::v90.angle()) { // 0 to 90
			best_origin = area.left_bottom();
			axis = gui::axis::x;
		}
		else { // 90 to 180
			best_origin = area.left_top();
			axis = gui::axis::z;
		}
		*/
		/*
		if (bFindMaxVisibility) {

			float visibility(0.0f);
			{ // original
				origin = p2D_add(best_origin, best_axis);
				xmOrigin = p2D_to_v2(origin);
				xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
				xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 2.0f);
				xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin); //green

				auto const [original_visibility, width] = gui::draw_string<true>(xmAxis, xmOrigin, origin, color, gui::flags::emissive, "${:d}", total_cost.amount); // abgr - rgba backwards
				visibility = original_visibility;
			}

			origin = best_origin;

			do
			{ // add
				origin = p2D_add(origin, best_axis);
				xmOrigin = p2D_to_v2(origin);
				xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
				xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 2.0f);
				xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin); //green

				auto const [adjacent_visibility, width] = gui::draw_string<true>(xmAxis, xmOrigin, origin, color, gui::flags::emissive, "${:d}", total_cost.amount); // abgr - rgba backwards
				if (adjacent_visibility > visibility) {
					visibility = adjacent_visibility;
					best_origin = origin;
				}
				else {
					break;
				}

			} while (true);

			bFindMaxVisibility = false; // always reset
		}*/
		/*
		// actually draw the text
		origin = best_origin;
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 2.0f);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin); //green
			
		auto const [quadrant_visibility, width] = gui::draw_string(axis, xmOrigin, color, gui::flags::emissive, "${:d}", total_cost.amount); // abgr - rgba backwards

		// checking required next frame?
		bFindMaxVisibility = (quadrant_visibility < MIN_VISIBILITY);

		// draw the current angle in the center of the square (for debugging)
		v2_rotation_t const vangle(world::getYaw());
		float const angle = XMConvertToDegrees(vangle.angle());
		origin = area.center();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 2.0f);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin); //green
		gui::draw_string(gui::axis::x, xmOrigin, color, gui::flags::emissive, "{:.{}f}", quadrant_visibility, 1); // abgr - rgba backwards
		*/
		/*
		static constexpr fp_seconds const
			interval = fp_seconds(1.0f / 24.0f), // seconds = 1/fps; 0.042s, 42ms per frame		
			total = fp_seconds(seconds(5));

		static struct {
			tTime tLast{ now() };
			fp_seconds accumulator{};
			fp_seconds total_accumulated{};
		} timing;

		float transition(0.0f);

		tTime const tNow(now());
		fp_seconds const fTDelta(tNow - timing.tLast);
		if ((timing.accumulator += fTDelta) >= interval) {

			if ((timing.total_accumulated += timing.accumulator) >= total) {

				timing.total_accumulated -= total;
			}

			timing.accumulator -= interval;
		}
		timing.tLast = tNow;

		origin = area.left_top();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 2.0f);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_horizontal_progress_bar(gui::axis::x, xmOrigin, origin, color, length, timing.total_accumulated / total, gui::flags::emissive);

		origin = area.left_bottom();
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT - 4.0f);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);
		gui::draw_vertical_progress_bar(gui::axis::x, xmOrigin, origin, color, 20, timing.total_accumulated / total, gui::flags::emissive);
		*/
	//}
	//else { // tool is active, however the user has not dragged or clicked the mouse to create an area

		/*
		XMVECTOR xmOrigin;
		point2D_t origin;
		uint32_t length(0);

		// line from tl to tr (x axis)
		origin = hoveredIndex;
		xmOrigin = p2D_to_v2(origin);
		xmOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOrigin);
		xmOrigin = XMVectorSetY(xmOrigin, GUI_HEIGHT);
		xmOrigin = XMVectorSubtract(xmOrigin, xmWorldOrigin);

		length = 50 << 1; // needs to be in minivoxels not voxels, hence the doubling of length todo the conversion.
		gui::draw_line(gui::axis::x, xmOrigin, origin, color, length, gui::flags::emissive);
		*/

		/* addional lighting
		XMVECTOR xmVoxelOrigin = p2D_to_v2(hoveredIndex);
		xmVoxelOrigin = XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmVoxelOrigin);
		xmVoxelOrigin = XMVectorSetY(xmVoxelOrigin, -10.0f);

		xmVoxelOrigin = XMVectorSubtract(xmVoxelOrigin, xmWorldOrigin);

		world::addVoxel(xmVoxelOrigin, hoveredIndex, color, Iso::mini::hidden | Iso::mini::emissive); // light only
		*/
	//}
}

void cZoningTool::commitZoneHistory() // commits current "zone" to grid
{
	undoZoneHistory();

	if (_ActivatedSubTool > 0) {
		rect2D_t const finalArea(orientAreaToRect(_segmentVoxelIndex[0], _segmentVoxelIndex[1]));

		world::zoning::zoneArea(finalArea, _ActivatedSubTool - 1);
	}
}

void cZoningTool::undoZoneHistory() // undo's current "road" from grid
{
	undoHistory();
}

void __vectorcall cZoningTool::PressAction(FXMVECTOR const xmMousePos)
{
	point2D_t const origin(MinCity::VoxelWorld->getHoveredVoxelIndex());

	undoZoneHistory();

	if (0 != _activePoint) { // new
		_activePoint = 0;
		_segmentVoxelIndex[0] = point2D_t{};
		_segmentVoxelIndex[1] = point2D_t{};
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
void __vectorcall cZoningTool::ClickAction(FXMVECTOR const xmMousePos)
{
	constinit static uint32_t lastTool(0),
							  lastCount(0);

	world::cBuildingGameObject* pGameObject(nullptr);

	using flags = Volumetric::eVoxelModelInstanceFlags;
	constexpr uint32_t const common_flags(/*flags::DESTROY_EXISTING_DYNAMIC |*/ flags::DESTROY_EXISTING_STATIC | flags::GROUND_CONDITIONING | flags::INSTANT_CREATION);

	if (lastTool != _ActivatedSubTool) {
		lastCount = 0; // reset 
		lastTool = _ActivatedSubTool;
	}

	switch (_ActivatedSubTool)
	{
	case eSubTool_Zoning::RESIDENTIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_RESIDENTAL>(MinCity::VoxelWorld->getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	case eSubTool_Zoning::COMMERCIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_COMMERCIAL>(MinCity::VoxelWorld->getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	case eSubTool_Zoning::INDUSTRIAL:
		if (lastCount == Volumetric::getVoxelModelCount<Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>())
			lastCount = 0;

		pGameObject = MinCity::VoxelWorld->placeNonUpdateableInstanceAt<world::cBuildingGameObject, Volumetric::eVoxelModels_Static::BUILDING_INDUSTRIAL>(MinCity::VoxelWorld->getHoveredVoxelIndex(),
			lastCount,
			common_flags);

		++lastCount;
		break;
	}	
}

void __vectorcall cZoningTool::DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart)
{
	if (_ActivatedSubTool > 0) { // have selected tool
		
		if (0 != _activePoint) { // have starting index?

			static point2D_t lastEndPoint;
			point2D_t const clampedEndPoint(MinCity::VoxelWorld->getHoveredVoxelIndex());

			if (clampedEndPoint != lastEndPoint) {
				lastEndPoint = clampedEndPoint;

				// clear last highlight
				undoZoneHistory();

				// set highlight
				highlightPerimeter(orientAreaToRect(_segmentVoxelIndex[0], clampedEndPoint), world::ZONING_COLOR[_ActivatedSubTool]);

				_segmentVoxelIndex[1] = clampedEndPoint;
				++_activePoint;
			}
		}
	}
}

void __vectorcall cZoningTool::MouseMoveAction(FXMVECTOR const xmMousePos)
{
	if (_ActivatedSubTool > 0) {  // have selected tool

		if (_activePoint > 1) { // have starting & ending index?

		}
		else { // tool is active, however the user has not dragged or clicked the mouse to create an area

			static point2D_t lastOrigin;
			point2D_t const origin(MinCity::VoxelWorld->getHoveredVoxelIndex());

			if (origin != lastOrigin) { // limited to delta of hovered mouse index. Never misses the action (important).

				static constexpr uint32_t const color(gui::color);  // abgr - rgba backwards

				// clear last highlight
				undoZoneHistory();

				// set highlight
				highlightCross(origin, color);
			}
		}
	}
}

// these functions should be defined last
void cZoningTool::deactivate()
{
	undoZoneHistory();
}

void cZoningTool::activate()
{
}


