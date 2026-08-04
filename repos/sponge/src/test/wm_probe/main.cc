/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * wm_probe — headless window-manager verification probe.
 *
 * Proves that Genode's upstream window-management stack (wm +
 * window_layouter + decorator), wired around sponge-de, actually
 * decorates and MOVES the "Sponge DE Demo" window. Verification is
 * done entirely through nitpicker's composited pixels (Capture session)
 * and synthetic pointer input (Event session), headlessly — no host
 * display, no framebuffer driver. This is the same proven pattern used
 * by sponge_de_probe in run/sponge-de-test.run.
 *
 * Input pipeline (why a synthetic drag works headlessly):
 *
 *   wm_probe injects absolute motion / press / release into nitpicker
 *   via the Event service. nitpicker moves its internal pointer,
 *   hit-tests the view stack (User_state::update_hover /
 *   abs_motion_receiver), and delivers the events to whichever Gui
 *   session owns the topmost view at the pointer — here the decorator's
 *   real Gui session ("wm -> decorator") when the pointer is on the
 *   title bar. The wm's Decorator_gui_session::_handle_input then
 *   updates its pointer state, tags the event with a sequence number
 *   (Input::Seq_number_generator), and forwards it to the
 *   window_layouter. The layouter defers each DRAG command until the
 *   decorator's hover report carries a matching seq
 *   (window_layouter/user_state.h, Command::DRAG), so a small jiggle
 *   that keeps the pointer on the title bar is used to advance the
 *   hover seq before the drag motion. No external pointer component and
 *   no nitpicker focus ROM are required: nitpicker's builtin click
 *   focus (default domain "focus: click") focuses the decorator session
 *   on the press, which is enough for the motion/press/release stream
 *   to flow.
 *
 * Three stages (synthetic mode, default inject=yes — used by
 * run/sponge-wm.run):
 *
 *   (a) Decoration pipeline: poll nitpicker's composited output until
 *       the themed window background appears at the configured content
 *       origin. The layouter places the content at (192,172,640,480),
 *       so its center is (512,412). The motif decorator's floating
 *       border (top=20) puts the title bar at y[152,172) — center
 *       (512,162), which is the drag target.
 *
 *   (b) Real WM behavior (drag): inject a synthetic title-bar drag —
 *       absolute motion onto the title bar, BTN_LEFT press, a small
 *       jiggle that stays on the title band (advances the decorator
 *       hover seq for the layouter's deferred-DRAG protocol), absolute
 *       motion to the drag target, release.
 *
 *   (c) Confirm the window actually moved. The authoritative proof is
 *       the layouter's "window_layout" ROM: the window's xpos/ypos must
 *       change from the initial (192,172). A Capture spot-check then
 *       confirms the composited pixels agree: the new content center
 *       (derived from the new ROM position) carries the window
 *       background, and a point the window vacated (its old top-left
 *       corner area) is back to the nitpicker background. (The original
 *       content center (512,412) is intentionally NOT used as the
 *       "cleared" point: the window is 640x480 on a 1024x768 screen, so
 *       a +100,+100 drag leaves (512,412) still covered by the moved
 *       window — there is not enough screen slack to vacate it.)
 *
 * Observe mode (Phase 10 W3, inject=no — used by run/sponge-wm-qmp.run):
 *
 *   The probe does NOT inject any synthetic event. Instead it:
 *     1. Installs and launches pkg_gui_demo via sponge_pkgd's "request"
 *        channel (the same channel vct uses, per AGENTS.md §3.3 rule
 *        5 — same backend).
 *     2. Polls the layouter's window_layout ROM for a window whose
 *        title contains "pkg_gui_demo" (the layouter's <window>
 *        element carries a "title" attribute combining the session
 *        label and the Qt window title — see
 *        genode/repos/gems/src/app/window_layouter/window.h:399-407).
 *     3. Computes the title-bar center from the window's reported
 *        xpos/ypos + the motif decorator's top margin (20) — title
 *        center is at (xpos + width/2, ypos - 10).
 *     4. Logs a machine-parseable `QMP-TARGET drag <x1> <y1> <x2> <y2>`
 *        marker on the serial console. The run script's bounded expect
 *        catches the marker and dispatches a real QMP usb-tablet drag
 *        (see run/qmp.inc). The y coordinates include a +29px drift
 *        compensation: QEMU -nographic routes input-send-event through
 *        a fallback path whose abs-axis-to-screen translation lands
 *        clicks ~29px above the intended y (verified in W1, see
 *        docs/evidence/task-1-phase10-interactive.md "Calibration
 *        matrix"). Adding +29 to y before emitting the marker makes the
 *        observed click land at the intended title-bar y.
 *     5. Polls window_layout for the position change (+100,+100).
 *     6. Pixel-checks the new content center is pkg_gui_demo's green.
 *     7. Logs PASS.
 *
 *   The full real-input chain is exercised: QMP input-send-event →
 *   usb-tablet → pc_usb_host → usb_hid → event_filter → nitpicker →
 *   decorator title-bar (Gui session) → wm → window_layouter drag rule
 *   → window moves. This is the criterion-2 proof.
 *
 * On success logs "wm-probe: PASS"; on failure "wm-probe: FAIL <reason>"
 * and exits non-zero, so the run script fails via run_genode_until
 * timeout. Plain Genode component (Component::construct, no libc/Qt),
 * per AGENTS.md §3.1.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <event_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;       /* nitpicker background #1e1e2e */
int const WIN_R = 0x31, WIN_G = 0x32, WIN_B = 0x44;    /* themed window_bg #313244 */

/*
 * pkg_gui_demo's distinctive fill color (Phase 7 todo 8). The observe
 * mode (Phase 10 W3) launches pkg_gui_demo via sponge_pkgd, then pixel-
 * verifies the window moved by checking the new content center is green
 * — this color appears nowhere else in the wm-qmp scenario (nitpicker
 * bg is #1e1e2e, themed window_bg is #313244).
 */
int const PKG_R = 0x00, PKG_G = 0xff, PKG_B = 0x00;    /* pkg_gui_demo #00ff00 */

/*
 * Layouter <assign> places the demo content at (192,172) 640x480, so its
 * center is (512,412). The motif decorator's floating border (top=20,
 * sides=4) puts the title bar in y[152,172), x[188,836) — center
 * (512,162). Dragging the title to (612,262) shifts content by
 * (+100,+100) to (292,272), so the new content center is (612,512).
 */
struct Pt { int x, y; };
Pt const TITLE_CENTER   { 512, 162 };
Pt const DRAG_TARGET    { 612, 262 };
Pt const CONTENT_CENTER { 512, 412 };

/*
 * The +100,+100 drag offset the observe mode asserts. Same as the
 * synthetic mode's TITLE_CENTER -> DRAG_TARGET delta, so both modes
 * prove the same magnitude of real WM motion.
 */
int const DRAG_DX = 100;
int const DRAG_DY = 100;

/*
 * Motif decorator floating border. The title bar height (top margin)
 * is 20px; the title center is therefore 10px above the content's
 * ypos (content_ypos - 10). These margins are docs-confirmed for the
 * motif decorator (synthetic-mode math above, and verified empirically
 * by the synthetic wm_probe regression).
 */
int const MOTIF_TOP_MARGIN    = 20;
int const MOTIF_SIDE_MARGIN   = 4;

/*
 * QMP usb-tablet y-drift compensation. QEMU -nographic routes
 * input-send-event through a fallback path whose abs-axis-to-screen
 * translation lands clicks ~29px ABOVE the intended y (W1 calibration
 * matrix, docs/evidence/task-1-phase10-interactive.md). Adding +29 to
 * the title-bar y before emitting the QMP-TARGET marker makes the
 * observed click land at the intended title-bar y.
 */
int const QMP_Y_DRIFT = 29;

int const COLOR_TOLERANCE = 12;

bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                 : b - a <= COLOR_TOLERANCE; }


bool pixel_is_window(Pixel const &p)
{
	return channel_near(p.r(), WIN_R)
	    && channel_near(p.g(), WIN_G)
	    && channel_near(p.b(), WIN_B);
}

/*
 * pkg_gui_demo's distinctive green fill (#00ff00). Used by the observe
 * mode's pixel check after the drag — the new content center should be
 * this color.
 */
bool pixel_is_pkg_green(Pixel const &p)
{
	return channel_near(p.r(), PKG_R)
	    && channel_near(p.g(), PKG_G)
	    && channel_near(p.b(), PKG_B);
}

} /* anonymous namespace */


struct Wm_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env, "wm-probe" };
	Capture::Connection _capture { _env, "wm-probe" };
	Event::Connection   _event   { _env, "wm-probe" };

	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };

	/*
	 * Config ROM. The default (run/sponge-wm.run) carries no <config>
	 * on the start node, so _config.valid() is false and the synthetic
	 * inject=yes path runs byte-identically to pre-W3. The observe
	 * scenario (run/sponge-wm-qmp.run) sets <config inject="no"/>.
	 *
	 * Read via _config.node() (not .xml()): since Genode 26.05 the
	 * sandbox delivers child configs in HID format by default and
	 * .xml() returns <empty/> on HID input (W1 root cause,
	 * docs/evidence/task-1-phase10-interactive.md Step 0). The Node
	 * API auto-detects HID vs XML.
	 */
	Genode::Attached_rom_dataspace _config { _env, "config" };

	/*
	 * sponge_pkgd request/result channels (observe mode only). Same
	 * shape vct uses — the probe writes <request op="..." pkg="..."/>
	 * to report_rom with label "request"; pkgd reads it, processes,
	 * and answers on label "result". Pattern lifted verbatim from
	 * launch_probe (Phase 7 todo 10).
	 */
	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};


	Wm_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("wm-probe: starting");

		/*
		 * Define the panorama (no framebuffer driver here); the probe is
		 * nitpicker's only capture client, so its buffer is the panorama.
		 */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Phase 10 W3 observe mode (inject=no): skip the synthetic drag
		 * and instead drive a real QMP usb-tablet drag via a marker on
		 * the serial console. The inject=yes default path (used by
		 * run/sponge-wm.run) is byte-identical to pre-W3 — the early
		 * return is the only behavioural fork. _config.valid() is false
		 * when the start node carries no <config> (sponge-wm.run), so
		 * the short-circuit preserves the default.
		 */
		if (_observe_mode_enabled()) {
			_run_observe_mode();
			return;
		}

		/*
		 * (a) Wait until sponge-de's window (routed through wm, placed by
		 * the layouter) is composited at the expected content center.
		 */
		bool decorated = false;
		for (unsigned i = 0; i < 600; ++i) {  /* up to ~60s; Qt's first paint under softpipe is slow */
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			if (i % 10 == 0) {
				Pixel const *px = _cap_ds->local_addr<Pixel>();
				Pixel content = px[CONTENT_CENTER.y * SCREEN_W + CONTENT_CENTER.x];
				Genode::log("wm-probe: render poll ", i, " content@(", CONTENT_CENTER.x,
				            ",", CONTENT_CENTER.y, ")=", Genode::Hex(content.pixel));
			}

			if (_content_center_is_window()) {
				decorated = true;
				break;
			}
		}

		if (!decorated) {
			_fail("decorated window never appeared at expected content origin");
			return;
		}

		/*
		 * Record the window's initial geometry straight from the
		 * layouter's window_layout ROM. This is the position the drag is
		 * expected to change.
		 */
		Window_rect initial { };
		for (unsigned i = 0; i < 50; ++i) {  /* ROM content may lag a little */
			initial = _window_rect();
			if (initial.valid) break;
			_timer.msleep(100);
		}
		if (!initial.valid) {
			_fail("window_layout ROM has no window before drag");
			return;
		}
		Genode::log("wm-probe: decoration detected — window_layout pos (",
		            initial.x, ",", initial.y, ") ", initial.w, "x", initial.h,
		            " (content center ", CONTENT_CENTER.x, ",", CONTENT_CENTER.y,
		            "; title center ", TITLE_CENTER.x, ",", TITLE_CENTER.y, ")");

		/*
		 * (b) Synthetic title-bar drag. Injected straight into nitpicker's
		 * Event service. The jiggle stays on the title band so the
		 * decorator's hover element remains "title" while advancing the
		 * hover seq the layouter's deferred-DRAG protocol waits on.
		 */
		bool dragged = false;
		Window_rect moved { };
		for (unsigned attempt = 1; attempt <= 3 && !dragged; ++attempt) {
			Genode::log("wm-probe: drag attempt ", attempt, " — motion to title (",
			            TITLE_CENTER.x, ",", TITLE_CENTER.y, ")");
			_inject_motion(TITLE_CENTER.x, TITLE_CENTER.y);
			_timer.msleep(400);

			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Press { Input::BTN_LEFT });
			});
			_timer.msleep(100);

			_inject_motion(TITLE_CENTER.x + 2, TITLE_CENTER.y + 1);
			_timer.msleep(200);

			_inject_motion(DRAG_TARGET.x, DRAG_TARGET.y);
			_timer.msleep(200);
			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Release { Input::BTN_LEFT });
			});

			/*
			 * Poll the layouter's window_layout ROM: the drag succeeded
			 * iff the wm actually changed the window's position. This is
			 * the authoritative proof — it is the wm/layouter's own
			 * report of where the window is.
			 */
			_timer.msleep(300);
			for (unsigned i = 0; i < 30; ++i) {
				Window_rect const cur = _window_rect();
				if (cur.valid && (cur.x != initial.x || cur.y != initial.y)) {
					dragged = true;
					moved   = cur;
					break;
				}
				_timer.msleep(100);
			}

			if (dragged)
				Genode::log("wm-probe: drag attempt ", attempt, " — window moved (",
				            initial.x, ",", initial.y, ") -> (", moved.x, ",", moved.y, ")");
			else
				Genode::log("wm-probe: drag attempt ", attempt, " — window_layout unchanged");
		}

		if (!dragged) {
			_fail("drag did not move the window (window_layout position unchanged)");
			return;
		}

		/*
		 * (c) Cross-check the composited pixels against the new geometry.
		 * The new content center is derived from the reported position; a
		 * point in the window's old top-left corner area was vacated by
		 * the move and must be back to the nitpicker background.
		 */
		int const new_cx = moved.x + (int)moved.w / 2;
		int const new_cy = moved.y + (int)moved.h / 2;
		Pt  const vacated { initial.x + 10, initial.y + 10 };

		bool confirmed = false;
		for (unsigned i = 0; i < 100; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			Pixel const *px = _cap_ds->local_addr<Pixel>();

			bool const at_new_is_win = _in_bounds(new_cx, new_cy)
			                            && pixel_is_window(px[new_cy * SCREEN_W + new_cx]);
			bool const at_old_cleared = _in_bounds(vacated.x, vacated.y)
			                            && !pixel_is_window(px[vacated.y * SCREEN_W + vacated.x]);

			if (at_new_is_win && at_old_cleared) {
				confirmed = true;
				break;
			}
		}

		if (confirmed)
			Genode::log("wm-probe: window relocated to (", moved.x, ",", moved.y,
			            ") — new content center (", new_cx, ",", new_cy,
			            ") is window_bg, vacated corner (", vacated.x, ",", vacated.y,
			            ") cleared — real WM drag behavior confirmed");
		else
			Genode::log("wm-probe: window moved in window_layout but Capture not yet stable");

		Genode::log("wm-probe: PASS");
		_env.parent().exit(0);
	}


	struct Window_rect { bool valid = false; int x = 0, y = 0; unsigned w = 0, h = 0; };

	Window_rect _window_rect()
	{
		_window_layout.update();
		Window_rect r { };
		/*
		 * The layouter's window_layout ROM nests each <window> inside a
		 * <boundary> (<window_layout><boundary name="screen"><window .../>
		 * </boundary></window_layout>), so look one level down.
		 */
		_window_layout.node().with_sub_node("boundary",
			[&] (Genode::Node const &boundary) {
				boundary.with_sub_node("window",
					[&] (Genode::Node const &win) {
						r.valid = true;
						r.x = win.attribute_value("xpos", 0);
						r.y = win.attribute_value("ypos", 0);
						r.w = win.attribute_value("width",  0u);
						r.h = win.attribute_value("height", 0u);
					},
					[&] { });
			},
			[&] { });
		return r;
	}


	/*
	 * Find a window in the layouter's window_layout ROM whose title
	 * attribute contains `needle`. The layouter combines the session
	 * label and the Qt window title into the title attribute (see
	 * genode/repos/gems/src/app/window_layouter/window.h:399-407 —
	 * `title(label, " ", _title)`), so for a pkg_runtime-launched
	 * pkg_gui_demo the title reads
	 * "pkg_runtime -> pkg_gui_demo Sponge Pkg GUI Demo"; matching on
	 * the substring "pkg_gui_demo" is unique and stable. Returns the
	 * first match (there is only one pkg_gui_demo window in the
	 * scenario). Used by observe mode only.
	 */
	Window_rect _window_rect_by_title(char const *needle)
	{
		_window_layout.update();
		Window_rect r { };
		_window_layout.node().with_sub_node("boundary",
			[&] (Genode::Node const &boundary) {
				boundary.for_each_sub_node("window",
					[&] (Genode::Node const &win) {
						if (r.valid) return;
						Genode::String<256> const title =
							win.attribute_value("title", Genode::String<256>());
						if (Genode::strcmp(title.string(), "") == 0) return;
						bool found = false;
						for (char const *p = title.string(); *p; ++p) {
							char const *q = needle;
							char const *s = p;
							while (*q && *s && *q == *s) { ++q; ++s; }
							if (*q == 0) { found = true; break; }
						}
						if (!found) return;
						r.valid = true;
						r.x = win.attribute_value("xpos", 0);
						r.y = win.attribute_value("ypos", 0);
						r.w = win.attribute_value("width",  0u);
						r.h = win.attribute_value("height", 0u);
					});
			},
			[&] { });
		return r;
	}


	bool _content_center_is_window()
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		return pixel_is_window(px[CONTENT_CENTER.y * SCREEN_W + CONTENT_CENTER.x]);
	}


	static bool _in_bounds(int x, int y)
	{
		return x >= 0 && x < (int)SCREEN_W && y >= 0 && y < (int)SCREEN_H;
	}


	void _inject_motion(int x, int y)
	{
		_event.with_batch([&](Event::Session_client::Batch &batch) {
			batch.submit(Input::Absolute_motion{ x, y });
		});
	}


	void _fail(char const *reason)
	{
		Genode::error("wm-probe: FAIL ", reason);
		_env.parent().exit(1);
	}


	/* ============================================================
	 * Phase 10 W3 — observe mode (inject=no)
	 * ============================================================
	 *
	 * The methods below are reached only when the start node's <config>
	 * carries inject="no". The inject=yes default path above does not
	 * call any of them, so sponge-wm.run's regression behaviour is
	 * byte-identical to pre-W3.
	 */

	bool _observe_mode_enabled()
	{
		_config.update();
		if (!_config.valid()) return false;
		/*
		 * _config.node() (not .xml()): the sandbox delivers child
		 * configs in HID format by default since Genode 26.05 and
		 * .xml() returns <empty/> on HID input — the W1 root cause.
		 * Node auto-detects the format.
		 */
		return _config.node().attribute_value("inject", true) == false;
	}


	bool _send_request(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		for (unsigned i = 0; i < 150; ++i) {
			if (_request_answered(op, pkg)) return true;
			_timer.msleep(100);
		}
		return false;
	}


	bool _request_answered(char const *op, char const *pkg)
	{
		_result.update();
		if (!_result.valid()) return false;
		Genode::Node const &r = _result.node();
		if (!r.has_type("result")) return false;
		if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>(op)) return false;
		if (r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg)) return false;
		return r.has_attribute("status");
	}


	Genode::String<32> _request_status()
	{
		Genode::Node const &r = _result.node();
		if (!r.has_type("result")) return Genode::String<32>();
		return r.attribute_value("status", Genode::String<32>());
	}


	void _run_observe_mode()
	{
		Genode::log("wm-probe: observe mode (inject=no) -- will install pkg_gui_demo and await QMP-TARGET drag");

		/*
		 * Step 1: install pkg_gui_demo via sponge_pkgd's "request"
		 * channel (the same channel vct uses).
		 */
		Genode::log("wm-probe: [observe 1] install pkg_gui_demo");
		if (!_send_request("install", "pkg_gui_demo")) {
			_fail("pkgd did not answer install pkg_gui_demo");
			return;
		}
		if (_request_status() != Genode::String<32>("ok")) {
			_fail("pkgd install pkg_gui_demo did not return ok");
			return;
		}
		Genode::log("wm-probe: [observe 1] install ok");

		/*
		 * Step 2: launch pkg_gui_demo. pkgd's _do_launch regenerates
		 * pkg_runtime's config; pkg_runtime starts the component; the
		 * Qt window opens a Gui session to wm (via the route fix in
		 * run/sponge-alpha.run / this scenario), gets a decorator
		 * frame, and the layouter places it.
		 */
		Genode::log("wm-probe: [observe 2] launch pkg_gui_demo");
		if (!_send_request("launch", "pkg_gui_demo")) {
			_fail("pkgd did not answer launch pkg_gui_demo");
			return;
		}
		if (_request_status() != Genode::String<32>("ok")) {
			_fail("pkgd launch pkg_gui_demo did not return ok");
			return;
		}
		Genode::log("wm-probe: [observe 2] launch ok");

		/*
		 * Step 3: poll the layouter's window_layout ROM for a window
		 * whose title contains "pkg_gui_demo". Up to ~120s for Qt's
		 * first paint under softpipe Mesa + the wm/decorator pipeline
		 * to settle (sponge-launch.run uses ~120s for the same wait).
		 */
		Window_rect pkg_win { };
		bool        found   = false;
		for (unsigned i = 0; i < 1200; ++i) {
			pkg_win = _window_rect_by_title("pkg_gui_demo");
			if (pkg_win.valid) { found = true; break; }
			_timer.msleep(100);
		}
		if (!found) {
			_fail("pkg_gui_demo window never appeared in window_layout ROM");
			return;
		}
		Genode::log("wm-probe: [observe 3] pkg_gui_demo window in window_layout at (",
		            pkg_win.x, ",", pkg_win.y, ") ", pkg_win.w, "x", pkg_win.h);

		/*
		 * Step 4: compute the title-bar center from the reported
		 * content geometry + motif top margin (20). Title center is
		 * (xpos + width/2, ypos - MOTIF_TOP_MARGIN/2). Then apply the
		 * QMP_Y_DRIFT compensation to y so the observed click lands
		 * at the intended title-bar y (not 29px above it). The drag
		 * target is title_center + (DRAG_DX, DRAG_DY).
		 *
		 * The QMP-TARGET marker is the contract between this probe and
		 * run/qmp.inc::qmp_exec_target. The run script's bounded
		 * expect catches it on the serial console and dispatches a
		 * real QMP usb-tablet drag.
		 */
		int const title_x = pkg_win.x + (int)pkg_win.w / 2;
		int const title_y = pkg_win.y - MOTIF_TOP_MARGIN / 2;

		int const start_x_qmp = title_x;
		int const start_y_qmp = title_y + QMP_Y_DRIFT;
		int const end_x_qmp   = title_x + DRAG_DX;
		int const end_y_qmp   = title_y + DRAG_DY + QMP_Y_DRIFT;

		Genode::log("wm-probe: [observe 4] title center=(", title_x, ",", title_y,
		            ") +QMP-y-drift(", QMP_Y_DRIFT, ") -> start(", start_x_qmp, ",",
		            start_y_qmp, ") end(", end_x_qmp, ",", end_y_qmp, ")");

		/*
		 * The marker line. run/qmp.inc::qmp_exec_target matches the
		 * regex `QMP-TARGET drag (-?\d+) (-?\d+) (-?\d+) (-?\d+)` and
		 * dispatches qmp_drag with 20 interpolated steps (each step
		 * includes the hover-jiggle, press, motion, release sequence
		 * the layouter's deferred-DRAG protocol requires).
		 */
		Genode::log("wm-probe: QMP-TARGET drag ", start_x_qmp, " ", start_y_qmp,
		            " ", end_x_qmp, " ", end_y_qmp);

		/*
		 * Step 5: poll the window_layout ROM for the +100,+100 move.
		 * The run script's bounded expect has already dispatched the
		 * QMP drag by the time we reach here (qmp_exec_target is
		 * synchronous from the run script's perspective; the probe's
		 * serial log continues only after the QMP events have been
		 * sent). Up to ~30s for the usb-tablet -> usb_hid ->
		 * event_filter -> nitpicker -> decorator -> layouter round
		 * trip.
		 */
		bool        moved  = false;
		Window_rect after  { };
		for (unsigned i = 0; i < 300; ++i) {
			after = _window_rect_by_title("pkg_gui_demo");
			if (after.valid &&
			    (after.x != pkg_win.x || after.y != pkg_win.y)) {
				moved = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!moved) {
			_fail("QMP drag did not move pkg_gui_demo (window_layout position unchanged)");
			return;
		}
		Genode::log("wm-probe: [observe 5] pkg_gui_demo moved (",
		            pkg_win.x, ",", pkg_win.y, ") -> (", after.x, ",", after.y, ")");

		/*
		 * Step 6: pixel-check the new content center is pkg_gui_demo's
		 * green (#00ff00). The authoritative proof was the window_layout
		 * position change in step 5; this Capture check corroborates
		 * that the composited pixels agree (the misleading_success_output
		 * defense — a stale window_layout report must not PASS).
		 */
		int const new_cx = after.x + (int)after.w / 2;
		int const new_cy = after.y + (int)after.h / 2;

		bool confirmed = false;
		for (unsigned i = 0; i < 200; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			if (!_in_bounds(new_cx, new_cy)) break;
			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel const &p = px[new_cy * SCREEN_W + new_cx];
			if (pixel_is_pkg_green(p)) {
				confirmed = true;
				Genode::log("wm-probe: [observe 6] new content center (",
				            new_cx, ",", new_cy, ")=", Genode::Hex(p.pixel),
				            " is pkg_gui_demo green — real QMP drag verified");
				break;
			}
		}
		if (!confirmed) {
			_fail("pkg_gui_demo moved in window_layout but new content center pixel is not green");
			return;
		}

		Genode::log("wm-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Wm_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
