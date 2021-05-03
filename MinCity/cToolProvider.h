#pragma once
#include "globals.h"
#include "tTime.h"
#include "cRoadTool.h"
#include "cZoningTool.h"

class cToolProvider final
{
public:
	cAbstractToolMethods* const& __restrict getActivatedTool() const {
		return(_ActivatedTool);
	}

	void setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool = std::nullopt);

	void Load();
private:
	cAbstractToolMethods*		_ActivatedTool;
	cRoadTool					_RoadTool;
	cZoningTool					_ZoningTool;
public:
	cToolProvider();
	~cToolProvider();
};