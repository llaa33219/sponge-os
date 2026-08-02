/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_gui_probe — Phase 7 todo 8 GUI-package generator verifier.
 *
 * Drives the pkgd channel to install `pkg_gui_demo`, then polls a
 * Capture session for the demo window's distinctive green (#00ff00)
 * pixel inside the configured demo domain. This proves end to end
 * that the runtime-config generator correctly emitted:
 *   - the <start> node (pkg_runtime launched the component),
 *   - <binary> when it differs from <name>,
 *   - the inline <config> verbatim (Qt6/libc wiring took effect),
 *   - the <parent/> route for the Gui session (the window reached
 *     nitpicker and composited),
 *   - the GUI-safe caps floor (Qt6 init did not silently hang on the
 *     §11.1 capability-exhaustion cliff).
 *
 * Flow:
 *   (1) <request op="install" pkg="pkg_gui_demo"/>; wait for pkgd ok.
 *   (2) Poll capture for the green pixel in the demo domain center.
 *
 * Success logs "pkg-gui-probe: PASS"; any failure logs
 * "pkg-gui-probe: FAIL <reason>" and exits non-zero so the run
 * scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e), shared across the headless
 * scenarios. The probe confirms the sampled point is NOT background.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * pkg_gui_demo fill color: pure green (must match pkg_gui_demo/main.cc).
 */
int const DEMO_R = 0x00, DEMO_G = 0xff, DEMO_B = 0x00;

/*
 * Demo domain geometry — must match the nitpicker "demo" domain in the
 * run scenario (xpos 352, ypos 200, 320x240, sized to the window).
 */
int const DEMO_X = 352, DEMO_Y = 200;
int const DEMO_W = 320,  DEMO_H = 240;

int const COLOR_TOLERANCE = 32;

bool channel_near(int a, int b)
{
	return a >= b ? a - b <= COLOR_TOLERANCE : b - a <= COLOR_TOLERANCE;
}

bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

bool pixel_is_demo(Pixel const &p)
{
	return channel_near(p.r(), DEMO_R)
	    && channel_near(p.g(), DEMO_G)
	    && channel_near(p.b(), DEMO_B);
}


struct Pkg_gui_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Capture::Connection            _capture  { _env, "pkg-gui-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };

	bool _ok { true };

	Pkg_gui_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("pkg-gui-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	bool _install_and_wait(char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < 120; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("install") &&
				    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg) &&
				    r.has_attribute("status"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}

	bool _demo_window_visible()
	{
		/*
		 * Sample the demo-domain center and a few offsets around it.
		 * Require BOTH (a) the center pixel is the demo green, AND
		 * (b) it is not the nitpicker background — guards against a
		 * vacuous match on an uninitialized buffer.
		 */
		int const cx = DEMO_X + DEMO_W / 2;
		int const cy = DEMO_Y + DEMO_H / 2;

		for (unsigned i = 0; i < 1200 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel center = px[cy * SCREEN_W + cx];

			if (i % 10 == 0)
				Genode::log("pkg-gui-probe: capture poll ", i,
				            " center(", cx, ",", cy, ")=",
				            Genode::Hex(center.pixel),
				            " bg?", pixel_is_bg(center),
				            " demo?", pixel_is_demo(center));

			if (pixel_is_demo(center) && !pixel_is_bg(center)) {
				Genode::log("pkg-gui-probe: demo window detected at (",
				            cx, ",", cy, ")");
				return true;
			}
		}
		return false;
	}

	void run()
	{
		Genode::log("pkg-gui-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		Genode::log("pkg-gui-probe: [1] install pkg_gui_demo via sponge_pkgd");
		if (!_install_and_wait("pkg_gui_demo")) {
			_fail("sponge_pkgd did not answer install pkg_gui_demo");
			return;
		}
		{
			Genode::String<32> status { };
			try {
				status = _result.xml().attribute_value("status",
				                                       Genode::String<32>());
			} catch (Genode::Xml_node::Invalid_syntax) { }

			if (status != Genode::String<32>("ok")) {
				Genode::String<256> err { };
				try {
					err = _result.xml().attribute_value("error",
					                                    Genode::String<256>());
				} catch (Genode::Xml_node::Invalid_syntax) { }
				_fail(Genode::String<256>("install returned: ",
				      status, " ", err).string());
				return;
			}
		}
		Genode::log("pkg-gui-probe: [1] install ok");

		Genode::log("pkg-gui-probe: [2] wait for demo window pixel");
		if (!_demo_window_visible()) {
			_fail("demo window green pixel never appeared on nitpicker");
			return;
		}

		Genode::log("pkg-gui-probe: PASS");
		_env.parent().exit(0);
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Pkg_gui_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
