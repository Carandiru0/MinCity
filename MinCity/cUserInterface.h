#pragma once
#include <optional>
#include <Utility/class_helper.h>
#include "cAbstractToolMethods.h"

// forward decls
class cUser;
class cToolProvider;

class no_vtable cUserInterface : no_copy
{
public:
	void Initialize();
	void OnLoaded();

	void Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	void Paint(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);

	uint32_t const getActivatedToolType() const;
	uint32_t const getActivatedSubToolType() const;
	void setActivatedTool(uint32_t const uiToolType, std::optional<uint32_t const> uiSubTool = std::nullopt);

	void KeyAction(int32_t const key, bool const down, bool const ctrl);
	void __vectorcall LeftMousePressAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseReleaseAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseClickAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseDragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart);
	void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos);

private:

private:
	cUser*				_user;
	cToolProvider*		_tools;
	bool				_reset_world_event_sent;
public:
	cUserInterface();
	~cUserInterface();
};