#include "pch.h"
#include "cUserInterface.h"
#include "cUser.h"
#include "cToolProvider.h"
#include "MinCity.h"


cUserInterface::cUserInterface()
	: _user(nullptr), _tools(nullptr), _reset_world_event_sent(false)
{

}

void cUserInterface::Initialize()
{
	_tools = new cToolProvider();
}

void cUserInterface::OnLoaded()
{
	SAFE_DELETE(_user);
	_user = new cUser();
	_user->create();
	_reset_world_event_sent = false; // reset
}

void cUserInterface::Paint()
{
	if (_tools) {

		_tools->Paint();
	}
	if (_user) {

		_user->Paint();
	}
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

void cUserInterface::Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	if (_user) {

		if (!_reset_world_event_sent) {
			if (_user->isDestroyed()) {
				MinCity::DispatchEvent(eEvent::RESET, new nanoseconds(milliseconds(5000)));
				_reset_world_event_sent = true;
			}
		}

		_user->Update(tNow, tDelta);
	}
}

void cUserInterface::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	auto* const ActivatedTool(_tools->getActivatedTool());

	switch (key)
	{
	case GLFW_KEY_DELETE:
	case GLFW_KEY_Q:
	case GLFW_KEY_PAGE_DOWN:
	case GLFW_KEY_E:
	case GLFW_KEY_HOME:
	case GLFW_KEY_R:
	case GLFW_KEY_END:
	case GLFW_KEY_F:
	case GLFW_KEY_LEFT:
	case GLFW_KEY_RIGHT:
	case GLFW_KEY_UP:
	case GLFW_KEY_DOWN:
	case GLFW_KEY_W:
	case GLFW_KEY_A:
	case GLFW_KEY_S:
	case GLFW_KEY_D:
	case GLFW_KEY_B: // beacon
		if (_user) {
			_user->KeyAction(key, down, ctrl);
		}
		break;
	default: // if not one of the reserved keys that control the user
		ActivatedTool->KeyAction(key, down, ctrl);
		break;
	}

	
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
	SAFE_DELETE(_user);
	SAFE_DELETE(_tools);
}


