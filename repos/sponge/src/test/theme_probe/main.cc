/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * theme_probe — vct<->Sponge-DE configuration-consistency verifier.
 *
 * Drives the one-way theme pipeline (vct-write -> sponge_configd ->
 * sponge_themed -> sponge-de) end to end and asserts that three
 * independent observations of the SAME logical value agree:
 *
 *   (1) sponge_configd's broadcast "config" ROM carries theme.active=<X>
 *   (2) sponge_themed's "theme" ROM has name=<X> AND its content contains
 *       <X>'s window_bg color (proving it resolved the right file)
 *   (3) sponge-de's "applied_theme" report has name=<X>
 *
 * The probe writes theme.active itself (vct is short-lived and report_rom
 * is a single-writer slot, so the probe owns the config_request channel,
 * exactly like pkg_seq_probe). It transitions default -> light so the
 * switch is observable, then asserts the 3-way match for "light" as the
 * PRIMARY gate.
 *
 * SECONDARY (informational, not gating): a Capture pixel check that the
 * demo window background actually repainted to the light window_bg. This
 * is logged but never fails the run on its own — the 3-way ROM agreement
 * is the authoritative gate.
 *
 * Success logs "theme-probe: PASS"; any failure logs
 * "theme-probe: FAIL <reason>" and the run scenario times out.
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

struct Pt { int x, y; };

/*
 * Demo domain geometry — must match the nitpicker "demo" domain in
 * run/sponge-theme.run (same as run/sponge-de-test.run).
 */
int const DEMO_X = 192, DEMO_Y = 172;
int const DEMO_W = 640,  DEMO_H = 480;
Pt const DEMO_PT { DEMO_X + DEMO_W/2, DEMO_Y + DEMO_H/2 };  /* (512,412) */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/* Light theme window_bg (#eff1f5) and default/Mocha window_bg (#313244). */
int const LIGHT_R = 0xef, LIGHT_G = 0xf1, LIGHT_B = 0xf5;
int const DEFAULT_R = 0x31, DEFAULT_G = 0x32, DEFAULT_B = 0x44;

int const COLOR_TOLERANCE = 8;

bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                 : b - a <= COLOR_TOLERANCE; }

bool pixel_is(Pixel const &p, int r, int g, int b)
{
	return channel_near(p.r(), r) && channel_near(p.g(), g) && channel_near(p.b(), b);
}


void expected_window_bg_rgb(char const *name, int &r, int &g, int &b)
{
	if (Genode::strcmp(name, "light") == 0)   { r = LIGHT_R;   g = LIGHT_G;   b = LIGHT_B;   return; }
	if (Genode::strcmp(name, "default") == 0) { r = DEFAULT_R; g = DEFAULT_G; b = DEFAULT_B; return; }
	r = g = b = -1;
}


struct Theme_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Capture::Connection            _capture  { _env, "theme-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };

	/* config channel: write config_request, read config_result. */
	Genode::Expanding_reporter     _request  { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace _result   { _env, "config_result" };

	/* Three observations. */
	Genode::Attached_rom_dataspace _config_rom   { _env, "config" };          /* configd broadcast */
	Genode::Attached_rom_dataspace _theme_rom    { _env, "theme" };           /* themed resolved   */
	Genode::Attached_rom_dataspace _applied_rom  { _env, "sponge_de_applied" };/* sponge-de applied */

	bool _ok { true };


	Theme_probe(Genode::Env &env) : _env(env) { }


	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("theme-probe: FAIL ", reason);
		_env.parent().exit(1);
	}


	/* ---- write theme.active and wait for the configd ok result ---- */
	bool _set_theme_active(char const *name)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   "theme.active");
			g.attribute("value", name);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("set") &&
				    r.attribute_value("key", Genode::String<128>()) == Genode::String<128>("theme.active") &&
				    r.attribute_value("value", Genode::String<128>()) == Genode::String<128>(name) &&
				    r.attribute_value("status", Genode::String<32>()) == Genode::String<32>("ok"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}


	/* ---- observation (1): theme.active value in the config broadcast ---- */
	bool _config_theme_active(Genode::String<64> &out)
	{
		_config_rom.update();
		if (!_config_rom.valid()) return false;
		try {
			Genode::Xml_node const root = _config_rom.xml();
			bool found { false };
			root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
				if (!found &&
				    k.attribute_value("name", Genode::String<64>())
				       == Genode::String<64>("theme.active")) {
					out   = k.attribute_value("value", Genode::String<64>());
					found = true;
				}
			});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) { return false; }
	}


	/* ---- observation (2): themed resolved name + content contains color ---- */
	bool _themed_matches(char const *name, char const *window_bg_hex)
	{
		_theme_rom.update();
		if (!_theme_rom.valid()) return false;
		try {
			Genode::Xml_node const t = _theme_rom.xml();
			if (!t.has_type("theme")) return false;
			if (t.attribute_value("name", Genode::String<64>())
			    != Genode::String<64>(name))
				return false;

			/* The raw theme-file bytes are the node's decoded content; the
			 * window_bg line must appear verbatim. */
			Genode::String<2048> const content =
				t.decoded_content<Genode::String<2048>>();

			/* Best-effort substring search (Genode::String has no find). */
			char buf[2048];
			Genode::size_t const n = content.length();
			Genode::size_t const cpy = n < sizeof(buf) - 1 ? n : sizeof(buf) - 1;
			for (Genode::size_t i = 0; i < cpy; ++i) buf[i] = content.string()[i];
			buf[cpy] = 0;

			Genode::size_t const needle_len = Genode::strlen(window_bg_hex);
			char const *const needle = window_bg_hex;
			for (Genode::size_t i = 0; i + needle_len <= cpy; ++i) {
				bool match { true };
				for (Genode::size_t j = 0; j < needle_len; ++j)
					if (buf[i + j] != needle[j]) { match = false; break; }
				if (match) return true;
			}
			return false;
		} catch (Genode::Xml_node::Invalid_syntax) { return false; }
	}


	/* ---- observation (3): sponge-de applied_theme name ---- */
	bool _applied_is(Genode::String<64> &out)
	{
		_applied_rom.update();
		if (!_applied_rom.valid()) return false;
		try {
			Genode::Xml_node const a = _applied_rom.xml();
			if (!a.has_type("applied_theme")) return false;
			out = a.attribute_value("name", Genode::String<64>());
			return true;
		} catch (Genode::Xml_node::Invalid_syntax) { return false; }
	}


	/*
	 * Wait until all three observations agree on `name`. This is the
	 * PRIMARY consistency gate. `window_bg_hex` proves themed resolved the
	 * right FILE (not just echoed the name).
	 */
	bool _wait_three_way(char const *name, char const *window_bg_hex,
	                     unsigned polls)
	{
		for (unsigned i = 0; i < polls && _ok; ++i) {
			Genode::String<64> cfg { };
			Genode::String<64> applied { };

			bool const c1 = _config_theme_active(cfg) &&
			                cfg == Genode::String<64>(name);
			bool const c2 = _themed_matches(name, window_bg_hex);
			bool const c3 = _applied_is(applied) &&
			                applied == Genode::String<64>(name);

			if (i % 5 == 0)
				Genode::log("theme-probe: 3-way poll for '", name,
				            "' cfg=", c1, " themed=", c2, " applied=", c3,
				            " (applied='", applied, "')");

			if (c1 && c2 && c3)
				return true;
			_timer.msleep(100);
		}
		return false;
	}


	void _capture_check(char const *name)
	{
		int r, g, b;
		expected_window_bg_rgb(name, r, g, b);
		if (r < 0) return;

		bool confirmed { false };
		for (unsigned i = 0; i < 200 && !confirmed; ++i) {  /* ~20s */
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel demo = px[DEMO_PT.y * SCREEN_W + DEMO_PT.x];
			if (pixel_is(demo, r, g, b)) {
				confirmed = true;
				Genode::log("theme-probe: capture confirms demo window bg "
				            "matches theme '", name, "' at (", DEMO_PT.x, ",",
				            DEMO_PT.y, ") = ", Genode::Hex(demo.pixel));
			}
		}

		if (!confirmed)
			Genode::log("theme-probe: capture inconclusive for theme '", name,
			            "' (PRIMARY 3-way gate still passed)");
	}


	void run()
	{
		Genode::log("theme-probe: starting");

		/* Allocate the capture buffer (defines nitpicker's panorama). */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Transition 1: default. Forces a real change away from configd's
		 * startup default ("light") so the pipeline is exercised, not just
		 * observed in its steady state.
		 */
		Genode::log("theme-probe: [1] set theme.active=default");
		if (!_set_theme_active("default"))
			{ _fail("configd did not accept set theme.active=default"); return; }

		if (!_wait_three_way("default", "#313244", 300))
			{ _fail("3-way match for 'default' did not converge"); return; }
		Genode::log("theme-probe: [1] 3-way match for 'default' confirmed");

		/*
		 * Transition 2: light. The PRIMARY gate — every hop must reflect
		 * the new value AND themed must carry light's window_bg.
		 */
		Genode::log("theme-probe: [2] set theme.active=light");
		if (!_set_theme_active("light"))
			{ _fail("configd did not accept set theme.active=light"); return; }

		if (!_wait_three_way("light", "#eff1f5", 300))
			{ _fail("3-way match for 'light' did not converge"); return; }
		Genode::log("theme-probe: [2] 3-way match for 'light' confirmed (PRIMARY)");

		/* SECONDARY: confirm the pixels actually repainted. */
		_capture_check("light");

		Genode::log("theme-probe: PASS");
		_env.parent().exit(0);
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Theme_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
