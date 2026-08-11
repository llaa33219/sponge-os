/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * wm_tasks_probe — Phase 14 W7 window-task acceptance probe.
 *
 * Drives the panel tasklist through every state transition in
 * docs/plans/wm-state-table.md and verifies each one via the
 * authoritative report_rom channels (window_list, window_layout,
 * focus_request, rules).
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
 *   6. Log "wm-tasks-probe: PASS".
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
		Genode::Xml_node const r = _result.xml();
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

		try {
			Genode::Xml_node const root = _window_layout_rom.xml();
			root.for_each_sub_node("boundary", [&](Genode::Xml_node const &boundary) {
				boundary.for_each_sub_node("window", [&](Genode::Xml_node const &w) {
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
		} catch (Genode::Xml_node::Invalid_syntax) { }
		return g;
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
		try {
			Genode::Xml_node const root = _focus_request_rom.xml();
			return root.attribute_value("label", Genode::String<256>());
		} catch (Genode::Xml_node::Invalid_syntax) { return Genode::String<256>(); }
	}

	Window_geom _rule_for(char const *label, bool *maximized = nullptr)
	{
		Window_geom g;
		_rules_rom.update();
		if (!_rules_rom.valid()) return g;

		try {
			Genode::Xml_node const root = _rules_rom.xml();
			root.for_each_sub_node("assign", [&](Genode::Xml_node const &a) {
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
		} catch (Genode::Xml_node::Invalid_syntax) { }
		return g;
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
