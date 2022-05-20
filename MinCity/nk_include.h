#pragma once

/**** ONLY INCLUDE NK_INCLUDE.H with a #define NK_IMPLEMENTATION beforehand IN ONE C/CPP FILE ****/
#pragma intrinsic(memset)
#pragma intrinsic(memcpy)

#define NK_PRIVATE
#define NK_ASSERT
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_KEYSTATE_BASED_INPUT
#define NK_MEMSET memset
#define NK_MEMCPY memcpy
#define NK_SQRT SFM::__sqrt
#define NK_SIN SFM::__sin
#define NK_COS SFM::__cos
#define NK_VSNPRINTF vsnprintf
#include <Nuklear.h>     
#include <DirectXPackedVector.h>
#include <cctype>
#include "nk_custom.h"
#include <Utility/stringconv.h>
#include <Utility/variadic.h>

// definitions : //
#ifdef NK_IMPLEMENTATION

#define NK_PUSH_FONT(context, font_index) nk_style_push_font(context, &_fonts[font_index]->handle)
#define NK_PUSH_TEXT_COLOR(context, color_choose) nk_style_push_color(context, &context->style.text.color, color_choose)
#define NK_PUSH_BG_COLOR(context, color_choose) nk_style_push_color(context, &context->style.window.fixed_background.data.color, color_choose)

#define NK_POP_FONT(context) nk_style_pop_font(context)
#define NK_POP_COLOR(context) nk_style_pop_color(context)

NK_API void // use with nk_layout_row_begin(ctx, NK_DYNAMIC or NK_STATIC, HEIGHT, # OF COLUMNS);
			//          nk_layout_row_push... for # of columns (minus one which would be usage of nk_seperator()
			//			nk_layout_row_end(ctx);
	nk_seperator(struct nk_context* const ctx, float const width_ratio)
{
	float const height = ctx->current->layout->row.height;	// save height of context "cursor"
	nk_layout_row_push(ctx, width_ratio);
	nk_spacing(ctx, 1);
	ctx->current->layout->row.height = height; // restore height of context "cursor"
}

STATIC_INLINE_PURE struct nk_color const
nk_rgb_from_XMCOLOR(DirectX::PackedVector::XMCOLOR const& xmColor)
{
	struct nk_color ret;
	ret.r = (nk_byte)xmColor.r;
	ret.g = (nk_byte)xmColor.g;
	ret.b = (nk_byte)xmColor.b;
	ret.a = (nk_byte)255;
	return(ret);
}

STATIC_INLINE_PURE struct nk_color const
nk_rgba_from_XMCOLOR(DirectX::PackedVector::XMCOLOR const& xmColor)
{
	struct nk_color ret;
	ret.r = (nk_byte)xmColor.r;
	ret.g = (nk_byte)xmColor.g;
	ret.b = (nk_byte)xmColor.b;
	ret.a = (nk_byte)xmColor.a;
	return(ret);
}

STATIC_INLINE_PURE struct nk_color const
nk_la(int l, int a)
{
	struct nk_color ret;

	l = NK_CLAMP(0, l, 255);

	ret.r = (nk_byte)l;
	ret.g = (nk_byte)l;
	ret.b = (nk_byte)l;
	ret.a = (nk_byte)NK_CLAMP(0, a, 255);
	return ret;
}

NK_API int
nk_filter_text(const struct nk_text_edit* box, nk_rune unicode)
{
	NK_UNUSED(box);
	if (unicode >= 'A' && unicode <= 'Z') return nk_true;
	else if (unicode >= 'a' && unicode <= 'z') return nk_true;
	else if (' ' == unicode) return nk_true;

	return nk_false;
}
NK_API int
nk_filter_numbers(const struct nk_text_edit* box, nk_rune unicode)
{
	NK_UNUSED(box);
	if (unicode >= '0' && unicode <= '9') return nk_true;

	return nk_false;
}
NK_API int
nk_filter_text_numbers(const struct nk_text_edit* box, nk_rune unicode)
{
	NK_UNUSED(box);
	if (nk_filter_text(box, unicode)) return nk_true;
	else if (nk_filter_numbers(box, unicode)) return nk_true;

	return nk_false;
}
STATIC_INLINE_PURE struct nk_vec2 const nk_CartToIso(struct nk_vec2 const pt2DSpace)
{
	/*
	_x = (_col * tile_width * .5) + (_row * tile_width * .5);
	_y = (_row * tile_hieght * .5) - ( _col * tile_hieght * .5);
	*/
	return(nk_vec2(pt2DSpace.x - pt2DSpace.y, (pt2DSpace.x + pt2DSpace.y) * 0.5f));
}

STATIC_INLINE_PURE struct nk_vec2 const nk_IsoToCart(struct nk_vec2 const ptIsoSpace)
{
	/*
	_x = (_col * tile_width * .5) + (_row * tile_width * .5);
	_y = (_row * tile_hieght * .5) - ( _col * tile_hieght * .5);
	*/
	return(nk_vec2(((ptIsoSpace.y * 2.0f) - ptIsoSpace.x) * 0.5f, ((ptIsoSpace.y * 2.0f) + ptIsoSpace.x) * 0.5f));
}

NK_LIB void*
nk_custom_malloc(nk_handle unused, void *old, nk_size size)
{
	NK_UNUSED(unused);
	NK_UNUSED(old);
	return(scalable_malloc(size));
}
NK_LIB void
nk_custom_free(nk_handle unused, void *ptr)
{
	NK_UNUSED(unused);
	scalable_free(ptr);
}
NK_LIB void*
nk_custom_aligned_malloc(nk_handle unused, void *old, nk_size size)
{
	NK_UNUSED(unused);
	NK_UNUSED(old);
	return(scalable_aligned_malloc(size, 16));
}
NK_LIB void
nk_custom_aligned_free(nk_handle unused, void *ptr)
{
	NK_UNUSED(unused);
	scalable_aligned_free(ptr);
}
NK_API void
nk_buffer_init_cmd_buffer(struct nk_buffer *buffer)
{
	struct nk_allocator alloc;
	alloc.userdata.ptr = 0;
	alloc.alloc = nk_custom_malloc;
	alloc.free = nk_custom_free;
	nk_buffer_init(buffer, &alloc, NK_DEFAULT_COMMAND_BUFFER_SIZE);
}
NK_API void
nk_font_atlas_init_custom(struct nk_font_atlas *atlas)
{
	NK_ASSERT(atlas);
	if (!atlas) return;
	nk_zero_struct(*atlas);
	atlas->temporary.userdata.ptr = 0;
	atlas->temporary.alloc = nk_custom_malloc;
	atlas->temporary.free = nk_custom_free;
	atlas->permanent.userdata.ptr = 0;
	atlas->permanent.alloc = nk_custom_malloc;
	atlas->permanent.free = nk_custom_free;
}

NK_API void
nk_draw_image(struct nk_command_buffer* const __restrict cb,		// for canvas
	struct nk_image const* const __restrict imgMap)
{
	if (nullptr == imgMap->handle.ptr)
		return;

	nk_draw_image(cb, nk_recti(0, 0, imgMap->w, imgMap->h), imgMap, nk_white);
}

NK_API int
nk_draw_image(struct nk_context* const __restrict ctx,			// normal
	struct nk_image const* const __restrict imgMap)
{
	NK_ASSERT(ctx);
	NK_ASSERT(ctx->current);
	NK_ASSERT(ctx->current->layout);
	if (!ctx || !ctx->current || !ctx->current->layout)
		return 0;
	if (nullptr == imgMap->handle.ptr)
		return(nk_false);

	int ret = 0;
	struct nk_window* const __restrict win = ctx->current;
	struct nk_command_buffer* const __restrict cb = &win->buffer;

	struct nk_rect r;
	enum nk_widget_layout_states state;
	if ((state = nk_widget(&r, ctx)))
	{
		nk_draw_image(cb, r, imgMap, nk_white);

		return(nk_true);
	}

	return(nk_false);
}

NK_API const struct nk_style_item*
nk_draw_minimap_nav_button(struct nk_command_buffer *out,
	const struct nk_rect bounds, nk_flags state,
	const struct nk_style_button *style)
{
	const struct nk_style_item *background;
	if (state & NK_WIDGET_STATE_HOVER)
		background = &style->hover;
	else if (state & NK_WIDGET_STATE_ACTIVED)
		background = &style->active;
	else background = &style->normal;

	/*
	x,Y		 X,Y
	+---------+
	|		  |
	|         |
	+---------+
	x,y		 X,y

	counter-clockwise winding order
	*/

	struct nk_vec2 isoRect[] = { 
		{bounds.x,					bounds.y},
		{bounds.x + bounds.w,		bounds.y},
		{bounds.x + bounds.w,		bounds.y + bounds.h},
		{bounds.x,					bounds.y + bounds.h}
	};

	constexpr uint32_t const point_count = 4;
	for (uint32_t iDx = 0; iDx < point_count; ++iDx)
	{
		isoRect[iDx] = nk_CartToIso(isoRect[iDx]);
	}

	nk_fill_polygon(out, (float* const)isoRect, point_count, background->data.color);
	nk_stroke_polygon(out, (float* const)isoRect, point_count, style->border, style->border_color);

	return background;
}
NK_API int
nk_minimap_nav_button(struct nk_context *ctx, struct nk_rect bounds, struct nk_color color)
{
	struct nk_window *win;
	struct nk_panel *layout;
	const struct nk_input *in;
	struct nk_style_button button;

	int ret = 0;
	struct nk_rect content;
	enum nk_widget_layout_states state;

	NK_ASSERT(ctx);
	NK_ASSERT(ctx->current);
	NK_ASSERT(ctx->current->layout);
	if (!ctx || !ctx->current || !ctx->current->layout)
		return 0;

	win = ctx->current;
	layout = win->layout;
	in = &ctx->input;

	if (!NK_INBOX(in->mouse.pos.x, in->mouse.pos.y, bounds.x, bounds.y, bounds.w, bounds.h))
		state = NK_WIDGET_ROM;
	else
		state = NK_WIDGET_VALID;

	if (!state) return 0;
	in = (state == NK_WIDGET_ROM || layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

	button = ctx->style.button;
	button.normal = nk_style_item_color(nk_rgba(255,0,0,64));
	button.hover = nk_style_item_color(nk_rgba(255, 0, 0, 127));
	button.active = nk_style_item_color(nk_rgba(255, 0, 0, 220));

	ret = nk_do_button(&ctx->last_widget_state, &win->buffer, bounds,
		&button, in, ctx->button_behavior, &content);
	nk_draw_minimap_nav_button(&win->buffer, bounds, ctx->last_widget_state, &button);
	return ret;
}

NK_API int
nk_minimap(struct nk_context* const __restrict ctx, 
	struct nk_image const* const __restrict imgMap, 
	struct nk_rect * const __restrict rectFocus,		// image space coordinates
	struct nk_color color)
{
	NK_ASSERT(ctx);
	NK_ASSERT(ctx->current);
	NK_ASSERT(ctx->current->layout);
	if (!ctx || !ctx->current || !ctx->current->layout)
		return 0;
	if (nullptr == imgMap->handle.ptr)
		return(nk_false);

	int ret = 0;
	struct nk_window * const __restrict win = ctx->current;
	struct nk_command_buffer * const __restrict cb = &win->buffer;

	struct nk_rect r;
	enum nk_widget_layout_states state;
	if ((state = nk_widget(&r, ctx)))
	{
		struct nk_panel * const layout = win->layout;
		struct nk_input const* const in = (state == NK_WIDGET_ROM || layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

		nk_draw_image(cb, r, imgMap, nk_white);

		struct nk_style_button button;
		struct nk_rect content;

		button = ctx->style.button;
		//button.normal = nk_style_item_color(color);
		//button.hover = nk_style_item_color(color);
		//button.active = nk_style_item_color(color);
		ret = nk_do_button(&ctx->last_widget_state, &win->buffer, r,
			&button, in, ctx->button_behavior, &content);

		// convert image-space rect coordinates to scale of image widget
		struct nk_rect rectSrc = *rectFocus;
		struct nk_rect rectTarget;
		struct nk_vec2 center;
		struct nk_vec2 const  imgInvSize{ 1.0f / (float)imgMap->w, 1.0f / (float)imgMap->h };

		if (ret) // mouse pressed
		{
			// set center (screenspace)
			center.x = in->mouse.pos.x - r.x;
			center.y = in->mouse.pos.y - r.y;
		}
		else {
			// normalize center of focus rect (gridspace -> screenspace)
			center.x = (rectSrc.x + rectSrc.w * 0.5f) * imgInvSize.x;
			center.y = ((imgMap->h - rectSrc.y) - rectSrc.h * 0.5f) * imgInvSize.y;	// invert y
																					// inverted y resulting nverted height
																					// scale by size of minimap widget
			center.x *= r.w; center.y *= r.h;
		}

		// normalize width, height and scale by size of widget (gridspace -> screenspace)
		rectTarget.w = (rectSrc.w * imgInvSize.x) * r.w;
		rectTarget.h = (rectSrc.h * imgInvSize.y) * r.h;

		// get the top left corner, relative to widget top left corner (screenspace)
		rectTarget.x = r.x + center.x - rectTarget.w * 0.5f;
		rectTarget.y = r.y + center.y - rectTarget.h * 0.5f; 

		//rectTarget = nk_shrink_rect(rectTarget, 0.99f);

		// draw ontop of minimap image the focus rect
		nk_minimap_nav_button(ctx, rectTarget, color);

		if (ret) // mouse pressed, update out variable for grid space position
		{
			// convert from screen-space back to grid-space
			struct nk_rect rectGrid;

			// normalize center of focus rect (screenspace -> gridspace)
			center.x = ((rectTarget.x - r.x) + rectTarget.w * 0.5f) / r.w;
			center.y = ((r.h - (rectTarget.y - r.y)) - rectTarget.h * 0.5f) / r.h;	// invert y
																					// inverted y resulting nverted height
																					// scale by image/grid
			center.x *= (float)imgMap->w;
			center.y *= (float)imgMap->h;

			// normalize width, height and scale by size of image/grid (screenspace -> gridspace)
			rectGrid.w = (rectTarget.w / r.w) * (float)imgMap->w;
			rectGrid.h = (rectTarget.h / r.h) * (float)imgMap->h;

			// get the top left corner, relative to image top left corner (gridspace)
			rectGrid.x = /*0*/ + center.x - rectGrid.w * 0.5f;
			rectGrid.y = /*0*/ + center.y - rectGrid.h * 0.5f;

			// update out, return true (mouse was pressed)
			*rectFocus = rectGrid;
		}
	}

	return(ret);
}

NK_API nk_bool
nk_is_mouse_down(struct nk_context* ctx, struct nk_rect bounds, enum nk_buttons btn)
{
	struct nk_rect c, v;

	NK_ASSERT(ctx);
	NK_ASSERT(ctx->current);
	if (!ctx || !ctx->current || ctx->active != ctx->current)
		return 0;

	c = ctx->current->layout->clip;
	c.x = (float)((int)c.x);
	c.y = (float)((int)c.y);
	c.w = (float)((int)c.w);
	c.h = (float)((int)c.h);

	nk_unify(&v, &c, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
	if (!NK_INTERSECT(c.x, c.y, c.w, c.h, bounds.x, bounds.y, bounds.w, bounds.h))
		return 0;
	if (!nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
		return nk_false;
	return nk_input_is_mouse_down(&ctx->input, btn);
}

NK_API void									  
nk_push_transparent_bg(struct nk_context* const __restrict ctx)
{
	nk_color const zero(nk_rgba(0,0,0,0));

	// transparent bg
	nk_style_push_color(ctx, &ctx->style.window.background, zero);
	nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(zero));
}
NK_API void									  
nk_pop_transparent_bg(struct nk_context* const __restrict ctx)
{
	nk_style_pop_color(ctx);
	nk_style_pop_style_item(ctx);
}

NK_API std::string const
nk_text_rotate_string(std::string_view const str, uint32_t offset)
{
	static constexpr char const BEGIN = 'A',
								END = 'Z';
	static constexpr uint32_t const RANGE = (END - BEGIN);

	offset = offset % (RANGE); // limit offset to range

	std::string rot_str("");
	uint32_t const str_length( (uint32_t const)str.length());
	for (uint32_t i = 0 ; i < str_length ; ++i) {

		char c = stringconv::toUpper(str[i]);

		if (' ' != c) { // keep spaces

			c += offset;
			if ( c > END ) {
				c -= RANGE; // wrap-around
			}
		}
		rot_str += c; // building new string
	}

	return(rot_str);
}

template<bool const bCursorIsEnd, typename T>  // bCursorIsEnd   - is end of string, should display full string up to cursor_index in length
NK_API void									   // !bCursorIsEnd  - is beginning index, front should be padded with empty spaces, all characters from cursor_index until end of string should be drawn
nk_row_text_animated(struct nk_context* const __restrict ctx, nk_text_animated_state const& __restrict state, T const& row)
{
	uint32_t const cursor_index = SFM::min(state.cursor_index, (uint32_t)row.length()); // safety clamp

	std::string szDrawn("");

	if constexpr (bCursorIsEnd) {

		szDrawn = row.substr(0, cursor_index);
	}
	else {
		szDrawn = nk_text_rotate_string(row, state.char_offset);
		szDrawn.replace(0, cursor_index, cursor_index, ' ');
	}

	nk_layout_row_dynamic(ctx, 30, 1);
	nk_label(ctx, szDrawn.c_str(), NK_TEXT_LEFT);
}

// animated text
// area: desired rect of screen to cover, offsets and width/height
// fWidth: must be between [0.0f...1.0f] defining desirable portion of area to consume with text
// font_first, font_second: 2 distinct fonts to animate together
template<typename ...T>  // parameter pack by T
NK_API void
nk_text_animated(struct nk_context* const __restrict ctx, struct nk_rect const& __restrict area,  
	float const fWidth, struct nk_font const* const font_first, 
	nk_text_animated_state& __restrict state,
	T&&... rows)
{
	fp_seconds const fTDelta(ctx->delta_time_seconds);

	// cursor index movement
	if ( (state.tAccumulateCursor += fTDelta) > state.MOVE_CURSOR_INTERVAL ) {

		++state.cursor_index;

		state.tAccumulateCursor -= state.MOVE_CURSOR_INTERVAL;
	}

	// character offset movement
	if ( (state.tAccumulateChar += fTDelta) > state.CHAR_INTERVAL ) {

		++state.char_offset;

		state.tAccumulateChar -= state.CHAR_INTERVAL;
	}

	nk_push_transparent_bg(ctx);

	{ // text bg psuedo window

		if (nk_begin(ctx, "background_text", area, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT))
		{
			nk_style_push_font(ctx, &font_first->handle);
			nk_layout_row_begin(ctx, NK_DYNAMIC, area.h, 2);

			nk_layout_row_push(ctx, fWidth);
			nk_group_begin(ctx, "background_text_grp", 0);

			// iterate variadic arguments (parameter pack) for n rows of text
			over_all<T...>::for_each([&](auto && row) {

				nk_row_text_animated<true>(ctx, state, row);

			}, std::forward<T&&>(rows)...);
			

			nk_group_end(ctx);

			if (fWidth < 1.0f) {
				nk_seperator(ctx, 1.0f - fWidth); // remainder column
			}

			nk_layout_row_end(ctx);
			nk_style_pop_font(ctx);
		}
		nk_end(ctx);
	}

	nk_pop_transparent_bg(ctx);
}

#endif // NK_IMPLEMENTATION


