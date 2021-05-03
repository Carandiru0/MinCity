#pragma once
#include "tTime.h"

// *public usage:

typedef struct nk_text_animated_state
{
	static constexpr fp_seconds const MOVE_CURSOR_INTERVAL = duration_cast<fp_seconds>(milliseconds(250)),
									  CHAR_INTERVAL = duration_cast<fp_seconds>(milliseconds(175));

	fp_seconds  tAccumulateCursor, tAccumulateChar;
	uint32_t	cursor_index,
				char_offset;


	void reset() {
		tAccumulateCursor = tAccumulateChar = zero_time_duration;
		char_offset = cursor_index = 0;
	}

} nk_text_animated_state;

STATIC_INLINE_PURE struct nk_color const
nk_la(int l, int a);

STATIC_INLINE_PURE struct nk_color const
nk_rgb_from_XMCOLOR(DirectX::PackedVector::XMCOLOR const& xmColor);

STATIC_INLINE_PURE struct nk_color const
nk_rgba_from_XMCOLOR(DirectX::PackedVector::XMCOLOR const& xmColor);

STATIC_INLINE_PURE struct nk_vec2 const 
nk_CartToIso(struct nk_vec2 const pt2DSpace);

STATIC_INLINE_PURE struct nk_vec2 const 
nk_IsoToCart(struct nk_vec2 const ptIsoSpace);

