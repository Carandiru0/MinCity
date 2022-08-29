#pragma once
#include <Utility/class_helper.h>

// forward decl
namespace world {

	class cYXIGameObject;
	class cYXISphereGameObject;

} // end ns

class cUser : no_copy
{
public:
	void Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	void KeyAction(int32_t const key, bool const down, bool const ctrl);

	/*
	void __vectorcall LeftMousePressAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseReleaseAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseClickAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseDragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart);
	void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos);
	*/

private:
	world::cYXIGameObject*				_ship;
	world::cYXISphereGameObject			*_shipSphereLeft, *_shipSphereRight;
public:
	cUser();
	~cUser();
};