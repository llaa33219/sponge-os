/* SPDX-License-Identifier: Apache-2.0
 *
 * chrome_geometry.cc — pure draw/affordance geometry for
 * sponge_decorator (see chrome_geometry.h).
 */

#include "chrome_geometry.h"

using namespace Sponge::Decorator;
using Sponge::Wm_protocol::Button;
using Sponge::Wm_protocol::Margins;
using Sponge::Wm_protocol::Rect;


Chrome_layout Sponge::Decorator::chrome_layout(long fw, long fh,
                                               Margins const &margins,
                                               bool resizeable)
{
	Chrome_layout l;

	l.outer = Rect { 0, 0, fw, fh };
	l.inner = Wm_protocol::inner_from_outer(l.outer, margins);
	l.title = Wm_protocol::title_band(l.outer, margins);

	l.strips[0] = Rect { 0, margins.top,
	                     margins.left, fh - margins.top };
	l.strips[1] = Rect { (int)(fw - margins.right), margins.top,
	                     margins.right, fh - margins.top };
	l.strips[2] = Rect { 0, (int)(fh - margins.bottom), fw, margins.bottom };

	l.buttons[0] = Wm_protocol::button_rect(l.outer, margins,
	                                        Button::CLOSE, resizeable);
	l.buttons[1] = Wm_protocol::button_rect(l.outer, margins,
	                                        Button::FULLSCREEN, resizeable);
	l.buttons[2] = Wm_protocol::button_rect(l.outer, margins,
	                                        Button::MINIMIZE, resizeable);

	long const corner = Wm_protocol::CHROME_CORNER_PX;

	l.corners[0] = Rect { 0, 0, corner, corner };
	l.corners[1] = Rect { (int)(fw - corner), 0, corner, corner };
	l.corners[2] = Rect { 0, (int)(fh - corner), corner, corner };
	l.corners[3] = Rect { (int)(fw - corner), (int)(fh - corner), corner, corner };

	auto grip_depth = [] (int margin) {
		return margin < (int)Wm_protocol::CHROME_GRIP_MAX
		     ? margin : (int)Wm_protocol::CHROME_GRIP_MAX; };

	l.edge_bands[0] = Rect { 0, 0, fw, grip_depth(margins.top) };
	l.edge_bands[1] = Rect { 0, (int)(fh - grip_depth(margins.bottom)), fw,
	                         grip_depth(margins.bottom) };
	l.edge_bands[2] = Rect { 0, 0, grip_depth(margins.left), fh };
	l.edge_bands[3] = Rect { (int)(fw - grip_depth(margins.right)), 0,
	                         grip_depth(margins.right), fh };

	return l;
}


/* ========================== glyph bitmaps ======================== */

namespace {

char const *const close_rows[GLYPH_H] = {
	"##.....##",
	".##...##.",
	"..##.##..",
	"...###...",
	"....#....",
	"...###...",
	"..##.##..",
	".##...##.",
	"##.....##",
};

char const *const minimize_rows[GLYPH_H] = {
	".........",
	".........",
	".........",
	".........",
	".........",
	".........",
	".#######.",
	".#######.",
	".........",
};

char const *const fullscreen_rows[GLYPH_H] = {
	"#########",
	"#########",
	"##.....##",
	"##.....##",
	"##.....##",
	"##.....##",
	"##.....##",
	"#########",
	"#########",
};

}  /* anonymous namespace */


Glyph_bitmap Sponge::Decorator::button_glyph_bitmap(Button button)
{
	switch (button) {
	case Button::CLOSE:
		return Glyph_bitmap { GLYPH_W, GLYPH_H, close_rows };
	case Button::FULLSCREEN:
		return Glyph_bitmap { GLYPH_W, GLYPH_H, fullscreen_rows };
	case Button::MINIMIZE:
		return Glyph_bitmap { GLYPH_W, GLYPH_H, minimize_rows };
	}

	return Glyph_bitmap { 0, 0, nullptr };
}


bool Sponge::Decorator::glyph_pixel(Glyph_bitmap const &glyph,
                                    unsigned x, unsigned y)
{
	if (x >= glyph.w || y >= glyph.h)
		return false;
	return glyph.rows[y][x] == '#';
}


Rect Sponge::Decorator::glyph_rect(Rect button)
{
	if (button.w <= 0 || button.h <= 0)
		return Rect { };

	long const gw = GLYPH_W, gh = GLYPH_H;
	if (button.w < gw || button.h < gh)
		return Rect { };

	return Rect { (int)(button.x + (button.w - gw) / 2),
	              (int)(button.y + (button.h - gh) / 2), gw, gh };
}


/* ========================== title text =========================== */

Rect Sponge::Decorator::title_region(Rect title_band, int right_limit)
{
	int const x = title_band.x + (int)TITLE_TEXT_PAD;
	int const w = right_limit - x;
	if (w <= 0)
		return Rect { };

	return Rect { x, title_band.y, w, title_band.h };
}


Text_origin Sponge::Decorator::title_text_origin(Rect region,
                                                 Text_painter::Font const &font)
{
	unsigned const height = font.height();
	int const y = region.y + (int)((region.h - (long)height) / 2);

	return Text_origin { region.x, (y < region.y) ? region.y : y };
}


Genode::String<256> Sponge::Decorator::fit_title(Text_painter::Font const &font,
                                                 char const *title,
                                                 char const *label,
                                                 Rect region)
{
	if (!title) title = "";
	if (!label) label = "";

	char const *full = (title[0] != '\0') ? title : label;

	long const max_w = region.w;
	if (max_w <= 0 || full[0] == '\0')
		return Genode::String<256>();

	static char const ellipsis[] = "...";

	Genode::Utf8_ptr const full_utf8(full);
	unsigned const full_w = font.string_width(full_utf8).decimal();
	if ((long)full_w <= max_w)
		return Genode::String<256>(full);

	unsigned const dots_w = font.string_width(Genode::Utf8_ptr(ellipsis)).decimal();
	if ((long)dots_w > max_w)
		return Genode::String<256>();

	/* cut at the last codepoint boundary whose prefix + "..." fits */
	long budget = max_w - (long)dots_w;
	char buf[256];
	Genode::size_t n = 0;

	Genode::Utf8_ptr utf8(full);
	for (char const *p = full; *p != '\0' && budget > 0; utf8 = utf8.next()) {

		long const advance = font.advance_info(utf8.codepoint()).advance.decimal();
		if (advance > budget)
			break;

		unsigned len = 1;
		unsigned char const lead = (unsigned char)*p;
		if (lead >= 0xf0)      len = 4;
		else if (lead >= 0xe0) len = 3;
		else if (lead >= 0xc0) len = 2;

		if (n + len + sizeof(ellipsis) > sizeof(buf))
			break;

		for (unsigned i = 0; i < len; i++)
			buf[n++] = p[i];
		p += len;
		budget -= advance;
	}

	for (char const *d = ellipsis; *d && n + 1 < sizeof(buf); d++)
		buf[n++] = *d;
	buf[n] = '\0';

	return Genode::String<256>(buf);
}
