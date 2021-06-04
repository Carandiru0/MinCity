#pragma once
#include <optional>
#include <Utility/class_helper.h>
#include "cAbstractToolMethods.h"

// forward decls
class cToolProvider;

class no_vtable cUserInterface : no_copy
{
public:
	void Initialize();

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
	cToolProvider* _tools;
public:
	cUserInterface();
	~cUserInterface();
};