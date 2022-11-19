#include "pch.h"
#include "cAbstractToolMethods.h"
#include "IsoVoxel.h"
#include "world.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

static constexpr fp_seconds const HIGHLIGHT_PULSE = fp_seconds(milliseconds(500));

void cAbstractToolMethods::pushHistory(vector<sUndoVoxel>&& undoHistory)
{
	std::move(undoHistory.begin(), undoHistory.end(), std::back_inserter(_undoHistory));
}

void cAbstractToolMethods::undoHighlights()
{
	// undoing
	// vector is iterated in reverse (newest to oldest) to properly restore the grid voxels
	for (vector<sUndoVoxel>::const_reverse_iterator undoVoxel = _undoHighlight.crbegin(); undoVoxel != _undoHighlight.crend(); ++undoVoxel)
	{
		Iso::Voxel oVoxel(world::getVoxelAt(undoVoxel->voxelIndex));

		if (Iso::isGroundOnly(oVoxel)) { // undo for highlights is specific, only if still ground

			Iso::clearPending(oVoxel);
			Iso::clearEmissive(oVoxel);
			Iso::clearColor(oVoxel);

			world::setVoxelAt(undoVoxel->voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		}
	}

	_undoHighlight.clear();
}
void cAbstractToolMethods::clearHistory()
{
	undoHighlights();

	_undoHistory.clear();
}
void cAbstractToolMethods::undoHistory()
{
	undoHighlights();

	// undoing
	// vector is iterated in reverse (newest to oldest) to properly restore the grid voxels
	for (vector<sUndoVoxel>::const_reverse_iterator undoVoxel = _undoHistory.crbegin(); undoVoxel != _undoHistory.crend(); ++undoVoxel)
	{
		world::setVoxelAt(undoVoxel->voxelIndex, std::forward<Iso::Voxel const&& __restrict>(undoVoxel->undoVoxel));
	}

	clearHistory();
}

void cAbstractToolMethods::paint()
{
	if (_blink) { // only need to "update" when blinking red (required per frame update)
		
		float const tNorm( SFM::triangle_wave(0.0f, 1.0f, getHighlightAmount()) );

		for (vector<sUndoVoxel>::const_iterator highlightedVoxel = _undoHighlight.cbegin(); highlightedVoxel != _undoHighlight.cend(); ++highlightedVoxel)
		{
			point2D_t const voxelIndex(highlightedVoxel->voxelIndex);

			Iso::Voxel oVoxel(world::getVoxelAt(voxelIndex));

			if (Iso::isEmissive(oVoxel) && Iso::isPending(oVoxel)) { // IsPending prevents highlighting occupied voxels

				uint32_t const original_color(Iso::getColor(highlightedVoxel->undoVoxel));

				if (original_color) { // had existing color before being highlighted

					uint32_t const color(SFM::lerp(0, _highlightColor, tNorm));

					Iso::setColor(oVoxel, color);

					world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
				}

				// otherwise if color exists that is not in conflict, which is unmodified still, so no need to do anything- it will be visible if it has a valid color (color != 0)
			}
		}
	}
}
float const cAbstractToolMethods::getHighlightAmount()
{
	tTime const tNow(now());
	fp_seconds const tDelta(tNow - _tLastHighlight);

	if (tDelta > HIGHLIGHT_PULSE) {
		_tLastHighlight = tNow;
	}
	
	return(SFM::saturate(time_to_float(tDelta / HIGHLIGHT_PULSE)));
}

bool const __vectorcall cAbstractToolMethods::highlightVoxel(point2D_t const voxelIndex) // w/o change to ground voxel color
{
	Iso::Voxel oVoxel(world::getVoxelAt(voxelIndex));

	if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) {

		_undoHighlight.emplace_back(voxelIndex, oVoxel);

		Iso::setPending(oVoxel);	// setPending prevents highlighting occupied voxels
		Iso::setEmissive(oVoxel);

		world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		return(true);
	}

	return(false);
}
bool const __vectorcall cAbstractToolMethods::highlightVoxel(point2D_t const voxelIndex, uint32_t const color) // w/ change to ground voxel color
{
	// center
	Iso::Voxel oVoxel(world::getVoxelAt(voxelIndex));

	if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) {

		_undoHighlight.emplace_back(voxelIndex, oVoxel);

		Iso::setPending(oVoxel);	// setPending prevents highlighting occupied voxels
		Iso::setColor(oVoxel, color);
		Iso::setEmissive(oVoxel);

		world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
		return(true);
	}

	return(false);
}
void __vectorcall cAbstractToolMethods::highlightCross(point2D_t const origin, uint32_t const color)
{
	_highlightColor = color;
	_blink = false; // reset
	
	// center
	if (highlightVoxel(origin, color)) {

		point2D_t voxelIndex;
		bool ok;

		// reset
		voxelIndex = origin;
		ok = false;

		// -X
		do {

			voxelIndex = p2D_add(voxelIndex, point2D_t(-1, 0));

			ok = world::isVoxelVisible(voxelIndex);
			if (ok) {
				// current
				Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

				_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
				highlightVoxel(voxelIndex, color);

				// outside
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(0, -1)));
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(0, 1)));
			}

		} while (ok);

		// reset
		voxelIndex = origin;
		ok = false;

		// +X
		do {

			voxelIndex = p2D_add(voxelIndex, point2D_t(1, 0));

			ok = world::isVoxelVisible(voxelIndex);
			if (ok) {
				// current
				Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

				_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
				highlightVoxel(voxelIndex, color);

				// outside
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(0, -1)));
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(0, 1)));
			}

		} while (ok);

		// reset
		voxelIndex = origin;
		ok = false;

		// -Z
		do {

			voxelIndex = p2D_add(voxelIndex, point2D_t(0, -1));

			ok = world::isVoxelVisible(voxelIndex);
			if (ok) {
				// current
				Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

				_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
				highlightVoxel(voxelIndex, color);
				
				// outside
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(-1, 0)));
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(1, 0)));
			}

		} while (ok);

		// reset
		voxelIndex = origin;
		ok = false;

		// +Z
		do {

			voxelIndex = p2D_add(voxelIndex, point2D_t(0, 1));

			ok = world::isVoxelVisible(voxelIndex);
			if (ok) {
				// current
				Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

				_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
				highlightVoxel(voxelIndex, color);
				
				// outside
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(-1, 0)));
				ok = highlightVoxel(p2D_add(voxelIndex, point2D_t(1, 0)));
			}

		} while (ok);
	}
}

void __vectorcall cAbstractToolMethods::highlightArea(rect2D_t area, uint32_t const color)
{
	_highlightColor = color;
	_blink = false; // reset
	
	point2D_t voxelIndex{};

	// current
	for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

		for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {
			
			if (world::isVoxelVisible(voxelIndex)) {
				
				Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

				_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
				highlightVoxel(voxelIndex, color);
			}
		}
	}
	
	//outside (perimeter only)
	area = r2D_grow(area, point2D_t(1));

	for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

		for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {

			if (area.left == voxelIndex.x || area.top == voxelIndex.y || (area.right - 1) == voxelIndex.x || (area.bottom - 1) == voxelIndex.y) {

				if (world::isVoxelVisible(voxelIndex)) {
					highlightVoxel(voxelIndex);
				}
			}
		}
	}
	
}

void __vectorcall cAbstractToolMethods::highlightPerimeter(rect2D_t area, uint32_t const color)
{
	static constexpr uint32_t const OUTSIDE_COUNT(4);
	
	_highlightColor = color;
	_blink = false; // reset
	
	point2D_t voxelIndex{};

	for (uint32_t outside = 0; outside < OUTSIDE_COUNT; ++outside) {

		for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

			for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {

				if (area.left == voxelIndex.x || area.top == voxelIndex.y || (area.right - 1) == voxelIndex.x || (area.bottom - 1) == voxelIndex.y) {

					if (world::isVoxelVisible(voxelIndex)) {

						if (0 == outside) {

							Iso::Voxel const oVoxel(world::getVoxelAt(voxelIndex));

							_blink |= bool(Iso::getColor(oVoxel)); // existing color? indicate w/ blinking that these ground voxels are to be replaced.
							highlightVoxel(voxelIndex, color);
						}
						else {
							highlightVoxel(voxelIndex); // surrounding, color remains untouched
						}
					}
				}
			}
		}

		area = r2D_grow(area, point2D_t(1)); // after the first surrounding iteration only.
	}
}