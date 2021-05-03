#include "pch.h"
#include "cToolProvider.h"

// implementation is defined "cNuklear.cpp" ONLY for nuklear single-file header lib
#include "nk_custom.h"

cToolProvider::cToolProvider()
	: _ActivatedTool(&_RoadTool)
{

}

void cToolProvider::Load()
{
}


void cToolProvider::setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool)
{
	if (nullptr != _ActivatedTool) {
		_ActivatedTool->deactivate();
	}

	switch (uiToolType)
	{
	case eTools::ZONING:
		_ActivatedTool = &_ZoningTool;
		break;
	case eTools::ROADS:
	default:
		_ActivatedTool = &_RoadTool;
		break;
	}

	if (std::nullopt != uiSubTool) {
		_ActivatedTool->setActivatedSubTool(*uiSubTool);
	}

	if (nullptr != _ActivatedTool) {
		_ActivatedTool->activate();
	}
}

cToolProvider::~cToolProvider()
{

}


