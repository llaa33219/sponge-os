/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_qmp_probe — native-WM QMP acceptance battery probe
 * (Criterion 16, M5 gate: docs/plans/phase16-sponge-wm.md §7.1).
 *
 * Scenario: run/sponge-wm-native-qmp.run (base-sel4 + QMP). The NATIVE
 * trio (sponge_wm + sponge_layouter + sponge_decorator) replaces the
 * upstream wm/window_layouter/decorator; the delivered panel tasklist
 * (TasklistController, UNCHANGED) supplies the tasklist restore path.
 *
 * Battery (§7.1, serialized QMP-TARGET markers dispatched by
 * run/qmp.inc's SHARED qmp_exec_target — this scenario defines no local
 * Tcl QMP procs. Content clicks use pointer-ROM-verified closed-loop
 * placement followed by a terminal press):
 *
 *   [step 2]   move — title-bar drag lands at the expected geometry
 *   [steps 3-10] 8-zone resize — all four edges and all four corners,
 *              full sequence in ONE run (the W9 honesty rule: a flaky
 *              zone is an honest partial, never relabeled green)
 *   [step 11]  focus — title and content clicks on each window flip
 *              window_layout's consumed `focused` attribute AND the
 *              window_chrome per-window focus truth: focused=yes
 *              follows exactly the clicked wid while the other client
 *              reads focused=no; content-click selections then remain
 *              stable for 12 consecutive layout/chrome observations
 *   [step 12]  minimize — native chrome minimizer parks the window at
 *              (-32000,-32000) (frozen convention)
 *   [step 13]  restore — tasklist click restores the saved geometry
 *              AND lands focused (U3 focus-after-restore), asserted on
 *              window_layout and window_chrome alike
 *   [step 14]  maximize — native chrome fullscreen fills the work area
 *              (D16W.5/D16W.10) and the tasklist's maximize inference
 *              republishes rules with maximized="yes" (frozen)
 *   [step 15]  restore-from-maximized — the same toggle restores the
 *              previous geometry exactly (rules maximized="no")
 *   [step 16]  close — native chrome closer destroys the window
 *
 * Every click coordinate is derived from live data (window_layout
 * geometry + decorator_margins + the proven panel-layout constants);
 * native-chrome buttons use the frozen Wm_protocol::button_rect layout.
 *
 * Marker discipline (run/qmp.inc): drag deltas stay <= 49 px per axis
 * so the shared drag arm walks exact rel-1 steps; the probe emits each
 * marker exactly once and polls the effect with bounded windows —
 * a miss is a FAIL, never a silent retry (a re-emitted marker would
 * desync the host's fixed dispatch count).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <timer_session/connection.h>

#include <sponge/wm_protocol.h>

#include "native_wm_protocol.h"
#include "native_wm_probe_util.h"

namespace {

using namespace Native_wm;

char const *const GUI_PREFIX  = "pkg_runtime -> pkg_gui_demo";
char const *const DEMO_PREFIX = "sponge-de -> Sponge DE Demo";

int const PKG_X = 50, PKG_Y = 320;
unsigned const PKG_W = 320, PKG_H = 240;

int const MOVE_DX = 40, MOVE_DY = 40;

unsigned const SCREEN_W = 1024, SCREEN_H = 768;


struct Native_wm_qmp_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "native-wm-qmp-probe" };

	Pkg_channel _pkg { _env };
	Capture_probe _capture;

	Genode::Attached_rom_dataspace _window_list    { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout  { _env, "window_layout" };
	Genode::Attached_rom_dataspace _window_chrome  { _env, "window_chrome" };
	Genode::Attached_rom_dataspace _rules          { _env, "rules" };
	Genode::Attached_rom_dataspace _margins_rom    { _env, "decorator_margins" };
	Genode::Attached_rom_dataspace _pointer_rom    { _env, "pointer" };
	Genode::Attached_rom_dataspace _hover_rom      { _env, "hover" };

	unsigned const panel_h;
	bool _pointer_materialized { false };

	static constexpr unsigned GEOMETRY_QUIESCENT_POLLS = 5;

	Native_wm_qmp_probe(Genode::Env &env, unsigned panel_height)
	: _env(env), _capture(env, SCREEN_W, SCREEN_H), panel_h(panel_height)
	{ }

	void _fail(char const *reason)
	{
		Genode::error("native-wm-qmp-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	Geom _geom(char const *prefix)
	{
		Geom g;
		_window_layout.update();
		if (!_window_layout.valid()) return g;
		find_layout_window_by_prefix(_window_layout.node(), prefix, g, nullptr);
		return g;
	}

	bool _wait_geom(char const *prefix, Geom &out, int x, int y,
	                unsigned w, unsigned h, int tol, char const *what)
	{
		Geom previous { };
		unsigned quiescent = 0;
		for (unsigned i = 0; i < 600; ++i) {
			out = _geom(prefix);
			bool const pos_ok =
				out.valid
			 && out.x >= x - tol && out.x <= x + tol
			 && out.y >= y - tol && out.y <= y + tol
			 && out.w == w && out.h == h;
			if (pos_ok) {
				quiescent = (quiescent && same_geom(out, previous))
				          ? quiescent + 1 : 1;
				previous = out;
				if (quiescent >= GEOMETRY_QUIESCENT_POLLS) return true;
			} else {
				quiescent = 0;
				previous = Geom { };
			}
			_timer.msleep(100);
		}
		Genode::error("native-wm-qmp-probe: FAIL ", what, " - window never "
		              "reached (", x, ",", y, ") ", w, "x", h, " tol=", tol);
		log_geom("native-wm-qmp-probe", "last seen", out);
		return false;
	}

	bool _wait_parked(char const *prefix, char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			if (parked(_geom(prefix))) return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-qmp-probe: FAIL ", what,
		              " - window never parked at (-32000,-32000)");
		return false;
	}

	bool _wait_absent(char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			_window_list.update();
			if (_window_list.valid()
			 && !window_list_find_prefix(_window_list.node(), GUI_PREFIX).found)
				return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-qmp-probe: FAIL ", what,
		              " - window still in window_list");
		return false;
	}

	bool _wait_focused(char const *prefix, bool want, char const *what)
	{
		for (unsigned i = 0; i < 300; ++i) {
			bool focused = false;
			Geom scratch;
			_window_layout.update();
			if (_window_layout.valid())
				find_layout_window_by_prefix(_window_layout.node(), prefix,
				                             scratch, &focused);
			if (scratch.valid && focused == want) return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-qmp-probe: FAIL ", what,
		              " - focused=", want ? "yes" : "no", " never observed");
		return false;
	}

	/*
	 * window_chrome (v1.1) per-window focus truth — the decorator's
	 * styling input. Parsed with the SHIPPED parser
	 * (Sponge::Wm_protocol::parse_window_chrome) so the probe reads
	 * exactly what sponge_decorator consumes. Entries are located by
	 * the frozen label identity (prefix scan, the delivered
	 * window_list_find_prefix idiom) with the concatenated title as
	 * fallback (find_layout_window_by_prefix idiom).
	 */
	struct Chrome_focus {
		bool          found    { false };
		unsigned long wid      { 0 };
		bool          focused  { false };
	};

	struct Chrome_focus_state {
		bool         valid         { false };
		Chrome_focus yes { }, no { };
		unsigned     focused_count { 0 };
	};

	Chrome_focus_state _chrome_focus_state(char const *yes_prefix,
	                                       char const *no_prefix)
	{
		Chrome_focus_state s;
		_window_chrome.update();
		if (!_window_chrome.valid()) return s;

		Genode::size_t const ny = Genode::strlen(yes_prefix);
		Genode::size_t const nn = Genode::strlen(no_prefix);
		auto hit = [&] (Sponge::Wm_protocol::Window_chrome const &c,
		                char const *prefix, Genode::size_t n) {
			if (Genode::strcmp(c.label.string(), prefix, n) == 0) return true;
			for (char const *p = c.title.string(); *p; ++p)
				if (Genode::strcmp(p, prefix, n) == 0) return true;
			return false;
		};

		s.valid = Sponge::Wm_protocol::parse_window_chrome(
			_window_chrome.node(),
			[&] (Sponge::Wm_protocol::Window_chrome const &c) {
				if (c.focused) ++s.focused_count;
				if (!s.yes.found && hit(c, yes_prefix, ny)) {
					s.yes.found = true;
					s.yes.wid = c.wid;
					s.yes.focused = c.focused;
				}
				if (!s.no.found && hit(c, no_prefix, nn)) {
					s.no.found = true;
					s.no.wid = c.wid;
					s.no.focused = c.focused;
				}
			});
		return s;
	}

	/*
	 * Bounded window_chrome focus assertion: focused=yes must follow
	 * EXACTLY the clicked window (yes_prefix) while the other client
	 * (no_prefix) reads focused=no — the whole snapshot must hold
	 * exactly one focused=yes entry. Same bounded-poll discipline as
	 * _wait_focused.
	 */
	bool _wait_chrome_focus(char const *yes_prefix, char const *no_prefix,
	                        char const *what)
	{
		for (unsigned i = 0; i < 300; ++i) {
			Chrome_focus_state const s =
				_chrome_focus_state(yes_prefix, no_prefix);
			if (s.valid && s.yes.found && s.no.found
			 && s.yes.focused && !s.no.focused && s.focused_count == 1) {
				Genode::log("native-wm-qmp-probe: chrome focus: wid ", s.yes.wid,
				            " focused=yes, wid ", s.no.wid,
				            " focused=no (", what, ")");
				return true;
			}
			_timer.msleep(100);
		}
		Chrome_focus_state const s = _chrome_focus_state(yes_prefix, no_prefix);
		Genode::error("native-wm-qmp-probe: FAIL ", what,
		              " - window_chrome focused=yes did not follow exactly "
		              "the clicked wid");
		Genode::log("native-wm-qmp-probe: chrome focus observed: yes(wid=",
		            s.yes.wid, " focused=", s.yes.focused ? "yes" : "no",
		            ") no(wid=", s.no.wid,
		            " focused=", s.no.focused ? "yes" : "no",
		            ") focused_count=", s.focused_count);
		return false;
	}

	bool _pointer_pos(long &x, long &y)
	{
		_pointer_rom.update();
		if (!_pointer_rom.valid()) return false;
		Genode::Node const n = _pointer_rom.node();
		x = n.attribute_value("xpos", -1L);
		y = n.attribute_value("ypos", -1L);
		return x >= 0 && y >= 0;
	}

	bool _wait_pointer_expected(long tx, long ty)
	{
		long px = -1, py = -1;
		unsigned settled = 0;
		for (unsigned i = 0; i < 3000; ++i) {
			if (_pointer_pos(px, py)
			 && px >= tx - 3 && px <= tx + 3
			 && py >= ty - 3 && py <= ty + 3) {
				if (++settled == 5) return true;
			} else {
				settled = 0;
			}
			_timer.msleep(20);
		}
		Genode::error("native-wm-qmp-probe: pointer did not settle at (",
		              tx, ",", ty, "); last position (", px, ",", py, ")");
		return false;
	}

	bool _goto_pos(long tx, long ty)
	{
		if (!_pointer_materialized) {
			emit_qmp_absmove(512, 384);
			if (!_wait_pointer_expected((long)SCREEN_W - 1,
			                            (long)SCREEN_H - 1))
				return false;
			_pointer_materialized = true;
		}

		long px = -1, py = -1;
		for (unsigned i = 0; i < 30; ++i) {
			if (!_pointer_pos(px, py)) return false;

			long dx = tx - px, dy = ty - py;
			if (dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3) {
				emit_qmp_move(1, 1);
				if (!_wait_pointer_expected(px + 1, py + 1)) return false;
				emit_qmp_move(-1, -1);
				return _wait_pointer_expected(px, py);
			}
			dx = dx > 100 ? 100 : (dx < -100 ? -100 : dx);
			dy = dy > 100 ? 100 : (dy < -100 ? -100 : dy);

			long expected_x = px + dx, expected_y = py + dy;
			expected_x = expected_x < 0 ? 0
			           : (expected_x > (long)SCREEN_W - 1
			              ? (long)SCREEN_W - 1 : expected_x);
			expected_y = expected_y < 0 ? 0
			           : (expected_y > (long)SCREEN_H - 1
			              ? (long)SCREEN_H - 1 : expected_y);

			emit_qmp_move((int)dx, (int)dy);
			if (!_wait_pointer_expected(expected_x, expected_y))
				return false;
		}
		return false;
	}

	void _log_hover(char const *what)
	{
		_hover_rom.update();
		if (!_hover_rom.valid()) {
			Genode::log("native-wm-qmp-probe: hover invalid before ", what);
			return;
		}
		Genode::Node const n = _hover_rom.node();
		Genode::String<256> const label =
			n.attribute_value("label", Genode::String<256>(""));
		Genode::log("native-wm-qmp-probe: hover before ", what,
		            " label='", label, "' x=",
		            n.attribute_value("xpos", -1L), " y=",
		            n.attribute_value("ypos", -1L));
	}

	bool _wait_hover_owner(char const *prefix, char const *what)
	{
		Genode::size_t const n = Genode::strlen(prefix);
		unsigned settled = 0;
		for (unsigned i = 0; i < 3000; ++i) {
			_hover_rom.update();
			if (_hover_rom.valid()) {
				Genode::String<256> const label = _hover_rom.node().attribute_value(
					"label", Genode::String<256>(""));
				bool match = false;
				for (char const *p = label.string(); *p && !match; ++p)
					match = Genode::strcmp(p, prefix, n) == 0;
				if (match && ++settled == 5) {
					_log_hover(what);
					return true;
				}
				if (!match) settled = 0;
			} else {
				settled = 0;
			}
			_timer.msleep(20);
		}
		_log_hover(what);
		Genode::error("native-wm-qmp-probe: hover owner did not reach '",
		              prefix, "' before ", what);
		return false;
	}

	bool _content_focus_stable(char const *yes_prefix, char const *no_prefix,
	                           char const *what)
	{
		for (unsigned cycle = 0; cycle < 12; ++cycle) {
			Geom yes_geom { }, no_geom { };
			bool yes_focused = false, no_focused = false;
			_window_layout.update();
			bool const layout_valid = _window_layout.valid();
			if (layout_valid) {
				find_layout_window_by_prefix(_window_layout.node(), yes_prefix,
				                             yes_geom, &yes_focused);
				find_layout_window_by_prefix(_window_layout.node(), no_prefix,
				                             no_geom, &no_focused);
			}

			Chrome_focus_state const chrome =
				_chrome_focus_state(yes_prefix, no_prefix);
			bool const stable =
				layout_valid && yes_geom.valid && no_geom.valid
			 && yes_focused && !no_focused
			 && chrome.valid && chrome.yes.found && chrome.no.found
			 && chrome.yes.focused && !chrome.no.focused
			 && chrome.focused_count == 1;
			if (!stable) {
				Genode::error("native-wm-qmp-probe: FAIL ", what,
				              " - focus reverted at stability cycle ", cycle);
				return false;
			}
			_timer.msleep(100);
		}
		Genode::log("native-wm-qmp-probe: ", what,
		            " stable for 12 layout/chrome observations");
		return true;
	}

	bool _rules_maximized_is(bool want, char const *what)
	{
		for (unsigned i = 0; i < 300; ++i) {
			_rules.update();
			if (_rules.valid()) {
				Geom tmp;
				bool m = false;
				if (rules_assign_for(_rules.node(), GUI_PREFIX, tmp, &m)
				 && m == want) {
					Genode::log("native-wm-qmp-probe: rules maximized=",
					            want ? "yes" : "no", " observed");
					return true;
				}
			}
			_timer.msleep(100);
		}
		Genode::error("native-wm-qmp-probe: FAIL ", what,
		              " - rules maximized=", want ? "yes" : "no",
		              " never published");
		Geom assigned;
		bool maximized = false;
		if (rules_assign_for(_rules.node(), GUI_PREFIX, assigned, &maximized))
			log_geom("native-wm-qmp-probe", "rules assignment", assigned);
		Genode::log("native-wm-qmp-probe: rules assignment maximized=",
		            maximized ? "yes" : "no");
		log_geom("native-wm-qmp-probe", "geometry at rules timeout",
		         _geom(GUI_PREFIX));
		return false;
	}

	bool _green_at(Geom const &g)
	{
		int const cx = g.x + (int)g.w / 2;
		int const cy = g.y + (int)g.h / 2;
		for (unsigned i = 0; i < 50; ++i) {
			_timer.msleep(100);
			if (!_capture.in_bounds(cx, cy)) return false;
			if (pixel_is_pkg_green(_capture.at(cx, cy))) return true;
		}
		return false;
	}

	void run()
	{
		Genode::log("native-wm-qmp-probe: starting native QMP acceptance battery");

		/* ---------- step 0: boot + window_list protocol v1 shape ---------- */
		bool ready = false;
		for (unsigned i = 0; i < 600; ++i) {
			_window_list.update();
			if (_window_list.valid()) { ready = true; break; }
			_timer.msleep(100);
		}
		if (!ready) {
			_fail("window_list ROM never became valid (sponge_wm not up)");
			return;
		}
		if (check_window_list_shape(_window_list.node(),
		                           "native-wm-qmp-probe") < 0) {
			_fail("window_list does not match protocol v1 shape");
			return;
		}

		/* ---------- step 1: launch + initial placement ---------- */
		if (!_pkg.request_ok("install", "pkg_gui_demo")) {
			_fail("install pkg_gui_demo did not answer ok");
			return;
		}
		if (!_pkg.request_ok("launch", "pkg_gui_demo")) {
			_fail("launch pkg_gui_demo did not answer ok");
			return;
		}
		Geom cur;
		if (!_wait_geom(GUI_PREFIX, cur, PKG_X, PKG_Y, PKG_W, PKG_H, 0,
		                "initial placement")) {
			_fail("pkg_gui_demo never placed at the initial geometry");
			return;
		}
		if (!_green_at(cur)) {
			_fail("pkg_gui_demo content center is not green at initial placement");
			return;
		}
		Genode::log("native-wm-qmp-probe: [step 1] pkg_gui_demo at (",
		            cur.x, ",", cur.y, ") ", cur.w, "x", cur.h);

		/* ---------- chrome metrics (shared by every click below) ---------- */
		_margins_rom.update();
		if (!_margins_rom.valid()) {
			_fail("decorator_margins ROM missing (sponge_decorator must report it)");
			return;
		}
		Chrome_margins const m = read_margins(_margins_rom.node(),
		                                     "native-wm-qmp-probe");
		if (!m.valid || m.top == 0) {
			_fail("decorator_margins invalid or zero top (no title band)");
			return;
		}

		/* ---------- step 2: move (title-bar drag) ---------- */
		{
			Pt const tc = title_center(cur, m);
			Genode::log("native-wm-qmp-probe: [step 2] drag (", tc.x, ",",
			            tc.y, ") by (", MOVE_DX, ",", MOVE_DY, ")");
			emit_qmp_drag(tc.x, tc.y, tc.x + MOVE_DX, tc.y + MOVE_DY);

			Geom want { PKG_X + MOVE_DX, PKG_Y + MOVE_DY, PKG_W, PKG_H, true };
			if (!_wait_geom(GUI_PREFIX, cur, want.x, want.y, want.w, want.h,
			                DRAG_TOL, "title-bar move"))
				_fail("move (title-bar drag) did not land at the expected geometry");
			if (cur.w != PKG_W || cur.h != PKG_H) {
				_fail("move changed the window size (move must only move)");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step 2] moved to (", cur.x,
			            ",", cur.y, ") size unchanged");
		}

		/* ---------- steps 3-10: 8-zone resize, full sequence in one run ---------- */
		static char const *const zone_names[8] =
			{ "north", "south", "east", "west", "nw", "ne", "sw", "se" };

		for (int z = 1; z <= 8; ++z) {
			Geom const before = cur;
			Zone_points const pts = zone_points(z, before, m);
			Zone_delta  const dz  = zone_delta(z);

			Genode::log("native-wm-qmp-probe: [step ", z + 2, "] zone ",
			            zone_names[z - 1], " drag (", pts.px, ",", pts.py,
			            ") -> (", pts.rx, ",", pts.ry, ")");
			emit_qmp_drag(pts.px, pts.py, pts.rx, pts.ry);

			bool ok = false;
			Geom previous { };
			unsigned quiescent = 0;
			for (unsigned i = 0; i < 600; ++i) {
				cur = _geom(GUI_PREFIX);
				if (!cur.valid) { _timer.msleep(100); continue; }

				int const got_dx = cur.x - before.x;
				int const got_dy = cur.y - before.y;
				int const got_dw = (int)cur.w - (int)before.w;
				int const got_dh = (int)cur.h - (int)before.h;

				bool const moved_ok =
					   got_dx >= dz.dx - DRAG_TOL && got_dx <= dz.dx + DRAG_TOL
					&& got_dy >= dz.dy - DRAG_TOL && got_dy <= dz.dy + DRAG_TOL
					&& got_dw >= dz.dw - DRAG_TOL && got_dw <= dz.dw + DRAG_TOL
					&& got_dh >= dz.dh - DRAG_TOL && got_dh <= dz.dh + DRAG_TOL;
				/* untouched fields must not move at all */
				bool const untouched_ok =
					   (dz.dx != 0 || got_dx == 0)
					&& (dz.dy != 0 || got_dy == 0)
					&& (dz.dw != 0 || got_dw == 0)
					&& (dz.dh != 0 || got_dh == 0);

				if (moved_ok && untouched_ok) {
					quiescent = (quiescent && same_geom(cur, previous))
					          ? quiescent + 1 : 1;
					previous = cur;
					if (quiescent >= GEOMETRY_QUIESCENT_POLLS) {
						ok = true;
						break;
					}
				} else {
					quiescent = 0;
					previous = Geom { };
				}
				_timer.msleep(100);
			}
			if (!ok) {
				log_geom("native-wm-qmp-probe", "before", before);
				log_geom("native-wm-qmp-probe", "after ", cur);
				_fail("8-zone resize: zone did not apply the expected delta");
				return;
			}
			if (!_green_at(cur)) {
				_fail("8-zone resize: content center is not green after zone");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step ", z + 2, "] zone ",
			            zone_names[z - 1], " applied: ", before.w, "x",
			            before.h, " -> ", cur.w, "x", cur.h);
		}

		/* ---------- step 11: focus (title click on each window) ---------- */
		{
			Geom demo;
			bool demo_ok = false;
			for (unsigned i = 0; i < 600; ++i) {
				demo = _geom(DEMO_PREFIX);
				if (demo.valid) { demo_ok = true; break; }
				_timer.msleep(100);
			}
			if (!demo_ok) {
				_fail("demo window not in window_layout for focus step");
				return;
			}
			Pt const demo_title = title_center(demo, m);
			Genode::log("native-wm-qmp-probe: [step 11] focus: click demo "
			            "title (", demo_title.x, ",", demo_title.y, ")");
			emit_qmp_click(demo_title.x, demo_title.y);
			if (!_wait_focused(DEMO_PREFIX, true, "demo title click focus")) {
				_fail("title click did not focus the demo window");
				return;
			}
			if (!_wait_chrome_focus(DEMO_PREFIX, GUI_PREFIX,
			                        "demo title click window_chrome focus")) {
				_fail("title click did not move window_chrome focused=yes "
				      "to the demo wid");
				return;
			}

			Pt const pkg_title = title_hit_point(cur, m);
			Genode::log("native-wm-qmp-probe: [step 11] focus: click pkg "
			            "title (", pkg_title.x, ",", pkg_title.y, ")");
			emit_qmp_click(pkg_title.x, pkg_title.y);
			if (!_wait_focused(GUI_PREFIX, true, "pkg title click focus")) {
				_fail("title click did not focus the pkg window");
				return;
			}
			if (!_wait_chrome_focus(GUI_PREFIX, DEMO_PREFIX,
			                        "pkg title click window_chrome focus")) {
				_fail("title click did not move window_chrome focused=yes "
				      "to the pkg wid");
				return;
			}
			Pt const demo_content {
				demo.x + (int)demo.w / 2, demo.y + (int)demo.h / 2 };
			Genode::log("native-wm-qmp-probe: [step 11] content click demo (",
			            demo_content.x, ",", demo_content.y, ")");
			if (!_goto_pos(demo_content.x, demo_content.y)) {
				_fail("demo content pointer did not reach the requested target");
				return;
			}
			if (!_wait_hover_owner(DEMO_PREFIX, "demo content press")) {
				_fail("demo content hover owner did not converge");
				return;
			}
			emit_qmp_press(demo_content.x, demo_content.y);
			if (!_wait_focused(DEMO_PREFIX, true, "demo content click focus")
			 || !_wait_chrome_focus(DEMO_PREFIX, GUI_PREFIX,
			                        "demo content click window_chrome focus")
			 || !_content_focus_stable(DEMO_PREFIX, GUI_PREFIX,
			                           "demo content click focus")) {
				_fail("demo content click focus did not persist");
				return;
			}

			Pt const pkg_content {
				cur.x + (int)cur.w / 2, cur.y + (int)cur.h / 2 };
			Genode::log("native-wm-qmp-probe: [step 11] content click pkg (",
			            pkg_content.x, ",", pkg_content.y, ")");
			if (!_goto_pos(pkg_content.x, pkg_content.y)) {
				_fail("pkg content pointer did not reach the requested target");
				return;
			}
			if (!_wait_hover_owner(GUI_PREFIX, "pkg content press")) {
				_fail("pkg content hover owner did not converge");
				return;
			}
			emit_qmp_press(pkg_content.x, pkg_content.y);
			if (!_wait_focused(GUI_PREFIX, true, "pkg content click focus")
			 || !_wait_chrome_focus(GUI_PREFIX, DEMO_PREFIX,
			                        "pkg content click window_chrome focus")
			 || !_content_focus_stable(GUI_PREFIX, DEMO_PREFIX,
			                           "pkg content click focus")) {
				_fail("pkg content click focus did not persist");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step 11] title/content focus "
			            "flip and stability verified");
		}

		/* ---------- step 12: minimize via native chrome minimizer ---------- */
		{
			Geom const saved = cur;
			Pt const btn = chrome_button_center(cur, m, Chrome_button::MINIMIZE);
			Genode::log("native-wm-qmp-probe: [step 12] chrome minimizer (",
			            btn.x, ",", btn.y, ")");
			emit_qmp_click(btn.x, btn.y);
			if (!_wait_parked(GUI_PREFIX, "chrome minimizer"))
				_fail("minimize (chrome minimizer) did not park the window");
			Genode::log("native-wm-qmp-probe: [step 12] parked [saved (",
			            saved.x, ",", saved.y, ") ", saved.w, "x", saved.h, "]");

			/* ---------- step 13: tasklist restore + focus ---------- */
			_window_layout.update();
			int const entry = layout_tracked_index(_window_layout.node(),
				[&](char const *title) {
					Genode::size_t const n = Genode::strlen(GUI_PREFIX);
					for (char const *p = title; *p; ++p)
						if (Genode::strcmp(p, GUI_PREFIX, n) == 0) return true;
					return false;
				});
			if (entry < 0) {
				_fail("parked window missing from window_layout tracked order");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step 13] tasklist restore, "
			            "entry ", entry);
			emit_qmp_click(tasklist_entry_x(entry), TASKLIST_Y);

			Geom restored;
			if (!_wait_geom(GUI_PREFIX, restored, saved.x, saved.y,
			                saved.w, saved.h, DRAG_TOL, "tasklist restore")) {
				_fail("tasklist restore did not land at the saved geometry");
				return;
			}
			if (!_wait_focused(GUI_PREFIX, true, "focus-after-restore")) {
				_fail("tasklist restore did not land focused (U3)");
				return;
			}
			if (!_wait_chrome_focus(GUI_PREFIX, DEMO_PREFIX,
			                        "tasklist focus window_chrome focus")) {
				_fail("tasklist focus did not move window_chrome focused=yes "
				      "to the pkg wid (U3)");
				return;
			}
			cur = restored;
			Genode::log("native-wm-qmp-probe: [step 13] restored + focused");
		}

		/* ---------- step 14: maximize via native chrome fullscreen ---------- */
		{
			Geom const saved = cur;
			Pt const btn = chrome_button_center(cur, m, Chrome_button::FULLSCREEN);
			Genode::log("native-wm-qmp-probe: [step 14] chrome fullscreen (",
			            btn.x, ",", btn.y, ")");
			emit_qmp_click(btn.x, btn.y);

			/*
			 * Run-side reading of D16W.5/D16W.10: the maximized OUTER
			 * rect (content + chrome margins) fills the work area
			 * (screen minus panel) exactly; inner geometry is the work
			 * area inset by the margins.
			 */
			Geom want {
				(int)m.left,
				(int)panel_h + (int)m.top,
				SCREEN_W - m.left - m.right,
				SCREEN_H - panel_h - m.top - m.bottom,
				true };
			bool ok = false;
			for (unsigned i = 0; i < 600; ++i) {
				cur = _geom(GUI_PREFIX);
				if (same_geom(cur, want)) { ok = true; break; }
				_timer.msleep(100);
			}
			if (!ok) {
				log_geom("native-wm-qmp-probe", "maximized got", cur);
				log_geom("native-wm-qmp-probe", "maximized want", want);
				_fail("maximize (chrome fullscreen) did not fill the work area");
				return;
			}
			if (!_rules_maximized_is(true,
			        "maximize inference did not publish rules maximized=yes")) {
				_fail("rules maximized=yes missing after maximize");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step 14] work area filled, "
			            "rules maximized=yes");

			/* ---------- step 15: restore via the same toggle ---------- */
			Pt const btn2 =
				chrome_button_center(cur, m, Chrome_button::FULLSCREEN);
			Genode::log("native-wm-qmp-probe: [step 15] chrome fullscreen "
			            "toggle (", btn2.x, ",", btn2.y, ")");
			emit_qmp_click(btn2.x, btn2.y);

			bool back = false;
			for (unsigned i = 0; i < 600; ++i) {
				cur = _geom(GUI_PREFIX);
				if (same_geom(cur, saved)) { back = true; break; }
				_timer.msleep(100);
			}
			if (!back) {
				log_geom("native-wm-qmp-probe", "restore got", cur);
				log_geom("native-wm-qmp-probe", "restore want", saved);
				_fail("maximize-restore did not return the previous geometry");
				return;
			}
			if (!_rules_maximized_is(false,
			        "unmaximize inference did not publish rules maximized=no")) {
				_fail("rules maximized=no missing after restore");
				return;
			}
			Genode::log("native-wm-qmp-probe: [step 15] previous geometry "
			            "restored, rules maximized=no");
		}

		/* ---------- step 16: close via native chrome closer ---------- */
		{
			Pt const btn = chrome_button_center(cur, m, Chrome_button::CLOSE);
			Genode::log("native-wm-qmp-probe: [step 16] chrome closer (",
			            btn.x, ",", btn.y, ")");
			emit_qmp_click(btn.x, btn.y);
			if (!_wait_absent("close (chrome closer)"))
				_fail("close (chrome closer) did not destroy the window");
			Genode::log("native-wm-qmp-probe: [step 16] window destroyed");
		}

		Genode::log("native-wm-qmp-probe: PASS");
		_env.parent().exit(0);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	/*
	 * panel_height must match the nitpicker panel domain in
	 * run/sponge-wm-native-qmp.run (proven height 28).
	 */
	unsigned panel_h = 28;
	Genode::Attached_rom_dataspace config { env, "config" };
	if (config.valid())
		panel_h = config.node().attribute_value("panel_height", 28u);

	static Native_wm_qmp_probe probe { env, panel_h };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
