/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_window — the provider-owned settings window (plain Genode Gui).
 *
 * Each first-party provider owns its UI (docs/12 §14.1): its own
 * top-level window, its own rows, its own errors. This class is the
 * shared rendering surface behind that rule — one small text-driven
 * window per provider, painted with the embedded 5x7 font (no Qt,
 * no libc, AGENTS.md §1.2 minimum privilege: Gui + Input only).
 *
 * The window is dumb: it renders a title, a scrollable row list, a
 * status line (bounded English errors / confirmations) and a help
 * line. All state and input policy live in sp_provider.h.
 */

#pragma once

#include <base/attached_dataspace.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <nitpicker_gfx/box_painter.h>
#include <os/pixel_rgb888.h>
#include <util/color.h>

#include "sp_font5x7.h"
#include "sp_rows.h"

namespace Sponge::Settings {

using SArea  = Genode::Surface_base::Area;
using SPoint = Genode::Surface_base::Point;
using SRect  = Genode::Surface_base::Rect;
using Pixel  = Genode::Pixel_rgb888;

class Provider_window
{
	public:

		enum {
			SCALE     = 2,
			CELL_W    = Font5x7::GLYPH_W * SCALE + 1,
			CELL_H    = Font5x7::GLYPH_H * SCALE + 1,
			WIDTH     = 620,
			HEIGHT    = 440,
			TITLE_H   = CELL_H + 8,
			STATUS_H  = CELL_H + 6,
			HELP_H    = CELL_H + 6,
			PAD       = 8,
			ROWS_X    = PAD,
			ROWS_Y    = TITLE_H + 4,
			ROWS_H    = HEIGHT - TITLE_H - STATUS_H - HELP_H - 8,
			ROWS_VISIBLE = ROWS_H / (CELL_H + 2),
			LABEL_COLS = 24,
		};

	private:

		Genode::Env           &_env;
		Gui::Connection        _gui;
		Gui::Top_level_view    _view;
		Framebuffer::Mode      _mode { SArea { WIDTH, HEIGHT }, false };
		Genode::Attached_dataspace _ds;

		Genode::Color const _bg      { Genode::Color::rgb(0x1e, 0x1e, 0x2e) };
		Genode::Color const _band    { Genode::Color::rgb(0x2a, 0x2a, 0x3e) };
		Genode::Color const _select  { Genode::Color::rgb(0x45, 0x45, 0x6a) };
		Genode::Color const _fg      { Genode::Color::rgb(0xd8, 0xd8, 0xe8) };
		Genode::Color const _dim     { Genode::Color::rgb(0x88, 0x88, 0xa0) };
		Genode::Color const _err     { Genode::Color::rgb(0xff, 0x88, 0x88) };
		Genode::Color const _ok      { Genode::Color::rgb(0x88, 0xe0, 0xa8) };
		Genode::Color const _accent  { Genode::Color::rgb(0xff, 0xd0, 0x70) };

		static Genode::Dataspace_capability
		_allocate_buffer(Gui::Connection &gui, Framebuffer::Mode const &mode)
		{
			/*
			 * The buffer must be allocated BEFORE the dataspace is
			 * requested (the sponge_console status_bar order — the
			 * client receives an invalid dataspace until it asked
			 * for a mode).
			 */
			gui.buffer(mode);
			return gui.framebuffer.dataspace();
		}

		void _text(Genode::Surface<Pixel> &surface, int x, int y,
		           char const *s, Genode::Color fg, unsigned max_chars) const
		{
			unsigned n = 0;
			for (char const *c = s; *c && n < max_chars; ++c, ++n) {

				Genode::uint8_t const * const glyph = Font5x7::glyph(*c);

				if (glyph) {
					for (int col = 0; col < Font5x7::GLYPH_W; ++col)
						for (int row = 0; row < Font5x7::GLYPH_H; ++row) {
							if (!(glyph[col] & (1u << row)))
								continue;
							Box_painter::paint(
								surface,
								SRect(SPoint(x + col * SCALE,
								             y + row * SCALE),
								      SArea(SCALE, SCALE)),
								fg);
						}
				}

				x += CELL_W;
			}
		}

		unsigned _text_width(char const *s, unsigned max_chars) const
		{
			unsigned n = 0;
			for (char const *c = s; *c && n < max_chars; ++c, ++n) { }
			return n * CELL_W;
		}

	public:

		Provider_window(Genode::Env &env, char const *identity)
		:
			_env(env),
			_gui(env, Gui::Session_label(identity)),
			_view(_gui),
			_ds(env.rm(), _allocate_buffer(_gui, _mode))
		{
			/*
			 * Window identity / display title. The session label
			 * and the view title both derive from the stable
			 * (package, settings-id) pair (docs/12 §14.7): the
			 * label is the wm/nitpicker identity, the title is the
			 * display text the decorator shows.
			 */
			_gui.enqueue<Gui::Session::Command::Title>(
				_view.id(), Gui::Title(identity));
			_gui.enqueue<Gui::Session::Command::Geometry>(
				_view.id(), SRect(SPoint(0, 0), _mode.area));
			_gui.execute();
		}

		Gui::Connection &gui() { return _gui; }

		unsigned rows_visible() const { return ROWS_VISIBLE; }

		void paint(char const *title, Row const *rows, unsigned n_rows,
		           unsigned selected, unsigned scroll,
		           char const *status, bool status_error,
		           char const *help)
		{
			Genode::Surface<Pixel> surface(
				_ds.local_addr<Pixel>(), _mode.area);

			Box_painter::paint(surface, SRect(SPoint(0, 0), _mode.area), _bg);

			/* title band */
			Box_painter::paint(surface,
			                   SRect(SPoint(0, 0), SArea(WIDTH, TITLE_H)),
			                   _band);
			_text(surface, PAD, 6, title, _accent, 48);

			/* row list */
			for (unsigned vis = 0; vis < ROWS_VISIBLE; ++vis) {
				unsigned const idx = scroll + vis;
				if (idx >= n_rows)
					break;

				Row const &r = rows[idx];
				int const y = ROWS_Y + (int)vis * (CELL_H + 2);
				bool const is_selected = (idx == selected);

				if (is_selected)
					Box_painter::paint(
						surface,
						SRect(SPoint(2, y - 2), SArea(WIDTH - 4, CELL_H + 3)),
						_select);

				Genode::Color const label_fg =
					r.kind == Row_kind::INFO ? _dim
				  : is_selected ? _fg
				  : r.kind == Row_kind::ACTION ? _accent
				  : _fg;

				_text(surface, ROWS_X, y, r.label, label_fg, LABEL_COLS);

				int const vx = ROWS_X + LABEL_COLS * CELL_W;
				char const *value = row_display(r);

				Genode::Color const value_fg =
					r.kind == Row_kind::INFO ? _dim
				  : is_selected ? _fg
				  : _dim;

				unsigned const budget =
					(unsigned)((WIDTH - vx - PAD) / CELL_W);
				_text(surface, vx, y, value, value_fg, budget);
			}

			/* status line */
			int const status_y = HEIGHT - STATUS_H - HELP_H;
			Box_painter::paint(surface,
			                   SRect(SPoint(0, status_y), SArea(WIDTH, STATUS_H)),
			                   _band);
			_text(surface, PAD, status_y + 3, status,
			      status_error ? _err : _ok,
			      (unsigned)((WIDTH - 2 * PAD) / CELL_W));

			/* help line */
			int const help_y = HEIGHT - HELP_H;
			_text(surface, PAD, help_y + 3, help, _dim,
			      (unsigned)((WIDTH - 2 * PAD) / CELL_W));

			_gui.framebuffer.refresh({ { 0, 0 }, _mode.area });
		}
};

}  /* namespace Sponge::Settings */
