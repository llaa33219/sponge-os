/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * alpha_probe — Phase 7 todo 4 composite Alpha-desktop verifier.
 *
 * Asserts all four Alpha criteria in bounded iterations inside ONE
 * headless Genode instance, then logs exactly "alpha-probe: PASS". Any
 * failure logs "alpha-probe: FAIL <reason>" and exits non-zero so the
 * run scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 *
 * Criteria:
 *   (a) themed sponge-de panel composited (Capture pixel check on the
 *       panel band — the default theme's panel_bg #1e1e2e is non-zero,
 *       proving the Qt6/Mesa-on-seL4 desktop painted),
 *   (b) sponge-de's "launcher" ROM carries <app name="hello"
 *       category="Utilities"/> — proves the pkgd install + broadcast +
 *       sponge-de launcher feed all work,
 *   (c) configd's broadcast "config" ROM is readable, non-empty, and
 *       parses as <config> with at least one <key> child — proves the
 *       live config pipeline is up,
 *   (d) lz_viewer's Leitzentrale window appears on the outer nitpicker
 *       (the marker patch #bf5fbf at (122,94)) — only happens after the
 *       probe flips leitzentrale.enabled=true via configd and the lz
 *       subsystem fader fades in.
 *
 * The probe owns both the pkgd request channel (installs hello) and the
 * configd config_request channel (enables leitzentrale) — report_rom is
 * single-writer per label and there is no vct in this scenario.
 *
 * Plain Genode component following AGENTS.md §3.1 (qualified Genode
 * types, no exceptions, Component::construct/stack_size exactly as the
 * framework expects).
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
 * lz_viewer stamps a distinct marker patch (#bf5fbf) into the top-left
 * of each streamed frame; lz_viewer places the window at (112,84) and
 * the marker at offset (10,10) within it, so the marker lands at
 * (122,94) on the outer nitpicker. Same constants as lz_viz_probe
 * (proven in run/sponge-leitzentrale.run).
 */
int const MARK_R = 0xbf, MARK_G = 0x5f, MARK_B = 0xbf;
int const MARK_X = 112 + 10;
int const MARK_Y = 84  + 10;
int const MARK_W = 40, MARK_H = 40;
unsigned const MARK_PIXEL_THRESHOLD = 500;

int const COLOR_SLACK = 24;


bool pixel_is_marker(Pixel const &p)
{
	auto near = [](int a, int b) { return a >= b ? a - b <= COLOR_SLACK
	                                             : b - a <= COLOR_SLACK; };
	return near(p.r(), MARK_R) && near(p.g(), MARK_G) && near(p.b(), MARK_B);
}


/*
 * Bounded iteration budgets. Each poll is 100-200ms; the totals are
 * generous because Qt6's first paint under softpipe Mesa is markedly
 * slower on seL4 than on base-linux, and the lz subsystem has a long
 * cold-start tail.
 */
unsigned const RENDER_POLL_ITERS   = 1200; /* ~120s for Qt first paint  */
unsigned const INSTALL_WAIT_ITERS  = 120;  /* ~12s for pkgd install ack */
unsigned const LAUNCHER_POLL_ITERS = 400;  /* ~80s for launcher report  */
unsigned const CONFIGD_POLL_ITERS  = 200;  /* ~20s for configd broadcast*/
unsigned const LZ_ENABLE_WAIT_ITERS= 120;  /* ~12s for configd set ack  */
unsigned const LZ_VIEWER_POLL_ITERS= 900;  /* ~90s for fader + first frame */


struct Alpha_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "alpha-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	/* pkgd channel: install hello. */
	Genode::Expanding_reporter     _pkg_request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _pkg_result  { _env, "result" };

	/* configd channel: enable leitzentrale + read broadcast. */
	Genode::Expanding_reporter     _cfg_request { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace _cfg_result  { _env, "config_result" };
	Genode::Attached_rom_dataspace _cfg_broadcast { _env, "config" };

	/* sponge-de's launcher report. */
	Genode::Attached_rom_dataspace _launcher { _env, "sponge_de_launcher" };

	bool _ok { true };

	Alpha_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("alpha-probe: FAIL ", reason);
		_env.parent().exit(1);
	}


	/*
	 * Report-style helpers: submit a request and poll the matching
	 * result ROM for an ok status. Shared shape with launcher_probe +
	 * theme_probe (proven).
	 */
	bool _pkg_install_and_wait(char const *pkg)
	{
		_pkg_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < INSTALL_WAIT_ITERS; ++i) {
			_pkg_result.update();
			if (!_pkg_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _pkg_result.xml();
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

	bool _cfg_set_and_wait(char const *key, char const *value)
	{
		_cfg_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < LZ_ENABLE_WAIT_ITERS; ++i) {
			_cfg_result.update();
			if (!_cfg_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _cfg_result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("set") &&
				    r.attribute_value("key", Genode::String<128>()) == Genode::String<128>(key) &&
				    r.attribute_value("value", Genode::String<128>()) == Genode::String<128>(value) &&
				    r.attribute_value("status", Genode::String<32>()) == Genode::String<32>("ok"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}


	/*
	 * Criterion (a): themed panel composited. Poll the panel band
	 * (x=512, y=4..24) for non-zero pixels. The default theme's
	 * panel_bg #1e1e2e is non-zero, so any painted panel passes; the
	 * pure-black untouched buffer does not. Same approach as
	 * launcher_probe (proven).
	 */
	bool _panel_rendered()
	{
		for (unsigned i = 0; i < RENDER_POLL_ITERS && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			for (int y = 4; y <= 24; y += 4) {
				Pixel p = px[y * SCREEN_W + 512];
				if (p.pixel != 0) {
					Genode::log("alpha-probe: (a) panel band rendered at (512,", y,
					            ") = ", Genode::Hex(p.pixel));
					return true;
				}
			}

			if (i % 20 == 0) {
				Pixel p = px[14 * SCREEN_W + 512];
				Genode::log("alpha-probe: (a) panel poll ", i,
				            " pixel=", Genode::Hex(p.pixel));
			}
		}
		return false;
	}


	/*
	 * Criterion (b): launcher report contains hello/Utilities.
	 */
	bool _launcher_has_hello()
	{
		_launcher.update();
		if (!_launcher.valid()) return false;
		try {
			Genode::Xml_node const root = _launcher.xml();
			if (!root.has_type("launcher")) return false;

			bool found { false };
			root.for_each_sub_node("app", [&](Genode::Xml_node const &a) {
				if (!found &&
				    a.attribute_value("name", Genode::String<64>())
				       == Genode::String<64>("hello") &&
				    a.attribute_value("category", Genode::String<64>())
				       == Genode::String<64>("Utilities"))
					found = true;
			});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _wait_launcher_has_hello()
	{
		for (unsigned i = 0; i < LAUNCHER_POLL_ITERS && _ok; ++i) {
			if (_launcher_has_hello()) {
				Genode::log("alpha-probe: (b) launcher report contains "
				            "hello/Utilities");
				return true;
			}
			_timer.msleep(200);
		}
		return false;
	}


	/*
	 * Criterion (c): configd broadcast ROM is live.
	 */
	bool _configd_broadcast_live()
	{
		for (unsigned i = 0; i < CONFIGD_POLL_ITERS && _ok; ++i) {
			_cfg_broadcast.update();
			if (!_cfg_broadcast.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const root = _cfg_broadcast.xml();
				if (!root.has_type("config")) { _timer.msleep(100); continue; }

				unsigned key_count { 0 };
				root.for_each_sub_node("key", [&](Genode::Xml_node const &) {
					++key_count;
				});

				if (key_count > 0) {
					Genode::log("alpha-probe: (c) configd broadcast live (",
					            key_count, " keys)");
					return true;
				}
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}


	/*
	 * Criterion (d): lz_viewer marker pixel appears on the outer
	 * nitpicker. Same math as lz_viz_probe (proven).
	 */
	bool _lz_viewer_visible()
	{
		for (unsigned i = 0; i < LZ_VIEWER_POLL_ITERS && _ok; ++i) {
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
				Genode::log("alpha-probe: (d) lz_viewer poll ", i,
				            " marker pixels=", mark_pixels);

			if (mark_pixels >= MARK_PIXEL_THRESHOLD) {
				Genode::log("alpha-probe: (d) Leitzentrale window live (",
				            mark_pixels, " marker pixels at (", MARK_X, ",",
				            MARK_Y, "))");
				return true;
			}
		}
		return false;
	}


	void run()
	{
		Genode::log("alpha-probe: starting");

		/* Allocate the capture buffer (defines the outer panorama). */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Step 1: kick off both setup writes in parallel — install
		 * hello via pkgd (for criterion b) and enable leitzentrale via
		 * configd (for criterion d). Then wait for both acks.
		 */
		Genode::log("alpha-probe: [1] install hello via sponge_pkgd");
		if (!_pkg_install_and_wait("hello")) {
			_fail("sponge_pkgd did not answer install hello");
			return;
		}
		{
			Genode::String<32> status { };
			try {
				status = _pkg_result.xml().attribute_value("status",
				                                           Genode::String<32>());
			} catch (Genode::Xml_node::Invalid_syntax) { }
			if (status != Genode::String<32>("ok")) {
				_fail(Genode::String<256>("install hello returned: ",
				      status).string());
				return;
			}
		}
		Genode::log("alpha-probe: [1] install hello ok");

		Genode::log("alpha-probe: [2] set leitzentrale.enabled=true");
		if (!_cfg_set_and_wait("leitzentrale.enabled", "true")) {
			_fail("configd did not accept set leitzentrale.enabled=true");
			return;
		}
		Genode::log("alpha-probe: [2] leitzentrale.enabled=true ack");

		/*
		 * Step 2: assert all four criteria, each in bounded iterations.
		 * Order: (c) configd broadcast (fastest, also confirms configd
		 * itself is up), then (a) panel pixel, then (b) launcher report
		 * (depends on the install above), then (d) lz_viewer pixel
		 * (slowest — lz subsystem boot + fader fade-in).
		 */
		Genode::log("alpha-probe: [3] assert (c) configd broadcast live");
		if (!_configd_broadcast_live()) {
			_fail("configd broadcast ROM never became live");
			return;
		}

		Genode::log("alpha-probe: [4] assert (a) themed panel rendered");
		if (!_panel_rendered()) {
			_fail("themed panel never composited on nitpicker");
			return;
		}

		Genode::log("alpha-probe: [5] assert (b) launcher has hello");
		if (!_wait_launcher_has_hello()) {
			_fail("launcher report never contained hello/Utilities");
			return;
		}

		Genode::log("alpha-probe: [6] assert (d) lz_viewer window visible");
		if (!_lz_viewer_visible()) {
			_fail("lz_viewer Leitzentrale window never appeared on screen");
			return;
		}

		Genode::log("alpha-probe: PASS");
		_env.parent().exit(0);
	}
};


} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Alpha_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
