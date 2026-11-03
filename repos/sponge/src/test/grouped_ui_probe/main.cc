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
 * a backend/pkgd LAUNCH substitute: this probe only OBSERVES and
 * EMITS MARKERS, with sanctioned pkgd `remove` teardowns only —
 * P5.1 falkon (after its browser receipt) and debug (after its Mark
 * receipt, before the calculator spawn) on the probe_request channel
 * (_teardown_pkg), never an input or launch path.
 *
 * Marker chain (guest -> host), in emission order:
 *
 *   grouped-ui-probe: ready simple-target=<app> terminal-target=terminal browser-target=falkon
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 hello menu entry (autostart)
 *   grouped-ui-probe: hello launcher entry click -> already-running (...)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 debug menu entry (autostart)
 *   grouped-ui-probe: debug launcher entry click -> already-running
 *   grouped-ui-probe: debug-class capture (...)
 *   QMP-TARGET tablet <x> <y>                 debug Mark button click
 *   grouped-ui-probe: debug-class mark-click delta (...)
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
 *   grouped-ui-probe: browser settle children=<n> polls=<k>
 *   grouped-ui-probe: falkon removed (...)       pkgd remove teardown (no QMP)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 files menu entry
 *   grouped-ui-probe: files-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 list-row click (double-click candidates)
 *   QMP-TARGET tablet <x> <y>                 list-row click (double-click candidates)
 *   grouped-ui-probe: files-class navigation delta (...)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 pdf_view menu entry
 *   grouped-ui-probe: pdf-view-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 focus click (page)
 *   QMP-TARGET key pgdn                       page-down input (KEY_PAGEDOWN)
 *   QMP-TARGET key down                       scroll input (KEY_DOWN)
 *   grouped-ui-probe: pdf-view-class page-down/scroll input delta (...)
 *   grouped-ui-probe: debug removed (...)     pkgd remove teardown (no QMP)
 *   QMP-TARGET tablet <x> <y>                 panel launcher toggle
 *   QMP-TARGET tablet <x> <y>                 calculator menu entry
 *   grouped-ui-probe: calculator-class first paint (...)
 *   QMP-TARGET tablet <x> <y>                 focus click (inputSpinBox1)
 *   QMP-TARGET type 17                        digit input
 *   grouped-ui-probe: calculator-class digits result delta (...)
 *   QMP-TARGET key up                         spin input
 *   grouped-ui-probe: calculator-class spin result delta (...)
 *   QMP-TARGET tablet <x> <y>                 focus click (textedit, above the sibling stack)
 *   QMP-TARGET type sponge                    final typed delta (concurrent)
 *   grouped-ui-probe: concurrent typed delta (...)
 *   grouped-ui-probe: concurrent windows n=<N>
 *   grouped-ui-probe: PASS
 *
 * P5.1 grouped app matrix (docs/plans/phase16-runtime-overhaul.md
 * "P5.1"): every built-in desktop package gets a real-launcher
 * receipt. hello and debug are GUI-carrying/autostart packages whose
 * launcher entry click is answered `already-running` (pkgd Gate 2);
 * hello is GUI-free so paint/input are recorded N/A by design. The
 * per-app input deltas are real QMP input only (usb-tablet abs +
 * send-key over run/qmp.inc) — never a synthetic nitpicker event.
 * The final assertion is the concurrent-window set: >=5 NON-FALKON
 * app windows from >=2 runtime classes mapped SIMULTANEOUSLY, with
 * the last typed delta delivered while the sibling windows remain
 * mapped. falkon and debug are excluded from the concurrent set BY
 * DESIGN: both are torn down before it (falkon's resident WebEngine
 * closure starved core's CNode construction during the R29 spawn,
 * debug's Qt closure the R30 calculator spawn), and each keeps its
 * own first-paint + input receipt above. The surviving set is
 * exactly five windows — textedit, terminal, files, pdf_view,
 * calculator — from the simple + terminal classes.
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

/* Sponge includes */
#include <sponge/backend_client.h>

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

/*
 * P5.1 app-matrix geometry, window-rect-relative (the same
 * empirically-pinned convention as URL_CLICK_*). The decorator metrics
 * used for the derivations below are the motif-decorator values the
 * terminal leg already proves on every run: 28 px title bar (the
 * typed band sits at ry+28..48) and 4 px borders
 * (decorator/window.h `_border_size`, referenced by run/qmp.inc's
 * tablet-drag recipe). Every constant here is RE-VERIFIED on each
 * acceptance run by its geometry marker line + the input-delta
 * predicate; the red-first calibration receipts live in
 * docs/evidence/phase16-p51-grouped-app-matrix.md.
 *
 * debug Mark button: the debug window self-resizes to 700x480 at the
 *   assign origin (R2 evidence, docs/evidence/phase16-p51-grouped-
 *   app-matrix.md), and the grouped arm's debug assign rule places it
 *   at ypos 60 (screen y 88) so the bottom edge (88+480=568) is
 *   on-screen. The Mark button hugs the client bottom-left, but the
 *   exact row depends on style-resolved layout margins (9..20 px,
 *   not derivable — R3 clicked the derived point and produced a zero
 *   delta), so the leg CLICKS A CANDIDATE COLUMN (six y values 12 px
 *   apart, x fixed on Mark's column) and the first candidate that
 *   fires is the receipt; the marker logs the winning y. Candidate
 *   clicks after the win land on the status label (harmless).
 * files first list row ("demo/" — alphabetically first at the VFS
 *   root): the middle row is list (left) + preview (right) with 380px
 *   minimums (files_window.cc), and the app reserves a "generous left
 *   strip" for the row click — the lean probe uses client (100, 54)
 *   (files_probe DBL_X/DBL_Y). The product click is therefore
 *   window-relative (104, 85): client origin (~4, ~28) + (100, 54).
 *   Window-relative x=104 is inside the list at ANY window size (the
 *   list is left-aligned), unlike the window center which at 800x600
 *   lands on the preview/splitter (R12 evidence). Candidate y values
 *   cover the row-band uncertainty.
 */
int const FILES_CLICK_DX = 104;
int const FILES_ROW_DY[3]  = { 63, 85, 107 };

/*
 * calculator inputSpinBox1 + result band. The window_layout rect
 *   origin sits at the DECORATED frame corner; the Qt widget (==
 *   calculatorform.ui form) origin is rect + (4,28) — decorator
 *   border + title. The chain: debug's Mark button (derived widget
 *   row center ~451) won at rect y 479 = 451+28; the terminal's
 *   proven text band is ry+28..48 = widget rows 0..20; the files
 *   first row (widget y ~54, lean probe DBL_Y) won at rect y 85.
 *   So form coordinates map to rect as form+(4,28): inputSpinBox1
 *   center (33,47.5) -> (37,76), outputWidget (196,33,36,27) ->
 *   (200,61,36,27). The result band covers the output label under
 *   either rect-origin reading and no blinking widget (the spinbox
 *   cursor stays left of it). Sampled at STRIDE 1: the label's
 *   "0"->"17" glyph change measured only 3 samples at stride 4
 *   (R23 red: changed=3 below TYPED_FLOOR=12) — fine sampling is
 *   what makes the result delta a measurable signal at all.
 */
int const DEBUG_MARK_DY[6] = { 419, 431, 443, 455, 467, 479 };
int const DEBUG_MARK_DX = 45;
int const CALC_SPIN_DX = 37, CALC_SPIN_DY = 76;
int const CALC_BAND_DX = 185, CALC_BAND_DY = 30;
int const CALC_BAND_DW = 95,  CALC_BAND_DH = 85;
int const CALC_BAND_STRIDE = 1;

/*
 * pdf_view page-down/scroll: mupdf 0.9 pdfapp maps KEY_PAGEDOWN to
 * '.' (page flip) and KEY_DOWN to 'j' (scroll by image->h/10). Two
 * port facts (R21/R22 red evidence, docs/evidence/phase16-p51-grouped-
 * app-matrix.md) shape this leg:
 *   - pdfapp.c clamps pageno to pagecount and re-renders only on a
 *     real page change, so page-down is a no-op on a one-page
 *     document — the product arm therefore stages a TWO-PAGE
 *     sample.pdf (run/sponge-desktop-disk.run staging block) and the
 *     pdf-doc receipt asserts pages>=2;
 *   - Pdf_view::show (app/pdf_view main.cc) applies no panx/pany to
 *     the blit, so scroll alone cannot change a pixel — its
 *     contribution is still dispatched and measured, logged beside
 *     page-down's, never hidden behind the pair's green label.
 * The assertion is the page-down/scroll input delta over the pair.
 */
int const PDF_PGDN_POLL_ITERS = 25;

/*
 * Concurrent-window assertion floor: >=5 app windows from >=2
 * runtime classes mapped simultaneously (P5.1 requirement).
 */
unsigned const CONCURRENT_MIN_WINDOWS = 5;
unsigned const CONCURRENT_MIN_CLASSES = 2;

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
	Genode::Attached_rom_dataspace _launch_result { _env, "launcher_result" };
	Genode::Attached_rom_dataspace _files_report  { _env, "files" };
	Genode::Attached_rom_dataspace _sample_pdf    { _env, "sample.pdf" };

	Sponge::Backend::ReportRomClient _pkgd {
		_env, "probe_request", "probe_result" };

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

	/* Snapshot a screen rect's stride-grid color buckets into `out'.
	 * Clamped to the screen: app windows may extend past the 768px
	 * display (self-resized Qt windows keep their requested size), so
	 * the sampler must never index outside the capture buffer.
	 * `stride' must match between a snapshot and every later _delta
	 * over the same buffer (the bucket arrays must stay aligned). */
	void _snapshot(int x, int y, int w, int h, unsigned *out,
	               int stride = SAMPLE_STRIDE)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0;
		for (int yy = y; yy < y + h && i < (unsigned)MAX_SAMPLES; yy += stride) {
			if (yy < 0 || yy >= (int)SCREEN_H)
				continue;
			for (int xx = x; xx < x + w && i < (unsigned)MAX_SAMPLES; xx += stride) {
				if (xx < 0 || xx >= (int)SCREEN_W)
					continue;
				out[i++] = _bucket(px[yy * SCREEN_W + xx]);
			}
		}
	}

	/* Count of stride-grid points changed vs a prior snapshot.
	 * Same screen clamping and stride as _snapshot (the two MUST visit
	 * the same sample sequence or the bucket arrays desynchronize). */
	unsigned _delta(int x, int y, int w, int h, unsigned const *base,
	                int stride = SAMPLE_STRIDE)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0, changed = 0;
		for (int yy = y; yy < y + h && i < (unsigned)MAX_SAMPLES; yy += stride) {
			if (yy < 0 || yy >= (int)SCREEN_H)
				continue;
			for (int xx = x; xx < x + w && i < (unsigned)MAX_SAMPLES; xx += stride) {
				if (xx < 0 || xx >= (int)SCREEN_W)
					continue;
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
	 * paint). Placement check (c): with exp_ox/exp_oy at their
	 * defaults the window_layout rect CENTER must sit inside WIN_REG
	 * (the class placement check for the 320x240-scale windows); an
	 * expected origin switches to an ORIGIN check, for self-resized
	 * windows whose frames exceed WIN_REG but whose placement still
	 * comes from the pkg_runtime assign rule.
	 */
	bool _poll_first_paint(char const *class_prefix, char const *app_token,
	                       int &rx, int &ry, int &rw, int &rh,
	                       unsigned max_iters = PAINT_ITERS_DEFAULT,
	                       int exp_ox = -1, int exp_oy = -1)
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
				bool placed = false;
				if (exp_ox >= 0) {
					placed = rx >= exp_ox - 4 && rx <= exp_ox + 4 &&
					         ry >= exp_oy - 4 && ry <= exp_oy + 4;
				} else {
					int const cx = rx + rw / 2, cy = ry + rh / 2;
					placed =
						cx >= WIN_REG_X && cx < WIN_REG_X + WIN_REG_W &&
						cy >= WIN_REG_Y && cy < WIN_REG_Y + WIN_REG_H;
				}
				if (!placed) {
					if (exp_ox >= 0)
						_fail("window not placed at the pkg_runtime assign "
						      "origin (x=", rx, " y=", ry, " w=", rw,
						      " h=", rh, " expected origin ", exp_ox, ",",
						      exp_oy, ")");
					else
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
	unsigned _poll_typed_delta(int x, int y, int w, int h, unsigned baseline,
	                           unsigned max_iters = 300,
	                           int stride = SAMPLE_STRIDE)
	{
		for (unsigned i = 0; i < max_iters; ++i) {
			_timer.msleep(200);
			unsigned changed = _delta(x, y, w, h, _snap, stride);
			if (i % 25 == 0)
				Genode::log("grouped-ui-probe: typed-delta poll ", i,
				            " changed=", changed, " baseline=", baseline);
			if (changed >= TYPED_FLOOR && changed > 2 * baseline)
				return changed;
		}
		return 0;
	}

	/* ---- P5.1 app-matrix helpers ---- */

	unsigned _nonbg_pct_rect(int x, int y, int w, int h)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned total = 0, nonbg = 0;
		for (int yy = y; yy < y + h; yy += SAMPLE_STRIDE)
			for (int xx = x; xx < x + w; xx += SAMPLE_STRIDE) {
				if (xx < 0 || yy < 0 || xx >= (int)SCREEN_W || yy >= (int)SCREEN_H)
					continue;
				++total;
				if (!_pixel_is_bg(px[yy * SCREEN_W + xx]))
					++nonbg;
			}
		return total ? (100 * nonbg) / total : 0;
	}

	/*
	 * Coarse render map of a rect — the red-first calibration
	 * instrument: a missed interaction point is corrected from the
	 * rendered layout it prints, not guessed at. The 8-level encoding
	 * separates the Qt default-palette button gray (~0xd4) from the
	 * window white (~0xff) and the dark title bar.
	 */
	void _diag_render(int x, int y, int w, int h)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		int const cols = 32, rows = 24;
		Genode::log("grouped-ui-probe: diag render (", x, ",", y, " ", w,
		            "x", h, ")");
		for (int r = 0; r < rows; ++r) {
			char line[40] { };
			for (int c = 0; c < cols && c < (int)sizeof(line) - 1; ++c) {
				int const sx = x + (c * w) / cols;
				int const sy = y + (r * h) / rows;
				if (sx < 0 || sy < 0 || sx >= (int)SCREEN_W || sy >= (int)SCREEN_H) {
					line[c] = '?';
					continue;
				}
				Pixel const &p = px[sy * SCREEN_W + sx];
				unsigned const lum =
					((unsigned)p.r() + p.g() + p.b()) / 3;
				line[c] = lum < 0x20 ? '.' : lum < 0x50 ? ':'
				      : lum < 0x80 ? '+' : lum < 0xa0 ? '*'
				      : lum < 0xc0 ? '/' : lum < 0xe0 ? '=' : '#';
			}
			Genode::log("grouped-ui-probe: diag render |",
			            (char const *)line, "|");
		}
	}

	/* Changed-sample bounding box vs `base' — logged after every new
	 * input interaction so each acceptance run re-verifies the pinned
	 * geometry (the P2.6 url-bar band convention). */
	void _log_delta_bbox(int x, int y, int w, int h, unsigned const *base,
	                     char const *what, int stride = SAMPLE_STRIDE)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
		unsigned i = 0, changed = 0;
		for (int yy = y; yy < y + h && i < (unsigned)MAX_SAMPLES; yy += stride) {
			if (yy < 0 || yy >= (int)SCREEN_H)
				continue;
			for (int xx = x; xx < x + w && i < (unsigned)MAX_SAMPLES; xx += stride) {
				if (xx < 0 || xx >= (int)SCREEN_W)
					continue;
				if (_bucket(px[yy * SCREEN_W + xx]) != base[i]) {
					++changed;
					if (xx < x0) x0 = xx;
					if (yy < y0) y0 = yy;
					if (xx > x1) x1 = xx;
					if (yy > y1) y1 = yy;
				}
				++i;
			}
		}
		if (x1 < 0)
			Genode::log("grouped-ui-probe: diag ", what, " bbox=<none>");
		else
			Genode::log("grouped-ui-probe: diag ", what, " bbox=(", x0, ",",
			            y0, " ", x1 - x0 + SAMPLE_STRIDE, "x",
			            y1 - y0 + SAMPLE_STRIDE, ") changed=", changed);
	}

	/*
	 * The documented already-running receipt (pkgd Gate 2): the
	 * launcher_result report is
	 * <result status="..." op="launch" pkg="..."/> (sponge_pkgd
	 * _report_launch_ok; launcher_controller.cc matches that root).
	 */
	bool _poll_launch_result(char const *pkg, char const *want_status,
	                         unsigned iters)
	{
		for (unsigned i = 0; i < iters; ++i) {
			_timer.msleep(200);
			_launch_result.update();
			if (_launch_result.valid() && _launch_result.size() > 0) {
				Genode::Node const r = _launch_result.node();
				if (r.has_type("result")) {
					Genode::String<32> op =
						r.attribute_value("op", Genode::String<32>());
					Genode::String<40> p =
						r.attribute_value("pkg", Genode::String<40>());
					Genode::String<32> st =
						r.attribute_value("status", Genode::String<32>());
					if (i % 25 == 0)
						Genode::log("grouped-ui-probe: launch-result poll ",
						            i, " op=", op.string(), " pkg=",
						            p.string(), " status=", st.string());
					if (op == Genode::String<32>("launch") &&
					    p == Genode::String<40>(pkg) &&
					    st == Genode::String<32>(want_status))
						return true;
				}
			}
		}
		return false;
	}

	/*
	 * The document the product media SERVES to pdf_view (the same ROM
	 * route the app's VFS <rom name="sample.pdf"/> takes). R17/R18
	 * showed mupdf reporting "cannot find startxref" on product media
	 * while the lean scenario parses the same file fine — this receipt
	 * pins whether the served bytes are the document at all.
	 */
	void _check_sample_pdf()
	{
		_sample_pdf.update();
		if (!_sample_pdf.valid() || _sample_pdf.size() == 0)
			_fail("sample.pdf ROM did not resolve (empty/invalid)");
		Genode::size_t const size = _sample_pdf.size();
		char const *data = _sample_pdf.local_addr<char>();
		bool head_ok = size >= 5
		            && data[0] == '%' && data[1] == 'P'
		            && data[2] == 'D' && data[3] == 'F';
		bool xref_ok = false;
		for (Genode::size_t i = 0; i + 9 <= size; ++i)
			if (Genode::strcmp(data + i, "startxref", 9) == 0)
				xref_ok = true;
		/* "/Type /Page " (trailing space) matches the page objects but
		 * not the "/Type /Pages" catalog node. */
		unsigned pages = 0;
		for (Genode::size_t i = 0; i + 12 <= size; ++i)
			if (Genode::strcmp(data + i, "/Type /Page ", 12) == 0)
				++pages;
		Genode::log("grouped-ui-probe: pdf-doc (size=", size,
		            " head=", head_ok ? "%PDF" : "??",
		            " startxref=", xref_ok ? "yes" : "no",
		            " pages=", pages, ")");
		if (!head_ok || !xref_ok) {
			Genode::log("grouped-ui-probe: pdf-doc head bytes: ",
			            (unsigned)(unsigned char)data[0], " ",
			            (unsigned)(unsigned char)data[1], " ",
			            (unsigned)(unsigned char)data[2], " ",
			            (unsigned)(unsigned char)data[3]);
			_fail("served sample.pdf is not the document (head/startxref)");
		}
		if (pages < 2)
			_fail("served sample.pdf has ", pages,
			      " pages (page-down cannot produce an input delta)");
	}

	/* files structural navigation receipt: <files path="..."
	 * entries="N"><last_action name="navigate" result="ok"/></files>. */
	bool _poll_files_path(char const *want_path, unsigned iters)
	{
		for (unsigned i = 0; i < iters; ++i) {
			_timer.msleep(200);
			_files_report.update();
			if (_files_report.valid() && _files_report.size() > 0) {
				Genode::Node const r = _files_report.node();
				if (r.has_type("files")) {
					Genode::String<64> path =
						r.attribute_value("path", Genode::String<64>());
					bool ok = false;
					r.with_optional_sub_node("last_action",
						[&] (Genode::Node const &a) {
							ok =
								a.attribute_value("name",
									Genode::String<32>()) ==
										Genode::String<32>("navigate") &&
								a.attribute_value("result",
									Genode::String<32>()) ==
										Genode::String<32>("ok");
						});
					if (i % 25 == 0)
						Genode::log("grouped-ui-probe: files-report poll ",
						            i, " path=", path.string());
					if (ok && path == Genode::String<64>(want_path))
						return true;
				}
			}
		}
		return false;
	}

	/*
	 * App-window census over the class prefixes: how many package
	 * windows are mapped right now, from how many runtime classes.
	 * `skip_browser` drops pkg_runtime_browser labels (the P5.1
	 * concurrent set excludes falkon BY DESIGN: it is torn down
	 * before the census and its receipt is the browser leg; debug
	 * is likewise torn down and simply absent). Labels are
	 * collected so a failure names the exact set.
	 */
	unsigned _count_app_windows(unsigned &classes, unsigned *ids,
	                            unsigned max_ids, bool skip_browser = false)
	{
		_window_list.update();
		unsigned n = 0;
		classes = 0;
		bool simple = false, terminal = false, browser = false;
		if (_window_list.valid() && _window_list.size() > 0) {
			Genode::Node const root = _window_list.node();
			root.for_each_sub_node("window", [&] (Genode::Node const &w) {
				Genode::String<160> label =
					w.attribute_value("label", Genode::String<160>());
				char const *l = label.string();
				bool const s = str_prefix(l, "pkg_runtime_simple -> ");
				bool const t = str_prefix(l, "pkg_runtime_terminal -> ");
				bool const b = str_prefix(l, "pkg_runtime_browser -> ");
				if (!s && !t && !b)
					return;
				if (skip_browser && b)
					return;
				if (s) simple = true;
				if (t) terminal = true;
				if (b) browser = true;
				if (ids && n < max_ids)
					ids[n] = w.attribute_value("id", 0u);
				++n;
			});
		}
		classes = (unsigned)simple + (unsigned)terminal + (unsigned)browser;
		return n;
	}

	/* Popup open + real Qt menu-entry click + popup close — the shared
	 * launch half of the P5.1 legs (steps (1)+(2) of _run_class,
	 * unchanged in the existing legs). */
	bool _open_and_click_entry(char const *what, App_entry const &entry)
	{
		_snapshot(DOMAIN_X, DOMAIN_Y, DOMAIN_W, DOMAIN_H, _snap);
		_emit_click(S_TOGGLE_X, S_TOGGLE_Y);
		if (!_poll_popup_open()) {
			_fail("launcher popup never opened (", what, ")");
			return false;
		}
		if (!_wait_entry_bands()) {
			_fail("launcher buttons never rendered for ",
			      (char const *)entry.name);
			return false;
		}
		if (!_diag_entry_bands(entry.name, _entry_click_y(entry)))
			return false;
		_emit_click(_entry_click_x(), _entry_click_y(entry));
		if (!_poll_popup_closed()) {
			_fail("launcher popup never closed (", what, ")");
			return false;
		}
		return true;
	}

	/*
	 * hello: GUI-free autostart (pkg/hello/metadata.xml <autostart/>).
	 * The real launcher entry click is answered `already-running`
	 * (pkgd Gate 2 — hello runs headless since install), which is the
	 * documented result; paint/input are N/A BY DESIGN and recorded as
	 * such so no green label hides an unmeasured input.
	 */
	void _run_hello_leg(App_entry const &entry)
	{
		if (!_open_and_click_entry("hello-class", entry))
			return;
		if (!_poll_launch_result("hello", "already-running", 150)) {
			_log_window_list();
			_fail("hello launcher entry click never answered "
			      "already-running");
			return;
		}
		Genode::log("grouped-ui-probe: hello launcher entry click -> "
		            "already-running (paint/input N/A by design)");
	}

	/*
	 * debug: the SPONGE DEBUG monitor autostarts at install (its
	 * metadata documents the chicken-and-egg guard), so the window and
	 * the FIRSTPAINT self-report exist since boot. The leg proves the
	 * real launcher entry (also answered already-running), Capture
	 * evidence that the window renders, and a Mark-click rendered
	 * delta (plus the app's own "sponge_debug: MARK 1" log, gated by
	 * the run script).
	 */
	void _run_debug_leg(App_entry const &entry)
	{
		if (!_open_and_click_entry("debug-class", entry))
			return;
		if (!_poll_launch_result("debug", "already-running", 150)) {
			_log_window_list();
			_fail("debug launcher entry click never answered "
			      "already-running");
			return;
		}
		Genode::log("grouped-ui-probe: debug launcher entry click -> "
		            "already-running");

		unsigned const id =
			_find_window("pkg_runtime_simple -> ", "debug");
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!id || !_window_rect(id, rx, ry, rw, rh)) {
			_log_window_list();
			_fail("debug window not registered/placed for capture");
			return;
		}
		unsigned const nonbg = _nonbg_pct_rect(rx, ry, rw, rh);
		Genode::log("grouped-ui-probe: debug-class capture "
		            "(label=pkg_runtime_simple -> debug rect=(", rx, ",",
		            ry, " ", rw, "x", rh, ") nonbg=", nonbg, "%)");
		if (nonbg < 10) {
			_diag_render(rx, ry, rw, rh);
			_fail("debug window shows no rendered content (nonbg=",
			      nonbg, "%)");
			return;
		}

		/*
		 * Mark click: a candidate column at x fixed on Mark's button.
		 * The first candidate that renders a delta is the receipt.
		 * Clicks after the win land on the status label (harmless),
		 * except the FINAL click which re-hits the winning point:
		 * its "sponge_debug: MARK" log is the only serial line that
		 * lands after the last QMP-TARGET marker and therefore the
		 * only one the run-script gate window can see (lines between
		 * marker dispatches are consumed by the dispatch expects).
		 * The dispatch count stays fixed at 6.
		 */
		int const mx = rx + DEBUG_MARK_DX;
		Genode::log("grouped-ui-probe: debug-class mark geometry (click_x=",
		            mx, " candidates=6)");
		_diag_render(rx, ry, rw, rh);
		unsigned best = 0, best_y = 0;
		for (unsigned c = 0; c < 6; ++c) {
			bool const last = (c == 5);
			int my = ry + 45;
			if (!best)
				my = ry + DEBUG_MARK_DY[c];
			else if (last)
				my = (int)best_y;
			_snapshot(rx, ry, rw, rh, _snap);
			_emit_click(mx, my);
			unsigned const changed =
				_poll_typed_delta(rx, ry, rw, rh, 0, 15);
			if (changed && !best) {
				best = changed;
				best_y = ry + DEBUG_MARK_DY[c];
				_log_delta_bbox(rx, ry, rw, rh, _snap, "mark-click");
				Genode::log("grouped-ui-probe: debug-class mark candidate ",
				            c, " y=", best_y, " changed=", changed);
			}
		}
		if (!best) {
			_diag_render(rx, ry, rw, rh);
			_fail("no Mark-click rendered delta (debug-class, 6 candidates)");
			return;
		}
		Genode::log("grouped-ui-probe: debug-class mark-click delta "
		            "(changed=", best, " click_y=", best_y, ")");
	}

	/*
	 * files: first paint + a REAL QMP double-click on the first list
	 * row ("demo/") navigating into it. Two QMP-TARGET tablet markers
	 * back-to-back = one Qt double-click (the host dispatches them
	 * consecutively inside Qt's double-click interval). Navigation is
	 * asserted structurally (the files report path "/" -> "/demo", the
	 * app's "sponge_files: navigate -> /demo" log is gated by the run
	 * script) AND as the rendered navigation delta.
	 */
	void _run_files_leg(App_entry const &entry)
	{
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!_open_and_click_entry("files-class", entry))
			return;
		/*
		 * Origin placement check: the files window self-resizes to its
		 * 800x600 geometry (files_window.cc setGeometry), so the frame
		 * exceeds WIN_REG — the class binding is the label prefix and
		 * the assign-rule ORIGIN (50,328).
		 */
		if (!_poll_first_paint("pkg_runtime_simple -> ", "files",
		                       rx, ry, rw, rh, PAINT_ITERS_DEFAULT,
		                       50, 328)) {
			_fail("no first paint for files in pkg_runtime_simple "
			      "(files-class)");
			return;
		}
		Genode::log("grouped-ui-probe: files-class first paint "
		            "(label=pkg_runtime_simple -> files rect=(", rx, ",",
		            ry, " ", rw, "x", rh, "))");

		unsigned const id = _find_window("pkg_runtime_simple -> ", "files");
		if (!id) {
			_fail("window vanished before double-click (files-class)");
			return;
		}
		/*
		 * Double-click candidates: three rows at the list's reserved
		 * left strip (FILES_CLICK_DX — the app guarantees the first
		 * row is hit there at any window size). Two back-to-back
		 * QMP-TARGET tablet markers = one Qt double-click. The first
		 * candidate that navigates is the receipt; post-win
		 * double-clicks land on the path label in the top bar
		 * (harmless — never on Copy/Delete) so the run-script
		 * dispatch count stays fixed at 6.
		 */
		int const dx = rx + FILES_CLICK_DX;
		unsigned best = 0, best_c = 0;
		/* Let the initial paint settle first: R8's candidate-0
		 * baseline measured 1000 changed samples of still-settling
		 * render noise and drowned the navigation delta. */
		_timer.msleep(3000);
		for (unsigned c = 0; c < 3; ++c) {
			int const dy = best ? ry + 30 : ry + FILES_ROW_DY[c];
			_snapshot(rx, ry, rw, rh, _snap);
			_timer.msleep(200);
			unsigned const baseline = _delta(rx, ry, rw, rh, _snap);
			_snapshot(rx, ry, rw, rh, _snap);
			_emit_click(dx, dy);
			_emit_click(dx, dy);
			unsigned const changed =
				_poll_typed_delta(rx, ry, rw, rh, baseline, 30);
			if (changed && !best) {
				best = changed;
				best_c = c;
				_log_delta_bbox(rx, ry, rw, rh, _snap,
				                "files-double-click");
				Genode::log("grouped-ui-probe: files-class double-click "
				            "candidate ", c, " y=", ry + FILES_ROW_DY[c],
				            " changed=", changed);
			}
		}
		if (!best) {
			_diag_render(rx, ry, rw, rh);
			_fail("no double-click navigation/render delta (files-class, "
			      "3 candidates)");
			return;
		}
		if (!_poll_files_path("/demo", 150)) {
			_log_window_list();
			_fail("files navigation never reached /demo (files report)");
			return;
		}
		Genode::log("grouped-ui-probe: files-class navigation delta "
		            "(changed=", best, " candidate=", best_c,
		            " path=/demo)");
	}

	/*
	 * pdf_view: first paint + page-down/scroll input. The input is the
	 * app's OWN physical-key bindings: app/pdf_view main.cc maps
	 * KEY_PAGEDOWN -> pdfapp '.' (page flip) and KEY_DOWN -> pdfapp
	 * 'j' (scroll) via `ev.key_press`, so QMP-TARGET key pgdn/down
	 * reach the handler as keycodes — unlike ASCII glyphs, whose
	 * chargen path lands in the codepoint handler instead. The
	 * product arm stages a two-page sample.pdf (the pdf-doc receipt
	 * asserts pages>=2) because page-down is a no-op on a one-page
	 * document (pdfapp.c clamps pageno to pagecount); scroll alone
	 * renders no delta on this port (Pdf_view::show applies no pan),
	 * so the assertion is the input delta over the pair with each
	 * input's contribution logged separately.
	 */
	void _run_pdf_leg(App_entry const &entry)
	{
		_check_sample_pdf();
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!_open_and_click_entry("pdf-view-class", entry))
			return;
		/*
		 * Origin placement check: pdf_view (raw Gui) comes up at its
		 * 512x512 default (main.cc _gui_window fallback) — the frame
		 * exceeds WIN_REG and the center check cannot hold; the class
		 * binding is the label prefix and the assign-rule ORIGIN.
		 */
		if (!_poll_first_paint("pkg_runtime_simple -> ", "pdf_view",
		                       rx, ry, rw, rh, PAINT_ITERS_DEFAULT,
		                       50, 328)) {
			_fail("no first paint for pdf_view in pkg_runtime_simple "
			      "(pdf-view-class)");
			return;
		}
		Genode::log("grouped-ui-probe: pdf-view-class first paint "
		            "(label=pkg_runtime_simple -> pdf_view rect=(", rx,
		            ",", ry, " ", rw, "x", rh, "))");

		unsigned const id =
			_find_window("pkg_runtime_simple -> ", "pdf_view");
		if (!id) {
			_fail("window vanished before focus (pdf-view-class)");
			return;
		}
		int const fcx = rx + rw / 2, fcy = ry + rh / 2;
		_snapshot(rx, ry, rw, rh, _snap);
		_emit_click(fcx, fcy);
		if (!_poll_focus(id)) {
			_fail("target window did not take focus (pdf-view-class)");
			return;
		}
		_timer.msleep(1000);

		/*
		 * page-down/scroll input, delivered as the app's own
		 * physical-key bindings (main.cc: KEY_PAGEDOWN -> pdfapp '.'
		 * page flip, KEY_DOWN -> pdfapp 'j' scroll). Both inputs are
		 * dispatched and MEASURED; the assertion is the input delta
		 * over the pair, with each input's contribution logged
		 * separately (never hidden).
		 */
		_snapshot(rx, ry, rw, rh, _snap_b);
		_timer.msleep(700);
		unsigned const baseline = _delta(rx, ry, rw, rh, _snap_b);

		_snapshot(rx, ry, rw, rh, _snap);
		_emit_key("pgdn");
		unsigned pgdn_changed = 0;
		for (unsigned i = 0; i < PDF_PGDN_POLL_ITERS; ++i) {
			_timer.msleep(200);
			pgdn_changed = _delta(rx, ry, rw, rh, _snap);
			if (pgdn_changed >= TYPED_FLOOR && pgdn_changed > 2 * baseline)
				break;
		}

		/* scroll contribution, measured against the post-page-down
		 * state so it stays independently visible */
		_snapshot(rx, ry, rw, rh, _snap_b);
		_emit_key("down");
		unsigned scroll_changed = 0;
		for (unsigned i = 0; i < 25; ++i) {
			_timer.msleep(200);
			scroll_changed = _delta(rx, ry, rw, rh, _snap_b);
			if (scroll_changed >= TYPED_FLOOR)
				break;
		}

		unsigned const changed = _delta(rx, ry, rw, rh, _snap);
		_log_delta_bbox(rx, ry, rw, rh, _snap, "pdf-input");
		if (changed < TYPED_FLOOR || changed <= 2 * baseline) {
			_diag_render(rx, ry, rw, rh);
			_fail("no page-down/scroll input delta (pdf-view-class "
			      "pgdn_changed=", pgdn_changed,
			      " scroll_changed=", scroll_changed, ")");
			return;
		}
		Genode::log("grouped-ui-probe: pdf-view-class page-down/scroll "
		            "input delta (changed=", changed, " baseline=",
		            baseline, " pgdn_changed=", pgdn_changed,
		            " scroll_changed=", scroll_changed, ")");
	}

	/*
	 * calculator: first paint + unconditional QMP digits/spin on
	 * inputSpinBox1 with the RESULT delta (the outputWidget band).
	 * Unconditional by contract — the calculator is its own leg, not
	 * the simple-leg fallback.
	 */
	void _run_calc_leg(App_entry const &entry)
	{
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!_open_and_click_entry("calculator-class", entry))
			return;
		if (!_poll_first_paint("pkg_runtime_simple -> ", "calculator",
		                       rx, ry, rw, rh)) {
			_fail("no first paint for calculator in pkg_runtime_simple "
			      "(calculator-class)");
			return;
		}
		Genode::log("grouped-ui-probe: calculator-class first paint "
		            "(label=pkg_runtime_simple -> calculator rect=(", rx,
		            ",", ry, " ", rw, "x", rh, "))");

		unsigned const id =
			_find_window("pkg_runtime_simple -> ", "calculator");
		if (!id) {
			_fail("window vanished before focus (calculator-class)");
			return;
		}
		int const fcx = rx + CALC_SPIN_DX, fcy = ry + CALC_SPIN_DY;
		int const bx = rx + CALC_BAND_DX, by = ry + CALC_BAND_DY;
		Genode::log("grouped-ui-probe: calculator-class spin geometry "
		            "(click=", fcx, ",", fcy, " band=(", bx, ",", by, " ",
		            CALC_BAND_DW, "x", CALC_BAND_DH, "))");
		_snapshot(rx, ry, rw, rh, _snap);
		_emit_click(fcx, fcy);
		if (!_poll_focus(id)) {
			_fail("target window did not take focus (calculator-class)");
			return;
		}
		_timer.msleep(1000);

		_snapshot(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap_b,
		          CALC_BAND_STRIDE);
		_timer.msleep(700);
		unsigned const baseline =
			_delta(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap_b,
			       CALC_BAND_STRIDE);
		_snapshot(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap,
		          CALC_BAND_STRIDE);
		_emit_type("17");
		unsigned const digit_changed =
			_poll_typed_delta(bx, by, CALC_BAND_DW, CALC_BAND_DH, baseline,
			                  300, CALC_BAND_STRIDE);
		_log_delta_bbox(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap,
		                "calc-digits", CALC_BAND_STRIDE);
		if (!digit_changed) {
			_diag_render(rx, ry, rw, rh);
			_fail("no digits result delta (calculator-class)");
			return;
		}
		Genode::log("grouped-ui-probe: calculator-class digits result "
		            "delta (changed=", digit_changed, " baseline=",
		            baseline, " typed=\"17\")");

		_snapshot(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap_b,
		          CALC_BAND_STRIDE);
		_timer.msleep(700);
		unsigned const spin_baseline =
			_delta(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap_b,
			       CALC_BAND_STRIDE);
		_snapshot(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap,
		          CALC_BAND_STRIDE);
		_emit_key("up");
		unsigned const spin_changed =
			_poll_typed_delta(bx, by, CALC_BAND_DW, CALC_BAND_DH,
			                  spin_baseline, 300, CALC_BAND_STRIDE);
		_log_delta_bbox(bx, by, CALC_BAND_DW, CALC_BAND_DH, _snap,
		                "calc-spin", CALC_BAND_STRIDE);
		if (!spin_changed) {
			_diag_render(rx, ry, rw, rh);
			_fail("no spin result delta (calculator-class)");
			return;
		}
		Genode::log("grouped-ui-probe: calculator-class spin result "
		            "delta (changed=", spin_changed, " baseline=",
		            spin_baseline, ")");
	}

	/*
	 * Browser closure settle (R5/R24): falkon's WebEngine closure
	 * keeps resolving (lib attach + QtWebEngineProcess spawn) after
	 * its typed delta, and R5 proved a child creation inside that
	 * window dies in core's CNode construction. Smallest observable
	 * settle: poll st_browser's child set and require it unchanged
	 * for SETTLE_STABLE_POLLS consecutive polls, bounded by
	 * SETTLE_MAX_POLLS. Logs one marker; never a fixed sleep (the
	 * wait must SHRINK once the closure is done).
	 */
	void _run_browser_settle()
	{
		enum { SETTLE_POLL_MS = 1000, SETTLE_STABLE_POLLS = 3,
		       SETTLE_MAX_POLLS = 30 };

		unsigned stable = 0, polls = 0, last_count = 0;
		bool first = true;

		while (stable < SETTLE_STABLE_POLLS && polls < SETTLE_MAX_POLLS) {
			_st_browser.update();
			unsigned count = 0;
			if (_st_browser.valid() && _st_browser.size() > 0) {
				_st_browser.node().for_each_sub_node("child",
					[&] (Genode::Node const &) { ++count; });
			}
			if (!first && count == last_count)
				++stable;
			else
				stable = 0;
			last_count = count;
			first = false;
			++polls;
			_timer.msleep(SETTLE_POLL_MS);
		}

		Genode::log("grouped-ui-probe: browser settle children=",
		            last_count, " polls=", polls, " stable=", stable);
	}

	/*
	 * P5.1 package teardown — the sanctioned pkgd calls from this
	 * probe (every launch above went through a real launcher entry
	 * click; this is teardown, never a launch substitute). pkgd's
	 * no-stop-until-remove contract drops the <start> node and frees
	 * the PD + closure: falkon after the browser leg (its ~509 MiB
	 * WebEngine closure starved core's CNode construction during the
	 * R29 files spawn), debug before the calculator spawn (R30:
	 * debug's Qt closure resident made the calculator's own mapping
	 * die in core's CNode construction).
	 *
	 * Gates before the marker: pkgd answers status=ok; the package
	 * window leaves window_list (window-gone); the class state report
	 * loses its live child named <pkg> (child absent/exited).
	 */
	void _teardown_pkg(char const *pkg, char const *class_prefix,
	                   Genode::Attached_rom_dataspace &state_rom,
	                   char const *state_name)
	{
		Genode::String<32> status { };
		bool answered = false;
		for (unsigned attempt = 0; attempt < 3 && !answered; ++attempt) {
			if (_pkgd.request("remove", pkg)) {
				status = _pkgd.result_xml().attribute_value(
					"status", Genode::String<32>());
				answered = true;
			} else {
				Genode::log("grouped-ui-probe: remove ", pkg,
				            " attempt ", attempt + 1, " unanswered");
				_timer.msleep(500);
			}
		}
		if (!answered) {
			_fail("pkgd never answered remove ", pkg,
			      " (probe_request channel)");
			return;
		}
		if (status != Genode::String<32>("ok")) {
			_fail("pkgd remove ", pkg, " status=", status);
			return;
		}

		unsigned polls = 0;
		bool window_gone = false, child_gone = false;
		for (; polls < 120; ++polls) {
			window_gone = _find_window(class_prefix, pkg) == 0;
			state_rom.update();
			unsigned live = 0;
			if (state_rom.valid() && state_rom.size() > 0)
				state_rom.node().for_each_sub_node("child",
					[&] (Genode::Node const &c) {
						Genode::String<48> const name =
							c.attribute_value("name",
							                  Genode::String<48>());
						if (Genode::strcmp(name.string(), pkg) == 0 &&
						    !c.has_attribute("exited"))
							++live;
					});
			child_gone = live == 0;
			if (window_gone && child_gone)
				break;
			_timer.msleep(500);
		}
		if (!window_gone || !child_gone) {
			_log_class_children(state_name, state_rom);
			_log_window_list();
			_fail(pkg, " teardown incomplete (window-gone=",
			      (int)window_gone, " child-gone=", (int)child_gone, ")");
			return;
		}
		Genode::log("grouped-ui-probe: ", pkg,
		            " removed (status=ok window-gone=1 child-gone=1 polls=",
		            polls, ")");
	}

	/*
	 * Final assertion (P5.1): >=5 non-falkon app windows from >=2
	 * runtime classes mapped SIMULTANEOUSLY, with the last typed
	 * delta delivered while the siblings remain mapped. falkon and
	 * debug are excluded BY DESIGN: both were torn down above (their
	 * PD/closure residency killed the R29/R30 spawns), and each
	 * keeps its own first-paint + input receipt. The surviving set
	 * is exactly five windows — textedit, terminal, files,
	 * pdf_view, calculator. The typed delta goes to the textedit
	 * window ABOVE the sibling stack (rx+rw/2, ry+100 — every
	 * stacked sibling starts at y=328 and reaches wider than
	 * textedit's center column, so the only textedit-only point is
	 * above the stack), measured over the matching clear band
	 * (y < 328: no sibling pixels, no sibling blink noise).
	 */
	void _run_concurrent()
	{
		unsigned classes = 0;
		unsigned ids_before[MAX_APPS] { };
		unsigned const n_before =
			_count_app_windows(classes, ids_before, MAX_APPS, true);
		Genode::log("grouped-ui-probe: concurrent-set poll n=", n_before,
		            " classes=", classes);
		if (n_before < CONCURRENT_MIN_WINDOWS ||
		    classes < CONCURRENT_MIN_CLASSES) {
			_log_window_list();
			_fail("concurrent window set too small (n=", n_before,
			      " classes=", classes, ")");
			return;
		}

		unsigned const id =
			_find_window("pkg_runtime_simple -> ", "textedit");
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!id || !_window_rect(id, rx, ry, rw, rh)) {
			_log_window_list();
			_fail("textedit window lost before the concurrent typed delta");
			return;
		}

		/*
		 * Sibling-stack clear band: the stacked siblings all sit at
		 * y=328 (the generic pkg_runtime assign origin + panel), so
		 * the textedit rows above it are the only region no sibling
		 * covers — click and measure there.
		 */
		int const cx = rx + rw / 2, cy = ry + 100;
		int const bx = rx, by = ry, bw = rw, bh = 328 - ry;
		_snapshot(bx, by, bw, bh, _snap);
		_emit_click(cx, cy);
		if (!_poll_focus(id)) {
			_fail("textedit did not take focus before the concurrent typed delta");
			return;
		}
		_timer.msleep(5000);
		unsigned const click_changed = _delta(bx, by, bw, bh, _snap);
		Genode::log("grouped-ui-probe: concurrent click_changed=",
		            click_changed);

		_snapshot(bx, by, bw, bh, _snap_b);
		_timer.msleep(700);
		unsigned const baseline = _delta(bx, by, bw, bh, _snap_b);
		_snapshot(bx, by, bw, bh, _snap);
		_emit_type("sponge");
		unsigned const changed =
			_poll_typed_delta(bx, by, bw, bh, baseline);
		if (!changed) {
			_diag_render(rx, ry, rw, rh);
			_fail("no concurrent typed delta (textedit, click_changed=",
			      click_changed, ")");
			return;
		}
		Genode::log("grouped-ui-probe: concurrent typed delta (changed=",
		            changed, " baseline=", baseline, ")");

		unsigned classes_after = 0;
		unsigned ids_after[MAX_APPS] { };
		unsigned const n_after =
			_count_app_windows(classes_after, ids_after, MAX_APPS, true);
		for (unsigned i = 0; i < n_before && i < MAX_APPS; ++i) {
			bool held = false;
			for (unsigned j = 0; j < n_after && j < MAX_APPS; ++j)
				if (ids_before[i] == ids_after[j])
					held = true;
			if (!held) {
				_log_window_list();
				_fail("sibling window id=", ids_before[i],
				      " vanished during the final typed delta");
				return;
			}
		}
		Genode::log("grouped-ui-probe: concurrent siblings held n=",
		            n_after);
		Genode::log("grouped-ui-probe: concurrent windows n=", n_before);
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
		 *     snapshot MUST be taken before the type marker leaves.
		 *     The post-click settle runs long enough to exhaust the
		 *     decorator/Qt repaint tail: R10 flaked when a ~17-sample
		 *     settling transient landed inside the baseline noise
		 *     window and pushed the 2x rule past the typed signal
		 *     (changed=30 baseline=17). */
		if (url_bar) {
			/*
			 * Browser leg: a FIXED 5 s settle puts the type
			 * dispatch inside falkon's WebEngine init storm (the
			 * 512MB->1GB AllocPages + completer-model teardowns
			 * land ~2-7 s after the click) and the keystrokes are
			 * lost — R25/26/27/28 all red this way (typed polls at
			 * band-noise level 3..9), while R5's type beat the
			 * storm and rendered changed=9845. Same intent as R10
			 * (exhaust the click repaint tail BEFORE the baseline
			 * window) but quiet-gated and short so the type lands
			 * first. Bounded: 6x500 ms, exit on first quiet poll;
			 * on cap the leg proceeds and the UNCHANGED typed-delta
			 * assertion remains the arbiter.
			 */
			int const sx = rx, sy = ry + URL_BAND_DY,
			          sw = rw, sh = URL_BAND_DH;
			unsigned noise = 0, polls = 0;
			_snapshot(sx, sy, sw, sh, _snap_b);
			do {
				_timer.msleep(500);
				noise = _delta(sx, sy, sw, sh, _snap_b);
				_snapshot(sx, sy, sw, sh, _snap_b);
				++polls;
			} while (noise > 2 && polls < 6);
			Genode::log("grouped-ui-probe: ", what,
			            " click-settle (noise=", noise, " polls=",
			            polls, ")");
		} else {
			_timer.msleep(5000);
		}

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
		 * P5.1 feed receipts: hello/Utilities (the launcher feed pair
		 * alpha_probe's criterion (b) asserts on product media) and
		 * the debug entry the Mark leg clicks.
		 */
		App_entry const *hello = _find_app("hello");
		if (!hello) {
			_fail("hello missing from the launcher report");
			return;
		}
		if (Genode::strcmp(hello->category, "Utilities") != 0) {
			_fail("hello launcher category is not Utilities (",
			      (char const *)hello->category, ")");
			return;
		}
		Genode::log("grouped-ui-probe: hello/Utilities");
		if (!_entry_visible(*hello)) {
			_fail("hello entry is clipped by the launcher domain");
			return;
		}
		App_entry const *dbg = _find_app("debug");
		if (!dbg || !_entry_visible(*dbg)) {
			_fail("debug entry is clipped by the launcher domain");
			return;
		}
		App_entry const *files = _find_app("files");
		if (!files || !_entry_visible(*files)) {
			_fail("files entry is clipped by the launcher domain");
			return;
		}
		App_entry const *pdf = _find_app("pdf_view");
		if (!pdf || !_entry_visible(*pdf)) {
			_fail("pdf_view entry is clipped by the launcher domain");
			return;
		}
		App_entry const *calc = _find_app("calculator");
		if (!calc || !_entry_visible(*calc)) {
			_fail("calculator entry is clipped by the launcher domain");
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

		/*
		 * P5.1 app matrix, in front-most / child-creation order: the
		 * two autostart packages first (their windows are the only
		 * ones up, so the debug Mark click lands on debug's window),
		 * then the simple/terminal class legs, then the browser leg
		 * at position 5 (after terminal, before files — R5's proven
		 * green shape), then a browser-settle gate + falkon teardown,
		 * then the launched simple-app legs. R24 proved the browser-LAST order is
		 * fatal: with six Qt/mupdf apps resident, falkon's own
		 * WebEngine closure load dies in rom_pkg (core CNode
		 * construction failure; the ROM create for
		 * libQt6WebEngineCore.lib.so never answers — docs/evidence/
		 * phase16-p51-grouped-app-matrix.md). R25/R26 proved the
		 * browser-THIRD order misroutes the calibrated URL-bar
		 * click/type even on a quiet band (click_changed=11265 /
		 * 10018 vs R5's 990, typed delta at noise level). The
		 * settle waits for the browser runtime's child set to
		 * stabilize before any later child spawn (R5's collision
		 * class: creating a files child while falkon's closure is
		 * still resolving dies in core's CNode construction). Then
		 * falkon is TORN DOWN (pkgd remove — R29 proved even a
		 * settled falkon starves the files spawn's CNode budget),
		 * and the remaining legs spawn into the freed budget. debug
		 * is torn down too, right before the calculator spawn (R30:
		 * the calculator's first mapping died in core's CNode
		 * construction with debug's Qt closure still resident).
		 * Layout safety: debug's entry sits BETWEEN calculator and
		 * files in the Utilities section, so its removal re-lays-out
		 * everything below it — but the files/pdf_view entry clicks
		 * are already banked by then, and the only remaining entry
		 * click (calculator) sits ABOVE debug and does not shift.
		 * The concurrent assertion is last: its typed delta is
		 * delivered into textedit while every surviving sibling
		 * stays mapped.
		 */
		_run_hello_leg(*hello);
		_run_debug_leg(*dbg);

		_run_class("simple-class", "pkg_runtime_simple -> ",
		           simple_name, *simple, simple_typed,
		           simple_focus_dx, simple_focus_dy, simple_extra_key);
		_run_class("terminal-class", "pkg_runtime_terminal -> ",
		           "terminal", *term, "echo grouped-ok", -1, -1, "ret");

		/*
		 * Browser class (P2.6 leg shape, UNCHANGED) — position 5,
		 * R5's proven slot. First paint with the browser poll
		 * budget; basic typed input "sponge" after a URL-bar focus
		 * click. The focus/typing geometry is the runtime
		 * calibration (URL_CLICK_* / URL_BAND_*, evidence run
		 * receipts in
		 * docs/evidence/phase16-p26-browser-acceptance.md): a
		 * client-pixel click on the URL field row, typed-delta
		 * measured over the bar-row/popup band, shared thresholds.
		 * No page-load assertion.
		 */
		_run_class("browser-class", "pkg_runtime_browser -> ",
		           "falkon", *browser, "sponge", 0, 0, nullptr,
		           PAINT_ITERS_BROWSER, true);

		/*
		 * Browser settle (R5/R24): observable child-set stability
		 * gate before the next child spawn.
		 */
		_run_browser_settle();

		/*
		 * P5.1 teardown: destroy falkon (pkgd remove, probe_request
		 * channel) and wait for its window + class child to go away
		 * BEFORE the remaining spawns — its WebEngine closure
		 * residency is what killed the R29 files spawn. The
		 * concurrent set below is non-falkon by design.
		 */
		_teardown_pkg("falkon", "pkg_runtime_browser -> ",
		              _st_browser, "st_browser");

		_run_files_leg(*files);
		_run_pdf_leg(*pdf);

		/*
		 * P5.1 teardown (2): destroy debug before the calculator
		 * spawn — R30 died in core's CNode construction at the
		 * calculator's first mapping with debug's Qt closure still
		 * resident. debug's receipt (first paint + Mark click) is
		 * already banked above. The concurrent set below is then
		 * exactly the five non-falkon windows (textedit, terminal,
		 * files, pdf_view, calculator).
		 */
		_teardown_pkg("debug", "pkg_runtime_simple -> ",
		              _st_simple, "st_simple");

		_run_calc_leg(*calc);

		/*
		 * Final assertion: the concurrent-window set with the last
		 * typed delta delivered while the siblings stay mapped.
		 */
		_run_concurrent();

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
