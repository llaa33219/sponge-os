/* SPDX-License-Identifier: Apache-2.0
 *
 * renderer.cc — chrome painting for sponge_decorator (see
 * renderer.h).
 */

#include "renderer.h"

#include <nitpicker_gfx/box_painter.h>
#include <nitpicker_gfx/text_painter.h>

using namespace Sponge::Decorator;

using Rect  = Sponge::Wm_protocol::Rect;
using SRect = Genode::Surface_base::Rect;
using SArea = Genode::Surface_base::Area;
using SPoint = Genode::Surface_base::Point;


namespace {

SRect to_surface_rect(Rect r)
{
	if (r.w <= 0 || r.h <= 0)
		return SRect { };

	return SRect { SPoint { r.x, r.y },
	               SArea { (unsigned)r.w, (unsigned)r.h } };
}

template <typename SURF>
void fill_plane(SURF &surface, Rect region, unsigned char value)
{
	SRect const clipped = SRect::intersect(surface.clip(),
	                                       to_surface_rect(region));
	if (!clipped.valid())
		return;

	unsigned const stride = surface.size().w;
	auto *addr = surface.addr();

	for (int y = clipped.y1(); y <= clipped.y2(); y++)
		for (int x = clipped.x1(); x <= clipped.x2(); x++)
			addr[y*(int)stride + x].pixel = value;
}

void fill_pixels(Genode::Surface<Genode::Pixel_rgb888> &surface,
                 Rect region, Genode::Color color)
{
	Box_painter::paint(surface, to_surface_rect(region), color);
}

bool rect_valid(Rect r) { return r.w > 0 && r.h > 0; }

}  /* anonymous namespace */


void Chrome_renderer::draw(Genode::Surface<Genode::Pixel_rgb888> &pixels,
                           Genode::Surface<Genode::Pixel_alpha8> &alpha,
                           Genode::Surface<Genode::Pixel_input8> &input_mask,
                           Chrome_layout const &layout, Theme const &theme,
                           Text_painter::Font const &font,
                           char const *title, char const *label,
                           bool focused, bool resizeable)
{
	Chrome_style const style = chrome_style(theme, focused);

	if (!title) title = "";
	if (!label) label = "";

	/* ---- pixel plane: frame, band, buttons, text ---- */

	fill_pixels(pixels, layout.title, style.title_bg);

	for (Rect const &strip : layout.strips)
		fill_pixels(pixels, strip, style.frame_border);

	Wm_protocol::Button const kinds[3] = {
		Wm_protocol::Button::CLOSE,
		Wm_protocol::Button::FULLSCREEN,
		Wm_protocol::Button::MINIMIZE,
	};

	int leftmost_button_x = (int)layout.outer.x + (int)layout.outer.w;

	for (unsigned i = 0; i < 3; i++) {

		bool present = (i != 1) || resizeable;
		Rect const btn = layout.buttons[i];
		if (!present || !rect_valid(btn))
			continue;

		leftmost_button_x = (btn.x < leftmost_button_x) ? btn.x
		                                               : leftmost_button_x;

		Genode::Color const glyph_color =
			(kinds[i] == Wm_protocol::Button::CLOSE) ? style.button_close_glyph
			                                         : style.button_glyph;

		fill_pixels(pixels, btn, style.button_bg);

		Glyph_bitmap const glyph = button_glyph_bitmap(kinds[i]);
		Rect const g = glyph_rect(btn);

		for (unsigned gy = 0; gy < glyph.h; gy++)
			for (unsigned gx = 0; gx < glyph.w; gx++)
				if (glyph_pixel(glyph, gx, gy))
					fill_pixels(pixels,
					            Rect { (int)(g.x + gx), (int)(g.y + gy), 1, 1 },
					            glyph_color);
	}

	Rect const text_region = title_region(
		layout.title, leftmost_button_x - (int)TITLE_TEXT_PAD);

	if (rect_valid(text_region)) {
		Genode::String<256> const fitted =
			fit_title(font, title, label, text_region);

		if (fitted.string()[0] != '\0') {
			Text_origin const origin = title_text_origin(text_region, font);
			Text_painter::paint(
				pixels,
				Text_painter::Position(
					Text_painter::Fixpoint_number(origin.x),
					Text_painter::Fixpoint_number(origin.y)),
				font, style.title_text, fitted.string());
		}
	}

	/* optional resize-affordance tint (grip_bg alpha 0 = not painted) */
	if (!style.grip_bg.transparent()) {
		for (Rect const &band : layout.edge_bands)
			fill_pixels(pixels, band, style.grip_bg);
		for (Rect const &corner : layout.corners)
			fill_pixels(pixels, corner, style.grip_bg);
	}

	/* ---- alpha plane: opaque exactly over the painted frame ---- */

	Rect const alpha_regions[4] = {
		layout.title, layout.strips[0], layout.strips[1], layout.strips[2],
	};

	for (unsigned i = 0; i < 4; i++)
		fill_plane(alpha, alpha_regions[i], 0xff);

	/* ---- input mask: frame + corner affordances consume, client passes ---- */

	fill_plane(input_mask, layout.outer, 1);
	fill_plane(input_mask, layout.inner, 0);
	for (Rect const &corner : layout.corners)
		fill_plane(input_mask, corner, 1);
}
