#pragma once
#include "globals.h"
#include "cAbstractToolMethods.h"
#include "voxBinary.h"

typedef struct sSelectedRoad {
	point2D_t			origin;
	uint32_t			roadNodeType;
} SelectedRoad;

class cRoadTool final : public cAbstractToolMethods
{
public:
	uint32_t const toolType() const final {
		return(eTools::ROADS);
	}
	virtual void deactivate() final;
	virtual void activate() final;

	virtual void paint() final;

	void __vectorcall PressAction(FXMVECTOR const xmMousePos) final;
	void __vectorcall ReleaseAction(FXMVECTOR const xmMousePos) final;
	void __vectorcall DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart) final;
	void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos) final;

private:
	bool const __vectorcall CreateRoadSegments(point2D_t currentPoint, point2D_t const endPoint);

	void __vectorcall deselect_road_intersect();
	bool const __vectorcall select_road_intersect(point2D_t const origin, uint32_t const roadNodeType);

	bool const __vectorcall search_and_select_closest_road(point2D_t const origin, int32_t const additional_search_width = 0);

	void __vectorcall ConditionRoadGround(point2D_t const currentPoint, uint32_t const currentDirection, int32_t const groundHeightStep, Iso::Voxel& __restrict oVoxel);

	void commitRoadHistory();
	void undoRoadHistory();

	void autotileRoadHistory();
	void decorateRoadHistory();
private: 
	vector<uint32_t>				_undoSignage;		// newly adding signage because of new road
	vector<Iso::voxelIndexHashPair>	_undoExistingSignage; // existing signage that interferes with new road

	point2D_t			_segmentVoxelIndex[2];
	uint32_t			_activePoint;

	SelectedRoad		_selection_start,
						_selected;

	int64_t				_seed_traffic_sign,
						_seed_signage;
public:
	cRoadTool();
	virtual ~cRoadTool() = default;
};




