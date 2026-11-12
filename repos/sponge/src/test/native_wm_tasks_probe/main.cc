/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_tasks_probe — native-WM tasklist/protocol acceptance probe
 * (Criterion 16, M2 gate: docs/plans/phase16-sponge-wm.md; the frozen
 * TasklistController contract is docs/05-sponge-de.md §4.6 protocol v1).
 *
 * Scenario: run/sponge-wm-native-tasks.run (base-sel4 + QMP). The
 * NATIVE trio (sponge_wm + sponge_layouter + sponge_decorator) replaces
 * the upstream wm/window_layouter/decorator; the delivered panel
 * tasklist (TasklistController, UNCHANGED) is driven exactly as in
 * run/sponge-wm-tasks.run — this probe proves the native producers keep
 * every consumed protocol attribute working.
 *
 * Sequence (state rows from docs/plans/wm-state-table.md):
 *
 *   [step 1/2]  install + launch pkg_gui_demo via sponge_pkgd
 *   [step 3]    window at (50,320,320,240) + live in window_list
 *               [row 1: (init) -> Normal-Visible]
 *   [step 4]    tasklist click 1 parks the window at (-32000,-32000)
 *               [row 3] + rules protocol assertions (shape, parking
 *               sentinel assign)
 *   [step 5]    tasklist click 2 restores the saved geometry [row 5]
 *   [step 6]    focus_request protocol assertions (verbatim label +
 *               id) and the rules target-relative anti-drift check
 *               [focus-after-restore per U3]
 *   [step 7]    native-chrome closer click destroys the window
 *               [rows 8-11] + rules republication drops the assign
 *   [step 8/9]  remove + re-install + re-launch; fresh window placed
 *   [step 10]   final rules/focus_request protocol review
 *   PASS
 *
 * Marker contract (run/qmp.inc shared dispatch, no local Tcl procs):
 *   QMP-TARGET click <x> <y>   — one per tasklist click (steps 4/5)
 *                                and one for the chrome closer (step 7)
 *
 * Every marker coordinate is derived from live ROM data (tasklist
 * entry centers from window_layout tracked order; the closer center
 * from the chrome metrics) — never hardcoded pixels except the proven
 * panel-layout constants in native_wm_probe_util.h.
 *
 * TODO(native-wm): the closer click target uses the PROVISIONAL native
 * chrome button layout (native_wm_probe_util.h chrome_button_center).
 * Rows 6/7 (maximize toggle) are intentionally NOT driven here: the
 * tasklist double-click toggle is the upstream path; the native
 * acceptance for maximize/restore is the chrome fullscreen button in
 * run/sponge-wm-native-qmp.run (with the rules maximized-inference
 * assertions).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <timer_session/connection.h>

#include "native_wm_protocol.h"
#include "native_wm_probe_util.h"

namespace {

using namespace Native_wm;

char const *const GUI_PREFIX = "pkg_runtime -> pkg_gui_demo";

int const INITIAL_X = 50, INITIAL_Y = 320;
unsigned const INITIAL_W = 320, INITIAL_H = 240;


struct Native_wm_tasks_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "native-wm-tasks-probe" };

	Pkg_channel _pkg { _env };

	Genode::Attached_rom_dataspace _window_list    { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout  { _env, "window_layout" };
	Genode::Attached_rom_dataspace _focus_request  { _env, "focus_request" };
	Genode::Attached_rom_dataspace _rules          { _env, "rules" };
	Genode::Attached_rom_dataspace _margins_rom    { _env, "decorator_margins" };

	explicit Native_wm_tasks_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		Genode::error("native-wm-tasks-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	bool _window_present()
	{
		_window_list.update();
		if (!_window_list.valid()) return false;
		return window_list_find_prefix(_window_list.node(), GUI_PREFIX).found;
	}

	Geom _window_geom(bool *focused = nullptr)
	{
		Geom g;
		_window_layout.update();
		if (!_window_layout.valid()) return g;
		find_layout_window_by_prefix(_window_layout.node(), GUI_PREFIX,
		                             g, focused);
		return g;
	}

	bool _wait_geom(Geom &out, int x, int y, unsigned w, unsigned h,
	                char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			out = _window_geom();
			if (out.valid && out.x == x && out.y == y
			 && out.w == w && out.h == h)
				return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tasks-probe: FAIL ", what,
		              " - window never reached (", x, ",", y, ") ", w, "x", h);
		log_geom("native-wm-tasks-probe", "last seen", out);
		return false;
	}

	int _tasklist_index()
	{
		_window_layout.update();
		if (!_window_layout.valid()) return -1;
		return layout_tracked_index(_window_layout.node(),
			[&](char const *title) {
				Genode::size_t const n = Genode::strlen(GUI_PREFIX);
				for (char const *p = title; *p; ++p)
					if (Genode::strcmp(p, GUI_PREFIX, n) == 0) return true;
				return false;
			});
	}

	void run()
	{
		Genode::log("native-wm-tasks-probe: starting native tasklist/protocol sequence");

		/* ---------- step 0: boot + window_list protocol v1 shape ---------- */
		Genode::log("native-wm-tasks-probe: [step 0] waiting for window_list ROM");
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
		                           "native-wm-tasks-probe") < 0) {
			_fail("window_list does not match protocol v1 shape");
			return;
		}

		/* ---------- steps 1/2: install + launch ---------- */
		if (!_pkg.request_ok("install", "pkg_gui_demo")) {
			_fail("install pkg_gui_demo did not answer ok");
			return;
		}
		if (!_pkg.request_ok("launch", "pkg_gui_demo")) {
			_fail("launch pkg_gui_demo did not answer ok");
			return;
		}

		/* ---------- step 3: placed + listed [row 1] ---------- */
		Geom initial;
		if (!_wait_geom(initial, INITIAL_X, INITIAL_Y, INITIAL_W, INITIAL_H,
		                "initial placement")) {
			_fail("pkg_gui_demo never placed at the initial geometry");
			return;
		}
		bool listed = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (_window_present()) { listed = true; break; }
			_timer.msleep(100);
		}
		if (!listed) {
			_fail("pkg_gui_demo never appeared in window_list (tasklist identity source)");
			return;
		}

		int const entry = _tasklist_index();
		if (entry < 0) {
			_fail("pkg_gui_demo not in window_layout tracked order");
			return;
		}
		_timer.msleep(1000); /* tasklist controller poll + Qt delivery settle */
		Genode::log("native-wm-tasks-probe: [step 3] pkg_gui_demo at (",
		            initial.x, ",", initial.y, ") ", initial.w, "x", initial.h,
		            " - in window_list, tasklist entry ", entry,
		            " [row 1: (init) -> Normal-Visible]");

		/* ---------- step 4: tasklist click 1 -> parked [row 3] ---------- */
		emit_qmp_click(tasklist_entry_x(entry), TASKLIST_Y);

		bool parked_ok = false;
		for (unsigned i = 0; i < 600; ++i) {
			Geom const g = _window_geom();
			if (parked(g)) { parked_ok = true; break; }
			_timer.msleep(100);
		}
		if (!parked_ok) {
			_fail("minimize transition: window never parked at (-32000,-32000)");
			return;
		}

		/*
		 * rules protocol assertions after the controller's minimize
		 * publication: shape, and the parking sentinel assign (frozen:
		 * "the established write value is xpos=ypos=-32000").
		 */
		_rules.update();
		if (!_rules.valid() || !check_rules_shape(_rules.node(),
		                                          "native-wm-tasks-probe")) {
			_fail("rules ROM missing or malformed after minimize");
			return;
		}
		Geom rule;
		bool rule_max = false;
		if (!rules_assign_for(_rules.node(), GUI_PREFIX, rule, &rule_max)) {
			_fail("rules carry no assign for pkg_gui_demo after minimize");
			return;
		}
		if (!parked(rule)) {
			Geom want { PARK_X, PARK_Y, INITIAL_W, INITIAL_H, true };
			Geom got = rule;
			got.valid = true;
			Genode::error("native-wm-tasks-probe: FAIL rules minimize assign "
			              "is not the parking sentinel");
			log_geom("native-wm-tasks-probe", "got", got);
			log_geom("native-wm-tasks-probe", "want", want);
			_env.parent().exit(1);
			return;
		}
		Genode::log("native-wm-tasks-probe: [step 4] parked at (",
		            PARK_X, ",", PARK_Y, ") - rules assign carries the "
		            "sentinel [row 3: Normal-Visible-Focused -> Minimized]");

		/* ---------- step 5/6: tasklist click 2 -> restore + focus [row 5] ---------- */
		emit_qmp_click(tasklist_entry_x(entry), TASKLIST_Y);

		Geom restored;
		if (!_wait_geom(restored, initial.x, initial.y, initial.w, initial.h,
		                "restore transition")) {
			_fail("restore transition: window did not return to the saved geometry");
			return;
		}

		/*
		 * focus_request protocol assertions (sole writer: tasklist).
		 * The label must be the verbatim window_list label of the
		 * window — read it back from window_list, never hardcoded.
		 */
		_window_list.update();
		Wl_entry const wl =
			window_list_find_prefix(_window_list.node(), GUI_PREFIX);
		if (!wl.found) {
			_fail("pkg_gui_demo vanished from window_list before focus check");
			return;
		}
		_focus_request.update();
		if (!_focus_request.valid()
		 || !check_focus_request(_focus_request.node(),
		                        "native-wm-tasks-probe", wl.label.string())) {
			_fail("focus_request missing or label mismatch after restore");
			return;
		}

		/*
		 * Anti-drift check: the rules assign coordinates are
		 * TARGET-RELATIVE (boundary origin subtracted; frozen consumer
		 * note, docs/05 §4.6 — feeding absolute values back drifts by
		 * the origin on every republish). Assert assign == reported
		 * geometry minus the boundary origin.
		 */
		_window_layout.update();
		Boundary const b = layout_boundary(_window_layout.node());
		if (!b.valid) {
			_fail("window_layout has no boundary after restore");
			return;
		}
		Geom rule2;
		bool rules_match = false;
		for (unsigned i = 0; i < 600; ++i) {
			_rules.update();
			if (rules_assign_for(_rules.node(), GUI_PREFIX, rule2, nullptr)
			 && rule2.x == restored.x - b.x && rule2.y == restored.y - b.y
			 && rule2.w == restored.w && rule2.h == restored.h) {
				rules_match = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!rules_match) {
			Genode::error("native-wm-tasks-probe: FAIL rules assign is not "
			              "target-relative (boundary origin drift?)");
			log_geom("native-wm-tasks-probe", "rules", rule2);
			log_geom("native-wm-tasks-probe", "layout", restored);
			Genode::log("native-wm-tasks-probe: boundary origin (", b.x, ",",
			            b.y, ")");
			_env.parent().exit(1);
			return;
		}
		Genode::log("native-wm-tasks-probe: [step 5] restored at (",
		            restored.x, ",", restored.y, ") ", restored.w, "x",
		            restored.h, " [row 5: Minimized -> Normal-Visible-Focused]");
		Genode::log("native-wm-tasks-probe: [step 6] focus_request label='",
		            wl.label.string(), "' verified; rules assign "
		            "target-relative geometry matches");

		/* ---------- step 7: chrome closer -> destroyed [rows 8-11] ---------- */
		_margins_rom.update();
		if (!_margins_rom.valid()) {
			_fail("decorator_margins ROM missing (sponge_decorator must report it)");
			return;
		}
		Chrome_margins const m = read_margins(_margins_rom.node(),
		                                     "native-wm-tasks-probe");
		if (!m.valid) {
			_fail("decorator_margins malformed");
			return;
		}
		Pt const closer =
			chrome_button_center(restored, m, Chrome_button::CLOSE);
		Genode::log("native-wm-tasks-probe: [step 7] clicking chrome closer at (",
		            closer.x, ",", closer.y, ")");
		emit_qmp_click(closer.x, closer.y);

		bool destroyed = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (!_window_present()) { destroyed = true; break; }
			_timer.msleep(100);
		}
		if (!destroyed) {
			_fail("destroy transition: window still in window_list after closer click");
			return;
		}
		bool assign_dropped = false;
		for (unsigned i = 0; i < 300; ++i) {
			_rules.update();
			Geom stale;
			if (_rules.valid()
			 && !rules_assign_for(_rules.node(), GUI_PREFIX, stale, nullptr)) {
				assign_dropped = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!assign_dropped) {
			_fail("rules still carry the assign after the window was destroyed");
			return;
		}
		Genode::log("native-wm-tasks-probe: [step 7] window destroyed, rules "
		            "assign dropped [rows 8/9/10/11: any state -> (destroyed)]");

		/* ---------- steps 8/9: remove + re-install + re-launch ---------- */
		if (!_pkg.request_ok("remove", "pkg_gui_demo")) {
			_fail("remove pkg_gui_demo did not answer ok");
			return;
		}
		if (!_pkg.request_ok("install", "pkg_gui_demo")) {
			_fail("re-install pkg_gui_demo did not answer ok");
			return;
		}
		if (!_pkg.request_ok("launch", "pkg_gui_demo")) {
			_fail("re-launch pkg_gui_demo did not answer ok");
			return;
		}
		Geom fresh;
		if (!_wait_geom(fresh, INITIAL_X, INITIAL_Y, INITIAL_W, INITIAL_H,
		                "re-launch placement")) {
			_fail("re-launched window never placed at the initial geometry");
			return;
		}
		_timer.msleep(1000);
		Genode::log("native-wm-tasks-probe: [step 9] re-launched pkg_gui_demo at (",
		            fresh.x, ",", fresh.y, ") ", fresh.w, "x", fresh.h);

		/* ---------- step 10: final protocol review ---------- */
		_rules.update();
		_focus_request.update();
		if (!_rules.valid()
		 || !check_rules_shape(_rules.node(), "native-wm-tasks-probe")) {
			_fail("final rules review failed");
			return;
		}
		if (!_focus_request.valid()
		 || !check_focus_request(_focus_request.node(),
		                        "native-wm-tasks-probe")) {
			_fail("final focus_request review failed");
			return;
		}
		Genode::log("native-wm-tasks-probe: [step 10] protocol v1 review clean");

		Genode::log("native-wm-tasks-probe: PASS");
		_env.parent().exit(0);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Native_wm_tasks_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
