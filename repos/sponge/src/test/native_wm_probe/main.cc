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
 *   step 6  fixture up: test/native_wm_fixture_client's window is in
 *           window_list as NON-resizeable (it never signals the Gui
 *           info ROM), placed at its static floating assign geometry
 *           (850,560,160x160) and carried in window_chrome.
 *   step 7  fixture title command: <fixture_cmd><title .../> updates
 *           the window_list title (the command channel is live).
 *   step 8  fixture destroy: <fixture_cmd><destroy_view/> removes the
 *           window from window_list AND window_chrome (absence).
 *   step 9  fixture recreate: <fixture_cmd><create_view .../> in the
 *           SAME Gui session restores the window under the SAME wid
 *           with a fresh title, the assign geometry again, chrome
 *           re-attached (window_chrome carries the wid again) and the
 *           session-derived resizeable="no" state re-applied; a fresh
 *           chrome-close click is then dispatched and the run script
 *           gates on sponge_wm's "close requested" audit line (the
 *           sticky-close latch was reset by the resurrection).
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

/*
 * The fixture window (test/native_wm_fixture_client): static floating
 * geometry pinned by the sponge_layouter assign in
 * run/sponge-wm-native.run. The fixture never signals the Gui info
 * ROM, so the session-derived resizeable state stays "no".
 */
char const *const FIXTURE_LABEL = "native_wm_fixture_client";
char const *const FIXTURE_TITLE_0 = "WM Fixture";
char const *const FIXTURE_TITLE_1 = "WM Fixture Renamed";
char const *const FIXTURE_TITLE_2 = "WM Fixture Reborn";

int const FIXTURE_X = 850, FIXTURE_Y = 560;
unsigned const FIXTURE_W = 160, FIXTURE_H = 160;


struct Native_wm_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "native-wm-probe" };
	Event::Connection _event { _env, "native-wm-probe" };

	Genode::Attached_rom_dataspace _window_list   { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };
	Genode::Attached_rom_dataspace _margins_rom   { _env, "decorator_margins" };
	Genode::Attached_rom_dataspace _window_chrome { _env, "window_chrome" };

	Genode::Expanding_reporter _fixture_cmd { _env, "fixture_cmd", "fixture_cmd" };

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

	struct Fixture_entry {
		unsigned long    wid { 0 };
		Genode::String<64> title { };
		unsigned         w { 0 }, h { 0 };
		bool             resizeable { false };
		bool             found { false };
	};

	Fixture_entry _fixture_entry()
	{
		Fixture_entry e;
		_window_list.update();
		if (!_window_list.valid()) return e;
		Genode::size_t const prefix_len = Genode::strlen(FIXTURE_LABEL);
		_window_list.node().for_each_sub_node("window",
			[&] (Genode::Node const &n) {
				if (e.found) return;
				Genode::String<256> const l =
					n.attribute_value("label", Genode::String<256>());
				if (Genode::strcmp(l.string(), FIXTURE_LABEL, prefix_len) != 0)
					return;
				e.wid        = n.attribute_value("id", 0UL);
				e.title      = n.attribute_value("title",
				                                 Genode::String<64>());
				e.w          = n.attribute_value("width",  0U);
				e.h          = n.attribute_value("height", 0U);
				e.resizeable = n.attribute_value("resizeable", false);
				e.found      = true;
			});
		return e;
	}

	bool _fixture_chrome_present(unsigned long wid)
	{
		_window_chrome.update();
		if (!_window_chrome.valid()) return false;
		bool found = false;
		_window_chrome.node().for_each_sub_node("window",
			[&] (Genode::Node const &n) {
				if (found) return;
				if (n.attribute_value("wid", 0UL) == wid)
					found = true;
			});
		return found;
	}

	Geom _fixture_geom()
	{
		Geom g;
		_window_layout.update();
		if (!_window_layout.valid()) return g;
		find_layout_window_by_prefix(_window_layout.node(), FIXTURE_LABEL,
		                             g);
		return g;
	}

	void _fixture_title_cmd(char const *text)
	{
		_fixture_cmd.generate_xml([&] (Genode::Xml_generator &g) {
			g.node("title", [&] { g.attribute("text", text); });
		});
	}

	void _fixture_destroy_cmd()
	{
		_fixture_cmd.generate_xml([&] (Genode::Xml_generator &g) {
			g.node("destroy_view", [&] { });
		});
	}

	void _fixture_create_cmd(char const *title)
	{
		_fixture_cmd.generate_xml([&] (Genode::Xml_generator &g) {
			g.node("create_view", [&] { g.attribute("title", title); });
		});
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
		Genode::log("native-wm-probe: [step 1] waiting for window_list ROM + demo window");
		bool ready = false;
		for (unsigned i = 0; i < 600; ++i) {
			_window_list.update();
			if (_window_list.valid()
			 && window_list_has(_window_list.node(), DEMO_LABEL)) {
				ready = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!ready) {
			_fail("demo window missing from window_list (waited ~60s)");
			return;
		}

		int const n = check_window_list_shape(_window_list.node(),
		                                      "native-wm-probe");
		if (n < 0) {
			_fail("window_list does not match protocol v1 shape");
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

		/* ---------- step 6: fixture up (non-resizeable, assign geom) ---- */
		Genode::log("native-wm-probe: [step 6] waiting for fixture window");
		Fixture_entry fe;
		Geom fg;
		bool fixture_up = false;
		for (unsigned i = 0; i < 200; ++i) {
			fe = _fixture_entry();
			fg = _fixture_geom();
			if (fe.found && fg.valid && _fixture_chrome_present(fe.wid)) {
				fixture_up = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!fixture_up) {
			_fail("fixture window missing from window_list/window_chrome");
			return;
		}
		unsigned long const fwid = fe.wid;
		if (fwid == 0) {
			_fail("fixture window_list entry has no id (wid)");
			return;
		}
		if (fe.resizeable) {
			_fail("fixture reports resizeable=yes (must be no: no info sigh)");
			return;
		}
		if (Genode::strcmp(fe.title.string(), FIXTURE_TITLE_0) != 0) {
			_fail("fixture window_list title is not the initial title");
			return;
		}
		if (fe.w != FIXTURE_W || fe.h != FIXTURE_H) {
			_fail("fixture window_list size is not the assigned size");
			return;
		}
		Geom const fwant { FIXTURE_X, FIXTURE_Y, FIXTURE_W, FIXTURE_H, true };
		if (!same_geom(fg, fwant)) {
			_fail_geom("fixture not at the assigned geometry", fg, fwant);
			return;
		}
		Genode::log("native-wm-probe: [step 6] fixture up (wid=", fwid,
		            ", resizeable=no)");

		/* ---------- step 7: fixture title command reaches window_list --- */
		_fixture_title_cmd(FIXTURE_TITLE_1);
		bool renamed = false;
		for (unsigned i = 0; i < 100; ++i) {
			fe = _fixture_entry();
			if (fe.found
			 && Genode::strcmp(fe.title.string(), FIXTURE_TITLE_1) == 0) {
				renamed = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!renamed) {
			_fail("fixture title command not reflected in window_list");
			return;
		}
		Genode::log("native-wm-probe: [step 7] fixture title renamed");

		/* ---------- step 8: destroy_view removes window_list/chrome ----- */
		Pt const close_pt = chrome_button_center(fg, m, Chrome_button::CLOSE);
		for (unsigned attempt = 1; attempt <= 3; ++attempt) {
			_inject_motion(close_pt.x, close_pt.y);
			_timer.msleep(300);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Press { Input::BTN_LEFT });
			});
			_timer.msleep(100);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Release { Input::BTN_LEFT });
			});
			_timer.msleep(300);
		}
		Genode::log("native-wm-probe: [step 8] pre-destroy close dispatched "
		            "(best effort: arms the sticky-close latch)");
		_timer.msleep(1500);

		_fixture_destroy_cmd();
		bool destroyed = false;
		for (unsigned i = 0; i < 100; ++i) {
			fe = _fixture_entry();
			Geom const g2 = _fixture_geom();
			if (!fe.found && !g2.valid && !_fixture_chrome_present(fwid)) {
				destroyed = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!destroyed) {
			_fail("fixture still present after destroy_view "
			      "(window_list/window_chrome)");
			return;
		}
		Genode::log("native-wm-probe: [step 8] fixture destroyed "
		            "(absent from window_list/window_chrome)");

		/* ---------- step 9: create_view resurrects the same wid --------- */
		_fixture_create_cmd(FIXTURE_TITLE_2);
		bool recreated = false;
		for (unsigned i = 0; i < 200; ++i) {
			fe = _fixture_entry();
			fg = _fixture_geom();
			if (fe.found && fg.valid && _fixture_chrome_present(fwid)) {
				recreated = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!recreated) {
			_fail("fixture did not reappear after create_view");
			return;
		}
		if (fe.wid != fwid) {
			Genode::error("native-wm-probe: FAIL recreate changed wid: got ",
			              fe.wid, " want ", fwid);
			_env.parent().exit(1);
			return;
		}
		if (Genode::strcmp(fe.title.string(), FIXTURE_TITLE_2) != 0) {
			_fail("recreated fixture title is not the create_view title");
			return;
		}
		if (fe.resizeable) {
			_fail("recreated fixture reports resizeable=yes "
			      "(session-derived no broken)");
			return;
		}
		if (fe.w != FIXTURE_W || fe.h != FIXTURE_H) {
			_fail("recreated fixture window_list size is not fresh");
			return;
		}
		if (!same_geom(fg, fwant)) {
			_fail_geom("recreated fixture not re-placed at the assign geometry",
			           fg, fwant);
			return;
		}
		if (!_fixture_chrome_present(fwid)) {
			_fail("window_chrome did not re-attach for the recreated fixture");
			return;
		}
		Genode::log("native-wm-probe: [step 9] fixture recreated (same wid ",
		            fwid, ", fresh title/geometry, chrome re-attached)");

		/*
		 * Fresh close request on the recreated window. The best-effort
		 * pre-destroy close above armed the sticky-close latch; if it
		 * landed, this second application (the run script's "close
		 * requested" gate) proves the resurrection reset the latch.
		 */
		Pt const close2 = chrome_button_center(fg, m, Chrome_button::CLOSE);
		Genode::log("native-wm-probe: [step 9] close dispatched at (",
		            close2.x, ",", close2.y, ")");
		for (unsigned attempt = 1; attempt <= 3; ++attempt) {
			_inject_motion(close2.x, close2.y);
			_timer.msleep(300);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Press { Input::BTN_LEFT });
			});
			_timer.msleep(100);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Release { Input::BTN_LEFT });
			});
			_timer.msleep(400);
		}
		_timer.msleep(5000);

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
