#pragma once
#include "globals.h"
#include "cAbstractToolMethods.h"
#include "UndoVoxel.h"
#include <vector>
#include "money_t.h"
#include "world.h"

class cZoningTool final : public cAbstractToolMethods
{
public:
	uint32_t const toolType() const final {
		return(eTools::ZONING);
	}
	virtual void deactivate() final;
	virtual void activate() final;
	
	virtual void paint() final;

	uint32_t const getActivatedSubTool() final { return(_ActivatedSubTool); }
	virtual void setActivatedSubTool(uint32_t const subtool) final;

	void __vectorcall PressAction(FXMVECTOR const xmMousePos) final;
	void __vectorcall ReleaseAction(FXMVECTOR const xmMousePos) final;
	void __vectorcall ClickAction(FXMVECTOR const xmMousePos) final;
	void __vectorcall DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart) final;
	void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos) final;

	void setCost(int64_t const cost);
private:
	void commitZoneHistory();
	void clearZoneHistory();
	void pushZoneHistory(UndoVoxel&& history);

	void __vectorcall highlightArea(rect2D_t const area);
private:
	vector<sUndoVoxel>	_undoHistory;
	
	point2D_t				_segmentVoxelIndex[2];
	uint32_t				_activePoint;
	uint32_t				_ActivatedSubTool;

	money_t					_cost;
public:
	cZoningTool();
	virtual ~cZoningTool() = default;
};


