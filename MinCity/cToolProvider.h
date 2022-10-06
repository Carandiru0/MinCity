#pragma once
#include "globals.h"
#include "tTime.h"
#include "cRoadTool.h"
#include "cZoningTool.h"
#include "cSelectTool.h"

class cToolProvider final
{
public:
	cAbstractToolMethods* const& __restrict getActivatedTool() const {
		return(_ActivatedTool);
	}

	void setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool = std::nullopt);

	void Paint();

private:
	cAbstractToolMethods*		_ActivatedTool;
	cZoningTool					_ZoningTool;
	cSelectTool					_SelectTool;
public:
	cToolProvider();
	~cToolProvider();
};