#include "pch.h"
#include "cAbstractToolMethods.h"
#include "IsoVoxel.h"
#include "world.h"
#include "MinCity.h"
#include "cVoxelWorld.h"


void cAbstractToolMethods::pushHistory(vector<sUndoVoxel>&& undoHistory)
{
	std::move(undoHistory.begin(), undoHistory.end(), std::back_inserter(_undoHistory));
}

void cAbstractToolMethods::clearHighlights()
{
	_undoHighlight.clear();
}
void cAbstractToolMethods::undoHighlights()
{
	// undoing
	// vector is iterated in reverse (newest to oldest) to properly restore the grid voxels
	for (vector<sUndoVoxel>::const_reverse_iterator undoVoxel = _undoHighlight.crbegin(); undoVoxel != _undoHighlight.crend(); ++undoVoxel)
	{
		world::setVoxelAt(undoVoxel->voxelIndex, std::forward<Iso::Voxel const&& __restrict>(undoVoxel->undoVoxel));
	}

	clearHighlights();
}
void cAbstractToolMethods::clearHistory()
{
	clearHighlights();

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
	for (vector<sUndoVoxel>::const_iterator highlightedVoxel = _undoHighlight.cbegin(); highlightedVoxel != _undoHighlight.cend(); ++highlightedVoxel)
	{
		point2D_t const voxelIndex(highlightedVoxel->voxelIndex);
		Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));

		if (pVoxel && Iso::isEmissive(*pVoxel)) {
			world::addLight(voxelIndex, Iso::getColor(*pVoxel));
		}
	}
}

bool const __vectorcall cAbstractToolMethods::highlightVoxel(point2D_t const voxelIndex, uint32_t const color)
{
	// center
	Iso::Voxel const* const pVoxel(world::getVoxelAt(voxelIndex));

	if (pVoxel) {
		Iso::Voxel oVoxel(*pVoxel);

		if (Iso::isGroundOnly(oVoxel) && Iso::isHashEmpty(oVoxel)) {

			_undoHighlight.emplace_back(voxelIndex, oVoxel);

			Iso::setPending(oVoxel);
			Iso::setColor(oVoxel, color);
			Iso::setEmissive(oVoxel);

			world::setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxel));
			return(true);
		}
	}

	return(false);
}
void __vectorcall cAbstractToolMethods::highlightCross(point2D_t const origin, uint32_t const color)
{
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
				ok = highlightVoxel(voxelIndex, color);
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
				ok = highlightVoxel(voxelIndex, color);
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
				ok = highlightVoxel(voxelIndex, color);
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
				ok = highlightVoxel(voxelIndex, color);
			}

		} while (ok);
	}
}

void __vectorcall cAbstractToolMethods::highlightArea(rect2D_t const area, uint32_t const color)
{
	point2D_t voxelIndex{};

	for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

		for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {
			
			if (world::isVoxelVisible(voxelIndex)) {
				highlightVoxel(voxelIndex, color);
			}
		}
	}
}

void __vectorcall cAbstractToolMethods::highlightPerimeter(rect2D_t const area, uint32_t const color)
{
	point2D_t voxelIndex{};

	for (voxelIndex.y = area.top; voxelIndex.y < area.bottom; ++voxelIndex.y) {

		for (voxelIndex.x = area.left; voxelIndex.x < area.right; ++voxelIndex.x) {

			if (area.left == voxelIndex.x || area.top == voxelIndex.y || (area.right - 1) == voxelIndex.x || (area.bottom - 1) == voxelIndex.y) {
				
				if (world::isVoxelVisible(voxelIndex)) {
					highlightVoxel(voxelIndex, color);
				}
			}
		}
	}
}