/*
 * \brief  Full-screen fault console / display client for Sponge
 * \author Sponge OS contributors
 * \date   2026
 *
 * A Qt-free, libc-free, single-purpose Gui client. It exists so that a
 * top-level fault (docs/17-runtime-component-topology.md §5) has a
 * user-visible surface that depends on NOTHING inside the subtree it
 * reports on: no Qt, no desktop environment, no configuration backend,
 * no session to any supervised component.
 *
 * Lifecycle. The component is started once, from a static sandbox, and
 * then follows the `console_state` report relayed by the TOP-LEVEL
 * report_rom:
 *
 *   armed="yes"  -> open a Gui session, map a full-screen buffer, paint.
 *   armed="no"   -> close the Gui session again; the display reverts to
 *                   whatever is underneath.
 *
 * Opening and CLOSING the session (rather than painting an idle screen)
 * is what makes "the console never coexists with the desktop" a
 * structural property instead of a convention: at no instant does this
 * component hold a Gui session while the desktop does, because the
 * supervisor arms it only after it has already destroyed the desktop
 * subtree (a config rewrite that bumps every <start> version).
 *
 * It also has a second role for the acceptance scenario: with
 * `role="desktop"` it stands in for the desktop, so that "the console
 * took the display" is a PIXEL claim and not a log line.
 *
 * Display ownership. This component does NOT open the framebuffer. The
 * compositor keeps the fb exactly as it is - one writer (vesa_fb or
 * boot_fb) via its Capture session - and this component is simply the
 * only Gui client while it runs. Seizing the device instead would mean
 * two components mapping the same physical memory, which is the
 * permanent corruption docs/11-environment.md row 12 measured on real
 * hardware (boot_fb's Capture blit repaints only the compositor's dirty
 * rectangles, so stale console text is never erased). "Takeover" here
 * means taking over the DISPLAY, which is the part the user sees.
 *
 * Sessions (minimum privilege, AGENTS.md §1.2): ROM "config",
 * ROM "console_state", and - only while armed - one Gui session.
 * No Timer, no Report, no session to the supervised subtree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <nitpicker_gfx/box_painter.h>
#include <os/pixel_rgb888.h>
#include <util/color.h>
#include <util/string.h>

/* local includes */
#include "font5x7.h"
#include "text_scan.h"

namespace Sponge {

	struct Console { struct Main; };
}

using namespace Genode;

using SArea  = Surface_base::Area;
using SPoint = Surface_base::Point;
using SRect  = Surface_base::Rect;

struct Sponge::Console::Main
{
	enum { SCALE = 3, LINE_GAP = 4, BUFSZ = 96 };

	Env &_env;

	/* static config: label, role, and the domain size to request */
	Attached_rom_dataspace _config { _env, "config" };

	/*
	 * The arming report, relayed by the top-level report_rom. It is
	 * requested ONLY by a console-role instance: a desktop-role instance
	 * is painted by its parent config and must not hold a second channel
	 * (and must not fail when no such relay exists in its sandbox).
	 */
	Constructible<Attached_rom_dataspace> _armed { };

	char _label_buf[BUFSZ] = "console";
	char _role_buf [BUFSZ] = "console";
	char _title_buf[BUFSZ] = "SYSTEM FAULT - RESTARTING";
	Color _bg { Color::rgb(0x60, 0x10, 0x10) };
	Color _fg { Color::rgb(0xff, 0xe0, 0xe0) };

	/* --- the armed screen ---------------------------------------- */

	struct Screen
	{
		/*
		 * The Framebuffer buffer must be allocated BEFORE the dataspace
		 * capability is requested: a Gui client receives an invalid
		 * dataspace until it has asked for a mode (status_bar does the
		 * same - the order is load-bearing, not stylistic).
		 */
		static Dataspace_capability _allocate_buffer(Gui::Connection &gui,
		                                             Framebuffer::Mode const &mode)
		{
			gui.buffer(mode);
			return gui.framebuffer.dataspace();
		}

		Gui::Connection         gui;
		Gui::Top_level_view     view;
		Framebuffer::Mode       mode { .area = { 1024, 768 }, .alpha = false };
		Attached_dataspace      ds;
		Color                   bg;
		Color                   fg;
		char                    role [BUFSZ];
		char                    title[BUFSZ];

		Screen(Env &env, char const *label, Color bg_, Color fg_,
		       char const *role_, char const *title_)
		:
			gui(env, Session_label(label)),
			view(gui),
			mode { .area = gui.panorama().convert<SArea>(
				[&] (Gui::Rect r) { return r.area; },
				[&] (Gui::Undefined) { return SArea { 1024, 768 }; }),
			       .alpha = false },
			ds(env.rm(), _allocate_buffer(gui, mode)),
			bg(bg_), fg(fg_)
		{
			Sponge::copy_value(role, role_, BUFSZ, BUFSZ);
			Sponge::copy_value(title, title_, BUFSZ, BUFSZ);
		}

		SArea area() const { return mode.area; }

		void _draw_text(Surface<Pixel_rgb888> &surface,
		                SPoint at, char const *text) const
		{
			for (char const *c = text; *c; c++) {

				Genode::uint8_t const * const glyph = Font5x7::glyph(*c);

				if (glyph) {
					for (int col = 0; col < Font5x7::GLYPH_W; col++)
						for (int row = 0; row < Font5x7::GLYPH_H; row++) {
							if (!(glyph[col] & (1u << row)))
								continue;
							Box_painter::paint(surface,
							                   SRect(SPoint(at.x + col * SCALE,
							                              at.y + row * SCALE),
							                        SArea(SCALE, SCALE)),
							                   fg);
						}
				}

				at.x += Font5x7::GLYPH_W * SCALE + LINE_GAP;
			}
		}

		void paint()
		{
			SArea const a = mode.area;

			Surface<Pixel_rgb888> surface(ds.local_addr<Pixel_rgb888>(), a);

			Box_painter::paint(surface, SRect(SPoint(0, 0), a), bg);

			/* a brighter band across the top: makes the takeover
			 * visible from a distance and gives a capture probe a
			 * second, differently-shaded reference region */
			Box_painter::paint(surface,
			                   SRect(SPoint(0, 0), SArea(a.w, a.h / 8)),
			                   Color::clamped_rgb(int(bg.r) + 60,
			                                       int(bg.g) + 60,
			                                       int(bg.b) + 60));

			int const text_h = Font5x7::GLYPH_H * SCALE;
			int const x     = 24;
			int const y     = int(a.h) / 3;

			_draw_text(surface, SPoint(x, y),            role);
			_draw_text(surface, SPoint(x, y + 4 * text_h), title);
			_draw_text(surface, SPoint(x, y + 8 * text_h),
			            "NO INPUT. WAIT FOR RECOVERY.");

			gui.framebuffer.refresh({ { 0, 0 }, a });

			gui.enqueue<Gui::Session::Command::Geometry>(view.id(),
			                                               SRect(SPoint(0, 0), a));
			gui.execute();
		}
	};

	Constructible<Screen> _screen { };

	Signal_handler<Main> _armed_sigh {
		_env.ep(), *this, &Main::_handle_armed };

	/* ------------------------------------------------------------------ */

	static void _hex(char const *s, unsigned n, unsigned &out)
	{
		unsigned v = 0;
		for (unsigned i = 0; i < n; i++) {
			char const c = s[i];
			v <<= 4;
			if      (c >= '0' && c <= '9') v |= unsigned(c - '0');
			else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
		}
		out = v;
	}

	static void _attr(char const *text, size_t sz, char const *key,
	                  char *dst, size_t cap)
	{
		dst[0] = '\0';

		if (!text)
			return;

		char const *v = nullptr; size_t n = 0;

		if (Sponge::scan_attr(text, sz, key, v, n))
			Sponge::copy_value(dst, v, n, cap);
	}

	static bool _yes(char const *text, size_t sz, char const *key)
	{
		if (!text)
			return false;

		char buf[16];
		_attr(text, sz, key, buf, sizeof(buf));

		return buf[0] == 'y' || buf[0] == 'Y';
	}

	/* ------------------------------------------------------------------ */

	void _read_static_config()
	{
		_config.update();

		if (!_config.valid())
			return;

		char const *cfg = _config.local_addr<char const>();
		size_t      const sz = _config.size();

		_attr(cfg, sz, "label", _label_buf, BUFSZ);
		_attr(cfg, sz, "role",  _role_buf,  BUFSZ);
		_attr(cfg, sz, "title", _title_buf, BUFSZ);

		char color[BUFSZ];
		_attr(cfg, sz, "color", color, BUFSZ);

		if (color[0] == '#' && Genode::strlen(color) >= 7) {
			unsigned r = 0, g = 0, b = 0;
			_hex(color + 1, 2, r);
			_hex(color + 3, 2, g);
			_hex(color + 5, 2, b);
			_bg = Color::rgb(Genode::uint8_t(r), Genode::uint8_t(g),
			                 Genode::uint8_t(b));
			_fg = Color::rgb(Genode::uint8_t(r + (255 - r) / 3),
			                 Genode::uint8_t(g + (255 - g) / 3),
			                 Genode::uint8_t(b + (255 - b) / 3));
		}

		if (_role_buf[0] != 'd')
			Sponge::copy_value(_role_buf, "console", BUFSZ, BUFSZ);
	}

	bool _is_console_role() const
	{
		return _role_buf[0] != 'd';   /* anything but "desktop" */
	}

	void _handle_armed()
	{
		if (_armed.constructed())
			_armed->update();

		bool const armed =
			_armed.constructed() &&
			_armed->valid() &&
			_yes(_armed->local_addr<char const>(), _armed->size(), "armed");

		if (armed == _screen.constructed())
			return;

		/* destroy before (re)constructing: the Gui session is closed and
		 * re-opened, which is what makes the console's presence on the
		 * display an exclusive property */
		if (_screen.constructed())
			_screen.destruct();

		if (armed) {

			char title[BUFSZ] = "SYSTEM FAULT - RESTARTING";
			char color[BUFSZ] = "#601010";

			if (_armed.constructed() && _armed->valid()) {
				_attr(_armed->local_addr<char const>(), _armed->size(),
				      "title", title, BUFSZ);
				_attr(_armed->local_addr<char const>(), _armed->size(),
				      "color", color, BUFSZ);
			}

			Color bg { _bg }, fg { _fg };

			if (color[0] == '#' && Genode::strlen(color) >= 7) {
				unsigned r = 0, g = 0, b = 0;
				_hex(color + 1, 2, r);
				_hex(color + 3, 2, g);
				_hex(color + 5, 2, b);
				bg = Color::rgb(Genode::uint8_t(r), Genode::uint8_t(g),
				                Genode::uint8_t(b));
				fg = Color::rgb(Genode::uint8_t(r + (255 - r) / 3),
				                Genode::uint8_t(g + (255 - g) / 3),
				                Genode::uint8_t(b + (255 - b) / 3));
			}

			_screen.construct(_env, _label_buf, bg, fg, _role_buf, title);
			_screen->paint();

			log("sponge_console: ARMED role=", (char const *)_role_buf,
			    " label=", (char const *)_label_buf, " ", _screen->area().w, "x",
			    _screen->area().h, " owns the display");
		} else {

			log("sponge_console: DISARMED — display released");
		}
	}

	/*
	 * A desktop-role instance paints once from its static config and is
	 * done: it has no arming channel by design. The screen MUST outlive
	 * this function - the Gui session is what makes the client exist in
	 * the compositor's view stack, so a stack-local screen would close
	 * the session on return and leave the display blank.
	 */
	void _paint_once()
	{
		if (_screen.constructed())
			return;

		_screen.construct(_env, _label_buf, _bg, _fg, _role_buf, _title_buf);
		_screen->paint();

		log("sponge_console: role=", (char const *)_role_buf,
		    " label=", (char const *)_label_buf, " ", _screen->area().w,
		    "x", _screen->area().h, " owns the display");
	}

	Main(Env &env) : _env(env)
	{
		_read_static_config();

		if (_is_console_role()) {
			_armed.construct(_env, "console_state");
			_armed->sigh(_armed_sigh);
			_handle_armed();
			log("sponge_console: ready (role=", (char const *)_role_buf,
			    " label=", (char const *)_label_buf, ", awaiting console_state)");
		} else {
			log("sponge_console: ready (role=", (char const *)_role_buf,
			    " label=", (char const *)_label_buf, ")");
			_paint_once();
		}
	}
};

void Component::construct(Genode::Env &env)
{
	static Sponge::Console::Main main { env };
}
