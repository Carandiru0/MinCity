#pragma once
#include "globals.h"
#include <Math/point2D_t.h>
#include <Utility/class_helper.h>

// *enumerations only*

// Tools:
BETTER_ENUM(eTools, uint32_t const,
	ROADS = 0,
	ZONING,
	SELECT
);
// SubTools:
// *first entry in enumerations must be zero and reserved for any subtool*
static constexpr uint32_t const SUBTOOL_RESERVED_NOT_IMPLEMENTED = 0U;

BETTER_ENUM(eSubTool_Zoning, uint32_t const,
	RESERVED = SUBTOOL_RESERVED_NOT_IMPLEMENTED,
	RESIDENTIAL,
	COMMERCIAL,
	INDUSTRIAL
);

// aLL TOOLS must inherit this class
class cAbstractToolMethods : no_copy
{
public: // no data inside abstract tool - only methods
	virtual uint32_t const toolType() const = 0;

	virtual void deactivate() = 0;
	virtual void activate() = 0;

	virtual uint32_t const getActivatedSubTool() { return(SUBTOOL_RESERVED_NOT_IMPLEMENTED); } // optional
	virtual void setActivatedSubTool(uint32_t const subtool) {} // optional

	// actions - inheritor designate include functionality, otherwise always optional (stubbed out)
	virtual void KeyAction(int32_t const key, bool const down, bool const ctrl) {};
	virtual void __vectorcall PressAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall ReleaseAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall ClickAction(FXMVECTOR const xmMousePos) {};
	virtual void __vectorcall DragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart) {};
	virtual void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos) {};

public: // no data inside abstract tool - only methods
	cAbstractToolMethods() = default;
	virtual ~cAbstractToolMethods() = default;
};
