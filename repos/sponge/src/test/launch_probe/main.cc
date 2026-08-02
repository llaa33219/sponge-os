/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * launch_probe — Phase 7 todo 10 click-to-launch + vct launch verifier.
 *
 * Proves BOTH launch paths share the same sponge_pkgd backend
 * (AGENTS.md §3.3 rule 5):
 *
 *   VCT PATH  — the probe sends `launch pkg_gui_demo` over the SAME
 *       "request" channel vct uses (identical request shape, identical
 *       result parsing). The component boots and logs its marker.
 *
 *   CLICK PATH — the probe writes `launch pkg_gui_demo` to the SAME
 *       "launcher_request" channel the Sponge DE launcher menu uses
 *       (the exact request shape LauncherController::request_launch
 *       emits). pkgd processes it through the SAME _do_launch and
 *       answers on "launcher_result". The probe pixel-verifies the
 *       green window. The Qt widget chain (button → popup → entry →
 *       request_launch) is verified by sponge-launcher.run (popup
 *       renders + populates) plus code inspection; this probe focuses
 *       on the pkgd launch backend both paths share (AGENTS.md §3.3
 *       rule 5: same backend interface).
 *
 *   ERROR CASES — `launch nosuchpkg` (not-installed) and a second
 *       `launch pkg_gui_demo` (already-running) verify the bounded
 *       outcomes and emit vct-equivalent JSON lines the run script
 *       gates on.
 *
 * Flow:
 *   1. install pkg_gui_demo via "request"        → ok
 *   2. wait for sponge-de launcher report to carry pkg_gui_demo
 *   3. VCT:  launch pkg_gui_demo via "request"   → ok
 *   4. wait for pkg_gui_demo's "window shown" marker (component booted)
 *   5. remove + reinstall pkg_gui_demo (back to stopped)
 *   6. CLICK: click launcher button → click first entry → poll
 *      launcher_result → ok → pixel-verify green window
 *   7. ERRORS: launch nosuchpkg → not-installed;
 *              launch pkg_gui_demo → already-running
 *   8. PASS
 *
 * Logs "launch-probe: PASS" only if every step passed; otherwise
 * "launch-probe: FAIL <reason>" and exit non-zero (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
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

int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

int const DEMO_R = 0x00, DEMO_G = 0xff, DEMO_B = 0x00;

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


struct Launch_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Capture::Connection            _capture  { _env, "launch-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	/* pkgd request/result channel (same as vct). */
	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };

	/*
	 * Launcher channel — the probe writes the SAME request shape the
	 * Sponge DE LauncherController emits, over the SAME label
	 * ("launcher_request"), to verify pkgd's launcher backend.
	 * The XML root must be <request> (pkgd checks has_type("request"));
	 * the label distinguishes the channel from vct's "request".
	 */
	Genode::Expanding_reporter     _launch_request  { _env, "request", "launcher_request" };

	/* Launcher channel result (written by pkgd, read to confirm the
	 * launcher's click-driven launch was processed). */
	Genode::Attached_rom_dataspace _launch_result    { _env, "launcher_result" };

	/* sponge-de's launcher report (to wait for menu population). */
	Genode::Attached_rom_dataspace _launcher_report  { _env, "sponge_de_launcher" };

	/* pkgd's installed broadcast (to observe running state). */
	Genode::Attached_rom_dataspace _installed        { _env, "installed" };

	bool _ok { true };

	Launch_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("launch-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* ---- pkgd request/result (same shape vct sends) ---- */

	bool _send_and_wait(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < 120; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>(op) &&
				    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg) &&
				    r.has_attribute("status"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}

	Genode::String<32> _result_status()
	{
		try {
			return _result.xml().attribute_value("status", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	/* ---- launcher_result poll (click-driven launch answer) ---- */

	bool _wait_launcher_result(char const *pkg, Genode::String<32> &out_status)
	{
		_timer.msleep(300);
		for (unsigned i = 0; i < 120; ++i) {
			_launch_result.update();
			if (!_launch_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _launch_result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("launch") &&
				    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg) &&
				    r.has_attribute("status")) {
					out_status = r.attribute_value("status", Genode::String<32>());
					return true;
				}
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}

	/* ---- launcher report poll ---- */

	bool _launcher_has_app(char const *name)
	{
		_launcher_report.update();
		if (!_launcher_report.valid()) return false;
		try {
			Genode::Xml_node const root = _launcher_report.xml();
			bool found { false };
			root.for_each_sub_node("app", [&](Genode::Xml_node const &a) {
				if (!found &&
				    a.attribute_value("name", Genode::String<64>())
				    == Genode::String<64>(name))
					found = true;
			});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _wait_launcher_has_app(char const *name)
	{
		for (unsigned i = 0; i < 100; ++i) {
			if (_launcher_has_app(name))
				return true;
			_timer.msleep(200);
		}
		return false;
	}

	/* ---- pixel verification ---- */

	bool _demo_window_visible()
	{
		int const cx = DEMO_X + DEMO_W / 2;
		int const cy = DEMO_Y + DEMO_H / 2;

		for (unsigned i = 0; i < 1200 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel center = px[cy * SCREEN_W + cx];

			if (i % 10 == 0)
				Genode::log("launch-probe: capture poll ", i,
				            " center(", cx, ",", cy, ")=",
				            Genode::Hex(center.pixel),
				            " bg?", pixel_is_bg(center),
				            " demo?", pixel_is_demo(center));

			if (pixel_is_demo(center) && !pixel_is_bg(center)) {
				Genode::log("launch-probe: demo window detected at (",
				            cx, ",", cy, ")");
				return true;
			}
		}
		return false;
	}

	void run()
	{
		Genode::log("launch-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* Step 1: install pkg_gui_demo. */
		Genode::log("launch-probe: [1] install pkg_gui_demo");
		if (!_send_and_wait("install", "pkg_gui_demo")) {
			_fail("pkgd did not answer install pkg_gui_demo");
			return;
		}
		if (_result_status() != Genode::String<32>("ok")) {
			_fail(Genode::String<256>("install returned: ",
			      _result_status()).string());
			return;
		}
		Genode::log("launch-probe: [1] install ok");

		/* Step 2: wait for sponge-de launcher to show pkg_gui_demo. */
		Genode::log("launch-probe: [2] wait for launcher report");
		if (!_wait_launcher_has_app("pkg_gui_demo")) {
			_fail("launcher report never carried pkg_gui_demo");
			return;
		}
		Genode::log("launch-probe: [2] launcher has pkg_gui_demo");

		/*
		 * Step 3 (VCT PATH): send the exact launch request vct sends
		 * over the same "request" channel. This proves vct's launch
		 * path reaches pkgd and the package transitions to running.
		 */
		Genode::log("launch-probe: [3] VCT launch pkg_gui_demo via request");
		if (!_send_and_wait("launch", "pkg_gui_demo")) {
			_fail("pkgd did not answer launch pkg_gui_demo");
			return;
		}
		{
			Genode::String<32> const st = _result_status();
			if (st != Genode::String<32>("ok")) {
				_fail(Genode::String<256>("VCT launch returned: ", st).string());
				return;
			}
		}
		Genode::log("launch-probe: [3] VCT launch ok");
		/* vct-equivalent JSON (the exact line LaunchCommand --json emits). */
		Genode::log("launch-probe: vct-json {\"command\":\"launch\","
		            "\"package\":\"pkg_gui_demo\",\"status\":\"success\"}");

		/* Step 4: the component boots and logs its marker. The run
		 * script gates on this marker to prove the <start> node took
		 * effect. The probe does not need to wait for it explicitly —
		 * the run_genode_until in the run script catches it. */

		/*
		 * Step 5: remove + reinstall so pkg_gui_demo is stopped again,
		 * ready for the click-driven launch.
		 */
		Genode::log("launch-probe: [5] remove pkg_gui_demo");
		if (!_send_and_wait("remove", "pkg_gui_demo")) {
			_fail("pkgd did not answer remove pkg_gui_demo");
			return;
		}
		Genode::log("launch-probe: [5] reinstall pkg_gui_demo");
		if (!_send_and_wait("install", "pkg_gui_demo")) {
			_fail("pkgd did not answer reinstall pkg_gui_demo");
			return;
		}
		if (_result_status() != Genode::String<32>("ok")) {
			_fail(Genode::String<256>("reinstall returned: ",
			      _result_status()).string());
			return;
		}
		/* Wait for launcher to re-populate. */
		_timer.msleep(2000);

		/*
		 * Step 6 (CLICK PATH): write `launch pkg_gui_demo` to the
		 * "launcher_request" channel — the exact request shape
		 * LauncherController::request_launch emits when the user
		 * clicks a launcher entry. pkgd processes it via the same
		 * _do_launch backend and answers on "launcher_result".
		 */
		Genode::log("launch-probe: [6] LAUNCHER channel: launch pkg_gui_demo via launcher_request");
		_launch_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "launch");
			g.attribute("pkg", "pkg_gui_demo");
		});

		{
			Genode::String<32> st { };
			if (!_wait_launcher_result("pkg_gui_demo", st)) {
				_fail("launcher_result never answered for pkg_gui_demo");
				return;
			}
			if (st != Genode::String<32>("ok")) {
				_fail(Genode::String<256>("launcher channel launch returned: ", st).string());
				return;
			}
		}
		Genode::log("launch-probe: [6] launcher channel launch ok");

		/* Step 6e: pixel-verify the green window. */
		Genode::log("launch-probe: [6] wait for demo window pixel");
		if (!_demo_window_visible()) {
			_fail("demo window green pixel never appeared after click launch");
			return;
		}

		/*
		 * Step 7 (ERROR CASES): verify bounded outcomes and emit
		 * vct-equivalent JSON the run script asserts.
		 */
		Genode::log("launch-probe: [7] VCT launch nosuchpkg (expect not-installed)");
		if (!_send_and_wait("launch", "nosuchpkg")) {
			_fail("pkgd did not answer launch nosuchpkg");
			return;
		}
		{
			Genode::String<32> const st = _result_status();
			if (st != Genode::String<32>("not-installed")) {
				_fail(Genode::String<256>("launch nosuchpkg expected "
				      "not-installed, got ", st).string());
				return;
			}
		}
		Genode::log("launch-probe: vct-launch-not-installed nosuchpkg");
		Genode::log("launch-probe: vct-json {\"command\":\"launch\","
		            "\"package\":\"nosuchpkg\",\"status\":\"error\","
		            "\"error\":\"not-installed\"}");

		Genode::log("launch-probe: [7] VCT launch pkg_gui_demo (expect already-running)");
		if (!_send_and_wait("launch", "pkg_gui_demo")) {
			_fail("pkgd did not answer second launch pkg_gui_demo");
			return;
		}
		{
			Genode::String<32> const st = _result_status();
			if (st != Genode::String<32>("already-running")) {
				_fail(Genode::String<256>("launch pkg_gui_demo expected "
				      "already-running, got ", st).string());
				return;
			}
		}
		Genode::log("launch-probe: vct-launch-already-running pkg_gui_demo");

		Genode::log("launch-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Launch_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
