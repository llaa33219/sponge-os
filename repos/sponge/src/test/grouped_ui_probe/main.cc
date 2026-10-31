/* SPDX-License-Identifier: Apache-2.0
 *
 * grouped_ui_probe — P2.4 D16R.11 grouped-runtime UI acceptance probe.
 *
 * Proves, on the PRODUCT grouped topology (sponge-desktop-disk with
 * pkg_runtime_simple / pkg_runtime_terminal / pkg_runtime_browser),
 * that REAL launcher input reaches all three runtime classes and
 * produces the D16R.11 acceptance shape per class:
 *
 *   first paint (Capture pixels) + a real typed-input event with a
 *   measurable rendered delta.
 *
 * Everything is driven over the run/qmp.inc QMP-TARGET marker
 * contract: the probe emits ONE marker at a time and only after the
 * previous UI state change is confirmed (the serialized choreography
 * contract of qmp.inc), so each host-side qmp_exec_target dispatch
 * consumes exactly one instruction. The clicks traverse the real
 * chain: QMP usb-tablet abs -> pc_usb_host -> usb_hid -> event_filter
 * -> nitpicker -> sponge-de's Qt widgets (panel toggle, launcher menu
 * entry) -> LauncherController -> sponge_pkgd launch request ->
 * class runtime <start> node. Never a synthetic nitpicker event, never
 * a backend/pkgd call from this probe: this probe only OBSERVES and
 * EMITS MARKERS.
 *
 * Marker chain (guest -> host), in emission order:
 *
 *   grouped-ui-probe: ready simple-target=<app> terminal-target=terminal browser-target=falkon
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 simple-class menu entry
 *   grouped-ui-probe: simple-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 focus click (target widget)
 *   QMP-TARGET type <typed-string>            typed input, simple class
 *   grouped-ui-probe: simple-class typed delta (...)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 terminal-class menu entry
 *   grouped-ui-probe: terminal-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 focus click, target window
 *   QMP-TARGET type echo grouped-ok           typed input, terminal class
 *   grouped-ui-probe: terminal-class typed delta (...)
 *   QMP-TARGET key ret                        submit
 *   grouped-ui-probe: terminal-class execution delta (...)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 browser-class menu entry
 *   grouped-ui-probe: browser-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 focus click (URL bar)
 *   QMP-TARGET type sponge                    typed input, browser class
 *   grouped-ui-probe: browser-class typed delta (...)
 *   grouped-ui-probe: PASS
 *
 * Any failure logs "grouped-ui-probe: FAIL <reason>" and exits
 * non-zero (fail-loud, bounded — never a silent hang).
 *
 * Simple-class target: textedit PREFERRED, calculator fallback. The
 * launcher popup lays its entries out as category sections in
 * first-seen order over a name-sorted app list, and the popup's
 * nitpicker domain is (0,28,341,532). With the `desktop` bake profile
 * the alpha-sorted list (calculator, debug, falkon, files, hello,
 * pdf_view, terminal, textedit) puts the Editors section's single
 * textedit entry at popup-local y=482..532 — fully covered by the
 * 532px domain. The probe computes the layout from the LIVE launcher
 * report and selects the first fully-visible preferred target
 * (textedit) or the fallback (calculator); if the launcher geometry
 * ever grows the textedit entry, the probe picks it automatically. The class evidence does not depend on which simple
 * app ran: the window label is checked for the `pkg_runtime_simple`
 * prefix, so the pixels can only come from the simple-class runtime.
 *
 * Browser/falkon first paint + basic input is the P2.6 leg (Phase 16
 * P2.6, docs/plans/phase16-runtime-overhaul.md "Falkon / browser
 * staging"): the BROWSER class target is the `falkon` launcher entry
 * (pkg_runtime_browser -> falkon), asserted with the same first-paint
 * predicate and the same typed-delta thresholds as the other classes.
 * Its first-paint poll carries a longer iteration budget (the WebEngine
 * closure resolves from disk before the window appears) but every
 * pixel threshold is shared and unchanged. Basic input is a typed
 * string into the browser's URL bar, clicked and measured at the
 * runtime-calibrated client-pixel geometry (URL_CLICK_* / URL_BAND_*);
 * page load / DHCP / HTTP are NOT asserted (criterion 15 follow-up).
 *
 * Class placement checks (the evidence cannot come from a different
 * runtime):
 *   (a) the st_simple / st_terminal / st_browser class state reports
 *       are live (the grouped runtimes exist and publish state),
 *   (b) the target window's wm window_list label carries the
 *       "pkg_runtime_simple -> " / "pkg_runtime_terminal -> " /
 *       "pkg_runtime_browser -> " prefix
 *       AND the target app token after it,
 *   (c) the layouter window_layout report places that window inside
 *       the pkg_runtime assign region.
 *
 * REPORT FORMAT (load-bearing): the wm's window_list, the layouter's
 * window_layout/focus and the class inits' state reports are produced
 * with Genode::Generator::generate, which emits HID unless the
 * producing component sets <config generate_xml="yes"/> (ldso's
 * format switch, base/src/lib/ldso/include/config.h) — none do. The
 * probe therefore parses every report ROM with the FORMAT-AGNOSTIC
 * Node API (Attached_rom_dataspace::node()), never .xml(): .xml() on
 * HID bytes silently yields <empty/> (attached_rom_dataspace.h), which
 * reads as "no window ever appears" and hides real failures. Only
 * sponge-de's launcher report is XML (generated via generate_xml).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>
#include <util/construct_at.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Panel launcher-toggle geometry (theme: padding=8, margin=4,
 * launcher_width=48, panel_height=28 — the button is always FIRST in
 * PanelWidget's QHBoxLayout regardless of panel.visible_widgets).
 * Calibrated empirically by sponge_de_probe (W2): toggle center (32,14).
 */
int const S_TOGGLE_X = 32;
int const S_TOGGLE_Y = 14;

/*
 * Launcher popup geometry. The "Sponge Launcher" window is pinned by
 * the nitpicker "launcher" domain at screen (0,28) 341x532 (the
 * product Tier-0 config). Popup-local layout follows
 * launcher_menu_view.cc::repopulate() with the default theme:
 *   root QVBoxLayout: contentsMargins(8,8,8,8), spacing 4
 *   category heading QLabel: ~20px tall (11pt bold + 2px padding)
 *   entry QPushButton: 50px tall (min-height 50px, 16px v-padding)
 *   entries QVBoxLayout spacing: 4
 * The heading height and the first-entry top (pad 8 + heading 20 +
 * gap 4 = 32) are calibrated against sponge_de_probe's W2 empirical
 * constants ("button top is at popup-local (8, 32)").
 */
int const DOMAIN_X = 0, DOMAIN_Y = 28, DOMAIN_W = 341, DOMAIN_H = 532;
int const PAD = 8, GAP = 4, HEADING_H = 20, BUTTON_H = 50;
int const MENU_W = 341;

/*
 * Region around every pkg_runtime window. The layouter's
 * `label_prefix: pkg_runtime` assign rule pins floating package
 * windows at (50,300,320,240); this region adds decorator-margin
 * slack so the whole decorated frame is covered whatever the exact
 * margins are. Used for popup/paint change detection (snapshots vs
 * baseline), never for identity — identity is bound by the
 * window_list label check.
 */
int const WIN_REG_X = 40, WIN_REG_Y = 280, WIN_REG_W = 360, WIN_REG_H = 280;

/* Sample-stride grid + 4-bit-per-channel buckets (textedit_probe's
 * proven compact snapshot recipe). */
int const SAMPLE_STRIDE = 4;
unsigned const COLOR_BUCKET_SHIFT = 4;

/* Max snapshot samples: the popup region is the largest (341x532). */
enum {
	POPUP_SAMPLES = (DOMAIN_W / SAMPLE_STRIDE) * (DOMAIN_H / SAMPLE_STRIDE),
	WIN_SAMPLES   = (WIN_REG_W / SAMPLE_STRIDE) * (WIN_REG_H / SAMPLE_STRIDE),
	MAX_SAMPLES   = POPUP_SAMPLES > WIN_SAMPLES ? POPUP_SAMPLES : WIN_SAMPLES
};

/*
 * Change-detection thresholds, counted in stride-grid sample points.
 *
 * POPUP_OPEN_FLOOR: with the `desktop` profile the opened popup draws
 *   ~8 entry buttons (325x50) + 4 headings over the domain — more than
 *   a third of the domain's grid points change. 150 leaves slack.
 * POPUP_CLOSED_MAX: the popup hid — the region returns to its
 *   pre-toggle pixels (nitpicker bg + whatever sits below the domain).
 * PAINT_FLOOR: a new package window covering ~320x240 replaces the
 *   previous content in WIN_REG — thousands of points change. 150
 *   still separates a real paint from encoder noise.
 * TYPED_FLOOR / 2x-baseline (textedit_probe's misleading_success_
 *   output defense): a lone cursor blink can never pass; a typed
 *   digit pair ("42" in a QSpinBox + its output label) changes ~30
 *   points at stride 4, "echo ok" changes hundreds.
 */
unsigned const POPUP_OPEN_FLOOR = 150;
unsigned const POPUP_CLOSED_MAX = 50;
unsigned const PAINT_FLOOR      = 150;
unsigned const TYPED_FLOOR      = 12;

/*
 * First-paint poll budgets (iterations of 200 ms). NOT thresholds —
 * every pixel predicate above is shared and unchanged across classes.
 * The browser class gets the longer budget because falkon's ~509 MiB
 * WebEngine closure resolves from disk (rom_pkg) before its window can
 * appear; the simple/terminal budget is the P2.4 value, untouched.
 */
unsigned const PAINT_ITERS_DEFAULT = 1500;
unsigned const PAINT_ITERS_BROWSER = 3000;

/*
 * Browser URL-bar focus/typing geometry in CLIENT-relative pixels
 * (P2.6 runtime calibration —
 * docs/evidence/phase16-p26-browser-acceptance.md). Falkon's 787x569
 * client lays out in FIXED pixels (tab bar rows 0..28, navigation bar
 * rows 28..60 with the URL field around x=264..784, bookmarks bar
 * rows 60..88, web view below), so unlike the calculatorform
 * 400x300-normalized path these offsets must not scale with the
 * window. URL_CLICK_* is the empirically proven focus point (the
 * evidence run typed "sponge" there and produced the URL-bar
 * completer delta); URL_BAND_* is that run's measured changed-sample
 * bounding box of the typed input (the bar row plus the completer
 * popup zone below it). Both are re-verified every acceptance run by
 * the typed-delta predicate and the window-rect log lines.
 */
int const URL_CLICK_DX = 393, URL_CLICK_DY = 37;
int const URL_BAND_DY = 28, URL_BAND_DH = 200;

/* Max launcher apps the layout computation accepts. */
enum { MAX_APPS = 32 };

struct App_entry
{
	char name[40]     { };
	char category[40] { };
	int  y            { };   /* popup-local button top */
};


bool str_prefix(char const *s, char const *prefix)
{
	return Genode::strcmp(s, prefix, Genode::strlen(prefix)) == 0;
}

bool str_contains(char const *haystack, char const *needle)
{
	Genode::size_t const n = Genode::strlen(needle);
	if (n == 0)
		return true;
	for (char const *p = haystack; *p; ++p)
		if (Genode::strcmp(p, needle, n) == 0)
			return true;
	return false;
}

/*
 * Case-insensitive name order (launcher_menu_view.cc less_than_alpha:
 * QString::compare(..., CaseInsensitive)).
 */
int alpha_cmp(char const *a, char const *b)
{
	for (;; ++a, ++b) {
		int ca = *a, cb = *b;
		if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
		if (ca != cb) return ca < cb ? -1 : 1;
		if (ca == 0) return 0;
	}
}

} /* anonymous namespace */


struct Grouped_ui_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "grouped-ui-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };

	Genode::Attached_rom_dataspace _launcher      { _env, "launcher" };
	Genode::Attached_rom_dataspace _window_list   { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };
	Genode::Attached_rom_dataspace _focus         { _env, "focus" };
	Genode::Attached_rom_dataspace _st_simple     { _env, "st_simple" };
	Genode::Attached_rom_dataspace _st_terminal   { _env, "st_terminal" };
	Genode::Attached_rom_dataspace _st_browser    { _env, "st_browser" };

	App_entry _apps[MAX_APPS] { };
	unsigned  _num_apps { 0 };

	unsigned _snap[MAX_SAMPLES]   { };
	unsigned _snap_b[MAX_SAMPLES] { };

	Grouped_ui_probe(Genode::Env &env) : _env(env) { }

	template <typename... ARGS>
	void _fail(ARGS const &... args)
	{
		Genode::error("grouped-ui-probe: FAIL ", args...);
		_env.parent().exit(1);
	}

	static unsigned _bucket(Pixel const &p)
	{
		return ((unsigned)p.r() >> COLOR_BUCKET_SHIFT) << 8
		     | ((unsigned)p.g() >> COLOR_BUCKET_SHIFT) << 4
		     | ((unsigned)p.b() >> COLOR_BUCKET_SHIFT);
	}

	/* Snapshot a screen rect's stride-grid color buckets into `out'. */
	void _snapshot(int x, int y, int w, int h, unsigned *out)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0;
		for (int yy = y; yy < y + h && i < (unsigned)MAX_SAMPLES; yy += SAMPLE_STRIDE)
			for (int xx = x; xx < x + w && i < (unsigned)MAX_SAMPLES; xx += SAMPLE_STRIDE)
				out[i++] = _bucket(px[yy * SCREEN_W + xx]);
	}

	/* Count of stride-grid points changed vs a prior snapshot. */
	unsigned _delta(int x, int y, int w, int h, unsigned const *base)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0, changed = 0;
		for (int yy = y; yy < y + h && i < (unsigned)MAX_SAMPLES; yy += SAMPLE_STRIDE) {
			for (int xx = x; xx < x + w && i < (unsigned)MAX_SAMPLES; xx += SAMPLE_STRIDE) {
				if (_bucket(px[yy * SCREEN_W + xx]) != base[i])
					++changed;
				++i;
			}
		}
		return changed;
	}

	/* ---- launcher report: app list + popup layout computation ---- */

	bool _parse_launcher()
	{
		_launcher.update();
		if (!_launcher.valid() || _launcher.size() == 0)
			return false;

		_num_apps = 0;
		Genode::Node const root = _launcher.node();
		if (!root.has_type("launcher"))
			return false;
		root.for_each_sub_node("app", [&] (Genode::Node const &a) {
			if (_num_apps >= MAX_APPS)
				return;
			Genode::String<40> name =
				a.attribute_value("name", Genode::String<40>());
			Genode::String<40> category =
				a.attribute_value("category", Genode::String<40>());
			Genode::copy_cstring(_apps[_num_apps].name, name.string(),
			                     sizeof(_apps[_num_apps].name));
			Genode::copy_cstring(_apps[_num_apps].category,
			                     category.string(),
			                     sizeof(_apps[_num_apps].category));
			++_num_apps;
		});
		return _num_apps > 0;
	}

	/*
	 * Mirror launcher_menu_view.cc::repopulate(): apps in alpha order,
	 * category sections in first-seen order, headings and buttons at
	 * the calibrated constants. Fills e.y with the popup-local button
	 * top of every entry.
	 */
	void _compute_layout()
	{
		/* insertion-sort the app set by name */
		for (unsigned i = 1; i < _num_apps; ++i) {
			App_entry key = _apps[i];
			unsigned j = i;
			while (j > 0 && alpha_cmp(_apps[j - 1].name, key.name) > 0) {
				_apps[j] = _apps[j - 1];
				--j;
			}
			_apps[j] = key;
		}

		/* first-seen category order == the root-layout add order */
		char cat_order[MAX_APPS][40] { };
		unsigned num_cat = 0;
		for (unsigned i = 0; i < _num_apps; ++i) {
			bool seen = false;
			for (unsigned c = 0; c < num_cat; ++c)
				if (Genode::strcmp(cat_order[c], _apps[i].category) == 0)
					seen = true;
			if (!seen && num_cat < MAX_APPS) {
				Genode::copy_cstring(cat_order[num_cat],
				                     _apps[i].category,
				                     sizeof(cat_order[0]));
				++num_cat;
			}
		}

		int y = PAD;
		bool first_section = true;
		for (unsigned c = 0; c < num_cat; ++c) {
			if (!first_section)
				y += GAP;   /* root spacing: prev entries item -> heading */
			first_section = false;
			y += HEADING_H + GAP;   /* heading + root spacing -> entries */

			bool first_entry = true;
			for (unsigned i = 0; i < _num_apps; ++i) {
				if (Genode::strcmp(_apps[i].category, cat_order[c]) != 0)
					continue;
				if (!first_entry)
					y += GAP;   /* entries-layout inner spacing */
				first_entry = false;
				_apps[i].y = y;
				y += BUTTON_H;
			}
		}
	}

	/* Fully visible in the launcher domain? (center-clickable) */
	bool _entry_visible(App_entry const &e) const
	{
		return e.y >= 0 && e.y + BUTTON_H <= DOMAIN_H;
	}

	/* x=14: proven left-fill click column (de_survival_probe) —
	 * right of the button's x:8 edge, left of the label glyphs. */
	int _entry_click_x() const { return DOMAIN_X + 14; }

	int _entry_click_y(App_entry const &e) const
	{
		return DOMAIN_Y + e.y + BUTTON_H / 2;
	}

	App_entry const *_find_app(char const *name) const
	{
		for (unsigned i = 0; i < _num_apps; ++i)
			if (Genode::strcmp(_apps[i].name, name) == 0)
				return &_apps[i];
		return nullptr;
	}

	/* ---- wm window_list / layouter window_layout parsing ---- */

	/*
	 * Label match: the class prefix is the runtime-class binding (the
	 * evidence cannot come from a different runtime), the app token
	 * is matched only AFTER the prefix (so `pkg_runtime_terminal`
	 * cannot self-match the "terminal" token).
	 */
	static bool _label_matches(char const *label, char const *class_prefix,
	                           char const *app_token)
	{
		if (!str_prefix(label, class_prefix))
			return false;
		return str_contains(label + Genode::strlen(class_prefix), app_token);
	}

	/*
	 * Find the window whose label matches. Returns the wm window id,
	 * 0 if absent.
	 */
	unsigned _find_window(char const *class_prefix, char const *app_token)
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0)
			return 0;

		unsigned found = 0;
		Genode::Node const root = _window_list.node();
		root.for_each_sub_node("window", [&] (Genode::Node const &w) {
			if (found)
				return;
			Genode::String<160> label =
				w.attribute_value("label", Genode::String<160>());
			if (!_label_matches(label.string(), class_prefix, app_token))
				return;
			found = w.attribute_value("id", 0u);
		});
		return found;
	}

	/*
	 * The layouter's window_layout report nests each placed window
	 * under a boundary node and identifies it by the wm window id
	 * (title = label + window title, window_layouter/window.h
	 * Window::generate):
	 *
	 *   <boundary ...><window id="2" title="..." xpos="50"
	 *       ypos="328" width="320" height="240"/></boundary>
	 *
	 * Locate the already-identified window id and return its nonzero
	 * geometry. Layout nodes carry `id`/`title`, never `label`.
	 */
	bool _window_rect(unsigned window_id,
	                  int &x, int &y, int &w, int &h)
	{
		_window_layout.update();
		if (!_window_layout.valid() || _window_layout.size() == 0)
			return false;

		bool found = false;
		Genode::Node const root = _window_layout.node();
		root.for_each_sub_node("boundary", [&] (Genode::Node const &b) {
			b.for_each_sub_node("window", [&] (Genode::Node const &n) {
				if (found)
					return;
				if (n.attribute_value("id", 0u) != window_id)
					return;
				x = n.attribute_value("xpos", 0);
				y = n.attribute_value("ypos", 0);
				w = n.attribute_value("width", 0);
				h = n.attribute_value("height", 0);
				found = w > 0 && h > 0;
			});
		});
		return found;
	}

	/* Current focused window id (layouter focus report), 0 if none. */
	unsigned _focused_id()
	{
		_focus.update();
		if (!_focus.valid() || _focus.size() == 0)
			return 0;
		unsigned id = 0;
		_focus.node().with_optional_sub_node("window",
			[&] (Genode::Node const &w) {
				id = w.attribute_value("id", 0u);
			});
		return id;
	}

	/* ---- bounded first-paint failure diagnostics ---- */

	static bool _pixel_is_bg(Pixel const &p)
	{
		return p.r() >= 0x16 && p.r() <= 0x26
		    && p.g() >= 0x16 && p.g() <= 0x26
		    && p.b() >= 0x26 && p.b() <= 0x36;
	}

	/* Fraction (percent) of sampled WIN_REG pixels that are not the
	 * nitpicker background — tells whether ANY window is composited
	 * in the pkg_runtime assign region. */
	unsigned _win_reg_nonbg_pct()
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned total = 0, nonbg = 0;
		for (int y = WIN_REG_Y; y < WIN_REG_Y + WIN_REG_H; y += SAMPLE_STRIDE) {
			for (int x = WIN_REG_X; x < WIN_REG_X + WIN_REG_W; x += SAMPLE_STRIDE) {
				++total;
				if (!_pixel_is_bg(px[y * SCREEN_W + x]))
					++nonbg;
			}
		}
		return total ? (100 * nonbg) / total : 0;
	}

	/*
	 * One-line-per-child dump of a class runtime's state report:
	 * name + binary + exited= (init's Child::report_state format).
	 * Distinguishes "never spawned" from "spawned and exited" from
	 * "alive but never opened a Gui session".
	 */
	void _log_class_children(char const *which,
	                         Genode::Attached_rom_dataspace &rom)
	{
		rom.update();
		if (!rom.valid() || rom.size() == 0) {
			Genode::log("grouped-ui-probe: diag ", which, " state: <empty>");
			return;
		}
		Genode::Node const root = rom.node();
		root.for_each_sub_node("child", [&] (Genode::Node const &c) {
			Genode::String<48> name =
				c.attribute_value("name", Genode::String<48>());
			Genode::String<64> binary =
				c.attribute_value("binary", Genode::String<64>());
			bool const has_exited =
				c.has_attribute("exited");
			int const exited =
				c.attribute_value("exited", -1);
			Genode::log("grouped-ui-probe: diag ", which, " child ",
			            name.string(), " bin=", binary.string(),
			            has_exited
			                ? Genode::String<32>(" exited=", exited)
			                : Genode::String<32>(""));
		});
	}

	/* Raw wm window_list dump (id + label) — the registration truth. */
	void _log_window_list()
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0) {
			Genode::log("grouped-ui-probe: diag window_list: <empty>");
			return;
		}
		unsigned n = 0;
		Genode::Node const root = _window_list.node();
		root.for_each_sub_node("window", [&] (Genode::Node const &w) {
			++n;
			Genode::String<160> label =
				w.attribute_value("label", Genode::String<160>());
			Genode::log("grouped-ui-probe: diag window id=",
			            w.attribute_value("id", 0u),
			            " label=", label.string());
		});
		Genode::log("grouped-ui-probe: diag window_list: ", n, " entries");
	}

	/*
	 * Compact failing-layer identification at first-paint timeout:
	 *   never-registered    — no matching window in window_list (child
	 *                         exited / never opened Gui / label drift)
	 *   registered-no-rect  — window in window_list but the layouter
	 *                         assigned no geometry (rules/assign miss —
	 *                         the launch-stall class)
	 *   placed-no-pixels    — registered + placed but the region never
	 *                         changed (occlusion / render stall)
	 */
	void _diag_first_paint(char const *class_prefix, char const *app_token,
	                       unsigned max_changed)
	{
		_log_window_list();
		_log_class_children("st_simple", _st_simple);
		_log_class_children("st_terminal", _st_terminal);
		_log_class_children("st_browser", _st_browser);
		unsigned const id = _find_window(class_prefix, app_token);
		int rx = 0, ry = 0, rw = 0, rh = 0;
		bool const have_rect = _window_rect(id, rx, ry, rw, rh);
		unsigned const nonbg = _win_reg_nonbg_pct();

		char const *layer = "placed-no-pixels";
		if (!id)
			layer = "never-registered";
		else if (!have_rect)
			layer = "registered-no-rect";

		Genode::log("grouped-ui-probe: diag first-paint layer=", layer,
		            " id=", id, " rect_ok=", (int)have_rect,
		            " max_changed=", max_changed,
		            " win_reg_nonbg=", nonbg, "%");
	}

	/* ---- QMP-TARGET emissions (the only logs allowed to carry the
	 *      marker text) ---- */

	void _emit_click(int x, int y)
	{
		Genode::log("QMP-TARGET tablet ", x, " ", y);
	}

	void _emit_key(char const *key)
	{
		Genode::log("QMP-TARGET key ", key);
	}

	void _emit_type(char const *s)
	{
		Genode::log("QMP-TARGET type ", s);
	}

	/* ---- bounded UI-state polls ---- */

	/* Uses the caller-provided popup-region baseline in _snap (taken
	 * BEFORE the toggle marker is emitted). */
	bool _poll_popup_open()
	{
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(200);
			unsigned changed = _delta(DOMAIN_X, DOMAIN_Y, DOMAIN_W,
			                          DOMAIN_H, _snap);
			if (i % 10 == 0)
				Genode::log("grouped-ui-probe: popup-open poll ", i,
				            " changed=", changed);
			if (changed >= POPUP_OPEN_FLOOR)
				return true;
		}
		return false;
	}

	bool _poll_popup_closed()
	{
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(200);
			bool const open = _popup_band_visible();
			if (i % 10 == 0)
				Genode::log("grouped-ui-probe: popup-closed poll ", i,
				            " open=", (int)open);
			if (!open)
				return true;
		}
		return false;
	}

	/*
	 * Evidence sensor: button-colored bands (window_bg #313244 +-8)
	 * in the OPEN popup at x=14 (left fill, the click column) and
	 * x=170 (evidence only — label glyphs break runs there). Logs
	 * every >=10px vertical run per column, then verifies the
	 * computed entry click y is inside an x=14 band. A gap there
	 * means the row model and the rendered menu disagree; failing
	 * fast with app + y beats a 20s popup-close timeout without a
	 * cause. Bounded: one scan per popup open.
	 */
	static bool _pixel_is_button(Pixel const &p)
	{
		return p.r() >= 0x29 && p.r() <= 0x39
		    && p.g() >= 0x2a && p.g() <= 0x3a
		    && p.b() >= 0x3c && p.b() <= 0x4c;
	}

	/* x=14 is outside every package window, so a button-colored run
	 * here can only be the open launcher, never the application behind it. */
	bool _popup_band_visible()
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		int run = 0;
		for (int y = DOMAIN_Y; y < DOMAIN_Y + DOMAIN_H; ++y) {
			run = _pixel_is_button(px[y * SCREEN_W + 14]) ? run + 1 : 0;
			if (run >= 10)
				return true;
		}
		return false;
	}

	bool _diag_entry_bands(char const *app_token, int click_y)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		static int const scan_cols[] = { 14, 170 };
		bool any = false, covers = false;
		for (unsigned c = 0; c < 2; ++c) {
			int const x = scan_cols[c];
			int run_y0 = -1;
			for (int y = DOMAIN_Y; y < DOMAIN_Y + DOMAIN_H; ++y) {
				if (_pixel_is_button(px[y * SCREEN_W + x])) {
					if (run_y0 < 0)
						run_y0 = y;
					continue;
				}
				if (run_y0 < 0)
					continue;
				if (y - run_y0 >= 10) {
					Genode::log("grouped-ui-probe: diag launcher bands x=",
					            x, " y=", run_y0, "..", y - 1);
					any = true;
					if (x == 14 && click_y >= run_y0 && click_y <= y - 1)
						covers = true;
				}
				run_y0 = -1;
			}
			if (run_y0 >= 0 && DOMAIN_Y + DOMAIN_H - run_y0 >= 10) {
				Genode::log("grouped-ui-probe: diag launcher bands x=",
				            x, " y=", run_y0, "..", DOMAIN_Y + DOMAIN_H - 1);
				any = true;
				if (x == 14 && click_y >= run_y0
				    && click_y <= DOMAIN_Y + DOMAIN_H - 1)
					covers = true;
			}
		}
		if (!any) {
			Genode::log("grouped-ui-probe: diag launcher bands none");
			_fail("launcher buttons not rendered (no bands) for ", app_token);
			return false;
		}
		Genode::log("grouped-ui-probe: diag launcher bands click y=", click_y,
		            covers ? " covered=yes" : " covered=no");
		if (!covers) {
			_fail("entry click y not covered by x=14 button band (", app_token,
			      " y=", click_y, ")");
			return false;
		}
		return true;
	}

	bool _wait_entry_bands()
	{
		for (unsigned i = 0; i < 50; ++i) {
			if (_popup_band_visible())
				return true;
			_timer.msleep(200);
			if (i % 10 == 0)
				Genode::log("grouped-ui-probe: launcher-band wait ", i);
		}
		return false;
	}

	/*
	 * First paint: the target window (identity bound by the class
	 * prefix + app token in window_list) has painted pixels into the
	 * pkg_runtime assign region — the region changed vs the
	 * post-popup-close baseline (taken here, before the window can
	 * paint). window_layout's rect must sit inside WIN_REG — the
	 * class placement check (c).
	 */
	bool _poll_first_paint(char const *class_prefix, char const *app_token,
	                       int &rx, int &ry, int &rw, int &rh,
	                       unsigned max_iters = PAINT_ITERS_DEFAULT)
	{
		_snapshot(WIN_REG_X, WIN_REG_Y, WIN_REG_W, WIN_REG_H, _snap);
		unsigned max_changed = 0;
		for (unsigned i = 0; i < max_iters; ++i) {
			_timer.msleep(200);

			unsigned const id = _find_window(class_prefix, app_token);
			bool const have_rect = _window_rect(id, rx, ry, rw, rh);
			unsigned changed = _delta(WIN_REG_X, WIN_REG_Y, WIN_REG_W,
			                          WIN_REG_H, _snap);
			if (changed > max_changed)
				max_changed = changed;
			if (i % 25 == 0)
				Genode::log("grouped-ui-probe: first-paint poll ", i,
				            " id=", id, " rect_ok=", (int)have_rect,
				            " rect=(", rx, ",", ry, " ", rw, "x", rh, ")",
				            " changed=", changed);
			if (i == 0)
				_diag_first_paint(class_prefix, app_token, max_changed);

			if (id && have_rect && changed >= PAINT_FLOOR) {
				int const cx = rx + rw / 2, cy = ry + rh / 2;
				bool const placed =
					cx >= WIN_REG_X && cx < WIN_REG_X + WIN_REG_W &&
					cy >= WIN_REG_Y && cy < WIN_REG_Y + WIN_REG_H;
				if (!placed) {
					_fail("window placed outside the pkg_runtime assign "
					      "region (x=", rx, " y=", ry, " w=", rw,
					      " h=", rh, ")");
					return false;
				}
				Genode::log("grouped-ui-probe: window id=", id,
				            " rect=(", rx, ",", ry, " ", rw, "x", rh,
				            ") changed=", changed);
				return true;
			}
		}
		_diag_first_paint(class_prefix, app_token, max_changed);
		return false;
	}

	bool _poll_focus(unsigned want_id)
	{
		for (unsigned i = 0; i < 150; ++i) {
			_timer.msleep(200);
			unsigned const id = _focused_id();
			if (i % 25 == 0)
				Genode::log("grouped-ui-probe: focus poll ", i,
				            " id=", id, " want=", want_id);
			if (id && id == want_id)
				return true;
		}
		return false;
	}

	/*
	 * Typed-input delta vs the pre-type snapshot in _snap: the change
	 * must exceed both an absolute floor and twice the cursor-blink
	 * baseline (textedit_probe's defense — a lone blink can never
	 * pass). Returns the measured delta (0 on timeout).
	 */
	unsigned _poll_typed_delta(int x, int y, int w, int h, unsigned baseline)
	{
		for (unsigned i = 0; i < 300; ++i) {
			_timer.msleep(200);
			unsigned changed = _delta(x, y, w, h, _snap);
			if (i % 25 == 0)
				Genode::log("grouped-ui-probe: typed-delta poll ", i,
				            " changed=", changed, " baseline=", baseline);
			if (changed >= TYPED_FLOOR && changed > 2 * baseline)
				return changed;
		}
		return 0;
	}

	/* ---- one class's full acceptance shape ---- */

	/*
	 * focus_dx/focus_dy: focus click into the observed window rect;
	 * negative = window center (textedit/terminal). Nonnegative
	 * offsets are normalized to the measured 400x300 calculatorform
	 * UI geometry (grid margin 9, VBox margin 1, inputSpinBox1 at
	 * (1,26) 46x25 -> center (34,48)) and scaled to the actual
	 * window rect, so inputSpinBox1 stays targeted at any client
	 * size (400x300 or 320x240).
	 *
	 * url_bar: the browser leg's runtime-calibrated path — the
	 * focus/typing geometry is the fixed client-pixel calibration
	 * (URL_CLICK_* / URL_BAND_*) applied to the observed rect, never
	 * the 400x300 scaling (falkon's chrome does not scale with the
	 * window). focus_dx/focus_dy are unused in this mode.
	 *
	 * paint_iters: first-paint poll budget (PAINT_ITERS_DEFAULT for
	 * simple/terminal, PAINT_ITERS_BROWSER for falkon — see the
	 * constant comment; pixel thresholds are shared and unchanged).
	 */
	void _run_class(char const *what, char const *class_prefix,
	                char const *app_token, App_entry const &entry,
	                char const *typed, int focus_dx, int focus_dy,
	                char const *extra_key,
	                unsigned paint_iters = PAINT_ITERS_DEFAULT,
	                bool url_bar = false)
	{
		int rx = 0, ry = 0, rw = 0, rh = 0;

		/* (1) open the launcher popup via the panel toggle (the
		 *     baseline is taken BEFORE the marker leaves). */
		_snapshot(DOMAIN_X, DOMAIN_Y, DOMAIN_W, DOMAIN_H, _snap);
		_emit_click(S_TOGGLE_X, S_TOGGLE_Y);
		if (!_poll_popup_open()) {
			_fail("launcher popup never opened (", what, ")");
			return;
		}

		/* (2) real Qt menu-entry click -> LauncherController ->
		 *     sponge_pkgd launch request -> class runtime */
		if (!_wait_entry_bands()) {
			_fail("launcher buttons never rendered for ", app_token);
			return;
		}
		if (!_diag_entry_bands(app_token, _entry_click_y(entry)))
			return;
		_emit_click(_entry_click_x(), _entry_click_y(entry));
		if (!_poll_popup_closed()) {
			_fail("launcher popup never closed (", what, ")");
			return;
		}

		/* (3) first paint + class placement */
		if (!_poll_first_paint(class_prefix, app_token, rx, ry, rw, rh,
		                       paint_iters)) {
			_fail("no first paint for ", app_token, " in ", class_prefix,
			      " (", what, ")");
			return;
		}
		Genode::log("grouped-ui-probe: ", what,
		            " first paint (label=", class_prefix, app_token,
		            " rect=(", rx, ",", ry, " ", rw, "x", rh, "))");
		/* (4) focus click on the target window (front-most after the
		 *     launch — the layouter brings wildcard-matched new
		 *     windows to the front) */
		unsigned const id = _find_window(class_prefix, app_token);
		if (!id) {
			_fail("window vanished before focus (", what, ")");
			return;
		}
		int const fcx = url_bar ? rx + URL_CLICK_DX
		              : focus_dx >= 0 ? rx + focus_dx * rw / 400
		                              : rx + rw / 2;
		int const fcy = url_bar ? ry + URL_CLICK_DY
		              : focus_dy >= 0 ? ry + focus_dy * rh / 300
		                              : ry + rh / 2;
		_snapshot(rx, ry, rw, rh, _snap);
		_emit_click(fcx, fcy);
		if (!_poll_focus(id)) {
			_fail("target window did not take focus (", what, ")");
			return;
		}
		/* (5) typed input -> measurable rendered delta. The pre-type

		 *     snapshot MUST be taken before the type marker leaves. */
		_timer.msleep(2000);

		unsigned const click_changed = _delta(rx, ry, rw, rh, _snap);
		Genode::log("grouped-ui-probe: ", what, " click_changed=",
		            click_changed);

		int const tx = extra_key ? rx + 8 : rx;
		int const ty = extra_key ? ry + 28
		            : url_bar    ? ry + URL_BAND_DY : ry;
		int const tw = extra_key ? rw - 16 : rw;
		int const th = extra_key ? 20
		            : url_bar    ? URL_BAND_DH : rh;

		if (url_bar)
			Genode::log("grouped-ui-probe: ", what,
			            " url-bar geometry (click=", fcx, ",", fcy,
			            " client=(", URL_CLICK_DX, ",", URL_CLICK_DY,
			            ") band=(", tx, ",", ty, " ", tw, "x", th, "))");

		_snapshot(tx, ty, tw, th, _snap_b);
		_timer.msleep(700);
		unsigned const baseline = _delta(tx, ty, tw, th, _snap_b);

		_snapshot(tx, ty, tw, th, _snap);
		if (typed[0] != '\0')
			_emit_type(typed);

		unsigned const changed = _poll_typed_delta(tx, ty, tw, th, baseline);
		if (!changed) {
			_fail("no typed-input delta (", what, " click_changed=",
			      click_changed, ")");
			return;
		}
		Genode::log("grouped-ui-probe: ", what, " typed delta (changed=",
		            changed, " baseline=", baseline, " click_changed=",
		            click_changed, " typed=\"", typed, "\")");

		if (extra_key) {
			/* Submit only after the host completed the type action. The
			 * execution sample excludes the prompt's input row, so
			 * continued typing cannot masquerade as command output. */
			int const ox = rx + 8, oy = ry + 48;
			int const ow = rw - 16, oh = rh - 56;
			_snapshot(ox, oy, ow, oh, _snap_b);
			_timer.msleep(700);
			unsigned const output_baseline = _delta(ox, oy, ow, oh, _snap_b);
			_snapshot(ox, oy, ow, oh, _snap);
			_emit_key(extra_key);
			unsigned const executed = _poll_typed_delta(ox, oy, ow, oh,
			                                            output_baseline);
			if (!executed) {
				_fail("no command-execution delta (", what,
				      " typed=\"", typed, "\")");
				return;
			}
			Genode::log("grouped-ui-probe: ", what,
			            " execution delta (changed=", executed,
			            " baseline=", output_baseline, ")");
		}
	}

	void run()
	{
		Genode::log("grouped-ui-probe: starting (P2.4 D16R.11)");
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Preconditions — grouped topology alive. The two class
		 * state reports must publish; this is check (a).
		 */
		bool simple_live = false, terminal_live = false, browser_live = false;
		for (unsigned i = 0; i < 600 && !(simple_live && terminal_live && browser_live); ++i) {
			_timer.msleep(200);
			_st_simple.update();
			_st_terminal.update();
			_st_browser.update();
			simple_live   = _st_simple.valid()   && _st_simple.size()   > 0;
			terminal_live = _st_terminal.valid() && _st_terminal.size() > 0;
			browser_live  = _st_browser.valid()  && _st_browser.size()  > 0;
			if (i % 50 == 0)
				Genode::log("grouped-ui-probe: class-state poll ", i,
				            " simple=", (int)simple_live,
				            " terminal=", (int)terminal_live,
				            " browser=", (int)browser_live);
		}
		if (!simple_live || !terminal_live || !browser_live) {
			_fail("grouped class runtimes did not publish state "
			      "(st_simple/st_terminal/st_browser) - grouped topology not live");
			return;
		}
		Genode::log("grouped-ui-probe: class placement preconditions ok "
		            "(st_simple + st_terminal + st_browser live)");

		/*
		 * Launcher report: wait for the FULL desktop pair set — the
		 * same set alpha_probe's final criterion asserts.
		 * alpha_probe's criterion (b) is its LAST step before
		 * "alpha-probe: PASS" and it watches this same report, so a
		 * complete set means the alpha marker is imminent. The settle
		 * BEFORE the ready marker then puts "ready" and every
		 * QMP-TARGET marker AFTER the alpha marker in the serial
		 * stream: a marker emitted before the host's gate window is
		 * consumed by that gate and lost (qmp.inc's serialized-
		 * choreography contract).
		 */
		static char const *const REQUIRED[] = {
			"hello", "terminal", "textedit", "files",
			"calculator", "pdf_view", "falkon" };

		for (unsigned i = 0; i < 3000; ++i) {
			_timer.msleep(200);
			bool all = _parse_launcher();
			if (all)
				for (char const *n : REQUIRED)
					if (!_find_app(n))
						all = false;
			if (all)
				break;
			if (i % 100 == 0)
				Genode::log("grouped-ui-probe: launcher-set poll ", i,
				            " apps=", _num_apps);
		}
		for (char const *n : REQUIRED)
			if (!_find_app(n)) {
				_fail("launcher report never carried the full desktop "
				      "pair set (missing ", n, ")");
				return;
			}
		_compute_layout();

		/* Log the computed popup table — the evidence shows exactly
		 * which entries the clicks target. (No log outside the
		 * _emit_* helpers may carry the marker text.) */
		for (unsigned i = 0; i < _num_apps; ++i)
			Genode::log("grouped-ui-probe: layout ",
			            (char const *)_apps[i].name, " [",
			            (char const *)_apps[i].category, "] y=",
			            _apps[i].y,
			            _entry_visible(_apps[i]) ? "" : " (clipped)");

		App_entry const *term = _find_app("terminal");
		if (!_entry_visible(*term)) {
			_fail("terminal entry is clipped by the launcher domain");
			return;
		}

		/*
		 * Browser target (P2.6): the falkon entry, fully visible at
		 * popup-local y=326 with the `desktop` profile (see the
		 * layout log). Its class binding is the window label prefix
		 * "pkg_runtime_browser -> " — the pixels can only come from
		 * the browser-class runtime.
		 */
		App_entry const *browser = _find_app("falkon");
		if (!browser || !_entry_visible(*browser)) {
			_fail("falkon entry is clipped by the launcher domain");
			return;
		}

		/* Simple target: textedit preferred (fully visible), the
		 * calculator fallback otherwise (see the header note). */
		char const *simple_name  = nullptr;
		char const *simple_typed = nullptr;
		char const *simple_extra_key = nullptr;
		int         simple_focus_dx = -1;
		int         simple_focus_dy = -1;
		App_entry const *simple = _find_app("textedit");
		if (simple && _entry_visible(*simple)) {
			simple_name  = "textedit";
			simple_typed = "hello";
		} else {
			simple = _find_app("calculator");
			if (!simple || !_entry_visible(*simple)) {
				_fail("no visible simple-class launcher entry "
				      "(textedit/calculator)");
				return;
			}
			simple_name  = "calculator";
			simple_typed = "42";
			simple_extra_key = nullptr;
			/* Click inputSpinBox1 directly (client offset (34,48),
			 * calculatorform.ui geometry — see _run_class). */
			simple_focus_dx = 34;
			simple_focus_dy = 48;
		}

		/*
		 * Ordering settle BEFORE the ready marker: alpha_probe's PASS
		 * lands within seconds of the pair set completing (its (b) is
		 * its last criterion), so sleeping here puts "ready" and every
		 * QMP-TARGET marker after the alpha marker in the serial
		 * stream, where the host's post-alpha gates can still see
		 * them.
		 */
		_timer.msleep(60000);

		Genode::log("grouped-ui-probe: ready simple-target=", simple_name,
		            " terminal-target=terminal browser-target=falkon");

		_run_class("simple-class", "pkg_runtime_simple -> ",
		           simple_name, *simple, simple_typed,
		           simple_focus_dx, simple_focus_dy, simple_extra_key);
		_run_class("terminal-class", "pkg_runtime_terminal -> ",
		           "terminal", *term, "echo grouped-ok", -1, -1, "ret");

		/*
		 * Browser leg (P2.6), after terminal per the acceptance
		 * order. First paint with the browser poll budget; basic
		 * typed input "sponge" after a URL-bar focus click. The
		 * focus/typing geometry is the runtime calibration
		 * (URL_CLICK_* / URL_BAND_*, evidence run receipts in
		 * docs/evidence/phase16-p26-browser-acceptance.md): a
		 * client-pixel click on the URL field row, typed-delta
		 * measured over the bar-row/popup band, shared thresholds.
		 * No page-load assertion.
		 */
		_run_class("browser-class", "pkg_runtime_browser -> ",
		           "falkon", *browser, "sponge", 0, 0, nullptr,
		           PAINT_ITERS_BROWSER, true);

		Genode::log("grouped-ui-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Grouped_ui_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
