/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_de_probe — headless GUI verification probe for Sponge DE.
 *
 * This component proves the Phase 3 completion criteria inside a single
 * headless Genode instance, with no host display and no fb_sdl:
 *
 *   (a) sponge-de's window is actually rendered into nitpicker's
 *       composited screen, verified by reading pixels through a Capture
 *       session (background area == nitpicker bg color; demo-domain
 *       area != bg color).
 *   (b) synthetic pointer input injected into nitpicker reaches the
 *       sponge-de widget, verified by an Event session round-trip:
 *       inject absolute motion + BTN_LEFT click, then watch sponge-de's
 *       "input" report (relayed by report_rom) for confirmation.
 *
 * It is a plain Genode component (Component::construct, no libc/Qt),
 * following AGENTS.md §3.1 (qualified Genode types, no exceptions).
 *
 * Success logs "sponge-de-probe: PASS"; on any failure it logs
 * "sponge-de-probe: FAIL <reason>" and exits non-zero, so the run
 * scenario (run/sponge-de-test.run) fails via run_genode_until timeout.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <event_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

/*
 * Screen geometry. The probe's capture buffer defines nitpicker's
 * panorama (bounding box) because there is no framebuffer driver in
 * this scenario. 1024x768 matches the domain layout below.
 */
unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e), from run/sponge-de-test.run.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Themed window_bg color (#313244, from default.theme). The demo window
 * fills its widget with this color, so the demo-domain center must show
 * it — a positive assertion that the window is rendered (stronger than
 * merely "differs from background", which would pass on an absent/black
 * window too).
 */
int const WIN_R = 0x31, WIN_G = 0x32, WIN_B = 0x44;

/*
 * Demo domain geometry — must match the nitpicker "demo" domain in the
 * run scenario (xpos 192, ypos 172, 640x480).
 */
int const DEMO_X = 192, DEMO_Y = 172;
int const DEMO_W = 640,  DEMO_H = 480;

/*
 * Probe sample points (absolute screen coordinates).
 *
 *   BG_PT   : (900,100) — outside every domain, in the desktop region
 *             nitpicker reliably composites with the <background> color.
 *             (Points below/beside the demo domain, e.g. (900,700), are
 *             left at buffer-zero by nitpicker's lazy dirty-rect
 *             compositing when there is no framebuffer driver to force
 *             full-screen redraws — so they are not usable as a stable
 *             background reference in the headless scenario.)
 *   DEMO_PT : demo-domain center (512,412) — must show the themed
 *             window_bg color (the window is drawn there).
 *
 * The panel-domain sample is informational only: the default theme's
 * panel_bg equals the nitpicker background (#1e1e2e), so a panel point
 * is NOT expected to differ. It is logged but never gates PASS.
 */
struct Pt { int x, y; };
Pt const BG_PT    { 900, 100 };
Pt const DEMO_PT  { DEMO_X + DEMO_W/2, DEMO_Y + DEMO_H/2 }; /* (512,412) */
Pt const PANEL_PT { 512, 14  };

/*
 * Synthetic click target. Window top-left is the demo-domain origin
 * (192,172); the demo button is roughly centered horizontally and
 * ~288px below the window top, i.e. screen (512,460). The point stays
 * inside the demo window so the press reaches sponge-de regardless of
 * which child widget happens to sit under the cursor.
 */
Pt const CLICK_PT { 512, 460 };

/* Allow a few bits of slack when comparing solid colors (blending at
 * view edges can shift a channel by a couple of units). */
int const COLOR_TOLERANCE = 8;

bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                : b - a <= COLOR_TOLERANCE; }

bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

bool pixel_is_window(Pixel const &p)
{
	return channel_near(p.r(), WIN_R)
	    && channel_near(p.g(), WIN_G)
	    && channel_near(p.b(), WIN_B);
}

} /* anonymous namespace */


struct Sponge_de_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "sponge-de-probe" };
	Event::Connection   _event   { _env, "sponge-de-probe" };

	/*
	 * ROM relayed by report_rom: a <policy> maps ROM label
	 * "sponge_de_input" to report "sponge-de -> input".
	 */
	Genode::Attached_rom_dataspace _input_rom { _env, "sponge_de_input" };

	/*
	 * Pixel buffer is only valid after Capture::Connection::buffer(),
	 * so it is constructed lazily in the body.
	 */
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};


	Sponge_de_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("sponge-de-probe: starting");

		/*
		 * Allocate the shared capture buffer. With nitpicker's empty
		 * <capture/> config the probe receives an unconstrained policy,
		 * so its buffer dimensions define the panorama. This is the
		 * same role fb_sdl plays in the interactive scenario.
		 */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * (a) Wait until sponge-de has actually painted. We poll
		 * capture_at() and inspect two pixels: the desktop background
		 * must be the configured nitpicker bg color, and the
		 * demo-domain center must show the themed window_bg color
		 * (positive proof the window is composited there).
		 */
		bool rendered = false;
		for (unsigned i = 0; i < 600; ++i) {  /* up to ~60s */
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel bg    = px[BG_PT.y  * SCREEN_W + BG_PT.x];
			Pixel demo  = px[DEMO_PT.y * SCREEN_W + DEMO_PT.x];

			if (i % 10 == 0)
				Genode::log("sponge-de-probe: capture poll ", i,
				            " bg=", Genode::Hex(bg.pixel),
				            " demo=", Genode::Hex(demo.pixel));

			if (pixel_is_bg(bg) && pixel_is_window(demo)) {
				rendered = true;
				Genode::log("sponge-de-probe: window detected in demo domain");
				break;
			}
		}

		if (!rendered) {
			_fail("rendering never appeared on nitpicker");
			return;
		}

		/* Log the (informational) panel-domain pixel. */
		{
			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel panel = px[PANEL_PT.y * SCREEN_W + PANEL_PT.x];
			Genode::log("sponge-de-probe: panel pixel at (", PANEL_PT.x,
			            ",", PANEL_PT.y, ") = ", Genode::Hex(panel.pixel),
			            " (informational; theme panel_bg == nitpicker bg)");
		}

		/*
		 * (b) Input round-trip. Establish a baseline of the input
		 * report, inject a synthetic click into nitpicker's Event
		 * session, then poll the report until sponge-de confirms it.
		 */
		_input_rom.update();  /* baseline */

		Genode::log("sponge-de-probe: injecting click at (",
		            CLICK_PT.x, ",", CLICK_PT.y, ")");
		_event.with_batch([&](Event::Session_client::Batch &batch) {
			batch.submit(Input::Absolute_motion{ CLICK_PT.x, CLICK_PT.y });
			batch.submit(Input::Press   { Input::BTN_LEFT });
			batch.submit(Input::Release { Input::BTN_LEFT });
		});

		bool delivered = false;
		for (unsigned i = 0; i < 200; ++i) {  /* up to ~20s */
			_timer.msleep(100);
			_input_rom.update();

			/*
			 * Detect the press by content: before sponge-de receives
			 * any input the report has no "press" attribute.
			 */
			if (_input_rom.valid() && _input_rom.xml().has_attribute("press")) {
				delivered = true;
				Genode::log("sponge-de-probe: input report confirms press");
				break;
			}
		}

		if (!delivered) {
			_fail("injected click did not reach sponge-de");
			return;
		}

		Genode::log("sponge-de-probe: PASS");
		_env.parent().exit(0);
	}


	void _fail(char const *reason)
	{
		Genode::error("sponge-de-probe: FAIL ", reason);
		_env.parent().exit(1);
	}
};


void Component::construct(Genode::Env &env)
{
	static Sponge_de_probe probe { env };
}


/*
 * The probe is single-threaded and blocking; a generous stack keeps the
 * capture/input RPC and ROM updates comfortable.
 */
Genode::size_t Component::stack_size() { return 64 * 1024; }
