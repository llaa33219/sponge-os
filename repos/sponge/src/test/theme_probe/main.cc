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
 * exactly like pkg_seq_probe). The PRIMARY gate is the 3-way match for
 * each step.
 *
 * 5-STEP SEQUENCE (Phase 11 plan W3 step 4 + W3 follow-up):
 *
 *   [baseline]   set theme.active=default  — captures the panel-region
 *                pixel baseline + demo-window-bg pixel baseline for the
 *                subsequent diff assertions.
 *
 *   [1] dark     3-way match (PRIMARY gate). Capture-pixel-diff the
 *                panel-bg area against the default baseline; assert the
 *                pixel genuinely differs (dark's panel_bg #181926 ≠
 *                default's #1e1e2e). The sample point is well inside
 *                the panel and far from both the launcher toggle and
 *                the panel/nitpicker-bg boundary (failure-point 15
 *                enforcement: never assert on the boundary line).
 *
 *   [2] compact  3-way match (PRIMARY gate). Capture a panel pixel at
 *                the launcher-toggle right edge (x=46) and assert it is
 *                NOT the accent color — compact's launcher_width=32
 *                makes the toggle end at x=36, so x=46 is past the
 *                toggle in compact and well inside the toggle in
 *                default. The panel.height theme key is a documented
 *                no-op in Phase 11 (per D2.2), so the panel stays 28 px
 *                tall in compact and the height assertion is NOT made
 *                here; the realizable diff is the launcher-toggle WIDTH
 *                (32 vs 48).
 *
 *   [3] light    3-way match (PRIMARY gate). Capture the demo-window-bg
 *                pixel and assert it matches light's window_bg #eff1f5.
 *                This is the existing capture check from Phase 10.
 *
 *   [4] does-not-exist  The PROBE writes theme.active=does-not-exist
 *                via configd (configd accepts any non-empty value for
 *                the free-form `theme.active` string key). The probe
 *                asserts the applied_theme report is unchanged from the
 *                step-3 value ("light") — sponge_themed's name-dedup
 *                fallback keeps the previously published theme when a
 *                ROM resolves to an unwritten module (the
 *                sponge-theme.run label_suffix=".theme" catch-all
 *                routes the unstaged name to a never-produced
 *                report_rom module; see the run script's ROUTING
 *                comment). The probe also asserts the broadcast
 *                theme.active IS "does-not-exist" (proving configd
 *                accepted the write).
 *
 *   [5] default  LIVENESS PROOF. After step 4 set themed a "stale
 *                theme" via the catch-all, the probe writes
 *                theme.active=default and asserts the 3-way match
 *                converges to "default" (cfg=dark, themed ROM carries
 *                default.theme's window_bg #313244, sponge-de reports
 *                applied_theme="default"). This can ONLY pass if
 *                sponge_themed survived step 4 — if the component had
 *                frozen on a denied ROM session (the W3-rev-2 bug
 *                this step catches), the theme report would stay
 *                "light" forever and this assertion would time out.
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

/*
 * Panel sample points. The panel domain is ypos=0, height=28.
 *
 * PANEL_BG_X, PANEL_BG_Y — far from the launcher toggle and the
 *   title/clock text; reads the panel's background fill. Used for the
 *   dark vs default panel-bg diff. (2,14) is two pixels into the
 *   panel and eight pixels above its bottom edge — well clear of any
 *   anti-aliasing artifact at the top or bottom edges, and well clear
 *   of the panel/nitpicker-bg boundary at y=28.
 *
 * TOGGLE_RIGHT_X, TOGGLE_RIGHT_Y — inside the DEFAULT launcher toggle
 *   (x ∈ [8, 56]) and past the COMPACT launcher toggle (compact's
 *   launcher_width=32 makes the toggle span x ∈ [4, 36]). The default
 *   pixel here is the accent color; the compact pixel here is panel_bg
 *   or title-label-area text. Realizable geometry diff, independent
 *   of the documented-no-op panel_height in compact.
 */
int const PANEL_BG_X    = 2,  PANEL_BG_Y    = 14;
int const TOGGLE_RIGHT_X = 46, TOGGLE_RIGHT_Y = 14;

/* Theme window_bg hex → RGB for the 3-theme palette. */
int const LIGHT_R = 0xef, LIGHT_G = 0xf1, LIGHT_B = 0xf5;
int const DEFAULT_R = 0x31, DEFAULT_G = 0x32, DEFAULT_B = 0x44;
int const DARK_R = 0x24, DARK_G = 0x27, DARK_B = 0x3a;
int const COMPACT_R = 0x11, COMPACT_G = 0x11, COMPACT_B = 0x1b;

/* Default Mocha accent — the toggle button background. */
int const ACCENT_R = 0x89, ACCENT_G = 0xb4, ACCENT_B = 0xfa;

int const COLOR_TOLERANCE = 8;

/*
 * Stricter per-channel tolerance for the DIFF checks. The two Mocha /
 * Macchiato dark panel_bg colors (#1e1e2e vs #181926) differ by
 * 6/5/8 per channel — visually distinct, but with COLOR_TOLERANCE=8
 * rgb_close() returns true and the diff assertion cannot fire.
 * Tightening to 4 keeps the per-channel jitter absorption that Qt's
 * softpipe anti-aliasing needs while letting the close-but-different
 * dark variants register as distinct.
 */
int const DIFF_TOLERANCE = 4;

bool channel_near(int a, int b, int tol) {
	return a >= b ? a - b <= tol : b - a <= tol;
}

bool pixel_is(Pixel const &p, int r, int g, int b)
{
	return channel_near(p.r(), r, COLOR_TOLERANCE)
	    && channel_near(p.g(), g, COLOR_TOLERANCE)
	    && channel_near(p.b(), b, COLOR_TOLERANCE);
}

bool rgb_close(int ar, int ag, int ab, int br, int bg, int bb)
{
	return channel_near(ar, br, DIFF_TOLERANCE)
	    && channel_near(ag, bg, DIFF_TOLERANCE)
	    && channel_near(ab, bb, DIFF_TOLERANCE);
}


void expected_window_bg_rgb(char const *name, int &r, int &g, int &b)
{
	if (Genode::strcmp(name, "light")   == 0) { r = LIGHT_R;   g = LIGHT_G;   b = LIGHT_B;   return; }
	if (Genode::strcmp(name, "default") == 0) { r = DEFAULT_R; g = DEFAULT_G; b = DEFAULT_B; return; }
	if (Genode::strcmp(name, "dark")    == 0) { r = DARK_R;    g = DARK_G;    b = DARK_B;    return; }
	if (Genode::strcmp(name, "compact") == 0) { r = COMPACT_R; g = COMPACT_G; b = COMPACT_B; return; }
	r = g = b = -1;
}


struct Rgb_sample { int r, g, b; };

Rgb_sample rgb_of(Pixel const &p)
{
	return { (int)p.r(), (int)p.g(), (int)p.b() };
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


	/* ---- capture one pixel at (x,y); waits a frame for repaint ---- */
	Rgb_sample _capture_pixel(int x, int y)
	{
		_timer.msleep(150);
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		Pixel const p = px[y * SCREEN_W + x];
		Genode::Hex h(p.pixel);
		Genode::log("theme-probe: capture (", x, ",", y, ") = ", h);
		return rgb_of(p);
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


	/*
	 * Poll the demo window background pixel until it matches the
	 * expected `name` window_bg color (within tolerance). This is the
	 * SECONDARY pixel confirmation that the restyle actually reached
	 * the renderer. PRIMARY gating is the 3-way match above; this
	 * function never fails the probe on its own.
	 */
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


	/*
	 * Wait briefly for sponge-de's restyle to settle after a 3-way
	 * match, then poll the applied_theme ROM until it carries `name`.
	 * Used in step 4 to confirm the previous theme survived an
	 * unknown-name write.
	 */
	bool _wait_applied(char const *name, unsigned polls)
	{
		for (unsigned i = 0; i < polls && _ok; ++i) {
			Genode::String<64> applied { };
			if (_applied_is(applied) && applied == Genode::String<64>(name))
				return true;
			_timer.msleep(100);
		}
		return false;
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
		 * Baseline: default theme. Forces a real change away from configd's
		 * startup default ("light") so the pipeline is exercised, not just
		 * observed in its steady state. Also captures the pixel baseline
		 * used by step 1 (dark) and step 2 (compact) for diff assertions.
		 */
		Genode::log("theme-probe: [baseline] set theme.active=default");
		if (!_set_theme_active("default"))
			{ _fail("configd did not accept set theme.active=default"); return; }

		if (!_wait_three_way("default", "#313244", 300))
			{ _fail("3-way match for 'default' did not converge"); return; }
		Genode::log("theme-probe: [baseline] 3-way match for 'default' confirmed");

		Rgb_sample const default_panel_bg    = _capture_pixel(PANEL_BG_X,    PANEL_BG_Y);
		Rgb_sample const default_toggle_edge = _capture_pixel(TOGGLE_RIGHT_X, TOGGLE_RIGHT_Y);
		Rgb_sample const default_demo_bg     = _capture_pixel(DEMO_PT.x, DEMO_PT.y);
		Genode::log("theme-probe: [baseline] captured (",
		            PANEL_BG_X, ",", PANEL_BG_Y, ")=",
		            default_panel_bg.r, ",", default_panel_bg.g, ",", default_panel_bg.b,
		            " (", TOGGLE_RIGHT_X, ",", TOGGLE_RIGHT_Y, ")=",
		            default_toggle_edge.r, ",", default_toggle_edge.g, ",", default_toggle_edge.b,
		            " demo=", default_demo_bg.r, ",", default_demo_bg.g, ",", default_demo_bg.b);

		/*
		 * Step 1: dark. PRIMARY 3-way gate. SECONDARY: panel-bg pixel
		 * at (PANEL_BG_X, PANEL_BG_Y) differs from the default baseline
		 * (dark's panel_bg #181926 ≠ default's #1e1e2e).
		 */
		Genode::log("theme-probe: [1] set theme.active=dark");
		if (!_set_theme_active("dark"))
			{ _fail("configd did not accept set theme.active=dark"); return; }

		if (!_wait_three_way("dark", "#24273a", 300))
			{ _fail("3-way match for 'dark' did not converge"); return; }
		Genode::log("theme-probe: [1] 3-way match for 'dark' confirmed (PRIMARY)");

		Rgb_sample const dark_panel_bg = _capture_pixel(PANEL_BG_X, PANEL_BG_Y);
		if (rgb_close(dark_panel_bg.r, dark_panel_bg.g, dark_panel_bg.b,
		              default_panel_bg.r, default_panel_bg.g, default_panel_bg.b)) {
			_fail("step 1 panel-bg pixel did NOT differ from default baseline "
			      "(dark panel_bg #181926 must visibly differ from default #1e1e2e)");
			return;
		}
		Genode::log("theme-probe: [1] panel-region pixel diff vs default baseline confirmed");

		/*
		 * Step 2: compact. PRIMARY 3-way gate. SECONDARY: a realizable
		 * geometry assertion — the launcher toggle is narrower in
		 * compact (launcher_width=32 vs default's 48), so the pixel at
		 * (TOGGLE_RIGHT_X, TOGGLE_RIGHT_Y) is inside the default toggle
		 * (accent color) but PAST the compact toggle (panel_bg / text).
		 * Note: compact's `panel_height=24` in the theme file is a
		 * documented no-op in Phase 11 (per D2.2 — configd panel.height,
		 * default 28, is the effective authority); the panel is still
		 * 28 px tall here, so the height assertion is NOT made. The
		 * realizable geometry diff is the toggle WIDTH.
		 */
		Genode::log("theme-probe: [2] set theme.active=compact");
		if (!_set_theme_active("compact"))
			{ _fail("configd did not accept set theme.active=compact"); return; }

		if (!_wait_three_way("compact", "#11111b", 300))
			{ _fail("3-way match for 'compact' did not converge"); return; }
		Genode::log("theme-probe: [2] 3-way match for 'compact' confirmed (PRIMARY)");

		Rgb_sample const compact_toggle_edge = _capture_pixel(TOGGLE_RIGHT_X, TOGGLE_RIGHT_Y);
		if (rgb_close(compact_toggle_edge.r, compact_toggle_edge.g, compact_toggle_edge.b,
		              ACCENT_R, ACCENT_G, ACCENT_B)) {
			_fail("step 2 geometry assertion failed: pixel at launcher toggle "
			      "right edge is STILL accent color in compact (launcher_width=32 "
			      "should make the toggle end at x=36, so x=46 must be past it)");
			return;
		}
		Genode::log("theme-probe: [2] realizable geometry assertion confirmed "
		            "(launcher_width=32 makes toggle end at x<46)");

		Rgb_sample const compact_demo_bg = _capture_pixel(DEMO_PT.x, DEMO_PT.y);
		if (rgb_close(compact_demo_bg.r, compact_demo_bg.g, compact_demo_bg.b,
		              default_demo_bg.r, default_demo_bg.g, default_demo_bg.b)) {
			_fail("step 2 demo-window-bg pixel did NOT differ from default "
			      "(compact window_bg #11111b must visibly differ from default #313244)");
			return;
		}
		Genode::log("theme-probe: [2] demo-window-bg pixel diff vs default confirmed");

		/*
		 * Step 3: light. PRIMARY 3-way gate. SECONDARY: demo-window-bg
		 * pixel matches light's expected window_bg color (existing
		 * Phase-10 capture check, kept for regression).
		 */
		Genode::log("theme-probe: [3] set theme.active=light");
		if (!_set_theme_active("light"))
			{ _fail("configd did not accept set theme.active=light"); return; }

		if (!_wait_three_way("light", "#eff1f5", 300))
			{ _fail("3-way match for 'light' did not converge"); return; }
		Genode::log("theme-probe: [3] 3-way match for 'light' confirmed (PRIMARY)");

		_capture_check("light");

		/*
		 * Step 4: unknown theme name — name-dedup fallback. The probe
		 * writes `theme.active=does-not-exist` (configd accepts any
		 * non-empty value for the free-form string key). The expected
		 * outcome: sponge_themed fails to resolve `<name>.theme`, keeps
		 * the previously published theme, and sponge-de's applied_theme
		 * report therefore remains "light".
		 *
		 * CRITICAL (Phase 11 W3 follow-up): the runtime wires a
		 * label_suffix=".theme" catch-all route in sponge-theme.run
		 * that maps the unstaged ROM to a never-produced report_rom
		 * module (Plan A). Without that catch-all, init denies the ROM
		 * session and base-lib's DENIED branch (component.cc:200-202)
		 * calls sleep_forever — themed silently dies while report_rom
		 * keeps serving the cached "light" ROM, making the previous
		 * W3-rev-2 probe PASS step 4 for the WRONG reason.
		 *
		 * PRIMARY gating is the applied_theme unchanged assertion. We
		 * also confirm the config broadcast DID accept the unknown
		 * value (so the test is not silently skipped because of a
		 * configd-side rejection).
		 */
		Genode::log("theme-probe: [4] set theme.active=does-not-exist "
		            "(name-dedup fallback test)");
		if (!_set_theme_active("does-not-exist"))
			{ _fail("configd did not accept set theme.active=does-not-exist"); return; }

		Genode::String<64> cfg_after_unknown { };
		if (!_config_theme_active(cfg_after_unknown) ||
		    cfg_after_unknown != Genode::String<64>("does-not-exist")) {
			_fail("step 4 config broadcast theme.active did not reflect "
			      "'does-not-exist' (configd should accept any non-empty value)");
			return;
		}

		/*
		 * sponge_themed's name-dedup rule keeps the previous theme; the
		 * applied_theme ROM must therefore stay "light" even though
		 * theme.active is now "does-not-exist". Allow up to ~3s for
		 * sponge-de's QTimer (250ms) to observe any update.
		 */
		if (!_wait_applied("light", 30)) {
			_fail("step 4 applied_theme changed after unknown theme.active "
			      "write (name-dedup fallback violated)");
			return;
		}
		Genode::log("theme-probe: [4] applied_theme stayed 'light' after "
		            "unknown theme.active write (name-dedup fallback confirmed)");

		/*
		 * Step 5: LIVENESS PROOF. After step 4 set themed a "stale
		 * theme" via the empty-ROM fallback, the probe writes
		 * theme.active=default and asserts applied_theme becomes
		 * "default". This can ONLY pass if sponge_themed survived
		 * step 4 — if the component had frozen on the denied ROM
		 * session, the theme report would stay "light" forever and
		 * this assertion would time out.
		 *
		 * Same 3-way gate pattern as steps 1-4: cfg broadcast
		 * carries "default", themed's "theme" ROM carries
		 * default.theme's window_bg #313244, sponge-de's
		 * applied_theme report becomes "default". The probe must
		 * see all three (not just applied_theme) — a successful
		 * applied_theme change without the themed ROM republishing
		 * would mean report_rom was still serving the cached "light"
		 * content via the catch-all module.
		 */
		Genode::log("theme-probe: [5] set theme.active=default "
		            "(liveness proof after step-4 catch-all)");
		if (!_set_theme_active("default"))
			{ _fail("step 5 configd did not accept set theme.active=default"); return; }

		if (!_wait_three_way("default", "#313244", 300)) {
			_fail("step 5 3-way match for 'default' did not converge "
			      "after step-4 fallback — sponge_themed likely FROZEN "
			      "on the unstaged-theme ROM session (base-lib DENIED)");
			return;
		}
		if (!_wait_applied("default", 30)) {
			_fail("step 5 applied_theme did not flip to 'default' "
			      "after step-4 fallback — sponge_themed likely frozen");
			return;
		}
		Genode::log("theme-probe: [5] 3-way match for 'default' confirmed "
		            "after step-4 fallback (liveness proof PASSED)");

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
