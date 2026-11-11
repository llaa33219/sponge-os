/* SPDX-License-Identifier: Apache-2.0
 *
 * chrome_geometry.h — pure draw/affordance geometry for
 * sponge_decorator.
 *
 * All chrome geometry derives from the frozen convention shared with
 * the layouter and the native probes through
 * <sponge/wm_protocol.h> ('title_band', 'button_rect', 'sizer_zone'):
 * buttons 24x16 with pad 4, right aligned right-to-left CLOSE,
 * FULLSCREEN, MINIMIZE (fullscreen only when resizeable); the title
 * band is the top margin; the eight resize zones are the outer
 * min(margin, 8) edge bands plus the 16x16 corner squares. This
 * module only ASSEMBLES those shared helpers into one draw model —
 * it never re-implements them.
 *
 * The eight resize affordances are realized as input-mask geometry
 * (the chrome view consumes input on the whole outer frame except the
 * client cutout, with the corner squares kept so the zones reach into
 * the client area) and may be tinted visibly via the theme's 'grip_bg'.
 *
 * PURITY: no Env, no sessions, no allocation, no exceptions, no
 * mutable global state. Font access goes through the public
 * 'Text_painter::Font' interface only (tests substitute a stub).
 */

#pragma once

#include <nitpicker_gfx/text_painter.h>
#include <sponge/wm_protocol.h>


namespace Sponge::Decorator {


/* Horizontal padding between the title-band left edge and the text. */
enum : unsigned { TITLE_TEXT_PAD = 8 };

/* Glyph bitmaps are 9x9, centered in the 24x16 button rect. */
enum : unsigned { GLYPH_W = 9, GLYPH_H = 9 };


/*
 * Draw model for one window's chrome. The outer frame is normalized
 * to (0,0) — the decorator never positions windows (the core does).
 * 'buttons' is computed from the shared 'Wm_protocol::button_rect'
 * with the window's real 'resizeable' flag, so absent affordances
 * (fullscreen on non-resizeable windows) shift the remaining group
 * exactly as the layouter hit-tests it (zero-size rect when absent).
 */
struct Chrome_layout
{
	Wm_protocol::Rect outer { };        /* (0, 0, fw, fh) */
	Wm_protocol::Rect inner { };        /* client cutout */
	Wm_protocol::Rect title { };        /* top-margin band */
	Wm_protocol::Rect strips[3] { };    /* left, right, bottom margins */
	Wm_protocol::Rect buttons[3] { };   /* [CLOSE, FULLSCREEN, MINIMIZE] */
	Wm_protocol::Rect corners[4] { };   /* [NW, NE, SW, SE] 16x16 */
	Wm_protocol::Rect edge_bands[4] { };/* [N, S, E, W] grip bands */
};

Chrome_layout chrome_layout(long fw, long fh,
                            Wm_protocol::Margins const &margins,
                            bool resizeable);


/* Button glyph bitmaps: 'h' rows of 'w' chars ('#' = on pixel). */
struct Glyph_bitmap
{
	unsigned w, h;
	char const *const *rows;
};

Glyph_bitmap button_glyph_bitmap(Wm_protocol::Button button);

bool glyph_pixel(Glyph_bitmap const &glyph, unsigned x, unsigned y);

/* Bitmap placement: centered in the button rect (floor centering). */
Wm_protocol::Rect glyph_rect(Wm_protocol::Rect button);


/*
 * Title text placement and fitting: the available region inside the
 * title band (left-padded, ending 'right_limit' px before the band's
 * right edge — callers pass the leftmost button's x minus padding),
 * and the display string ('client title, falling back to label',
 * truncated at a codepoint boundary with "..." when too wide).
 */
Wm_protocol::Rect title_region(Wm_protocol::Rect title_band,
                               int right_limit);

/* Text draw origin (top-left of the font's bounding box). */
struct Text_origin { int x, y; };

Text_origin title_text_origin(Wm_protocol::Rect region,
                              Text_painter::Font const &font);

Genode::String<256> fit_title(Text_painter::Font const &font,
                              char const *title, char const *label,
                              Wm_protocol::Rect region);


}  /* namespace Sponge::Decorator */
