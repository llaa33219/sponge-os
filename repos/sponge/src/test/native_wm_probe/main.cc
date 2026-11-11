/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_probe — native-WM boot / window-lifecycle smoke probe
 * (Criterion 16, M1 gate: docs/plans/phase16-sponge-wm.md).
 *
 * Scenario: run/sponge-wm-native.run (base-linux or base-sel4, headless).
 * Topology: the NATIVE trio (sponge_wm + sponge_layouter +
 * sponge_decorator) in place of the upstream wm/window_layouter/
 * decorator, with sponge-de's demo window as the client. No upstream
 * component is present in this topology (D16W.7 parity rule).
 *
 * What this probe asserts (reusable protocol-v1 assertions from
 * native_wm_common/):
 *
 *   step 1  boot: window_list ROM appears and matches protocol v1
 *           shape (docs/05-sponge-de.md §4.6 — the M1 "window_list
 *           matches protocol v1" gate).
 *   step 2  lifecycle created/placed: the demo window is listed in
 *           window_list (label + title identity) and placed by
 *           sponge_layouter at the configured content geometry.
 *   step 3  render: the composited content center paints the demo
 *           widget background (Capture check — a stale window_layout
 *           report must not pass, misleading_success_output).
 *   step 4  input/move: a synthetic title-bar drag (nitpicker Event
 *           injection, the proven wm_probe pattern) moves the window
 *           by exactly the drag delta (user-directed floating,
 *           D16W.5).
 *   step 5  render after move: the new content center paints the
 *           window and the vacated point is back to background.
 *
 * TODO(native-wm): the title-bar center is derived from the
 * decorator_margins report shape the run topology wires
 * (sponge_decorator -> decorator_margins). If the native chrome's
 * title band differs from "the top margin band", update
 * Native_wm::title_center (native_wm_probe_util.h) in one place.
 *
 * The probe never prints PASS without every check above executing;
 * any miss logs "native-wm-probe: FAIL <reason>" and exits 1 (the
 * run script's bounded run_genode_until then fails loud).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <event_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <timer_session/connection.h>

#include "native_wm_protocol.h"
#include "native_wm_probe_util.h"

namespace {

using namespace Native_wm;

/*
 * The demo window's configured content geometry — must match the
 * sponge_layouter initial rules in run/sponge-wm-native.run
 * (the run/sponge-wm.run proven placement).
 */
int const DEMO_X = 192, DEMO_Y = 172;
unsigned const DEMO_W = 640, DEMO_H = 480;

char const *const DEMO_LABEL = "sponge-de -> Sponge DE Demo";
char const *const DEMO_TITLE = "Sponge DE Demo";

int const DRAG_DX = 100, DRAG_DY = 100;


struct Native_wm_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "native-wm-probe" };
	Event::Connection _event { _env, "native-wm-probe" };

	Genode::Attached_rom_dataspace _window_list   { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };
	Genode::Attached_rom_dataspace _margins_rom   { _env, "decorator_margins" };

	Capture_probe _capture;

	Native_wm_probe(Genode::Env &env)
	: _env(env), _capture(env, 1024, 768)
	{ }

	void _fail(char const *reason)
	{
		Genode::error("native-wm-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	void _fail_geom(char const *reason, Geom const &got, Geom const &want)
	{
		Genode::error("native-wm-probe: FAIL ", reason);
		log_geom("native-wm-probe", "got ", got);
		log_geom("native-wm-probe", "want ", want);
		_env.parent().exit(1);
	}

	Geom _demo_geom(bool *focused = nullptr)
	{
		Geom g;
		_window_layout.update();
		if (!_window_layout.valid()) return g;
		find_layout_window(_window_layout.node(), DEMO_LABEL, DEMO_TITLE,
		                   g, focused);
		return g;
	}

	void _inject_motion(int x, int y)
	{
		_event.with_batch([&](Event::Session_client::Batch &batch) {
			batch.submit(Input::Absolute_motion{ x, y });
		});
	}

	void run()
	{
		Genode::log("native-wm-probe: starting native WM smoke sequence");

		/* ---------- step 1: boot + window_list protocol v1 shape ---------- */
		Genode::log("native-wm-probe: [step 1] waiting for window_list ROM");
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

		int const n = check_window_list_shape(_window_list.node(),
		                                      "native-wm-probe");
		if (n < 0) {
			_fail("window_list does not match protocol v1 shape");
			return;
		}
		if (!window_list_has(_window_list.node(), DEMO_LABEL)) {
			_fail("demo window missing from window_list");
			return;
		}
		Genode::log("native-wm-probe: [step 1] window_list matches protocol v1");

		/* ---------- step 2: lifecycle created + placed ---------- */
		Genode::log("native-wm-probe: [step 2] waiting for demo window placement");
		Geom g;
		bool placed = false;
		for (unsigned i = 0; i < 600; ++i) {
			g = _demo_geom();
			if (g.valid && g.x == DEMO_X && g.y == DEMO_Y
			 && g.w == DEMO_W && g.h == DEMO_H) {
				placed = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!placed) {
			Geom want { DEMO_X, DEMO_Y, DEMO_W, DEMO_H, true };
			_fail_geom("demo window not at the configured content geometry",
			           g, want);
			return;
		}
		Genode::log("native-wm-probe: [step 2] demo window placed at (",
		            g.x, ",", g.y, ") ", g.w, "x", g.h);

		/* ---------- step 3: render ---------- */
		bool rendered = false;
		for (unsigned i = 0; i < 200; ++i) {
			_timer.msleep(100);
			int const cx = g.x + (int)g.w / 2;
			int const cy = g.y + (int)g.h / 2;
			if (!_capture.in_bounds(cx, cy)) break;
			if (pixel_is_demo_window(_capture.at(cx, cy))) {
				rendered = true;
				break;
			}
		}
		if (!rendered) {
			_fail("demo content center pixel is not the demo window background");
			return;
		}
		Genode::log("native-wm-probe: [step 3] demo window rendered");

		/* ---------- step 4: synthetic title-bar drag moves the window ---------- */
		_margins_rom.update();
		if (!_margins_rom.valid()) {
			_fail("decorator_margins ROM missing (sponge_decorator must report it)");
			return;
		}
		Chrome_margins const m = read_margins(_margins_rom.node(),
		                                     "native-wm-probe");
		if (!m.valid || m.top == 0) {
			_fail("decorator_margins invalid or zero top (no title band)");
			return;
		}

		Pt const tc = title_center(g, m);
		Genode::log("native-wm-probe: [step 4] synthetic drag from title center (",
		            tc.x, ",", tc.y, ") by (", DRAG_DX, ",", DRAG_DY, ")");

		for (unsigned attempt = 1; attempt <= 3; ++attempt) {
			_inject_motion(tc.x, tc.y);
			_timer.msleep(400);

			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Press { Input::BTN_LEFT });
			});
			_timer.msleep(100);

			/* jiggle on the title band while held, then the drag delta */
			_inject_motion(tc.x + 2, tc.y + 1);
			_timer.msleep(200);
			_inject_motion(tc.x + DRAG_DX, tc.y + DRAG_DY);
			_timer.msleep(200);
			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Release { Input::BTN_LEFT });
			});

			bool moved = false;
			for (unsigned i = 0; i < 100; ++i) {
				Geom const cur = _demo_geom();
				if (cur.valid && (cur.x != g.x || cur.y != g.y)) {
					g = cur;
					moved = true;
					break;
				}
				_timer.msleep(100);
			}

			if (moved && g.x == DEMO_X + DRAG_DX && g.y == DEMO_Y + DRAG_DY) {
				Genode::log("native-wm-probe: [step 4] window moved to (",
				            g.x, ",", g.y, ") — exact drag delta applied");
				break;
			}
			if (moved) {
				Geom want { DEMO_X + DRAG_DX, DEMO_Y + DRAG_DY, DEMO_W, DEMO_H, true };
				_fail_geom("title-bar drag moved the window by the wrong delta",
				           g, want);
				return;
			}
			if (attempt == 3) {
				_fail("title-bar drag did not move the window (3 attempts)");
				return;
			}
			Genode::log("native-wm-probe: [step 4] drag attempt ", attempt,
			            " did not move the window, retrying");
		}

		/* ---------- step 5: render after move (vacated point cleared) ---------- */
		int const new_cx = g.x + (int)g.w / 2;
		int const new_cy = g.y + (int)g.h / 2;
		int const vac_x  = DEMO_X + 10;
		int const vac_y  = DEMO_Y + 10;

		bool confirmed = false;
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(100);
			if (!_capture.in_bounds(new_cx, new_cy)
			 || !_capture.in_bounds(vac_x, vac_y)) break;
			bool const at_new = pixel_is_demo_window(_capture.at(new_cx, new_cy));
			bool const vacated = pixel_is_background(_capture.at(vac_x, vac_y));
			if (at_new && vacated) { confirmed = true; break; }
		}
		if (!confirmed) {
			_fail("post-move pixel check failed (window not at new center or "
			      "vacated point not background)");
			return;
		}
		Genode::log("native-wm-probe: [step 5] post-move pixels confirmed");

		Genode::log("native-wm-probe: PASS");
		_env.parent().exit(0);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Native_wm_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
