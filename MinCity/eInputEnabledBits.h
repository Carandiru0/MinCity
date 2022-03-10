#pragma once
#include "betterenums.h"

BETTER_ENUM(eInputEnabledBits, uint32_t const,
	NONE = 0,

	KEYS = (1 << 0),
	MOUSE_BUTTON_LEFT = (1 << 1),
	MOUSE_BUTTON_RIGHT = (1 << 2),
	MOUSE_SCROLL_WHEEL = (1 << 3),
	MOUSE_MOTION = (1 << 4),
	MOUSE_EDGE_SCROLL = MOUSE_MOTION | (1 << 5),
	MOUSE_LEFT_DRAG = MOUSE_MOTION | MOUSE_BUTTON_LEFT | (1 << 6),
	MOUSE_RIGHT_DRAG = MOUSE_MOTION | MOUSE_BUTTON_RIGHT | (1 << 7),

	ALL = 0xFFFFFFFF
);



