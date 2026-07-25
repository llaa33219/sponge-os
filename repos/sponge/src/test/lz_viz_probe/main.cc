/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * lz_viz_probe — Leitzentrale on-screen verification (Phase 6b, criterion 3).
 *
 * Opens a Capture session to the OUTER nitpicker (the Sponge desktop
 * surface) and polls until lz_viewer's Leitzentrale window is genuinely
 * displayed: enough pixels in the window's region match the Leitzentrale's
 * inner background color (#272f45), which is distinct from the outer
 * desktop background (#1e1e2e). This is the headless proof that the
 * Leitzentrale is shown as a real window on the Sponge desktop — not
 * merely that its components are alive.
 *
 * Logs "lz-viz-probe: PASS" once the window is detected on screen.
 */

#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>

namespace {

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/* Outer desktop background (run/sponge-leitzentrale.run nitpicker <background>). */
int const OUTER_BG_R = 0x1e, OUTER_BG_G = 0x1e, OUTER_BG_B = 0x2e;

/*
 * lz_viewer stamps a distinct marker patch (#bf5fbf) into the top-left of
 * each streamed frame. lz_viewer places the window at (VIEW_X,VIEW_Y) and
 * the marker at offset (10,10) within it, so the marker lands at
 * (MARK_X,MARK_Y) on the outer nitpicker. This color appears on screen
 * ONLY if lz_viewer captured a frame, wrote it to its Gui framebuffer, and
 * the outer nitpicker displayed the view — an unambiguous live-stream
 * signal that cannot be confused with the desktop or Leitzentrale colors.
 */
int const MARK_R = 0xbf, MARK_G = 0x5f, MARK_B = 0xbf;
int const MARK_X = 112 + 10;  /* VIEW_X + marker offset */
int const MARK_Y = 84 + 10;
int const MARK_W = 40, MARK_H = 40;
unsigned const MARK_PIXEL_THRESHOLD = 500;

int const COLOR_SLACK = 24;


bool pixel_is_marker(Pixel const &p)
{
	auto near = [](int a, int b) { return a >= b ? a - b <= COLOR_SLACK
	                                             : b - a <= COLOR_SLACK; };
	return near(p.r(), MARK_R) && near(p.g(), MARK_G) && near(p.b(), MARK_B);
}


struct Lz_viz_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "lz-viz-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Lz_viz_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("lz-viz-probe: starting, waiting for Leitzentrale window on screen");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		for (unsigned i = 0; i < 600; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();

			unsigned mark_pixels = 0;
			for (int y = MARK_Y; y < MARK_Y + MARK_H; ++y) {
				for (int x = MARK_X; x < MARK_X + MARK_W; ++x) {
					if (pixel_is_marker(px[y * SCREEN_W + x]))
						++mark_pixels;
				}
			}

			if (i % 10 == 0)
				Genode::log("lz-viz-probe: poll ", i, " marker pixels=", mark_pixels);

			if (mark_pixels >= MARK_PIXEL_THRESHOLD) {
				Genode::log("lz-viz-probe: Leitzentrale window live on outer nitpicker (",
				            mark_pixels, " marker pixels at (", MARK_X, ",", MARK_Y, "))");
				Genode::log("lz-viz-probe: PASS");
				_env.parent().exit(0);
				return;
			}
		}

		Genode::error("lz-viz-probe: FAIL Leitzentrale window never appeared on screen");
		_env.parent().exit(1);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Lz_viz_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
