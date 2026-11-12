/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_tiling_probe — native-WM Master-Stack / mode / reflow
 * acceptance probe (Criterion 16, M4/M5 gates:
 * docs/plans/phase16-sponge-wm.md D16W.5 + docs/plans/wm-state-table.md
 * "Native WM Extension").
 *
 * Scenario: run/sponge-wm-native-tiling.run (base-sel4 + QMP). Three
 * windows exist from the start: sponge-de's demo window, the
 * pkg_gui_demo window launched by this probe, and the non-resizeable
 * native_wm_fixture_client fixture. The two resizeable clients carry
 * the arrangement assertions, which are EXACT (computed geometry, no
 * pointer tolerance — the plan's gate: "window_layout reports match
 * the expected arrangement after every reflow"); the fixture is
 * checked by an independent helper (see the fixture note below).
 *
 * Run-side reading of D16W.5 (recorded as the acceptance contract;
 * the implementers must match it or record the deviation):
 *   - placement acts on the OUTER rect (content + chrome margins);
 *     master outer = left half of the work area, stack windows split
 *     the right half into equal bands top to bottom, no gaps
 *     (window gaps are a non-goal);
 *   - work area = screen minus panel (panel_height, default 28);
 *   - master = first ELIGIBLE window in window_list order (the native
 *     sponge_wm must report window_list in window creation order);
 *     eligible = resizeable && non-minimized && non-maximized;
 *   - a lone eligible window stays the left-half master (the right
 *     half is simply unoccupied — the literal "left-half master" rule).
 *
 * Sequence:
 *   [step 2]  wm.layout.mode floating -> tiling (the `vct config set
 *             wm.layout.mode tiling` backend write, D16W.11) +
 *             arrangement reflow
 *   [step 3]  minimize (tasklist) -> parked, reflow (stack band drops)
 *   [step 4]  restore (tasklist) -> re-enters the arrangement, reflow
 *   [step 5]  minimize the master -> master slot passes on, reflow
 *   [step 6]  restore the master -> creation-order arrangement, reflow
 *   [step 7]  maximize (chrome fullscreen) -> work-area fill, leaves
 *             the arrangement, rules maximized="yes" inference
 *   [step 8]  maximize-restore -> arrangement slot back
 *   [step 9]  tiling -> floating: every window KEEPS its geometry
 *   [step 10] floating -> tiling: reflow to the arrangement
 *   [step 11] rules-drift soak (>=20 cycles, §7.2): tasklist rules
 *             write + restore + mode switch out/in; after every cycle
 *             window_layout must equal the expected arrangement EXACTLY
 *   [step 12] close the master (chrome closer) -> the next window
 *             becomes master and reflows to the left half (worked
 *             example 10)
 *
 * Marker contract (run/qmp.inc shared dispatch): QMP-TARGET click for
 * tasklist and maximizer actions, sync after the soak, then absmove +
 * closed-loop rel-1 moves + press for the final chrome close. No local
 * Tcl QMP procs.
 *
 * `wm.layout.mode` is a closed configd enum (floating|tiling, default
 * floating, D16W.11); the probe drives the same request channel as vct.
 *
 * Fixture note: Master-Stack acceptance needs >= 2 RESIZEABLE windows.
 * If window_list reports the demo window non-resizeable it floats
 * (D16W.5) and this probe fails at step 1 naming the gap — the
 * fix is a resizeable second client (or a resizeable demo widget).
 *
 * Non-resizeable fixture (test/native_wm_fixture_client, commit
 * 1013b48577): it never opens the Gui info ROM, so window_list must
 * report resizeable="no" and D16W.5 makes it FLOAT at exactly
 * (850,560) 160x160 (the sponge_layouter static assign) in every
 * mode and through every reflow. _assert_fixture is deliberately
 * INDEPENDENT of the two-client machinery: it never joins _classify,
 * the slots[]/eligible counts, or the step-12 min_clients logic. It
 * runs at step 1 and inside every arrangement check (plus the step-9
 * mode-switch retention), so fixture exclusion from Master-Stack is
 * continuously proven. The fixture normally needs no fixture_cmd
 * command in this scenario (its fixture_cmd ROM is routed through
 * report_rom and stays empty); a command would be published with a
 * Genode::Expanding_reporter on "fixture_cmd" directly, without
 * touching native_wm_common.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <timer_session/connection.h>

#include "native_wm_protocol.h"
#include "native_wm_probe_util.h"

namespace {

using namespace Native_wm;

char const *const GUI_PREFIX  = "pkg_runtime -> pkg_gui_demo";
char const *const DEMO_PREFIX = "sponge-de -> Sponge DE Demo";

/*
 * The non-resizeable fixture client (test/native_wm_fixture_client).
 * Label prefix matches its wm session label; the geometry is the
 * sponge_layouter static assign and the fixture's own view rect.
 */
char const *const FIXTURE_PREFIX = "native_wm_fixture_client";
int const FIXTURE_X = 850, FIXTURE_Y = 560;
unsigned const FIXTURE_W = 160, FIXTURE_H = 160;

unsigned const SCREEN_W = 1024, SCREEN_H = 768;


struct Native_wm_tiling_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "native-wm-tiling-probe" };

	Pkg_channel   _pkg  { _env };
	Config_channel _cfg { _env };

	Genode::Attached_rom_dataspace _window_list    { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout  { _env, "window_layout" };
	Genode::Attached_rom_dataspace _rules          { _env, "rules" };
	Genode::Attached_rom_dataspace _margins_rom    { _env, "decorator_margins" };
	Genode::Attached_rom_dataspace _pointer_rom    { _env, "pointer" };

	unsigned const panel_h;
	unsigned const soak_cycles;
	Chrome_margins _margins { };

	Native_wm_tiling_probe(Genode::Env &env, unsigned panel,
	                       unsigned soak)
	: _env(env), panel_h(panel), soak_cycles(soak)
	{ }

	void _fail(char const *reason)
	{
		Genode::error("native-wm-tiling-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	Geom _geom(char const *prefix, bool *focused = nullptr)
	{
		Geom g;
		_window_layout.update();
		if (!_window_layout.valid()) return g;
		find_layout_window_by_prefix(_window_layout.node(), prefix, g, focused);
		return g;
	}

	bool _wait_geom(char const *prefix, Geom &out, int x, int y,
	                unsigned w, unsigned h, char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			out = _geom(prefix);
			if (out.valid && out.x == x && out.y == y
			 && out.w == w && out.h == h)
				return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tiling-probe: FAIL ", what, " - window "
		              "never reached (", x, ",", y, ") ", w, "x", h);
		log_geom("native-wm-tiling-probe", "last seen", out);
		return false;
	}

	bool _wait_parked(char const *prefix, char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			if (parked(_geom(prefix))) return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tiling-probe: FAIL ", what,
		              " - window never parked");
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
		Genode::error("native-wm-tiling-probe: pointer did not settle at (",
		              tx, ",", ty, "); last position (", px, ",", py, ")");
		return false;
	}

	/*
	 * Emit at most one in-flight move. The host walks the requested delta
	 * in rel-1 steps, so the expected position is known without modelling
	 * event_filter acceleration. Waiting for that position replaces the
	 * former blind 300 ms delay, which allowed stale requests to queue.
	 */
	bool _goto_pos(long tx, long ty)
	{
		/* Textedit's proven materialization lands in the bottom-right
		 * corner on this host, synchronizing stale relay state to a known
		 * real pointer position before relative positioning begins. */
		emit_qmp_absmove(512, 384);
		if (!_wait_pointer_expected((long)SCREEN_W - 1,
		                            (long)SCREEN_H - 1))
			return false;

		long px = -1, py = -1;
		for (unsigned i = 0; i < 30; ++i) {
			if (!_pointer_pos(px, py)) return false;

			long dx = tx - px, dy = ty - py;
			if (dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3)
				return true;
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

	bool _wait_visible(char const *prefix, char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			Geom const g = _geom(prefix);
			if (g.valid && !parked(g)) return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tiling-probe: FAIL ", what,
		              " - window never became visible");
		return false;
	}

	bool _wait_not_maximized(char const *prefix, char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			if (!fills_boundary(_geom(prefix),
			                    layout_boundary(_window_layout.node())))
				return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tiling-probe: FAIL ", what,
		              " - window never left the maximized state");
		return false;
	}

	int _tasklist_index(char const *prefix)
	{
		_window_layout.update();
		if (!_window_layout.valid()) return -1;
		Genode::size_t const n = Genode::strlen(prefix);
		return layout_tracked_index(_window_layout.node(),
			[&](char const *title) {
				for (char const *p = title; *p; ++p)
					if (Genode::strcmp(p, prefix, n) == 0) return true;
				return false;
			});
	}

	void _click_tasklist(char const *prefix, char const *what)
	{
		int const idx = _tasklist_index(prefix);
		if (idx < 0) {
			_fail(what);
			return;
		}
		emit_qmp_click(tasklist_entry_x(idx), TASKLIST_Y);
	}

	/* ---------- Master-Stack geometry (run-side D16W.5 reading) ---------- */

	struct Slot {
		char const *prefix;
		bool        resizeable;
		bool        parked;
		bool        maximized;
		Geom        geom;
	};

	/*
	 * Scan window_list in order (creation order contract) and classify
	 * the two known clients against their current layout geometry.
	 * Returns the number of classified clients.
	 */
	int _classify(Slot out[2])
	{
		_window_list.update();
		_window_layout.update();

		int n = 0;
		auto classify = [&](char const *prefix) {
			Wl_entry const wl =
				window_list_find_prefix(_window_list.node(), prefix);
			Slot s { };
			s.prefix     = prefix;
			s.resizeable = wl.found && wl.resizeable;
			s.geom       = _geom(prefix);
			s.parked     = parked(s.geom);
			s.maximized  = !s.parked && fills_boundary(s.geom,
			                layout_boundary(_window_layout.node()));
			out[n++] = s;
		};

		bool demo_seen = false, pkg_seen = false;
		_window_list.node().for_each_sub_node("window",
			[&](Genode::Node const &w) {
				Genode::String<256> const l =
					w.attribute_value("label", Genode::String<256>());
				if (!demo_seen
				 && Genode::strcmp(l.string(), DEMO_PREFIX,
				                   Genode::strlen(DEMO_PREFIX)) == 0) {
					demo_seen = true;
					classify(DEMO_PREFIX);
				} else if (!pkg_seen
				        && Genode::strcmp(l.string(), GUI_PREFIX,
				                          Genode::strlen(GUI_PREFIX)) == 0) {
					pkg_seen = true;
					classify(GUI_PREFIX);
				}
			});
		return n;
	}

	Geom _slot_content(int slot_index, int eligible_total) const
	{
		unsigned const work_h = SCREEN_H - panel_h;
		int    const work_y  = (int)panel_h;
		int    const half_w  = (int)(SCREEN_W / 2);

		int ox, oy, ow, oh;
		if (slot_index == 0) {
			ox = 0;      oy = work_y;
			ow = half_w; oh = (int)work_h;
		} else {
			int const bands  = eligible_total - 1;
			int const band   = slot_index - 1;
			int const y0     = work_y + (int)((work_h * (unsigned)band) / (unsigned)bands);
			int const y1     = work_y + (int)((work_h * (unsigned)(band + 1)) / (unsigned)bands);
			ox = half_w; oy = y0;
			ow = (int)SCREEN_W - half_w; oh = y1 - y0;
		}

		Geom g;
		g.x = ox + (int)_margins.left;
		g.y = oy + (int)_margins.top;
		g.w = (unsigned)(ow - (int)_margins.left - (int)_margins.right);
		g.h = (unsigned)(oh - (int)_margins.top - (int)_margins.bottom);
		g.valid = true;
		return g;
	}

	Geom _maximized_content() const
	{
		Geom g;
		g.x = (int)_margins.left;
		g.y = (int)panel_h + (int)_margins.top;
		g.w = SCREEN_W - _margins.left - _margins.right;
		g.h = SCREEN_H - panel_h - _margins.top - _margins.bottom;
		g.valid = true;
		return g;
	}

	/*
	 * The reflow assertion: every client must be parked, work-area
	 * maximized, or placed at its exact Master-Stack slot. Returns
	 * false and logs the mismatch otherwise.
	 */
	bool _assert_arrangement(char const *what, unsigned min_clients = 2,
	                         bool require_all_eligible = false)
	{
		/* fixture exclusion/floating must hold through every reflow;
		 * it never joins the slots[]/eligible counts below */
		if (!_assert_fixture(what))
			return false;

		Slot slots[2] { };
		int const n = _classify(slots);
		if (n < (int)min_clients) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - only ", n, " known client(s) in window_list");
			return false;
		}

		int eligible_total = 0;
		for (int i = 0; i < n; ++i)
			if (slots[i].resizeable && !slots[i].parked && !slots[i].maximized)
				++eligible_total;

		if (require_all_eligible && eligible_total != n)
			return false;

		int slot = 0;
		for (int i = 0; i < n; ++i) {
			Slot const &s = slots[i];

			if (!s.geom.valid) {
				Genode::error("native-wm-tiling-probe: FAIL ", what,
				              " - ", s.prefix, " missing from window_layout");
				return false;
			}

			if (s.parked) {
				Genode::log("native-wm-tiling-probe: ", s.prefix, " parked");
				continue;
			}

			if (s.maximized) {
				Geom const want = _maximized_content();
				if (!same_geom(s.geom, want)) {
					Genode::error("native-wm-tiling-probe: FAIL ", what,
					              " - maximized window does not fill the "
					              "work area");
					log_geom("native-wm-tiling-probe", "got ", s.geom);
					log_geom("native-wm-tiling-probe", "want", want);
					return false;
				}
				Genode::log("native-wm-tiling-probe: ", s.prefix,
				            " work-area maximized");
				continue;
			}

			if (!s.resizeable) {
				Genode::log("native-wm-tiling-probe: ", s.prefix,
				            " non-resizeable (floating, not asserted)");
				continue;
			}

			Geom const want = _slot_content(slot, eligible_total);
			if (!same_geom(s.geom, want)) {
				Genode::error("native-wm-tiling-probe: FAIL ", what,
				              " - ", s.prefix, " not at its Master-Stack "
				              "slot ", slot, "/", eligible_total);
				log_geom("native-wm-tiling-probe", "got ", s.geom);
				log_geom("native-wm-tiling-probe", "want", want);
				return false;
			}
			Genode::log("native-wm-tiling-probe: ", s.prefix, " slot ", slot,
			            "/", eligible_total, " at (", s.geom.x, ",",
			            s.geom.y, ") ", s.geom.w, "x", s.geom.h);
			++slot;
		}

		if (slot != eligible_total) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - placed ", slot, " of ", eligible_total,
			              " eligible windows");
			return false;
		}
		return true;
	}

	bool _wait_arrangement(char const *what, unsigned min_clients = 2,
	                       bool require_all_eligible = false)
	{
		for (unsigned i = 0; i < 300; ++i) {
			if (_assert_arrangement(what, min_clients,
			                        require_all_eligible)) return true;
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Independent non-resizeable fixture check. It deliberately never
	 * touches _classify, the slots[]/eligible counts, or the step-12
	 * min_clients logic: the fixture is PROVEN excluded from the
	 * Master-Stack arrangement by asserting it stays exactly at its
	 * floating geometry (D16W.5: non-resizeable windows always float)
	 * and that window_list reports resizeable=no. Returns false and
	 * logs the mismatch otherwise.
	 */
	bool _assert_fixture(char const *what)
	{
		_window_list.update();
		_window_layout.update();

		Wl_entry const wl =
			window_list_find_prefix(_window_list.node(), FIXTURE_PREFIX);
		if (!wl.found) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - fixture missing from window_list");
			return false;
		}
		if (wl.resizeable) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - fixture reported resizeable=yes; it must be "
			              "resizeable=no (never opens the Gui info ROM)");
			return false;
		}

		Geom want;
		want.x = FIXTURE_X; want.y = FIXTURE_Y;
		want.w = FIXTURE_W; want.h = FIXTURE_H;
		want.valid = true;
		Geom const g = _geom(FIXTURE_PREFIX);
		if (!same_geom(g, want)) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - fixture must float at exactly (850,560) "
			              "160x160 (non-resizeable floats, D16W.5)");
			log_geom("native-wm-tiling-probe", "got ", g);
			log_geom("native-wm-tiling-probe", "want", want);
			return false;
		}
		Genode::log("native-wm-tiling-probe: fixture floats at (", g.x,
		            ",", g.y, ") ", g.w, "x", g.h,
		            " (resizeable=no, not tiled)");
		return true;
	}

	bool _wait_fixture(char const *what)
	{
		for (unsigned i = 0; i < 600; ++i) {
			if (_assert_fixture(what)) return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool _wait_geometries_unchanged(Geom const &demo, Geom const &pkg,
	                                char const *what)
	{
		for (unsigned i = 0; i < 300; ++i) {
			Geom const d = _geom(DEMO_PREFIX);
			Geom const p = _geom(GUI_PREFIX);
			if (same_geom(d, demo) && same_geom(p, pkg)) return true;
			_timer.msleep(100);
		}
		Genode::error("native-wm-tiling-probe: FAIL ", what,
		              " - geometry changed across the mode switch");
		return false;
	}

	bool _set_mode(char const *mode, char const *what)
	{
		if (!_cfg.set("wm.layout.mode", mode)) {
			Genode::error("native-wm-tiling-probe: FAIL ", what,
			              " - configd refused wm.layout.mode=", mode,
			              " (TODO(native-wm/M4): the key must be registered "
			              "in sponge_configd's closed registry per D16W.11)");
			return false;
		}
		return true;
	}

	void run()
	{
		Genode::log("native-wm-tiling-probe: starting native tiling sequence");

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
		                           "native-wm-tiling-probe") < 0) {
			_fail("window_list does not match protocol v1 shape");
			return;
		}

		_margins_rom.update();
		if (!_margins_rom.valid()) {
			_fail("decorator_margins ROM missing (sponge_decorator must report it)");
			return;
		}
		_margins = read_margins(_margins_rom.node(), "native-wm-tiling-probe");
		if (!_margins.valid) {
			_fail("decorator_margins malformed");
			return;
		}

		/* ---------- step 1: launch + eligibility ---------- */
		if (!_pkg.request_ok("install", "pkg_gui_demo")) {
			_fail("install pkg_gui_demo did not answer ok");
			return;
		}
		if (!_pkg.request_ok("launch", "pkg_gui_demo")) {
			_fail("launch pkg_gui_demo did not answer ok");
			return;
		}

		Geom demo, pkg;
		bool demo_ok = false;
		for (unsigned i = 0; i < 600; ++i) {
			demo = _geom(DEMO_PREFIX);
			if (demo.valid) { demo_ok = true; break; }
			_timer.msleep(100);
		}
		if (!demo_ok) {
			_fail("demo window never appeared in window_layout");
			return;
		}
		if (!_wait_geom(GUI_PREFIX, pkg, 50, 320, 320, 240,
		                "pkg_gui_demo placement")) {
			_fail("pkg_gui_demo never placed at (50,320,320,240)");
			return;
		}

		{
			Slot slots[2] { };
			int const n = _classify(slots);
			int eligible = 0;
			for (int i = 0; i < n; ++i)
				if (slots[i].resizeable) ++eligible;
			if (eligible < 2) {
				Genode::error("native-wm-tiling-probe: FAIL Master-Stack "
				              "acceptance requires >= 2 resizeable windows; "
				              "window_list reports ", eligible);
				for (int i = 0; i < n; ++i)
					Genode::log("native-wm-tiling-probe:   ", slots[i].prefix,
					            " resizeable=", slots[i].resizeable ? "yes" : "no");
				_env.parent().exit(1);
				return;
			}
		}
		if (!_wait_fixture("step 1 fixture")) {
			_fail("fixture is not resizeable=no + floating at "
			      "(850,560) 160x160");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 1] fixture floats at "
		            "(850,560) 160x160 (resizeable=no, excluded from "
		            "Master-Stack)");
		Genode::log("native-wm-tiling-probe: [step 1] two clients live: demo (",
		            demo.x, ",", demo.y, ") ", demo.w, "x", demo.h,
		            " / pkg (", pkg.x, ",", pkg.y, ") ", pkg.w, "x", pkg.h);

		/* ---------- step 2: mode floating -> tiling + reflow ---------- */
		if (!_set_mode("tiling", "step 2"))
			{ _fail("wm.layout.mode=tiling write refused"); return; }
		if (!_wait_arrangement("step 2 reflow")) {
			_fail("tiling-mode arrangement mismatch after mode switch");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 2] Master-Stack "
		            "arrangement applied");

		/* ---------- step 3: minimize (tasklist) -> parked + reflow ---------- */
		_click_tasklist(GUI_PREFIX, "step 3: pkg_gui_demo not tracked");
		if (!_wait_parked(GUI_PREFIX, "step 3 minimize")) {
			_fail("minimize in tiling mode did not park the window");
			return;
		}
		if (!_wait_arrangement("step 3 reflow")) {
			_fail("arrangement mismatch after minimize reflow");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 3] parked + reflowed");

		/* ---------- step 4: restore (tasklist) -> re-enters + reflow ---------- */
		_click_tasklist(GUI_PREFIX, "step 4: pkg_gui_demo not tracked");
		if (!_wait_visible(GUI_PREFIX, "step 4 restore")) {
			_fail("restore did not make pkg_gui_demo visible");
			return;
		}
		if (!_wait_arrangement("step 4 reflow")) {
			_fail("restore in tiling mode did not re-enter the arrangement");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 4] restored into the "
		            "arrangement");

		/* ---------- step 5: minimize the master -> slot passes on ---------- */
		_click_tasklist(DEMO_PREFIX, "step 5: demo not tracked");
		if (!_wait_parked(DEMO_PREFIX, "step 5 minimize master")) {
			_fail("minimize of the master did not park it");
			return;
		}
		if (!_wait_arrangement("step 5 reflow")) {
			_fail("master slot did not pass to the next eligible window");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 5] master slot passed on");

		/* ---------- step 6: restore the master -> creation order ---------- */
		_click_tasklist(DEMO_PREFIX, "step 6: demo not tracked");
		if (!_wait_visible(DEMO_PREFIX, "step 6 restore master")) {
			_fail("restored master never became visible");
			return;
		}
		if (!_wait_arrangement("step 6 reflow")) {
			_fail("restored master did not reclaim the master slot");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 6] creation-order "
		            "arrangement restored");

		/* ---------- step 7: maximize (chrome fullscreen) ---------- */
		{
			Geom const band = _geom(GUI_PREFIX);
			Pt const btn = chrome_button_center(band, _margins,
			                                   Chrome_button::FULLSCREEN);
			Genode::log("native-wm-tiling-probe: [step 7] chrome fullscreen (",
			            btn.x, ",", btn.y, ")");
			emit_qmp_click(btn.x, btn.y);

			Geom const want = _maximized_content();
			bool ok = false;
			for (unsigned i = 0; i < 600; ++i) {
				if (same_geom(_geom(GUI_PREFIX), want)) { ok = true; break; }
				_timer.msleep(100);
			}
			if (!ok) {
				log_geom("native-wm-tiling-probe", "maximized got",
				         _geom(GUI_PREFIX));
				log_geom("native-wm-tiling-probe", "maximized want", want);
				_fail("maximize in tiling mode did not fill the work area");
				return;
			}
			if (!_wait_arrangement("step 7 reflow")) {
				_fail("arrangement mismatch after maximize reflow");
				return;
			}
			Genode::log("native-wm-tiling-probe: [step 7] maximized, left the "
			            "arrangement, reflowed");
		}

		/* ---------- step 8: maximize-restore -> arrangement slot back ---------- */
		{
			Geom const maxed = _geom(GUI_PREFIX);
			Pt const btn = chrome_button_center(maxed, _margins,
			                                   Chrome_button::FULLSCREEN);
			Genode::log("native-wm-tiling-probe: [step 8] chrome fullscreen "
			            "toggle (", btn.x, ",", btn.y, ")");
			emit_qmp_click(btn.x, btn.y);
			if (!_wait_not_maximized(GUI_PREFIX, "step 8 maximize-restore")) {
				_fail("maximize-restore left pkg_gui_demo maximized");
				return;
			}
			if (!_wait_arrangement("step 8 reflow")) {
				_fail("maximize-restore did not return the arrangement slot");
				return;
			}
			Genode::log("native-wm-tiling-probe: [step 8] back in the "
			            "arrangement");
		}

		/* ---------- step 9: tiling -> floating keeps geometries ---------- */
		{
			Geom const d0 = _geom(DEMO_PREFIX);
			Geom const p0 = _geom(GUI_PREFIX);
			if (!_set_mode("floating", "step 9"))
				{ _fail("wm.layout.mode=floating write refused"); return; }
			if (!_wait_geometries_unchanged(d0, p0, "step 9 retention")) {
				_fail("leaving tiling mode changed window geometry");
				return;
			}
			if (!_wait_fixture("step 9 fixture retention")) {
				_fail("leaving tiling mode moved the floating fixture");
				return;
			}
			Genode::log("native-wm-tiling-probe: [step 9] floating mode keeps "
			            "current geometry (no hidden history)");
		}

		/* ---------- step 10: floating -> tiling reflows again ---------- */
		if (!_set_mode("tiling", "step 10"))
			{ _fail("wm.layout.mode=tiling write refused"); return; }
		if (!_wait_arrangement("step 10 reflow")) {
			_fail("arrangement mismatch after re-entering tiling");
			return;
		}
		Genode::log("native-wm-tiling-probe: [step 10] tiling arrangement "
		            "reapplied");

		/* ---------- step 11: rules-drift soak (>=20 cycles, §7.2) ---------- */
		Genode::log("native-wm-tiling-probe: [step 11] soak: ", soak_cycles,
		            " cycles of rules write + restore + mode switch");
		for (unsigned c = 1; c <= soak_cycles; ++c) {
			_click_tasklist(GUI_PREFIX, "soak: pkg_gui_demo not tracked");
			if (!_wait_parked(GUI_PREFIX, "soak minimize")) {
				Genode::error("native-wm-tiling-probe: FAIL soak cycle ", c,
				              " minimize");
				_env.parent().exit(1);
				return;
			}
			_click_tasklist(GUI_PREFIX, "soak: pkg_gui_demo not tracked");
			if (!_wait_arrangement("soak restore")) {
				Genode::error("native-wm-tiling-probe: FAIL soak cycle ", c,
				              " restore");
				_env.parent().exit(1);
				return;
			}
			if (!_set_mode("floating", "soak"))
				{ _fail("soak: floating write refused"); return; }
			if (!_set_mode("tiling", "soak"))
				{ _fail("soak: tiling write refused"); return; }
			if (!_wait_arrangement("soak reflow")) {
				Genode::error("native-wm-tiling-probe: FAIL soak cycle ", c,
				              " - window_layout drifted from the expected "
				              "arrangement (rules drift)");
				_env.parent().exit(1);
				return;
			}
			if (c % 5 == 0)
				Genode::log("native-wm-tiling-probe: [step 11] soak cycle ",
				            c, "/", soak_cycles, " clean");
		}
		Genode::log("native-wm-tiling-probe: [step 11] soak clean (",
		            soak_cycles, " cycles, no drift)");
		emit_qmp_sync();

		/* ---------- step 12: close the master -> slot passes on ---------- */
		{
			Geom const d = _geom(DEMO_PREFIX);
			Pt const btn = chrome_button_center(d, _margins,
			                                   Chrome_button::CLOSE);
			Genode::log("native-wm-tiling-probe: [step 12] chrome closer on "
			            "the master (", btn.x, ",", btn.y, ")");
			if (!_goto_pos(btn.x, btn.y)) {
				_fail("could not position pointer on the chrome closer");
				return;
			}
			emit_qmp_press(btn.x, btn.y);

			bool gone = false;
			for (unsigned i = 0; i < 600; ++i) {
				_window_list.update();
				if (_window_list.valid()
				 && !window_list_find_prefix(_window_list.node(),
				                            DEMO_PREFIX).found) {
					gone = true;
					break;
				}
				_timer.msleep(100);
			}
			if (!gone) {
				_fail("close of the master did not destroy its window");
				return;
			}
			if (!_wait_arrangement("step 12 reflow", 1, true)) {
				_fail("remaining window did not become master after close");
				return;
			}
			Genode::log("native-wm-tiling-probe: [step 12] master closed, "
			            "remaining window reflowed to master");
		}

		Genode::log("native-wm-tiling-probe: PASS");
		_env.parent().exit(0);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	/*
	 * panel_height must match the nitpicker panel domain in
	 * run/sponge-wm-native-tiling.run (proven height 28); soak_cycles
	 * is the §7.2 rules-drift soak length (default 20).
	 */
	unsigned panel_h = 28, soak = 20;
	Genode::Attached_rom_dataspace config { env, "config" };
	if (config.valid()) {
		panel_h = config.node().attribute_value("panel_height", 28u);
		soak    = config.node().attribute_value("soak_cycles", 20u);
	}

	static Native_wm_tiling_probe probe { env, panel_h, soak };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
