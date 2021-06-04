#include "pch.h"
#include "cUserInterface.h"
#include "cToolProvider.h"


cUserInterface::cUserInterface()
	: _tools(nullptr)
{

}

void cUserInterface::Initialize()
{
	_tools = new cToolProvider();
	_tools->Load();
}

uint32_t const cUserInterface::getActivatedToolType() const
{
	return(_tools->getActivatedTool()->toolType());
}
uint32_t const cUserInterface::getActivatedSubToolType() const
{
	return(_tools->getActivatedTool()->getActivatedSubTool());
}
void cUserInterface::setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool)
{
	_tools->setActivatedTool(uiToolType, uiSubTool);
}

void cUserInterface::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->KeyAction(key, down, ctrl);
}
void __vectorcall cUserInterface::LeftMousePressAction(FXMVECTOR const xmMousePos)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->PressAction(xmMousePos);
}
void __vectorcall cUserInterface::LeftMouseReleaseAction(FXMVECTOR const xmMousePos)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->ReleaseAction(xmMousePos);
}
void __vectorcall cUserInterface::LeftMouseClickAction(FXMVECTOR const xmMousePos)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->ClickAction(xmMousePos);
}
void __vectorcall cUserInterface::LeftMouseDragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->DragAction(xmMousePos, xmLastDragPos, tDragStart);

}
void __vectorcall cUserInterface::MouseMoveAction(FXMVECTOR const xmMousePos)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	ActivatedTool->MouseMoveAction(xmMousePos);
}

cUserInterface::~cUserInterface()
{
	SAFE_DELETE(_tools);
}


