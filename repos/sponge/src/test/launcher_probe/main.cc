/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * launcher_probe — Sponge DE launcher integration verifier (Phase 5c).
 *
 * Drives the pkgd channel to install `hello`, then watches sponge-de's
 * "launcher" report until it carries the freshly-installed app with its
 * declared category. This closes the Phase 5c criterion: the launcher
 * actually populated from pkgd's rich list via the shared
 * sponge_backend_client channel plumbing.
 *
 * Flow:
 *
 *   (1) Send <request op="install" pkg="hello"/>; wait for pkgd's ok.
 *   (2) Poll sponge-de's "launcher" ROM (relayed from its launcher
 *       Reporter by report_rom) for an <app name="hello"
 *       category="Utilities"/> entry.
 *   (3) Optional Capture pixel check that the panel band rendered at
 *       the top of the screen — informational only, never gates PASS
 *       (same policy as theme_probe).
 *
 * Success logs "launcher-probe: PASS"; any failure logs
 * "launcher-probe: FAIL <reason>" and exits non-zero.
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

struct Launcher_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };
	Genode::Attached_rom_dataspace _launcher { _env, "sponge_de_launcher" };

	bool _ok { true };

	Launcher_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("launcher-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	bool _send_install_and_wait(char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < 80; ++i) {
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

	bool _launcher_has_app(char const *name, char const *category)
	{
		_launcher.update();
		if (!_launcher.valid())
			return false;
		try {
			Genode::Xml_node const root = _launcher.xml();
			if (!root.has_type("launcher"))
				return false;

			bool found { false };
			root.for_each_sub_node("app", [&](Genode::Xml_node const &a) {
				if (!found &&
				    a.attribute_value("name", Genode::String<64>())
				       == Genode::String<64>(name) &&
				    a.attribute_value("category", Genode::String<64>())
				       == Genode::String<64>(category))
					found = true;
			});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _wait_launcher_contains(char const *name, char const *category,
	                             unsigned polls)
	{
		for (unsigned i = 0; i < polls && _ok; ++i) {
			bool got = _launcher_has_app(name, category);

			unsigned count { 0 };
			try {
				if (_launcher.valid()) {
					_launcher.xml().for_each_sub_node("app",
						[&](Genode::Xml_node const &) { ++count; });
				}
			} catch (Genode::Xml_node::Invalid_syntax) { }

			if (i % 5 == 0)
				Genode::log("launcher-probe: poll ", i,
				            " looking for app='", name, "' cat='", category,
				            "' (launcher count=", count, ") found=", got);

			if (got)
				return true;
			_timer.msleep(200);
		}
		return false;
	}

	void _capture_panel_check()
	{
		Capture::Connection capture { _env, "launcher-probe" };
		Genode::Constructible<Genode::Attached_dataspace> cap_ds { };

		capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                 .mm       = Capture::Area(0, 0),
		                 .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                             Capture::Area(SCREEN_W, SCREEN_H) } });
		cap_ds.construct(_env.rm(), capture.dataspace());

		bool confirmed { false };
		for (unsigned i = 0; i < 200 && !confirmed; ++i) {
			_timer.msleep(100);
			capture.capture_at(Capture::Point(0, 0));

			/*
			 * Sample the panel band: scan a few Y values in the top
			 * 0..28px row band, at horizontal x=512 (panel center).
			 * A non-zero pixel at any of them implies the panel
			 * composited something there (the default theme's panel_bg
			 * is #1e1e2e, which is non-zero).
			 */
			Pixel const *px = cap_ds->local_addr<Pixel>();
			for (int y = 4; y <= 24 && !confirmed; y += 4) {
				Pixel p = px[y * SCREEN_W + 512];
				if (p.pixel != 0) {
					confirmed = true;
					Genode::log("launcher-probe: capture confirms panel band "
					            "rendered at (512,", y, ") = ",
					            Genode::Hex(p.pixel));
				}
			}
		}

		if (!confirmed)
			Genode::log("launcher-probe: capture inconclusive for panel band "
			            "(PRIMARY launcher-report gate still passed)");
	}

	void run()
	{
		Genode::log("launcher-probe: starting");

		/* Step 1: install hello via the pkgd channel. */
		Genode::log("launcher-probe: [1] install hello via sponge_pkgd");
		if (!_send_install_and_wait("hello")) {
			_fail("sponge_pkgd did not answer install hello");
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
		Genode::log("launcher-probe: [1] install ok");

		/*
		 * pkg_runtime needs a beat to start hello; not strictly required
		 * for the launcher check (sponge-de's launcher only cares about
		 * pkgd's installed set, not the running child), but it makes the
		 * scenario self-contained for future click-to-launch tests.
		 */
		_timer.msleep(500);

		/*
		 * Step 2: poll sponge-de's launcher report for the hello entry
		 * with category="Utilities" (from pkg/hello/metadata.xml).
		 */
		Genode::log("launcher-probe: [2] wait for launcher report");
		if (!_wait_launcher_contains("hello", "Utilities", 200)) {
			_fail("launcher report never contained hello/Utilities");
			return;
		}
		Genode::log("launcher-probe: [2] launcher report contains "
		            "hello/Utilities");

		/* Step 3 (optional): panel band pixel check. */
		_capture_panel_check();

		Genode::log("launcher-probe: PASS");
		_env.parent().exit(0);
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Launcher_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
