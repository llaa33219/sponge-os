/* SPDX-License-Identifier: Apache-2.0
 *
 * paint.h — window painting for the settings_demo providers.
 *
 * Lightweight nitpicker_gfx surface painting (Box_painter plus the
 * embedded 5x7 font), the sponge_decorator / sponge_settings
 * precedent. No Qt, no OpenGL, no libc. The palette is one
 * compile-time block (AGENTS.md §3.4 deviation noted in
 * sponge_settings/view.h: a third-party provider has no theme ROM in
 * its capability contract either).
 */

#pragma once

#include <nitpicker_gfx/box_painter.h>
#include <os/pixel_rgb888.h>
#include <os/surface.h>

#include "font5x7.h"

namespace Sponge::Settings_demo {

namespace Palette {

inline constexpr Genode::Color WINDOW_BG  = Genode::Color::rgb(0x1e, 0x1e, 0x2e);
inline constexpr Genode::Color HEADER_BG  = Genode::Color::rgb(0x27, 0x27, 0x3a);
inline constexpr Genode::Color HEADER_TXT = Genode::Color::rgb(0x9c, 0xd1, 0xf2);
inline constexpr Genode::Color FIELD_BG   = Genode::Color::rgb(0x16, 0x16, 0x22);
inline constexpr Genode::Color LABEL_TXT  = Genode::Color::rgb(0x8a, 0x8a, 0x9c);
inline constexpr Genode::Color VALUE_TXT  = Genode::Color::rgb(0xe8, 0xe8, 0xf0);
inline constexpr Genode::Color CARET      = Genode::Color::rgb(0x6c, 0x8e, 0xc8);
inline constexpr Genode::Color HINT_TXT   = Genode::Color::rgb(0x70, 0x70, 0x84);
inline constexpr Genode::Color STATUS_BG  = Genode::Color::rgb(0x16, 0x16, 0x22);
inline constexpr Genode::Color STATUS_TXT = Genode::Color::rgb(0xd8, 0xd8, 0xe2);
inline constexpr Genode::Color DIVIDER    = Genode::Color::rgb(0x33, 0x33, 0x46);

} /* namespace Palette */


struct Provider_paint
{
	char const *header;
	char const *value_label;
	char const *value;
	char const *hint;
	char const *status;
	bool        caret_on;
};


namespace detail {

using Rect = Genode::Surface_base::Rect;

inline Rect rect(int x, int y, unsigned w, unsigned h)
{
	return Rect { .at = { x, y }, .area = { w, h } };
}

inline void draw_glyph(Genode::Surface<Genode::Pixel_rgb888> &surface,
                       int x, int y, int scale, Genode::Color color,
                       Genode::uint8_t const &column)
{
	for (int row = 0; row < Font5x7::GLYPH_H; ++row) {
		if ((column & (1u << row)) == 0)
			continue;
		Box_painter::paint(surface, rect(x, y + row * scale,
		                                 (unsigned)scale, (unsigned)scale),
		                   color);
	}
}

} /* namespace detail */


inline void draw_text(Genode::Surface<Genode::Pixel_rgb888> &surface,
                      int x, int y, int scale, Genode::Color color,
                      char const *text)
{
	for (unsigned i = 0; text[i] != '\0'; ++i) {
		Genode::uint8_t const *glyph = Font5x7::glyph(text[i]);
		if (glyph == nullptr)
			continue;
		for (int col = 0; col < Font5x7::GLYPH_W; ++col)
			detail::draw_glyph(surface,
			                   x + (i * (Font5x7::GLYPH_W + 1) + col) * scale,
			                   y, scale, color, glyph[col]);
	}
}


inline int text_width(char const *text, int scale)
{
	unsigned n = 0;
	while (text[n] != '\0') ++n;
	if (n == 0)
		return 0;
	return (int)(n * (Font5x7::GLYPH_W + 1) - 1) * scale;
}


/*
 * Paint one full frame. Geometry is fixed and dense: header band,
 * one labeled value field with a block caret, one hint line, and the
 * bounded status bar. 'status' and 'hint' are caller-bounded English
 * (fill_error_text output or short literals).
 */
inline void paint_provider(Genode::Surface<Genode::Pixel_rgb888> &surface,
                           Provider_paint const &p)
{
	unsigned const W = surface.size().w;
	unsigned const H = surface.size().h;

	if (W == 0 || H == 0)
		return;

	int const scale    = 2;
	int const glyph_h  = Font5x7::GLYPH_H * scale;
	int const margin   = 12;
	int const header_h = 26;
	int const status_h = 22;

	Box_painter::paint(surface, detail::rect(0, 0, W, H),
	                   Palette::WINDOW_BG);
	Box_painter::paint(surface, detail::rect(0, 0, W, (unsigned)header_h),
	                   Palette::HEADER_BG);
	Box_painter::paint(surface, detail::rect(0, header_h, W, 1),
	                   Palette::DIVIDER);

	draw_text(surface, margin, (header_h - Font5x7::GLYPH_H) / 2, 1,
	          Palette::HEADER_TXT, p.header);

	int const field_y = header_h + 14;
	int const field_h = glyph_h + 16;

	draw_text(surface, margin, field_y, 1, Palette::LABEL_TXT, p.value_label);

	Box_painter::paint(surface, detail::rect(0, field_y + 12, W,
	                                         (unsigned)field_h),
	                   Palette::FIELD_BG);

	int const value_x = margin;
	int const value_y = field_y + 12 + (field_h - glyph_h) / 2;

	draw_text(surface, value_x, value_y, scale, Palette::VALUE_TXT, p.value);

	if (p.caret_on) {
		int const caret_x = value_x + text_width(p.value, scale) + scale;
		Box_painter::paint(surface,
		                   detail::rect(caret_x, value_y,
		                                (unsigned)(Font5x7::GLYPH_W * scale),
		                                (unsigned)glyph_h),
		                   Palette::CARET);
	}

	draw_text(surface, margin, field_y + 12 + field_h + 12, 1,
	          Palette::HINT_TXT, p.hint);

	if ((int)H >= status_h) {
		int const bar_y = (int)H - status_h;

		Box_painter::paint(surface, detail::rect(0, bar_y, W,
		                                         (unsigned)status_h),
		                   Palette::STATUS_BG);
		Box_painter::paint(surface, detail::rect(0, bar_y, W, 1),
		                   Palette::DIVIDER);

		draw_text(surface, margin, bar_y + (status_h - Font5x7::GLYPH_H) / 2,
		          1, Palette::STATUS_TXT, p.status);
	}
}

} /* namespace Sponge::Settings_demo */
