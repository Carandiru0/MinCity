#pragma once
#include "nk_include.h"
#include "nk_custom.h"

enum theme { THEME_GREYSCALE };

void
nk_set_style(struct nk_context *ctx, enum theme theme)
{
	struct nk_color table[NK_COLOR_COUNT];
	
    table[NK_COLOR_TEXT] = nk_la(227, 255);
    table[NK_COLOR_WINDOW] = nk_la(16, 150);
    table[NK_COLOR_HEADER] = nk_la(8, 150);
    table[NK_COLOR_BORDER] = nk_la(64, 150);
    table[NK_COLOR_BUTTON] = nk_la(64, 225);
    table[NK_COLOR_BUTTON_HOVER] = nk_la(127, 255);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_la(0, 255);
    table[NK_COLOR_TOGGLE] = nk_la(80, 220);
    table[NK_COLOR_TOGGLE_HOVER] = nk_la(72, 220);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_la(183, 255);
    table[NK_COLOR_SELECT] = nk_la(48, 220);
    table[NK_COLOR_SELECT_ACTIVE] = nk_la(127, 220);
    table[NK_COLOR_SLIDER] = nk_la(0, 220);
    table[NK_COLOR_SLIDER_CURSOR] = nk_la(48, 220);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_la(72, 220);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_la(72, 220);
    table[NK_COLOR_PROPERTY] = nk_la(57, 255);
    table[NK_COLOR_EDIT] = nk_la(64, 225);
    table[NK_COLOR_EDIT_CURSOR] = nk_la(250, 255);
    table[NK_COLOR_COMBO] = nk_la(57, 255);
    table[NK_COLOR_CHART] = nk_la(0, 127);
    table[NK_COLOR_CHART_COLOR] = nk_la(0, 200);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_la(0, 200);
    table[NK_COLOR_SCROLLBAR] = nk_la(48, 220);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_la(0, 220);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_la(72, 220);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_la(0, 255);
    table[NK_COLOR_TAB_HEADER] = nk_la(72, 220);

	nk_style_from_table(ctx, table);
}




