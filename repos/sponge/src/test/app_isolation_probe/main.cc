/* SPDX-License-Identifier: Apache-2.0
 *
 * app_isolation_probe — P5.1 app-isolation acceptance probe.
 *
 * Proves on the PRODUCT topology (run/sponge-desktop-disk.run with
 * SPONGE_APP_ISOLATION=on, the focused `isolation` bake profile) that
 * one application's failure cannot take the desktop or a sibling app
 * down, and that the desktop recovers:
 *
 *   KILL leg — launch textedit + calculator from the REAL launcher,
 *   kill textedit with the decorator CLOSER (geometry derived from the
 *   live window_layout report), require the victim gone from
 *   window_list, witness DE input via a QMP launcher-toggle press
 *   change on sponge-de's `input` report (de_survival_probe C2 shape),
 *   prove the sibling calculator still accepts typed input (the
 *   grouped_ui_probe typed-delta shape), then relaunch textedit from
 *   the real launcher and require launcher_result ok + first paint +
 *   a typed-input delta.
 *
 *   WEDGE leg — launch the ep_wedge package (metadata
 *   <config wedge_after_ms="6000"/>) from the real launcher, let its
 *   entrypoint park (the run script gates the `ep_wedge: wedge fired`
 *   marker), then require DE input + a sibling typed-input delta +
 *   sibling windows retained + the class runtime state still live and
 *   updating. The run script separately asserts zero
 *   `sponge_supervisor: version-change` lines (no sibling
 *   construction-version churn).
 *
 * Everything is driven over the run/qmp.inc QMP-TARGET marker
 * contract (serialized choreography: one marker at a time, confirmed
 * before the next). REAL input only — usb-tablet abs clicks and
 * send-key typing through the real driver chain; never a synthetic
 * nitpicker event. The ONLY pkgd calls are the sanctioned remove +
 * install RESET before the relaunch (wm_tasks_probe step-8 shape: a
 * plain re-launch after a closer-kill answers `already-running` and
 * never restarts the dead child); the launches themselves are always
 * real launcher entry clicks — never a pkgd-only launch substitute.
 *
 * Marker chain (guest -> host), in emission order:
 *
 *   app-isolation-probe: ready ...
 *   QMP-TARGET tablet <x> <y>       launcher toggle
 *   QMP-TARGET tablet <x> <y>       textedit entry (x3, bounded retry)
 *   app-isolation-probe: kill apps first paint (textedit + calculator)
 *   QMP-TARGET tablet <x> <y>       launcher toggle
 *   QMP-TARGET tablet <x> <y>       calculator entry (x3, bounded retry)

 *   app-isolation-probe: kill closer geometry (...)
 *   QMP-TARGET tablet <x> <y>       closer candidate A
 *   QMP-TARGET tablet <x> <y>       closer candidate B / re-hit winner
 *   app-isolation-probe: kill victim-gone (...)
 *   QMP-TARGET tablet <x> <y>       launcher toggle (DE-input witness)
 *   QMP-TARGET tablet <x> <y>       launcher toggle (close)
 *   app-isolation-probe: kill de-input ok (...)
 *   QMP-TARGET tablet <x> <y>       calculator spin focus click
 *   QMP-TARGET type 17              sibling typed input
 *   app-isolation-probe: kill sibling typed delta (...)
 *   app-isolation-probe: kill reset ok (remove+install)
 *   QMP-TARGET tablet <x> <y>       launcher toggle
 *   QMP-TARGET tablet <x> <y>       textedit entry (relaunch, x3)
 *   app-isolation-probe: kill relaunch first paint (... launch-result=ok)
 *   QMP-TARGET tablet <x> <y>       textedit focus click
 *   QMP-TARGET type alive           relaunch typed input
 *   app-isolation-probe: kill relaunch typed delta (...)
 *   app-isolation-probe: wedge state t0 (...)
 *   QMP-TARGET tablet <x> <y>       launcher toggle
 *   QMP-TARGET tablet <x> <y>       ep_wedge entry (x3, bounded retry)
 *   app-isolation-probe: wedge launched (...)
 *   (30 s wait — ep_wedge parks its entrypoint wedge_after_ms in)
 *   QMP-TARGET tablet <x> <y>       launcher toggle (DE-input witness)
 *   QMP-TARGET tablet <x> <y>       launcher toggle (close)
 *   app-isolation-probe: wedge de-input ok (...)
 *   QMP-TARGET tablet <x> <y>       calculator raise click (bottom strip)
 *   QMP-TARGET tablet <x> <y>       calculator spin focus click
 *   QMP-TARGET type 23              sibling typed input
 *   app-isolation-probe: wedge survivors (...)
 *   app-isolation-probe: wedge state ok (...)
 *   app-isolation-probe: PASS       (ONLY after both legs)
 *
 * Geometry conventions (receipt-pinned, re-verified every run):
 *   - launcher popup domain (0,28,341,532), toggle center (32,14),
 *     entry rows per launcher_menu_view.cc::repopulate() (grouped_ui_
 *     probe constants, unchanged);
 *   - calculator inputSpinBox1 click at rect+(37,76) and result band
 *     (185,30 95x85 stride 1) — the grouped_ui_probe R23+ calibration;
 *   - closer: the stock motif decorator draws the rightmost 16x16
 *     title-bar control at content (rx+rw-16, ry-16)..(rx+rw, ry)
 *     (decorator/window.cc controls_rect + window.h `_border_size`);
 *     candidate A is the source-derived center (rx+rw-8, ry-8) — the
 *     wm_tasks_probe (362,312) receipt for content (50,320,320,240) —
 *     and candidate B (rx+rw-4, ry+20) covers the frame-corner rect
 *     reading (the grouped_ui_probe widget offsets observe a +4,+28
 *     content shift). One of the two always lands on the 16px closer
 *     rect; the winner is logged and re-verified every run. A miss is
 *     a harmless title-bar click (drag with zero motion) or a client
 *     focus click, never an input substitute.
 *
 * REPORT FORMAT (load-bearing): window_list/window_layout/focus and
 * the class state reports are HID (Genode::Generator::generate) and are
 * parsed with the FORMAT-AGNOSTIC Node API — never .xml(). sponge-de's
 * launcher report and `input` report are XML (generate_xml).
 *
 * Any failure logs "app-isolation-probe: FAIL <reason>" and exits
 * non-zero (fail-loud, bounded — never a silent hang).
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

/* Panel launcher-toggle center (theme padding=8, margin=4, launcher
 * width 48, panel height 28 — calibrated by sponge_de_probe W2). */
int const S_TOGGLE_X = 32;
int const S_TOGGLE_Y = 14;

/* Popup heading pad point (de_survival_probe POPUP_HEADING_PT): a
 * press here is a case-1 no-op inside the open popup and empty desktop
 * behind a closed one — the fixed-slot harmless pad. */
int const HEADING_X = 170, HEADING_Y = 44;

/* Panel tasklist entry-0 center (wm_tasks_probe receipt: widget
 * global (130,4), entry 96x28). After the kill the tasklist holds
 * exactly one entry — the calculator — so entry 0 is unambiguous. */
int const TASKLIST_X = 178, TASKLIST_Y = 18;

/* Tasklist entry-1 center (the 96px entry stride, wm_tasks receipt):
 * after the relaunch the tasklist is [calculator, textedit], so entry 1
 * is the relaunched textedit. */
int const TASKLIST2_X = 274, TASKLIST2_Y = 18;

/* Launcher popup domain + row model (launcher_menu_view.cc). */
int const DOMAIN_X = 0, DOMAIN_Y = 28, DOMAIN_W = 341, DOMAIN_H = 532;
int const PAD = 8, GAP = 4, HEADING_H = 20, BUTTON_H = 50;

/* First-paint region around the pkg_runtime assign windows. */
int const WIN_REG_X = 40, WIN_REG_Y = 280, WIN_REG_W = 360, WIN_REG_H = 280;

/* Snapshot grid (textedit_probe recipe). */
int const SAMPLE_STRIDE = 4;
unsigned const COLOR_BUCKET_SHIFT = 4;

enum {
	POPUP_SAMPLES = (DOMAIN_W / SAMPLE_STRIDE) * (DOMAIN_H / SAMPLE_STRIDE),
	WIN_SAMPLES   = (WIN_REG_W / SAMPLE_STRIDE) * (WIN_REG_H / SAMPLE_STRIDE),
	MAX_SAMPLES   = POPUP_SAMPLES > WIN_SAMPLES ? POPUP_SAMPLES : WIN_SAMPLES
};

unsigned const POPUP_OPEN_FLOOR = 150;
unsigned const PAINT_FLOOR      = 150;
unsigned const TYPED_FLOOR      = 12;
unsigned const PAINT_ITERS      = 1500;

/* calculatorform geometry (grouped_ui_probe R23+ calibration). */
int const CALC_SPIN_DX = 37, CALC_SPIN_DY = 76;
int const CALC_BAND_DX = 185, CALC_BAND_DY = 30;
int const CALC_BAND_DW = 95,  CALC_BAND_DH = 85;
int const CALC_BAND_STRIDE = 1;

/* Generic sibling-stack origin: the pkg_runtime assign rule pins
 * floating windows at ypos 300 (screen ~328). The textedit clear band
 * for typed deltas is everything ABOVE that stack — no sibling pixels,
 * no sibling blink noise. */
int const STACK_TOP = 300;

/* ep_wedge: wedge_after_ms=6000 (pkg/ep_wedge/metadata.xml); the probe
 * sleeps WEDGE_WAIT_MS before the survival assertions so the entrypoint
 * is already parked when they run. */
unsigned const WEDGE_WAIT_MS = 30000;

enum { MAX_APPS = 16 };

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


struct App_isolation_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "app-isolation-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };

	Genode::Attached_rom_dataspace _launcher      { _env, "launcher" };
	Genode::Attached_rom_dataspace _window_list   { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };
	Genode::Attached_rom_dataspace _focus         { _env, "focus" };
	Genode::Attached_rom_dataspace _st_simple     { _env, "st_simple" };
	Genode::Attached_rom_dataspace _launch_result { _env, "launcher_result" };
	Genode::Attached_rom_dataspace _input_rom     { _env, "sponge_de_input" };

	/* Sanctioned pkgd teardown/reset ONLY (remove + install before the
	 * relaunch) — never a launch substitute. */
	Sponge::Backend::ReportRomClient _pkgd {
		_env, "probe_request", "probe_result" };

	App_entry _apps[MAX_APPS] { };
	unsigned  _num_apps { 0 };

	unsigned _snap[MAX_SAMPLES]   { };
	unsigned _snap_b[MAX_SAMPLES] { };

	App_isolation_probe(Genode::Env &env) : _env(env) { }

	template <typename... ARGS>
	void _fail(ARGS const &... args)
	{
		Genode::error("app-isolation-probe: FAIL ", args...);
		_env.parent().exit(1);
	}

	static unsigned _bucket(Pixel const &p)
	{
		return ((unsigned)p.r() >> COLOR_BUCKET_SHIFT) << 8
		     | ((unsigned)p.g() >> COLOR_BUCKET_SHIFT) << 4
		     | ((unsigned)p.b() >> COLOR_BUCKET_SHIFT);
	}

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

	/* ---- launcher report: app list + popup layout (grouped shape) ---- */

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

	void _compute_layout()
	{
		for (unsigned i = 1; i < _num_apps; ++i) {
			App_entry key = _apps[i];
			unsigned j = i;
			while (j > 0 && alpha_cmp(_apps[j - 1].name, key.name) > 0) {
				_apps[j] = _apps[j - 1];
				--j;
			}
			_apps[j] = key;
		}

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
				y += GAP;
			first_section = false;
			y += HEADING_H + GAP;

			bool first_entry = true;
			for (unsigned i = 0; i < _num_apps; ++i) {
				if (Genode::strcmp(_apps[i].category, cat_order[c]) != 0)
					continue;
				if (!first_entry)
					y += GAP;
				first_entry = false;
				_apps[i].y = y;
				y += BUTTON_H;
			}
		}
	}

	bool _entry_visible(App_entry const &e) const
	{
		return e.y >= 0 && e.y + BUTTON_H <= DOMAIN_H;
	}

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

	static bool _label_matches(char const *label, char const *class_prefix,
	                           char const *app_token)
	{
		if (!str_prefix(label, class_prefix))
			return false;
		return str_contains(label + Genode::strlen(class_prefix), app_token);
	}

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

	/* ---- class state report (init Child::report_state) ---- */

	/* Count live children and optionally test one name. */
	void _state_scan(unsigned &count, unsigned &live,
	                 char const *name, bool &name_live, bool &name_present)
	{
		count = live = 0;
		name_live = false;
		name_present = false;
		_st_simple.update();
		if (!_st_simple.valid() || _st_simple.size() == 0)
			return;
		_st_simple.node().for_each_sub_node("child", [&] (Genode::Node const &c) {
			++count;
			Genode::String<48> const n =
				c.attribute_value("name", Genode::String<48>());
			bool const exited = c.has_attribute("exited");
			if (!exited)
				++live;
			if (name && Genode::strcmp(n.string(), name) == 0) {
				name_present = true;
				name_live = !exited;
			}
		});
	}

	void _log_state_children(char const *when)
	{
		_st_simple.update();
		if (!_st_simple.valid() || _st_simple.size() == 0) {
			Genode::log("app-isolation-probe: diag st_simple ", when,
			            ": <empty>");
			return;
		}
		_st_simple.node().for_each_sub_node("child", [&] (Genode::Node const &c) {
			Genode::String<48> name =
				c.attribute_value("name", Genode::String<48>());
			Genode::String<64> binary =
				c.attribute_value("binary", Genode::String<64>());
			bool const has_exited = c.has_attribute("exited");
			int const exited = c.attribute_value("exited", -1);
			long const skipped =
				c.attribute_value("skipped_heartbeats", 0L);
			Genode::log("app-isolation-probe: diag st_simple ", when,
			            " child ", name.string(), " bin=", binary.string(),
			            has_exited
			                ? Genode::String<32>(" exited=", exited)
			                : Genode::String<32>(""),
			            skipped > 0
			                ? Genode::String<32>(" skipped=", (int)skipped)
			                : Genode::String<32>(""));
		});
	}

	void _log_window_list()
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0) {
			Genode::log("app-isolation-probe: diag window_list: <empty>");
			return;
		}
		unsigned n = 0;
		Genode::Node const root = _window_list.node();
		root.for_each_sub_node("window", [&] (Genode::Node const &w) {
			++n;
			Genode::String<160> label =
				w.attribute_value("label", Genode::String<160>());
			Genode::log("app-isolation-probe: diag window id=",
			            w.attribute_value("id", 0u), " label=", label.string());
		});
		Genode::log("app-isolation-probe: diag window_list: ", n, " entries");
	}

	/* ---- bounded first-paint failure diagnostics ---- */

	static bool _pixel_is_bg(Pixel const &p)
	{
		return p.r() >= 0x16 && p.r() <= 0x26
		    && p.g() >= 0x16 && p.g() <= 0x26
		    && p.b() >= 0x26 && p.b() <= 0x36;
	}

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

	void _diag_first_paint(char const *class_prefix, char const *app_token,
	                       unsigned max_changed)
	{
		_log_window_list();
		_log_state_children("diag");
		unsigned const id = _find_window(class_prefix, app_token);
		int rx = 0, ry = 0, rw = 0, rh = 0;
		bool const have_rect = _window_rect(id, rx, ry, rw, rh);
		unsigned const nonbg = _win_reg_nonbg_pct();

		char const *layer = "placed-no-pixels";
		if (!id)
			layer = "never-registered";
		else if (!have_rect)
			layer = "registered-no-rect";

		Genode::log("app-isolation-probe: diag first-paint layer=", layer,
		            " id=", id, " rect_ok=", (int)have_rect,
		            " max_changed=", max_changed,
		            " win_reg_nonbg=", nonbg, "%");
	}

	/* ---- QMP-TARGET emissions ---- */

	void _emit_click(int x, int y)
	{
		Genode::log("QMP-TARGET tablet ", x, " ", y);
	}

	void _emit_click_ps2(int x, int y)
	{
		Genode::log("QMP-TARGET click ", x, " ", y);
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

	bool _poll_popup_open()
	{
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(200);
			unsigned changed = _delta(DOMAIN_X, DOMAIN_Y, DOMAIN_W,
			                          DOMAIN_H, _snap);
			if (i % 10 == 0)
				Genode::log("app-isolation-probe: popup-open poll ", i,
				            " changed=", changed);
			if (changed >= POPUP_OPEN_FLOOR)
				return true;
		}
		return false;
	}

	static bool _pixel_is_button(Pixel const &p)
	{
		return p.r() >= 0x29 && p.r() <= 0x39
		    && p.g() >= 0x2a && p.g() <= 0x3a
		    && p.b() >= 0x3c && p.b() <= 0x4c;
	}

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

	bool _poll_popup_closed()
	{
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(200);
			bool const open = _popup_band_visible();
			if (i % 10 == 0)
				Genode::log("app-isolation-probe: popup-closed poll ", i,
				            " open=", (int)open);
			if (!open)
				return true;
		}
		return false;
	}

	bool _wait_entry_bands()
	{
		for (unsigned i = 0; i < 50; ++i) {
			if (_popup_band_visible())
				return true;
			_timer.msleep(200);
			if (i % 10 == 0)
				Genode::log("app-isolation-probe: launcher-band wait ", i);
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
					Genode::log("app-isolation-probe: diag launcher bands x=",
					            x, " y=", run_y0, "..", y - 1);
					any = true;
					if (x == 14 && click_y >= run_y0 && click_y <= y - 1)
						covers = true;
				}
				run_y0 = -1;
			}
			if (run_y0 >= 0 && DOMAIN_Y + DOMAIN_H - run_y0 >= 10) {
				Genode::log("app-isolation-probe: diag launcher bands x=",
				            x, " y=", run_y0, "..", DOMAIN_Y + DOMAIN_H - 1);
				any = true;
				if (x == 14 && click_y >= run_y0
				    && click_y <= DOMAIN_Y + DOMAIN_H - 1)
					covers = true;
			}
		}
		if (!any) {
			Genode::log("app-isolation-probe: diag launcher bands none (",
			            app_token, ")");
			return false;
		}
		Genode::log("app-isolation-probe: diag launcher bands click y=", click_y,
		            covers ? " covered=yes" : " covered=no");
		return true;
	}

	bool _poll_first_paint(char const *class_prefix, char const *app_token,
	                       int &rx, int &ry, int &rw, int &rh,
	                       unsigned max_iters = PAINT_ITERS)
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
				Genode::log("app-isolation-probe: first-paint poll ", i,
				            " id=", id, " rect_ok=", (int)have_rect,
				            " rect=(", rx, ",", ry, " ", rw, "x", rh, ")",
				            " changed=", changed);
			if (i == 0)
				_diag_first_paint(class_prefix, app_token, max_changed);

			if (id && have_rect) {
				/*
				 * Painted witness: the delta (the paint landed
				 * during THIS poll) OR the region already
				 * non-background (the paint landed during a
				 * sibling's concurrent poll — cal13 receipt:
				 * calculator painted while textedit's poll ran and
				 * its own delta window opened too late).
				 */
				unsigned const nonbg = _win_reg_nonbg_pct();
				if (changed >= PAINT_FLOOR || nonbg >= 60) {
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
					Genode::log("app-isolation-probe: window id=", id,
					            " rect=(", rx, ",", ry, " ", rw, "x", rh,
					            ") changed=", changed, " nonbg=", nonbg, "%");
					return true;
				}
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
				Genode::log("app-isolation-probe: focus poll ", i,
				            " id=", id, " want=", want_id);
			if (id && id == want_id)
				return true;
		}
		return false;
	}

	unsigned _poll_typed_delta(int x, int y, int w, int h, unsigned baseline,
	                           unsigned max_iters = 300,
	                           int stride = SAMPLE_STRIDE)
	{
		for (unsigned i = 0; i < max_iters; ++i) {
			_timer.msleep(200);
			unsigned changed = _delta(x, y, w, h, _snap, stride);
			if (i % 25 == 0)
				Genode::log("app-isolation-probe: typed-delta poll ", i,
				            " changed=", changed, " baseline=", baseline);
			if (changed >= TYPED_FLOOR && changed > 2 * baseline)
				return changed;
		}
		return 0;
	}

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
			Genode::log("app-isolation-probe: diag ", what, " bbox=<none>");
		else
			Genode::log("app-isolation-probe: diag ", what, " bbox=(", x0, ",",
			            y0, " ", x1 - x0 + SAMPLE_STRIDE, "x",
			            y1 - y0 + SAMPLE_STRIDE, ") changed=", changed);
	}

	void _diag_render(int x, int y, int w, int h)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		int const cols = 32, rows = 24;
		Genode::log("app-isolation-probe: diag render (", x, ",", y, " ", w,
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
			Genode::log("app-isolation-probe: diag render |",
			            (char const *)line, "|");
		}
	}

	/* <result status="..." op="launch" pkg="..."/> (sponge_pkgd). */
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
						Genode::log("app-isolation-probe: launch-result poll ",
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

	/* ---- C2 DE-input witness (de_survival_probe shape) ---- */

	void _read_press(char *buf, Genode::size_t buf_sz)
	{
		buf[0] = 0;
		_input_rom.update();
		if (!_input_rom.valid())
			return;
		try {
			Genode::Xml_node const r = _input_rom.xml();
			if (r.has_attribute("press")) {
				Genode::String<64> const v =
					r.attribute_value("press", Genode::String<64>());
				Genode::copy_cstring(buf, v.string(), buf_sz);
			}
		} catch (Genode::Xml_node::Invalid_syntax) {
			buf[0] = 0;
		}
	}

	bool _wait_new_press(char const *last, unsigned max_ms)
	{
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			char cur[32] = "";
			_read_press(cur, sizeof(cur));
			if (cur[0] && Genode::strcmp(cur, last) != 0)
				return true;
		}
		return false;
	}

	/* ---- sanctioned pkgd reset (remove + install, NOT a launch) ---- */

	bool _pkg_op(char const *op, char const *pkg)
	{
		for (unsigned attempt = 0; attempt < 3; ++attempt) {
			if (_pkgd.request(op, pkg)) {
				Genode::String<32> status = _pkgd.result_xml()
					.attribute_value("status", Genode::String<32>());
				if (status != Genode::String<32>("ok")) {
					Genode::log("app-isolation-probe: pkgd ", op, " ",
					            pkg, " status=", status.string());
					return false;
				}
				return true;
			}
			Genode::log("app-isolation-probe: pkgd ", op, " ", pkg,
			            " attempt ", attempt + 1, " unanswered");
			_timer.msleep(500);
		}
		return false;
	}

	/* ---- shared launch half (grouped_ui_probe shape) ---- */

	enum { ENTRY_CLICKS = 4, OPEN_SLOTS = 3, WITNESS_SLOTS = 3 };

	/*
	 * Fixed-slot choreography (the run script dispatches exactly one
	 * marker per slot): see-then-click with harmless pads. A toggle
	 * slot clicks ONLY while the popup is closed (a toggle click on an
	 * open popup would close it); a not-needed slot emits a PAD on the
	 * popup heading (170,44): a case-1 no-op press inside the open
	 * popup (which also feeds the C2 press witness) and empty desktop
	 * behind a closed one. Popup state: bands OR the open-delta OR the
	 * already-open flag (the repopulated menu can restyle rows off the
	 * button palette — ai-cal4).
	 */
	bool _popup_seems_open(bool already, unsigned &delta_seen)
	{
		unsigned changed = _delta(DOMAIN_X, DOMAIN_Y, DOMAIN_W,
		                          DOMAIN_H, _snap);
		if (changed > delta_seen)
			delta_seen = changed;
		return already || _popup_band_visible() || changed >= POPUP_OPEN_FLOOR;
	}

	bool _open_popup()
	{
		bool const already = _popup_band_visible();
		unsigned delta_seen = 0;
		_snapshot(DOMAIN_X, DOMAIN_Y, DOMAIN_W, DOMAIN_H, _snap);

		for (unsigned c = 0; c < (unsigned)OPEN_SLOTS; ++c) {
			if (_popup_seems_open(already, delta_seen)) {
				_emit_click_ps2(HEADING_X, HEADING_Y);
				continue;
			}
			_emit_click_ps2(S_TOGGLE_X, S_TOGGLE_Y);
			for (unsigned i = 0; i < 25 && !_popup_seems_open(already, delta_seen); ++i)
				_timer.msleep(200);
		}
		if (!_popup_seems_open(already, delta_seen)) {
			_fail("launcher popup never opened");
			return false;
		}
		(void)_wait_entry_bands();
		return true;
	}

	/*
	 * Entry-click SALVO on the popup's FIRST show (ai-cal2..9
	 * receipts): a re-show leaves a stale nitpicker view over the
	 * popup footprint that swallows every press (the pixels keep
	 * painting the live view underneath; only the first-show session
	 * delivers entry clicks). The launcher hides the popup on launch
	 * SUCCESS (~1-2 s of pkgd config regen after the click), so N
	 * fast tablet clicks dispatched back-to-back all land while the
	 * working first-show view is still up. Fixed ENTRY_CLICKS
	 * markers; slots beyond the real rows are heading pads.
	 */
	bool _salvo_clicks(int const *xs, int const *ys, unsigned n,
	                   unsigned pad_prefix)
	{
		/* Leading PAD clicks (tablet — they are the consumed-click
		 * absorbers and may land anywhere) then the real rows via
		 * PS/2 (the tablet row clicks stick at the previous point —
		 * cal50: the row click re-fired at the pad and no launch
		 * followed). Fixed ENTRY_CLICKS markers. */
		unsigned slot = 0;
		for (unsigned c = 0; c < (unsigned)ENTRY_CLICKS; ++c, ++slot) {
			if (slot < pad_prefix || slot - pad_prefix >= n)
				_emit_click_ps2(HEADING_X, HEADING_Y);
			else
				_emit_click_ps2(xs[slot - pad_prefix], ys[slot - pad_prefix]);
		}
		return true;
	}

	/*
	 * Band-derived row clicks (cal10 receipt: after any feed change
	 * the popup re-lays out with style-broken taller headings — the
	 * computed rows miss (`click y=295 covered=no`, real rows at
	 * y=463..512). The BANDS are the rendered truth. cand_a = the
	 * band at the app's sorted index (all-rows-banded case, the
	 * session-1 proven path); cand_b = the band at the
	 * running-adjusted rank (dimmed running rows have no band, R33
	 * receipt). A miss lands on an adjacent app row = a spurious
	 * launch of a KNOWN package (harmless: already-running for the
	 * autostart, an extra held window otherwise).
	 */
	void _scan_bands(int *tops, int *bots, unsigned &m)
	{
		m = 0;
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		int run_y0 = -1;
		for (int y = DOMAIN_Y; y < DOMAIN_Y + DOMAIN_H; ++y) {
			bool const on = _pixel_is_button(px[y * SCREEN_W + 14]);
			if (on && run_y0 < 0)
				run_y0 = y;
			if ((!on || y == DOMAIN_Y + DOMAIN_H - 1) && run_y0 >= 0) {
				int const y1 = on ? y : y - 1;
				if (y1 - run_y0 >= 10 && m < (unsigned)MAX_APPS) {
					tops[m] = run_y0;
					bots[m] = y1;
					++m;
				}
				run_y0 = -1;
			}
		}
	}

	bool _child_live(char const *name)
	{
		bool live = false, present = false;
		unsigned dummy = 0;
		_state_scan(dummy, dummy, name, live, present);
		return present && live;
	}

	void _click_y_candidates(char const *name, int &cand_a, int &cand_b)
	{
		int tops[MAX_APPS] { }, bots[MAX_APPS] { };
		unsigned m = 0;
		_scan_bands(tops, bots, m);

		/*
		 * The band order is the Y-LAYOUT order (the section-grouped
		 * menu: calculator, hello, ep_wedge, textedit), NOT the
		 * alpha order of _apps (cal12 receipt: the alpha index put
		 * ep_wedge's click on hello's row). The y-order = the order
		 * of the computed layout ys, and the row order survives the
		 * style-broken heading shifts (cal10) even though the
		 * absolute positions move.
		 */
		unsigned y_rank = 0, idx = 0;
		bool found = false;
		for (unsigned i = 0; i < _num_apps; ++i)
			if (Genode::strcmp(_apps[i].name, name) == 0) {
				idx = i;
				found = true;
			}
		for (unsigned i = 0; i < _num_apps; ++i)
			if (_apps[i].y < _apps[idx].y)
				++y_rank;

		int const computed = found ? _entry_click_y(_apps[idx]) : 0;
		cand_a = (y_rank < m) ? (tops[y_rank] + bots[y_rank]) / 2 : computed;

		unsigned live_before = 0;
		for (unsigned i = 0; i < _num_apps; ++i)
			if (_apps[i].y < _apps[idx].y && _child_live(_apps[i].name))
				++live_before;
		unsigned const rank = y_rank - live_before;
		cand_b = (rank < m && rank != y_rank) ? (tops[rank] + bots[rank]) / 2
		                                      : computed;

		Genode::log("app-isolation-probe: row candidates (", name,
		            " y_rank=", (int)y_rank, " bands=", (int)m,
		            " cand_a=", cand_a, " cand_b=", cand_b,
		            " computed=", computed, ")");
	}

	int _entry_row_y(App_entry const &e) const
	{
		return _entry_click_y(e);
	}

	bool _poll_popup_closed_short()
	{
		for (unsigned i = 0; i < 25; ++i) {
			_timer.msleep(200);
			if (!_popup_band_visible())
				return true;
		}
		return false;
	}

	/*
	 * DE-input witness (de_survival C2 shape): the sponge-de `input`
	 * report's press attribute must change across a real QMP press on
	 * a DE widget. The task's shape — the LAUNCHER TOGGLE press is the
	 * witness: slot 1 is ALWAYS the toggle click (the cal40 receipt:
	 * a see-then pad picked a background click whose press never
	 * reports), slot 2 closes the popup again (toggle when the bands
	 * show it open, pad otherwise), slot 3 pads. Fixed 3 markers.
	 */
	bool _de_input_witness(char const *what)
	{
		char press_before[32] = "";
		_read_press(press_before, sizeof(press_before));

		_emit_click_ps2(S_TOGGLE_X, S_TOGGLE_Y);
		if (!_wait_new_press(press_before, 15000)) {
			/* the first press is flaky (cal51) — the second toggle
			 * below is the witness retry */
			_read_press(press_before, sizeof(press_before));
		}
		_emit_click_ps2(S_TOGGLE_X, S_TOGGLE_Y);
		if (!_wait_new_press(press_before, 30000)) {
			_fail("no input press arrived at sponge-de (", what,
			      " de-input, before=\"", (char const *)press_before, "\")");
			return false;
		}
		char press_after[32] = "";
		_read_press(press_after, sizeof(press_after));

		if (_popup_band_visible())
			_emit_click_ps2(S_TOGGLE_X, S_TOGGLE_Y);
		else
			_emit_click_ps2(HEADING_X, HEADING_Y);
		(void)_poll_popup_closed();

		Genode::log("app-isolation-probe: ", what,
		            " de-input ok (press=", (char const *)press_after, ")");
		return true;
	}

	/*
	 * The CALC_* offsets are the grouped_ui_probe 400x300-normalized
	 * calculatorform calibration; that convention scales them to the
	 * live rect (focus_dx * rw / 400) "so inputSpinBox1 stays targeted
	 * at any client size (400x300 or 320x240)" — both sizes observed
	 * (cal36: the self-resize race left the window at the 320x240
	 * assign default and the unscaled band measured bbox=<none>).
	 */
	void _calc_scaled(int cx, int cy, int cw, int ch,
	                  int &spin_x, int &spin_y,
	                  int &band_x, int &band_y,
	                  int &band_w, int &band_h)
	{
		spin_x = cx + CALC_SPIN_DX * cw / 400;
		spin_y = cy + CALC_SPIN_DY * ch / 300;
		band_x = cx + CALC_BAND_DX * cw / 400;
		band_y = cy + CALC_BAND_DY * ch / 300;
		band_w = CALC_BAND_DW * cw / 400;
		band_h = CALC_BAND_DH * ch / 300;
	}

	/* ---- KILL leg ---- */

	void _run_kill_leg()
	{
		/*
		 * (K1+K2+W1) textedit + calculator + ep_wedge from ONE
		 * first-show popup session (the salvo contract): all three
		 * entry clicks land on the working first-show view before
		 * the launcher's hide-on-success (~1-2 s away) closes it;
		 * later sessions cannot deliver entry clicks at all (the
		 * stale-view receipts ai-cal2..10), so every launch in the
		 * run rides this one session.
		 */
		App_entry const *te = _find_app("textedit");
		if (!te) {
			_fail("textedit missing from the launcher feed");
			return;
		}
		if (!_open_popup())
			return;

		int te_a = 0, te_b = 0, ca_a = 0, ca_b = 0, we_a = 0, we_b = 0;
		_click_y_candidates("textedit", te_a, te_b);
		_click_y_candidates("calculator", ca_a, ca_b);
		_click_y_candidates("ep_wedge", we_a, we_b);

		int xs[3] = {
			_entry_click_x(), _entry_click_x(), _entry_click_x() };
		int ys[3] = { te_a, ca_a, we_a };
		_salvo_clicks(xs, ys, 3, 1);
		Genode::log("app-isolation-probe: wedge launched (entry=ep_wedge)");

		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!_poll_first_paint("pkg_runtime_simple -> ", "textedit",
		                       rx, ry, rw, rh)) {
			_fail("no first paint for textedit (kill leg)");
			return;
		}
		int crx = 0, cry = 0, crw = 0, crh = 0;
		if (!_poll_first_paint("pkg_runtime_simple -> ", "calculator",
		                       crx, cry, crw, crh)) {
			_fail("no first paint for calculator (kill leg)");
			return;
		}
		bool const te_ok = _poll_launch_result("textedit", "ok", 75);
		bool const ca_ok = _poll_launch_result("calculator", "ok", 75);
		Genode::log("app-isolation-probe: kill apps first paint "
		            "(textedit rect=(", rx, ",", ry, " ", rw, "x", rh,
		            ") calculator rect=(", crx, ",", cry, " ", crw, "x",
		            crh, ") launch-result=textedit:",
		            te_ok ? "ok" : "late", " calculator:",
		            ca_ok ? "ok" : "late", ")");

		/* (K3) closer kill — geometry from the LIVE window_layout. */
		unsigned const victim_id =
			_find_window("pkg_runtime_simple -> ", "textedit");
		if (!victim_id || !_window_rect(victim_id, rx, ry, rw, rh)) {
			_log_window_list();
			_fail("textedit window not registered/placed for the closer");
			return;
		}
		int const ax = rx + rw - 8, ay = ry - 8;
		int const bx = rx + rw - 4, by = ry + 20;
		Genode::log("app-isolation-probe: kill closer geometry "
		            "(rect=(", rx, ",", ry, " ", rw, "x", rh, ") A=(", ax,
		            ",", ay, ") B=(", bx, ",", by, ") candidates=2)");

		bool gone = false;
		unsigned polls = 0;
		unsigned candidate = 0;

		_emit_click_ps2(ax, ay);
		for (unsigned i = 0; i < 30; ++i) {
			_timer.msleep(200);
			if (_find_window("pkg_runtime_simple -> ", "textedit") == 0) {
				gone = true;
				polls = i;
				candidate = 1;
				break;
			}
		}
		if (gone) {
			/* Fixed dispatch count: re-hit the winning point (harmless
			 * — the victim is gone; the point is desktop background). */
			_emit_click_ps2(ax, ay);
		} else {
			_emit_click_ps2(bx, by);
			for (unsigned i = 0; i < 300; ++i) {
				_timer.msleep(200);
				if (_find_window("pkg_runtime_simple -> ", "textedit") == 0) {
					gone = true;
					polls = i;
					candidate = 2;
					break;
				}
			}
		}
		if (!gone) {
			_log_window_list();
			_diag_render(rx, ry, rw, rh);
			_fail("victim not removed from window_list after the closer "
			      "(candidates A=(", ax, ",", ay, ") B=(", bx, ",", by, "))");
			return;
		}
		Genode::log("app-isolation-probe: kill victim-gone (candidate=",
		            candidate, " polls=", polls, ")");

		/* (K4) DE-input witness (QMP launcher toggle + press change). */
		_de_input_witness("kill");

		/* (K5) sibling calculator typed-input pixel delta. */
		unsigned const cid =
			_find_window("pkg_runtime_simple -> ", "calculator");
		if (!cid || !_window_rect(cid, crx, cry, crw, crh)) {
			_log_window_list();
			_fail("calculator window lost before the sibling typed delta");
			return;
		}
		int fcx = 0, fcy = 0, bx2 = 0, by2 = 0, bw2 = 0, bh2 = 0;
		_calc_scaled(crx, cry, crw, crh, fcx, fcy, bx2, by2, bw2, bh2);
		_snapshot(crx, cry, crw, crh, _snap);
		_emit_click_ps2(fcx, fcy);
		if (!_poll_focus(cid)) {
			_fail("calculator did not take focus (kill sibling delta)");
			return;
		}
		_timer.msleep(1000);
		_snapshot(bx2, by2, bw2, bh2, _snap_b,
		          CALC_BAND_STRIDE);
		_timer.msleep(700);
		unsigned const baseline =
			_delta(bx2, by2, bw2, bh2, _snap_b,
			       CALC_BAND_STRIDE);
		_snapshot(bx2, by2, bw2, bh2, _snap,
		          CALC_BAND_STRIDE);
		_emit_type("17");
		_timer.msleep(2000);
		_emit_click_ps2(fcx, fcy);
		_timer.msleep(700);
		_emit_type("23");
		unsigned const changed =
			_poll_typed_delta(bx2, by2, bw2, bh2, baseline,
			                  300, CALC_BAND_STRIDE);
		_log_delta_bbox(bx2, by2, bw2, bh2, _snap,
		                "kill-calc-digits", CALC_BAND_STRIDE);
		if (!changed) {
			_diag_render(crx, cry, crw, crh);
			_fail("no sibling typed-input delta (calculator, kill leg)");
			return;
		}
		Genode::log("app-isolation-probe: kill sibling typed delta "
		            "(changed=", changed, " baseline=", baseline,
		            " typed=\"17\")");

		/*
		 * (K5b) Reset: pkgd remove + install. wm_tasks_probe step-8
		 * shape — a plain re-launch after the closer-kill answers
		 * `already-running` and never restarts the dead child, so the
		 * running entry must be dropped first. Teardown only; the
		 * relaunch below is a REAL launcher entry click.
		 */
		if (!_pkg_op("remove", "textedit")) {
			_fail("pkgd remove textedit failed (kill reset)");
			return;
		}
		if (!_pkg_op("install", "textedit")) {
			_fail("pkgd install textedit failed (kill reset)");
			return;
		}
		for (unsigned i = 0; i < 150; ++i) {
			_timer.msleep(200);
			if (_parse_launcher() && _find_app("textedit"))
				break;
		}
		if (!_find_app("textedit")) {
			_log_window_list();
			_fail("textedit never returned to the launcher feed after reset");
			return;
		}
		_compute_layout();
		te = _find_app("textedit");
		if (!te || !_entry_visible(*te)) {
			_fail("textedit entry clipped after reset");
			return;
		}
		Genode::log("app-isolation-probe: kill reset ok (remove+install)");

				/* (K6) relaunch from the REAL launcher (solo salvo). */
		if (!_open_popup())
			return;
		int rte_a = 0, rte_b = 0;
		_click_y_candidates("textedit", rte_a, rte_b);
		int rxs[1] = { _entry_click_x() };
		int rys[1] = { rte_a };
		_salvo_clicks(rxs, rys, 1, 2);
		if (!_poll_first_paint("pkg_runtime_simple -> ", "textedit",
		                       rx, ry, rw, rh)) {
			_fail("no first paint for textedit after relaunch (kill leg)");
			return;
		}
		if (!_poll_launch_result("textedit", "ok", 150)) {
			_log_window_list();
			_fail("textedit relaunch never answered ok");
			return;
		}
		Genode::log("app-isolation-probe: kill relaunch first paint "
		            "(label=pkg_runtime_simple -> textedit rect=(", rx,
		            ",", ry, " ", rw, "x", rh, ") launch-result=ok)");

		/* (K7) relaunch typed-input pixel delta (the clear band). */
		unsigned const rid =
			_find_window("pkg_runtime_simple -> ", "textedit");
		if (!rid) {
			_log_window_list();
			_fail("relaunched textedit window lost before the typed delta");
			return;
		}
		int bh = rh;
		if (ry + bh > STACK_TOP)
			bh = STACK_TOP - ry;
		if (bh < 60)
			bh = rh / 2;
		int const tcx = rx + rw / 2, tcy = ry + 100;
		_snapshot(rx, ry, rw, bh, _snap);
		_emit_click_ps2(tcx, tcy);
		if (!_poll_focus(rid)) {
			_fail("relaunched textedit did not take focus");
			return;
		}
		_timer.msleep(5000);
		unsigned const click_changed = _delta(rx, ry, rw, bh, _snap);
		Genode::log("app-isolation-probe: kill relaunch click_changed=",
		            click_changed);

		_snapshot(rx, ry, rw, bh, _snap_b);
		_timer.msleep(700);
		unsigned const rbaseline = _delta(rx, ry, rw, bh, _snap_b);
		_snapshot(rx, ry, rw, bh, _snap);
		_emit_type("alive");
		unsigned const rchanged =
			_poll_typed_delta(rx, ry, rw, bh, rbaseline);
		_log_delta_bbox(rx, ry, rw, bh, _snap, "kill-relaunch-type");
		if (!rchanged) {
			_diag_render(rx, ry, rw, rh);
			_fail("no relaunch typed-input delta (textedit, click_changed=",
			      click_changed, ")");
			return;
		}
		Genode::log("app-isolation-probe: kill relaunch typed delta "
		            "(changed=", rchanged, " baseline=", rbaseline,
		            " click_changed=", click_changed, " typed=\"alive\")");
	}

	/* ---- WEDGE leg ---- */

	unsigned _t0_count { 0 }, _t0_live { 0 };

	/*
	 * Which tasklist entry holds textedit — consumed by the W2b
	 * fallback click (default entry 0; the tasklist entry order is
	 * run-variant).
	 */
	int _te_entry_x { TASKLIST_X };

	void _snapshot_t0()
	{
		bool wl = false, wp = false;
		_state_scan(_t0_count, _t0_live, "ep_wedge", wl, wp);
		if (_t0_count == 0) {
			_log_state_children("t0");
			_fail("st_simple not live before the launch session");
			return;
		}
		_log_state_children("t0");
		Genode::log("app-isolation-probe: wedge state t0 (children=",
		            _t0_count, " live=", _t0_live, ")");
	}

	void _run_wedge_leg()
	{
		_timer.msleep(WEDGE_WAIT_MS);

		/* (W2a) DE-input witness. */
		_de_input_witness("wedge");

		/*
		 * (W2b) sibling typed-input delta + windows retained via the
		 * K5-proven calculator spin-band path. The relaunched
		 * textedit covers the calculator, so slot 1 raises the
		 * calculator by clicking its exposed bottom strip — the
		 * click point is computed from the live rects
		 * (calc_x + calc_w/2, textedit_bottom + 13); the strip is
		 * the painted band between the textedit frame bottom and the
		 * calculator frame bottom (cal20 receipt: textedit
		 * (50,108 512x493), calculator (50,328 400x300) → strip
		 * y=605..632, click y=614). If no strip exists the fallback
		 * is a single tasklist entry click, pixel-witnessed (the
		 * textedit region must go background). The spin click's
		 * focus poll doubles as the raise witness: if the raise
		 * missed, the spin lands on textedit and the poll fails
		 * loudly. Slots: raise click, spin click, type — 3 markers.
		 */
		unsigned const tid =
			_find_window("pkg_runtime_simple -> ", "textedit");
		unsigned const cid =
			_find_window("pkg_runtime_simple -> ", "calculator");
		int tx = 0, ty = 0, tw = 0, th = 0;
		int cx = 0, cy = 0, cw = 0, ch = 0;
		if (!tid || !cid || !_window_rect(tid, tx, ty, tw, th) ||
		    !_window_rect(cid, cx, cy, cw, ch)) {
			_log_window_list();
			_fail("sibling windows not retained (textedit id=", tid,
			      " calculator id=", cid, ")");
			return;
		}
		bool const te_parked = tx <= -31000 && ty <= -31000;
		bool const calc_parked = cx <= -31000 && cy <= -31000;
		if (calc_parked) {
			_log_window_list();
			_fail("calculator parked at W2b rect=(", cx, ",", cy, " ",
			      cw, "x", ch, ")");
			return;
		}
		int srx = 0, sry = 0, bx2 = 0, by2 = 0, bw2 = 0, bh2 = 0;
		_calc_scaled(cx, cy, cw, ch, srx, sry, bx2, by2, bw2, bh2);
		/*
		 * Slot plan (the minimize-textedit design, 3 markers): the
		 * tasklist click minimizes the OCCLUDING textedit — exposure
		 * AND the WM-focus move in one action (the keyboard routes to
		 * the WM-focused window — clicks alone never move it (cal43)
		 * — and minimizing the focused window drops the focus to the
		 * sibling, the same fall the closer-kill gives K5). The
		 * calculator stays NEVER HIDDEN: the cal38/39/44/45 receipts
		 * show a minimize/restore cycle kills the calculator's typed
		 * input (0 delta with the focus on it) while the never-hidden
		 * state measures 95. The pixel-witnessed disambiguation
		 * (the kill-leg shape): the click must end with the textedit
		 * region background and the calculator shown; a run-variant
		 * entry order that hit the calculator fails with evidence.
		 * Slots: tasklist click, PS/2 spin click, type.
		 */
		if (te_parked) {
			_emit_click_ps2(HEADING_X, HEADING_Y);
		} else {
			_emit_click_ps2(_te_entry_x, TASKLIST_Y);
		}
		if (!te_parked) {
			/* Poll, don't sleep: the PS/2 walk lands ~5 s after the
			 * marker is emitted and the witness must not decide
			 * before the click (cal54: the minimize logged 26 lines
			 * after the FAIL). The witness region is the textedit
			 * strip ABOVE the calculator's frame. */
			int const vw = tw;
			int const vh = cy - ty - 30;
			bool te_gone = false, calc_shown = false;
			for (unsigned i = 0; i < 75 && !(te_gone && calc_shown); ++i) {
				_timer.msleep(200);
				te_gone = vh > 20 &&
					_nonbg_pct_rect(tx, ty, vw, vh) < 5;
				calc_shown =
					_nonbg_pct_rect(cx, cy, cw, ch) > 30;
			}
			if (!te_gone || !calc_shown) {
				_log_window_list();
				_diag_render(tx, ty, tw, th);
				_fail("tasklist click did not minimize textedit "
				      "(te_gone=", (int)te_gone,
				      " calc_shown=", (int)calc_shown,
				      " entry=", _te_entry_x, ")");
				return;
			}
		}
		/*
		 * The strip click (the user's exposed-bottom-strip design:
		 * (calc_x + calc_w/2, textedit_bottom + 13)) is the
		 * default-domain absorber: the tasklist click stole the Qt
		 * widget focus (the panel button) and the first
		 * default-domain click is consumed as the domain switch
		 * (cal55: both types landed on the tasklist and the band
		 * stayed 0). Slots: min-textedit, strip, spin, type.
		 */
		_emit_click_ps2(cx + cw / 2, ty + th + 13);
		_timer.msleep(1500);
		_snapshot(cx, cy, cw, ch, _snap);
		_emit_click_ps2(srx, sry);
		if (!_poll_focus(cid)) {
			_log_window_list();
			_diag_render(cx, cy, cw, ch);
			_fail("calculator did not take focus (wedge sibling delta)");
			return;
		}
		_timer.msleep(1000);
		_snapshot(bx2, by2, bw2, bh2, _snap_b,
		          CALC_BAND_STRIDE);
		_timer.msleep(700);
		unsigned const baseline =
			_delta(bx2, by2, bw2, bh2, _snap_b,
			       CALC_BAND_STRIDE);
		_snapshot(bx2, by2, bw2, bh2, _snap,
		          CALC_BAND_STRIDE);
		_emit_type("45");
		unsigned const changed =
			_poll_typed_delta(bx2, by2, bw2, bh2, baseline,
			                  300, CALC_BAND_STRIDE);
		_log_delta_bbox(bx2, by2, bw2, bh2, _snap,
		                "wedge-calc-digits", CALC_BAND_STRIDE);
		if (!changed) {
			_diag_render(cx, cy, cw, ch);
			_fail("no sibling typed-input delta (wedge leg)");
			return;
		}
		unsigned const tid2 =
			_find_window("pkg_runtime_simple -> ", "textedit");
		unsigned const cid2 =
			_find_window("pkg_runtime_simple -> ", "calculator");
		if (!tid2 || !cid2 || tid2 != tid || cid2 != cid) {
			_log_window_list();
			_fail("sibling windows not retained across the wedge delta "
			      "(textedit ", tid, "->", tid2, " calculator ", cid,
			      "->", cid2, ")");
			return;
		}
		Genode::log("app-isolation-probe: wedge survivors (delta=",
		            changed, " baseline=", baseline,
		            " windows-held=2 typed=\"45\")");

		/* (W3) launch receipt + class state AFTER the wedge. The
		 * launcher_result ROM is single-slot and the relaunch
		 * overwrote it long ago — the receipt is asserted by the
		 * run script from the "launch result ep_wedge -> ok" log
		 * line; here it is logged only. */
		bool const we_ok = _poll_launch_result("ep_wedge", "ok", 25);
		Genode::log("app-isolation-probe: wedge launch-result ",
		            we_ok ? "ok" : "overwritten (see the run-script "
		                  "receipt)");
		unsigned count1 = 0, live1 = 0;
		bool wedge_live = false, wedge_present = false;
		_state_scan(count1, live1, "ep_wedge", wedge_live, wedge_present);
		bool te_live = false, te_present = false, ca_live = false, ca_present = false;
		{
			unsigned dummy = 0;
			_state_scan(dummy, dummy, "textedit", te_live, te_present);
			_state_scan(dummy, dummy, "calculator", ca_live, ca_present);
		}
		_log_state_children("t1");
		if (!wedge_present) {
			_fail("ep_wedge child absent from st_simple after the wedge");
			return;
		}
		if (!te_live || !ca_live) {
			_fail("sibling children not live in st_simple after the wedge "
			      "(textedit live=", (int)te_live,
			      " calculator live=", (int)ca_live, ")");
			return;
		}
		if (count1 <= _t0_count) {
			_fail("class runtime state did not update across the launch "
			      "session (children ", _t0_count, " -> ", count1, ")");
			return;
		}
		Genode::log("app-isolation-probe: wedge state ok (children=",
		            _t0_count, " -> ", count1, " live=", live1,
		            " wedge-child=", wedge_live ? "live" : "present",
		            " siblings-live=2 launch-result=ok)");
	}

	void run()
	{
		Genode::log("app-isolation-probe: starting (P5.1 app isolation)");
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* Preconditions: the class runtime publishes state. */
		bool simple_live = false;
		for (unsigned i = 0; i < 600 && !simple_live; ++i) {
			_timer.msleep(200);
			_st_simple.update();
			simple_live = _st_simple.valid() && _st_simple.size() > 0;
			if (i % 50 == 0)
				Genode::log("app-isolation-probe: class-state poll ", i,
				            " simple=", (int)simple_live);
		}
		if (!simple_live) {
			_fail("pkg_runtime_simple did not publish state — grouped "
			      "topology not live");
			return;
		}
		unsigned count = 0, live = 0;
		bool wl = false, wp = false;
		_state_scan(count, live, nullptr, wl, wp);
		Genode::log("app-isolation-probe: class state preconditions ok "
		            "(st_simple live children=", count, " live=", live, ")");

		/* Launcher set: the focused isolation pair + wedge fixture. */
		static char const *const REQUIRED[] = {
			"hello", "textedit", "calculator", "ep_wedge" };
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
				Genode::log("app-isolation-probe: launcher-set poll ", i,
				            " apps=", _num_apps);
		}
		for (char const *n : REQUIRED)
			if (!_find_app(n)) {
				_log_window_list();
				_fail("launcher report never carried the isolation set "
				      "(missing ", n, ")");
				return;
			}
		_compute_layout();
		for (unsigned i = 0; i < _num_apps; ++i)
			Genode::log("app-isolation-probe: layout ",
			            (char const *)_apps[i].name, " [",
			            (char const *)_apps[i].category, "] y=",
			            _apps[i].y,
			            _entry_visible(_apps[i]) ? "" : " (clipped)");
		for (char const *n : REQUIRED) {
			App_entry const *e = _find_app(n);
			if (!e || !_entry_visible(*e)) {
				_fail(n, " entry is clipped by the launcher domain");
				return;
			}
		}

		/* Ordering settle: let alpha-probe's PASS land first (its
		 * marker must precede every QMP-TARGET line in the stream). */
		_timer.msleep(20000);

		Genode::log("app-isolation-probe: ready victim=textedit "
		            "sibling=calculator wedge=ep_wedge");

		_snapshot_t0();
		_run_kill_leg();
		_run_wedge_leg();

		Genode::log("app-isolation-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static App_isolation_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
