#include "pch.h"
#include "cToolProvider.h"

// implementation is defined "cNuklear->cpp" ONLY for nuklear single-file header lib
#include "nk_custom.h"

cToolProvider::cToolProvider()
	: _ActivatedTool(&_RoadTool)
{

}

void cToolProvider::Paint()
{
	if (nullptr != _ActivatedTool) {
		_ActivatedTool->paint();
	}
}

void cToolProvider::setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool)
{
	if (nullptr != _ActivatedTool) {
		_ActivatedTool->deactivate();
	}

	switch (uiToolType)
	{
	case eTools::SELECT:
		_ActivatedTool = &_SelectTool;
		break;
	case eTools::ZONING:
		_ActivatedTool = &_ZoningTool;
		break;
	case eTools::ROADS:
	default:
		_ActivatedTool = &_RoadTool; // always default to road tool.
		break;
	}

	if (nullptr != _ActivatedTool) {

		if (std::nullopt != uiSubTool) {
			_ActivatedTool->setActivatedSubTool(*uiSubTool);
		}

		_ActivatedTool->activate();
	}
}

cToolProvider::~cToolProvider()
{

}


