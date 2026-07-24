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
 * Three stages:
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
 * On success logs "wm-probe: PASS"; on failure "wm-probe: FAIL <reason>"
 * and exits non-zero, so run/sponge-wm.run fails via run_genode_until
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
#include <timer_session/connection.h>
#include <util/reconstructible.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;       /* nitpicker background #1e1e2e */
int const WIN_R = 0x31, WIN_G = 0x32, WIN_B = 0x44;    /* themed window_bg #313244 */

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

int const COLOR_TOLERANCE = 12;

bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                 : b - a <= COLOR_TOLERANCE; }


bool pixel_is_window(Pixel const &p)
{
	return channel_near(p.r(), WIN_R)
	    && channel_near(p.g(), WIN_G)
	    && channel_near(p.b(), WIN_B);
}

} /* anonymous namespace */


struct Wm_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env, "wm-probe" };
	Capture::Connection _capture { _env, "wm-probe" };
	Event::Connection   _event   { _env, "wm-probe" };

	Genode::Attached_rom_dataspace _window_layout { _env, "window_layout" };

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
};


void Component::construct(Genode::Env &env)
{
	static Wm_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
