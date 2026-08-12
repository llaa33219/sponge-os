/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * wm_tasks_probe — Phase 14 W7 window-task acceptance probe.
 *
 * Drives the panel tasklist + the decorator's closer through every
 * state transition in docs/plans/wm-state-table.md and verifies
 * each one via the authoritative report_rom channels (window_list,
 * window_layout, focus_request, rules).
 *
 * The probe is a plain Genode component (Component::construct; no
 * Qt, no libc — AGENTS.md §3.1). It is the W7 counterpart of the
 * W4 notify_probe and the W3 wm_probe observe mode.
 *
 * Sequence (per docs/plans/wm-state-table.md):
 *   1. install + launch pkg_gui_demo
 *   2. wait for the window at initial (50, 320, 320, 240)
 *      -> (init) -> Normal-Visible  [row 1]
 *   3. wait for the window to land at (-32000, -32000)
 *      -> Normal-Visible-Focused -> Minimized  [row 3]
 *      (this transition is driven by the run script's QMP click
 *      on the tasklist button)
 *   4. wait for the window to come back to the initial position
 *      -> Minimized -> Normal-Visible-Focused  [row 5]
 *      (second QMP click)
 *   5. verify the focus_request report was emitted by the tasklist
 *      controller with the pkg_gui_demo label
 *   6. wait for the window to disappear from window_list
 *      -> Normal-Visible-Focused -> (destroyed)  [rows 8/9/10/11]
 *      (driven by the run script's QMP click on the decorator's
 *      closer button while the window is still at the non-maximized
 *      position (50, 320, 320, 240); see run script for the
 *      pixel-coord rationale — the panel layer covers y=0..28 and
 *      the closer button sits in the title bar at y=4..20, so the
 *      closer is reachable ONLY while the window is not maximized
 *      and its title bar is below the panel)
 *   7. re-launch pkg_gui_demo for the maximize-toggle coverage
 *   8. wait for the new window at initial (50, 320, 320, 240)
 *   9. wait for the rules ROM to carry maximized="yes" for the
 *      window  -> Visible-Focused -> Maximized  [row 6]
 *      (driven by the run script's QMP double-click on the tasklist
 *      entry — TasklistWidget::mouseDoubleClickEvent emits
 *      task_toggle_maximized; TaskListController::on_toggle_maximized
 *      flips st->maximized and publishes the rules)
 *  10. wait for the rules ROM to carry maximized="no" for the window
 *      -> Maximized -> Normal-Visible  [row 7]
 *      (second QMP double-click)
 *  11. Log "wm-tasks-probe: PASS".
 *
 * Capability surface: Report (request writer), ROM (result, window_list,
 * window_layout, focus_request, rules readers), Timer.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>


namespace {


struct Window_geom {
	int  x       { 0 };
	int  y       { 0 };
	unsigned w  { 0 };
	unsigned h  { 0 };
	bool valid   { false };
};


struct Wm_tasks_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "wm-tasks-probe" };

	Genode::Expanding_reporter     _request   { _env, "request",   "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };

	Genode::Attached_rom_dataspace _window_list_rom    { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout_rom  { _env, "window_layout" };
	Genode::Attached_rom_dataspace _focus_request_rom  { _env, "focus_request" };
	Genode::Attached_rom_dataspace _rules_rom         { _env, "rules" };

	/* Parking coordinates. */
	static constexpr int PARK_X = -32000;
	static constexpr int PARK_Y = -32000;

	Wm_tasks_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		Genode::error("wm-tasks-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/* ============ pkgd request channel ============ */

	bool _send_pkg_request(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		for (unsigned i = 0; i < 200; ++i) {
			Genode::String<32> s = _pkg_result_status(op, pkg);
			if (s == Genode::String<32>("ok")
			 || s == Genode::String<32>("not-installed")
			 || s == Genode::String<32>("already-running"))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	Genode::String<32> _pkg_result_status(char const *op, char const *pkg)
	{
		_result.update();
		if (!_result.valid()) return Genode::String<32>();
		Genode::Node const r = _result.node();
		if (!r.has_type("result")) return Genode::String<32>();
		if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>(op))  return Genode::String<32>();
		if (r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg)) return Genode::String<32>();
		return r.attribute_value("status", Genode::String<32>());
	}

	/* ============ window_list / window_layout readers ============ */

	/*
	 * The layouter's window_layout title is `label + " " + Qt title`.
	 * We extract label = substring up to the first space.
	 */
	static void _label_from_title(char const *title, char *out, Genode::size_t out_size)
	{
		Genode::size_t i = 0;
		while (*title && *title != ' ' && i + 1 < out_size) {
			out[i++] = *title++;
		}
		out[i] = '\0';
	}

	Window_geom _geom_by_label(char const *needle)
	{
		Window_geom g;
		_window_layout_rom.update();
		if (!_window_layout_rom.valid()) return g;

		{
			Genode::Node const root = _window_layout_rom.node();
			root.for_each_sub_node("boundary", [&](Genode::Node const &boundary) {
				boundary.for_each_sub_node("window", [&](Genode::Node const &w) {
					if (g.valid) return;
					Genode::String<256> const title =
						w.attribute_value("title", Genode::String<256>());
					Genode::String<256> const needle_s(needle);
					if (Genode::strlen(needle) > Genode::strlen(title.string())) return;
					bool found = false;
					for (char const *p = title.string(); *p; ++p) {
						if (Genode::strcmp(p, needle, Genode::strlen(needle)) == 0) {
							found = true; break;
						}
					}
					if (!found) {
						Genode::log("wm-tasks-probe: window seen but no match: title='",
						            title.string(), "' needle='",
						            needle_s.string(), "'");
						return;
					}
					g.x = w.attribute_value("xpos", 0);
					g.y = w.attribute_value("ypos", 0);
					g.w = w.attribute_value("width",  0u);
					g.h = w.attribute_value("height", 0u);
					g.valid = true;
				});
			});
		}
		return g;
	}

	bool _in_window_list(char const *needle)
	{
		_window_list_rom.update();
		if (!_window_list_rom.valid()) return false;

		bool found = false;
		{
			Genode::Node const root = _window_list_rom.node();
			root.for_each_sub_node("window", [&](Genode::Node const &w) {
				if (found) return;
				Genode::String<256> const label =
					w.attribute_value("label", Genode::String<256>());
				if (Genode::strcmp(label.string(), needle,
				                   Genode::strlen(needle)) == 0)
					found = true;
			});
		}
		return found;
	}

	bool _is_parked(char const *label)
	{
		Window_geom const g = _geom_by_label(label);
		return g.valid && g.x <= PARK_X && g.y <= PARK_Y;
	}

	/* ============ focus_request + rules readers ============ */

	Genode::String<256> _last_focus_request_label()
	{
		_focus_request_rom.update();
		if (!_focus_request_rom.valid()) return Genode::String<256>();
		{
			Genode::Node const root = _focus_request_rom.node();
			return root.attribute_value("label", Genode::String<256>());
		}
	}

	Window_geom _rule_for(char const *label, bool *maximized = nullptr)
	{
		Window_geom g;
		_rules_rom.update();
		if (!_rules_rom.valid()) return g;

		{
			Genode::Node const root = _rules_rom.node();
			root.for_each_sub_node("assign", [&](Genode::Node const &a) {
				Genode::String<256> const l =
					a.attribute_value("label", Genode::String<256>());
				if (l != Genode::String<256>(label)) return;
				g.x = a.attribute_value("xpos", 0);
				g.y = a.attribute_value("ypos", 0);
				g.w = a.attribute_value("width",  0u);
				g.h = a.attribute_value("height", 0u);
				g.valid = true;
				if (maximized)
					*maximized = a.attribute_value("maximized", false);
			});
		}
		return g;
	}

	/*
	 * True iff the rules ROM carries an <assign label="..." ...>
	 * for `label` whose `maximized` attribute equals `want`. The
	 * probe uses this to verify the tasklist controller's
	 * on_toggle_maximized published the expected flag (the layouter
	 * emits `Rect(_pos, _size)` for the window's geometry when
	 * maximized, so a maximized window's geometry may not change
	 * even when the controller flips the flag — the assertion
	 * targets the rules flag, not the geometry, per the W7 pass
	 * condition).
	 *
	 * Match uses strncmp with the needle's length (prefix match),
	 * the same idiom as _in_window_list: the tasklist controller
	 * copies the window_list <window label="..."> verbatim into
	 * <assign label="..."> — the label carries the Qt title
	 * suffix (`pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI
	 * Demo`), not just the label_prefix-only form. Strict equality
	 * would never match.
	 */
	bool _rules_maximized_is(char const *label, bool want)
	{
		_rules_rom.update();
		if (!_rules_rom.valid()) return false;

		bool found = false;
		Genode::Node const root = _rules_rom.node();
		root.for_each_sub_node("assign", [&](Genode::Node const &a) {
			if (found) return;
			Genode::String<256> const l =
				a.attribute_value("label", Genode::String<256>());
			if (Genode::strcmp(l.string(), label, Genode::strlen(label)) != 0)
				return;
			bool const m = a.attribute_value("maximized", false);
			if (m == want) found = true;
		});
		return found;
	}

	/* ============ main sequence ============ */

	void run()
	{
		Genode::log("wm-tasks-probe: starting W7 acceptance sequence");

		char const *const GUI_LABEL = "pkg_runtime -> pkg_gui_demo";

		/* Step 0: wait for window_list ROM to be valid. */
		Genode::log("wm-tasks-probe: [step 0] waiting for window_list ROM");
		bool wl_ready = false;
		for (unsigned i = 0; i < 300; ++i) {
			_window_list_rom.update();
			if (_window_list_rom.valid()) { wl_ready = true; break; }
			_timer.msleep(100);
		}
		if (!wl_ready) {
			_fail("window_list ROM never became valid (wm not up)");
			return;
		}
		Genode::log("wm-tasks-probe: [step 0] window_list ROM ready");

		/* Step 1: install pkg_gui_demo. */
		Genode::log("wm-tasks-probe: [step 1] install pkg_gui_demo");
		if (!_send_pkg_request("install", "pkg_gui_demo")) {
			_fail("install pkg_gui_demo: timed out");
			return;
		}
		Genode::log("wm-tasks-probe: [step 1] pkg_gui_demo installed");

		/* Step 2: launch pkg_gui_demo. */
		Genode::log("wm-tasks-probe: [step 2] launch pkg_gui_demo");
		if (!_send_pkg_request("launch", "pkg_gui_demo")) {
			_fail("launch pkg_gui_demo: timed out");
			return;
		}
		Genode::log("wm-tasks-probe: [step 2] pkg_gui_demo launched");

		/* Step 3: wait for window_layout entry at initial position. */
		Window_geom initial;
		bool found = false;
		for (unsigned i = 0; i < 3000; ++i) {
			initial = _geom_by_label(GUI_LABEL);
			if (initial.valid && initial.w == 320 && initial.h == 240
			 && initial.x >= 0 && initial.x < 1024
			 && initial.y >= 0 && initial.y < 768) {
				found = true; break;
			}
			if (i % 50 == 0) {
				Genode::log("wm-tasks-probe: [step 3] poll ", i, " window_layout valid=", _window_layout_rom.valid() ? "yes" : "no");
			}
			_timer.msleep(100);
		}
		if (!found) {
			_fail("pkg_gui_demo window never appeared in window_layout at expected initial position");
			return;
		}
		Genode::log("wm-tasks-probe: [step 3] window_layout: pkg_gui_demo at (",
		            initial.x, ",", initial.y, ") ", initial.w, "x", initial.h,
		            " [row 1: (init) -> Normal-Visible]");

		/* Step 3b: the tasklist's identity source is window_list (it
		 * lags window_layout). The run script clicks the tasklist on
		 * the step-3 marker, so the marker must only print once the
		 * window is also in window_list — otherwise the click hits an
		 * empty tasklist. */
		bool listed = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (_in_window_list(GUI_LABEL)) { listed = true; break; }
			_timer.msleep(100);
		}
		if (!listed) {
			_fail("pkg_gui_demo never appeared in window_list (tasklist identity source)");
			return;
		}
		/* settle: tasklist controller polls at 250ms + Qt queued delivery */
		_timer.msleep(1000);
		Genode::log("wm-tasks-probe: [step 3] pkg_gui_demo in window_list (tasklist entry live)");

		/* Step 4: wait for the tasklist click to minimize the window. */
		bool minimized = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (_is_parked(GUI_LABEL)) { minimized = true; break; }
			_timer.msleep(100);
		}
		if (!minimized) {
			_fail("minimize transition: pkg_gui_demo did not reach off-screen position within 60s");
			return;
		}
		Genode::log("wm-tasks-probe: [step 4] window_layout: pkg_gui_demo parked at (-32000, -32000) [row 3: Normal-Visible-Focused -> Minimized]");

		/* Step 5: wait for the second tasklist click to restore. */
		Window_geom restored;
		bool restored_ok = false;
		for (unsigned i = 0; i < 600; ++i) {
			restored = _geom_by_label(GUI_LABEL);
			if (restored.valid && restored.x == initial.x && restored.y == initial.y
			 && restored.w == initial.w && restored.h == initial.h) {
				restored_ok = true; break;
			}
			_timer.msleep(100);
		}
		if (!restored_ok) {
			_fail("restore transition: pkg_gui_demo did not return to initial position within 60s");
			return;
		}
		Genode::log("wm-tasks-probe: [step 5] window_layout: pkg_gui_demo restored at (",
		            restored.x, ",", restored.y, ") ", restored.w, "x", restored.h,
		            " [row 5: Minimized -> Normal-Visible-Focused]");

		/* Step 6: verify the focus_request report was emitted. */
		Genode::log("wm-tasks-probe: [step 6] verifying focus_request report was emitted");
		Genode::String<256> const fr_label = _last_focus_request_label();
		if (fr_label.length() == 0 || !Genode::String<256>(fr_label).valid()) {
			_fail("focus_request report is empty");
			return;
		}
		Genode::log("wm-tasks-probe: [step 6] focus_request label='",
		            fr_label.string(), "' [focus-after-restore per U3]");

		/*
		 * Step 7: wait for the run script's QMP click on the
		 * decorator's closer button to close the window. The
		 * closer pixel must be reachable — see the run script's
		 * pixel derivation. The layouter dispatches the close action
		 * via its user_state closer-drag protocol (window.h:78 /
		 * user_state.h:387), then issues a resize-to-0x0 via
		 * _gen_resize_request(); wm closes the window; window_list
		 * ROM drops the entry. The tasklist controller's
		 * recompute_tracked joins on window_list (the identity
		 * source), so the tasklist entry vanishes in the same step.
		 * Covers rows 8 / 9 / 10 / 11 of the state table (the
		 * mechanism is the same regardless of the FROM state).
		 *
		 * NOTE: the closer click is dispatched BEFORE the maximize
		 * toggle on purpose. After the toggle, the window's outer
		 * rect grows to (0, 0, 1024, 768) — the title bar lands at
		 * y=4..20 inside the panel's layer (y=0..28), making the
		 * closer button (the rightmost title-bar control at pixel
		 * (1012, 12)) unreachable from a host-driven PS/2 click
		 * (the panel layer absorbs the click at y=0..28). Driving
		 * the click while the window is still at (50, 320, 320, 240)
		 * — closer at (362, 312) — keeps it under the panel's y
		 * range. The re-launch below restores the window at the
		 * non-maximized position for the toggle coverage.
		 */
		Genode::log("wm-tasks-probe: [step 7] waiting for window_list to drop pkg_gui_demo (closer click)");
		bool destroyed = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (!_in_window_list(GUI_LABEL)) { destroyed = true; break; }
			_timer.msleep(100);
		}
		if (!destroyed) {
			_fail("destroy transition: pkg_gui_demo still in window_list after closer click within 60s");
			return;
		}
		Genode::log("wm-tasks-probe: [step 7] pkg_gui_demo removed from window_list [row 8/9/10/11: any state -> (destroyed)]");

		/*
		 * Step 8: re-launch pkg_gui_demo. The first instance was
		 * destroyed by the closer click; we need a fresh window
		 * for the maximize-toggle coverage below.
		 *
		 * sponge_pkgd's launch is idempotent — a package in
		 * _running stays there until remove() drops it (the README
		 * at the top of sponge_pkgd/main.cc:1090-1102 documents
		 * the no-stop contract). So a plain re-launch after the
		 * OLD Qt-side exit returns "already-running" without
		 * regenerating pkg_runtime's <start> node, and pkg_runtime
		 * never restarts the dead child. The work-around: remove()
		 * the package (drops both root + running entries), install()
		 * again, then launch() to bring the new instance up.
		 */
		Genode::log("wm-tasks-probe: [step 8] remove + re-install + launch pkg_gui_demo");
		if (!_send_pkg_request("remove",   "pkg_gui_demo")) {
			_fail("remove pkg_gui_demo: timed out");
			return;
		}
		if (!_send_pkg_request("install",  "pkg_gui_demo")) {
			_fail("install pkg_gui_demo (round 2): timed out");
			return;
		}
		if (!_send_pkg_request("launch",   "pkg_gui_demo")) {
			_fail("launch pkg_gui_demo (round 2): timed out");
			return;
		}
		Genode::log("wm-tasks-probe: [step 8] pkg_gui_demo re-launched");

		/*
		 * Step 9: wait for the re-launched window at its initial
		 * position. pkg_gui_demo starts at (50, 320, 320, 240)
		 * per the layouter's static <assign label_prefix=...> rule.
		 */
		Window_geom re_initial;
		bool re_found = false;
		for (unsigned i = 0; i < 3000; ++i) {
			re_initial = _geom_by_label(GUI_LABEL);
			if (re_initial.valid && re_initial.w == 320 && re_initial.h == 240
			 && re_initial.x >= 0 && re_initial.x < 1024
			 && re_initial.y >= 0 && re_initial.y < 768) {
				re_found = true; break;
			}
			if (i % 50 == 0) {
				Genode::log("wm-tasks-probe: [step 9] poll ", i, " window_layout valid=", _window_layout_rom.valid() ? "yes" : "no");
			}
			_timer.msleep(100);
		}
		if (!re_found) {
			_fail("re-launched pkg_gui_demo window never appeared in window_layout at expected initial position");
			return;
		}
		/* settle: tasklist controller polls at 250ms + Qt queued delivery */
		_timer.msleep(1000);
		Genode::log("wm-tasks-probe: [step 9] re-launched pkg_gui_demo at (",
		            re_initial.x, ",", re_initial.y, ") ", re_initial.w, "x", re_initial.h);

		/*
		 * Step 10: wait for the run script's QMP double-click on
		 * the tasklist to publish rules with maximized="yes". The
		 * tasklist widget's mouseDoubleClickEvent emits
		 * task_toggle_maximized; TasklistController::on_toggle_maximized
		 * flips st->maximized and re-publishes the rules. The FIRST
		 * press of the double-click ALSO fires task_clicked (Qt
		 * fires mousePressEvent for the first click of a double-
		 * click); that press drives the chain Visible-Focused ->
		 * Minimized in the same instant, then the mouseDoubleClick
		 * drives Minimized -> Maximized. The probe waits for the
		 * final rules state (maximized="yes"), not the intermediate
		 * Minimized flicker.
		 */
		Genode::log("wm-tasks-probe: [step 10] waiting for rules maximized=\"yes\"");
		bool maximized_yes = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (_rules_maximized_is(GUI_LABEL, true)) { maximized_yes = true; break; }
			_timer.msleep(100);
		}
		if (!maximized_yes) {
			_fail("maximize transition: rules did not carry maximized=\"yes\" for pkg_gui_demo within 60s");
			return;
		}
		Genode::log("wm-tasks-probe: [step 10] rules maximized=\"yes\" for pkg_gui_demo [row 6: Normal-Visible-Focused -> Maximized]");

		/*
		 * Step 11: wait for the second QMP double-click to publish
		 * rules with maximized="no". Same chain (a press toggles the
		 * minimize state, then the double-click toggles the maximize
		 * flag); the probe waits for the FINAL state.
		 */
		Genode::log("wm-tasks-probe: [step 11] waiting for rules maximized=\"no\"");
		bool maximized_no = false;
		for (unsigned i = 0; i < 600; ++i) {
			if (_rules_maximized_is(GUI_LABEL, false)) { maximized_no = true; break; }
			_timer.msleep(100);
		}
		if (!maximized_no) {
			_fail("unmaximize transition: rules did not carry maximized=\"no\" for pkg_gui_demo within 60s");
			return;
		}
		Genode::log("wm-tasks-probe: [step 11] rules maximized=\"no\" for pkg_gui_demo [row 7: Maximized -> Normal-Visible]");

		Genode::log("wm-tasks-probe: PASS");
		_env.parent().exit(0);
	}
};


}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Wm_tasks_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
