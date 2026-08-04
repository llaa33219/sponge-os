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
 *   (b) pointer input reaches the sponge-de widget, verified by an
 *       Event session path. In the default inject=yes mode the probe
 *       synthesizes an absolute-motion + BTN_LEFT click into
 *       nitpicker's Event service. In inject=no mode (selected by
 *       <config inject="no"/> — used by run/sponge-de-sel4-
 *       interactive.run) the probe only OBSERVES a click arriving
 *       through the real driver path (ps2/usb_hid -> event_filter),
 *       which the run script injects from the host via a QEMU
 *       usb-tablet. In both modes the press is confirmed via
 *       sponge-de's "input" report (relayed by report_rom).
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
 *
 * OBSERVE_PT is the click target the host should drive in observe mode
 * (inject=no): the demo-domain center (512,412), so the press lands on
 * the demo window body — anywhere inside the window reaches sponge-de's
 * input report regardless of which child widget is under the cursor.
 * This is the point emitted as `QMP-TARGET click 512 412` for the host
 * run script to pick up via bounded expect on the QEMU serial.
 */
Pt const CLICK_PT { 512, 460 };
Pt const OBSERVE_PT { DEMO_X + DEMO_W/2, DEMO_Y + DEMO_H/2 }; /* (512,412) */

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
	 * Optional config. Controls whether the probe synthesizes the click
	 * itself (default; headless null-framebuffer scenarios such as
	 * run/sponge-de-test.run) or only OBSERVES a click arriving through
	 * the real input driver path (run/sponge-de-sel4-interactive.run,
	 * where the click is injected from the host via a QEMU usb-tablet).
	 * Absent config => inject=yes, preserving the original behavior.
	 */
	Genode::Attached_rom_dataspace _config { _env, "config" };

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
		 *
		 * The poll cap defaults to 600 (~60s, enough on base-linux).
		 * The base-sel4 interactive scenario passes a larger value via
		 * <config render_iters="..."/> because Qt6 first paint under
		 * the software (softpipe) Mesa is markedly slower on seL4.
		 *
		 * Config is read via the Genode::Node API (Genode 26.05),
		 * which transparently accepts both XML and HID-format config
		 * deliveries. The sandbox's inline-config ROM service emits
		 * HID by default since the format became the framework
		 * default; using the older _config.xml() accessor here would
		 * return <empty/> on HID input and silently fall back to the
		 * default (the W0 unexpected-green root cause).
		 */
		unsigned const render_iters =
			(!_config.valid()) ? 600 :
			_config.node().attribute_value("render_iters", 600u);
		bool rendered = false;
		for (unsigned i = 0; i < render_iters; ++i) {
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
		 * (b) Input round-trip. inject=yes (default) synthesizes the
		 * click into nitpicker's Event service (headless null-fb
		 * scenarios). inject=no only watches for a press arriving via
		 * the real driver path (base-sel4 interactive scenario, where
		 * the run script injects a QEMU usb-tablet click from the
		 * host). The default must stay "yes" so run/sponge-de-test.run
		 * is unchanged.
		 */
		bool const inject = (!_config.valid()) ||
		                    _config.node().attribute_value("inject", true);

		unsigned const INJECT_WATCH_ITERS = 200;  /* ~20s after self-inject */
		unsigned const OBSERVE_WATCH_ITERS = 900; /* ~90s for host injection */

		_input_rom.update();  /* baseline */

		if (inject) {
			Genode::log("sponge-de-probe: injecting click at (",
			            CLICK_PT.x, ",", CLICK_PT.y, ")");
			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Absolute_motion{ CLICK_PT.x, CLICK_PT.y });
				batch.submit(Input::Press   { Input::BTN_LEFT });
				batch.submit(Input::Release { Input::BTN_LEFT });
			});
		} else {
			Genode::log("sponge-de-probe: observe mode (inject=no) -- "
			            "awaiting external click via the real input "
			            "driver path (usb-tablet/ps2 -> event_filter)");
			/*
			 * Emit the QMP-TARGET marker so the host run script can
			 * dispatch a real QMP input-send-event click at the demo
			 * window center. The bounded expect on the QEMU serial
			 * (run/qmp.inc::qmp_exec_target) catches this line and
			 * forwards the click through the usb-tablet absolute
			 * pointer → usb_hid → event_filter → nitpicker →
			 * sponge-de. Target is OBSERVE_PT (demo-domain center
			 * 512,412) — anywhere inside the demo window reaches
			 * sponge-de's input report.
			 */
			Genode::log("QMP-TARGET click ", OBSERVE_PT.x, " ", OBSERVE_PT.y);
		}

		bool delivered = false;
		unsigned const watch_iters = inject ? INJECT_WATCH_ITERS
                                            : OBSERVE_WATCH_ITERS;
		for (unsigned i = 0; i < watch_iters; ++i) {
			_timer.msleep(100);
			_input_rom.update();

			/*
			 * Detect the press by content: before sponge-de receives
			 * any input the report has no "press" attribute.
			 */
			if (_input_rom.valid() && _input_rom.xml().has_attribute("press")) {
				delivered = true;
				/*
				 * Log the observed press coordinates for host-side
				 * calibration of QMP absolute-axis scaling. sponge-de's
				 * input report carries ax/ay in the press attribute.
				 */
				Genode::log("sponge-de-probe: input report confirms press",
				            " press=", _input_rom.xml().attribute_value("press", Genode::String<64>{}));
				break;
			}
		}

		if (!delivered) {
			_fail(inject ? "injected click did not reach sponge-de"
			             : "external click did not reach sponge-de "
			               "(usb-tablet injection missing or input driver "
			               "path not wired)");
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
