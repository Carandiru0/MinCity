#pragma once
#include "globals.h"
#include <Math/point2D_t.h>
#include <Utility/class_helper.h>
#include "UndoVoxel.h"
#include <vector>

// *enumerations only*

// Tools:
BETTER_ENUM(eTools, uint32_t const,
	ROADS = 0,
	ZONING,
	SELECT
);
// SubTools:
// *first entry in enumerations must be zero and reserved for any subtool*
static constexpr uint32_t const SUBTOOL_RESERVED_NOT_IMPLEMENTED = 0U;

BETTER_ENUM(eSubTool_Zoning, uint32_t const,
	RESERVED = SUBTOOL_RESERVED_NOT_IMPLEMENTED,
	RESIDENTIAL,
	COMMERCIAL,
	INDUSTRIAL
);

// aLL TOOLS must inherit this class
class cAbstractToolMethods : no_copy
{
public: // no data inside abstract tool - only methods
	virtual uint32_t const toolType() const = 0;

	virtual void deactivate() = 0;
	virtual void activate() = 0;

	virtual void paint();

	virtual uint32_t const getActivatedSubTool() { return(SUBTOOL_RESERVED_NOT_IMPLEMENTED); } // optional
	virtual void setActivatedSubTool(uint32_t const subtool) {} // optional

	// actions - inheritor designate include functionality, otherwise always optional (stubbed out)
	virtual void KeyAction(int32_t const key, bool const down, bool const ctrl) {};
	virtual void __vectorcall PressAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall ReleaseAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall ClickAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart) {};
	virtual void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos) {};

protected:
	__inline vector<sUndoVoxel> const& getHistory() const { return(_undoHistory); }

	template<class... Args>
	void pushHistory(Args&&... args);
	void pushHistory(vector<sUndoVoxel>&& undoHistory);
	void clearHistory();
	void undoHistory();

	bool const __vectorcall highlightVoxel(point2D_t const voxelIndex, uint32_t const color);
	void __vectorcall highlightCross(point2D_t const voxelIndex, uint32_t const color);
	void __vectorcall highlightArea(rect2D_t const area, uint32_t const color);

private:
	void clearHighlights(); // privste, use public method clearHistory / undoHistory - simplifies derived classea implementation.
	void undoHighlights();

private:
	vector<sUndoVoxel>				_undoHistory, _undoHighlight;


public:
	cAbstractToolMethods() = default;
	virtual ~cAbstractToolMethods() = default;
};

template<class... Args>
void cAbstractToolMethods::pushHistory(Args&&... args)
{
	_undoHistory.emplace_back(std::forward<Args&&...>(args...));
}

STATIC_INLINE_PURE rect2D_t const __vectorcall orientAreaToRect(point2D_t const start_pt, point2D_t const end_pt)
{
	// minimum = top left, maximum = bottom right
	return(rect2D_t(p2D_min(start_pt, end_pt), p2D_max(start_pt, end_pt)));
}