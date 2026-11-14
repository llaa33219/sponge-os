/* SPDX-License-Identifier: Apache-2.0
 *
 * view.cc — native listing painting for sponge_settings. See view.h.
 */

#include "view.h"

#include <nitpicker_gfx/box_painter.h>

namespace Sponge::Settings {

using Genode::Pixel_rgb888;
using Genode::Surface;
using Rect = Genode::Surface_base::Rect;

namespace {

/*
 * Icon marker: the shell's capability contract gives it no package
 * File_system session and docs/12 §14 specifies no icon-pixel
 * delivery channel, so the declared icon is rendered as a distinct
 * icon-present marker (filled frame glyph) while an absent icon falls
 * back to the default glyph (hollow frame). The declared icon NAME
 * stays on the model (Entry::icon) — this function is the single seam
 * where real icon pixels would plug in when a delivery channel exists.
 */
void draw_icon_marker(Surface<Pixel_rgb888> &surface, Rect row,
                      bool icon_present, Genode::Color mark)
{
	int const box = Layout::ICON_BOX;
	int const x   = Layout::MARGIN;
	int const y   = row.y1() + (int)row.h() / 2 - box / 2;

	Rect const frame { x, y, (unsigned)box, (unsigned)box };
	Box_painter::paint(surface, frame, mark);

	if (icon_present) {
		int const inner = box - 4;
		Box_painter::paint(surface,
		                   Rect { x + 2, y + 2, (unsigned)inner, (unsigned)inner },
		                   Palette::WINDOW_BG);
		Box_painter::paint(surface,
		                   Rect { x + 4, y + 4, (unsigned)inner - 4, (unsigned)inner - 4 },
		                   mark);
	}
}


void draw_text(Surface<Pixel_rgb888> &surface,
               Text_painter::Font const &font,
               int x, int y, Genode::Color color, char const *text)
{
	Text_painter::paint(
		surface,
		Text_painter::Position(Text_painter::Fixpoint_number(x),
		                       Text_painter::Fixpoint_number(y)),
		font, color, text);
}


void join_keywords(Entry const &entry, char *buf, unsigned buf_size)
{
	unsigned n = 0;

	auto append = [&] (char const *s) {
		while (*s != '\0' && n + 1 < buf_size)
			buf[n++] = *s++;
	};

	for (unsigned k = 0; k < entry.num_keywords; ++k) {
		if (k > 0)
			append(", ");
		append(entry.keywords[k].string());
	}
	buf[n] = '\0';
}

} /* anonymous namespace */


void paint_listing(Surface<Pixel_rgb888> &surface,
                   Text_painter::Font const &font,
                   Model const &model,
                   int scroll,
                   char const *status_line,
                   int highlight)
{
	unsigned const W = surface.size().w;
	unsigned const H = surface.size().h;

	if (W == 0 || H == 0)
		return;

	int const list_h = (int)H - Layout::STATUS_H > 0
	                 ? (int)H - Layout::STATUS_H : 0;

	Box_painter::paint(surface, Rect { 0, 0, W, H }, Palette::WINDOW_BG);

	if (model.num_entries == 0) {
		draw_text(surface, font, Layout::MARGIN, 20,
		          Palette::EMPTY_TEXT, "No settings entries installed");
	}

	Layout::Row rows[Layout::MAX_ROWS];
	unsigned const num_rows = Layout::build_rows(model, rows, Layout::MAX_ROWS);

	for (unsigned r = 0; r < num_rows; ++r) {

		Layout::Row const &row = rows[r];

		int const y = row.y - scroll;

		if (y + row.h <= 0 || y >= list_h)
			continue;

		Rect const band { 0, y, W, (unsigned)row.h };

		int const clip_top    = y > 0 ? y : 0;
		int const clip_bottom = y + row.h < list_h ? y + row.h : list_h;

		surface.clip(Rect { 0, clip_top, W,
		                    (unsigned)(clip_bottom - clip_top) });

		if (row.kind == Layout::Row::Kind::CATEGORY) {

			Box_painter::paint(surface, band, Palette::HEADER_BG);

			int const text_y = y + ((int)row.h - (int)font.height()) / 2;

			draw_text(surface, font, Layout::MARGIN, text_y,
			          Palette::HEADER_TEXT, category_label(row.category));

		} else {

			Entry const &entry = model.entries[row.entry];
			bool const pending = (highlight == (int)row.entry);

			if (pending)
				Box_painter::paint(surface, band, Palette::PENDING_BG);

			draw_icon_marker(surface, band, entry.has_icon(),
			                 entry.has_icon() ? Palette::ICON_MARK
			                                  : Palette::DEFAULT_MARK);

			int const text_x = Layout::MARGIN + Layout::ICON_BOX + 10;
			int const title_y = y + 6;

			draw_text(surface, font, text_x, title_y,
			          Palette::ENTRY_TITLE, entry.title.string());

			if (entry.num_keywords > 0) {
				char hint[128];
				join_keywords(entry, hint, sizeof(hint));
				draw_text(surface, font, text_x,
				          title_y + (int)font.height() + 3,
				          Palette::ENTRY_HINT, hint);
			}
		}

		surface.clip(Rect { 0, 0, W, H });
	}

	/* bounded single-line English status surface */
	if ((int)H >= Layout::STATUS_H) {
		int const bar_y = (int)H - Layout::STATUS_H;

		Box_painter::paint(surface, Rect { 0, bar_y, W, (unsigned)Layout::STATUS_H },
		                   Palette::STATUS_BG);
		Box_painter::paint(surface, Rect { 0, bar_y, W, 1 }, Palette::DIVIDER);

		surface.clip(Rect { 0, bar_y, W, (unsigned)Layout::STATUS_H });

		int const text_y = bar_y + (Layout::STATUS_H - (int)font.height()) / 2;

		draw_text(surface, font, Layout::MARGIN, text_y,
		          Palette::STATUS_TEXT, status_line);

		surface.clip(Rect { 0, 0, W, H });
	}
}

} /* namespace Sponge::Settings */
