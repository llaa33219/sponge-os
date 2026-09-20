/* SPDX-License-Identifier: Apache-2.0
 *
 * release_resize_probe — see target.mk for the high-level contract.
 *
 * Structural observation contract (the misleading_success_output defense):
 * the verifier does NOT trust log lines. It observes three independent
 * sources after every drag:
 *   (a) the layouter's window_layout ROM (xpos/ypos/width/height attrs
 *       on the pkg_gui_demo <window> entry);
 *   (b) the layouter's resize_request ROM (a <window id=... width=...
 *       height=.../> entry for the same id, emitted by window_layouter's
 *       _gen_resize_request());
 *   (c) Capture-sampled pixels in the window's new content area.
 * All three must agree before the probe accepts a zone. Misleading-
 * success scenarios (e.g. a deco hover that didn't reach the
 * decorator, a layouter rule ignored, a wm forwarder that swallowed
 * the request) are all caught by the cross-source agreement.
 *
 * plain Genode component (Component::construct, no libc, no Qt)
 * per AGENTS.md §3.1. modeled on wm_probe's observe mode
 * (repos/sponge/src/test/wm_probe/main.cc).
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

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;
int const PKG_R = 0x00, PKG_G = 0xff, PKG_B = 0x00;

/*
 * Motif decorator constants. The motif decorator's floating border is
 * top=20 sides=4 with a 16-px corner zone (decorator/window.h:115-128)
 * that switches the hover from "border" to "sizer" on the perimeter.
 *
 * The initial pkg_gui_demo window is placed by layouter rule
 * `pkg_runtime -> (50, 320, 320, 240)` (the W3 fix mirrored in
 * run/sponge-wm-qmp.run). The motif outer geometry starts at
 * (50 - MOTIF_LEFT, 320 - MOTIF_TOP) = (46, 300) with size
 * (320 + 4 + 4, 240 + 20 + 4) = (328, 264).
 */
int const MOTIF_TOP = 20;
int const MOTIF_LEFT = 4;
int const MOTIF_RIGHT = 4;
int const MOTIF_BOTTOM = 4;
int const MOTIF_CORNER = 16;

int const COLOR_TOLERANCE = 12;

bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                  : b - a <= COLOR_TOLERANCE; }

bool pixel_is_pkg_green(Pixel const &p)
{
	return channel_near(p.r(), PKG_R)
	    && channel_near(p.g(), PKG_G)
	    && channel_near(p.b(), PKG_B);
}

bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

bool in_bounds(int x, int y)
{
	return x >= 0 && x < (int)SCREEN_W && y >= 0 && y < (int)SCREEN_H;
}

/*
 * Eight hit zones are computed inline in _run() with the motif outer
 * geometry available at run time. The pixel coordinates the run-script
 * host dispatches via QMP are derived from the live window_layout
 * rect, not baked into the probe (the window may have drifted after
 * earlier drags in the sequence).
 */

} /* anonymous namespace */


struct Release_resize_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer      { _env, "release-resize-probe" };
	Capture::Connection _capture    { _env, "release-resize-probe" };

	/*
	 * ROMs consumed by the probe. window_layout + resize_request are
	 * the two layouter-output ROMs the W9 verification observes
	 * structurally. The report_rom policies in the run scenario
	 * provide them under these exact labels.
	 */
	Genode::Attached_rom_dataspace _window_layout    { _env, "window_layout" };
	Genode::Attached_rom_dataspace _resize_request   { _env, "resize_request" };

	Genode::Constructible<Genode::Attached_rom_dataspace> _decorator_margins_rom { };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	unsigned _margin_top    { (unsigned)MOTIF_TOP };
	unsigned _margin_left   { (unsigned)MOTIF_LEFT };
	unsigned _margin_right  { (unsigned)MOTIF_RIGHT };
	unsigned _margin_bottom { (unsigned)MOTIF_BOTTOM };

	/*
	 * sponge_pkgd request/result channels — same pattern wm_probe
	 * uses (Phase 10 W3 observe mode). The probe writes
	 * <request op="..." pkg="..."/> and reads the matching <result
	 * op="..." pkg="..." status="..."/> from the "result" ROM.
	 */
	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};


	Release_resize_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("release-resize-probe: starting (criterion 9; Phase 16 W9)");

		/*
		 * Define the panorama (no framebuffer driver here). Capture
		 * session sizes to the Genode::Framebuffer default 1024x768
		 * (no fb driver -> the probe's buffer is nitpicker's
		 * panorama, identically to wm_probe).
		 */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		_run();
	}


	/********************************************************************
	 * Window rect matching + poll helpers
	 *******************************************************************/

	struct Window_rect { bool valid = false; int x = 0, y = 0;
	                     unsigned w = 0, h = 0;
	                     unsigned id = 0; };

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
						r.x = win.attribute_value("xpos",  0);
						r.y = win.attribute_value("ypos",  0);
						r.w = win.attribute_value("width",  0u);
						r.h = win.attribute_value("height", 0u);
						r.id = win.attribute_value("id",    0u);
					});
			},
			[&] { });
		return r;
	}

	/*
	 * The layouter's resize_request ROM shape (verbatim from
	 * genode/repos/gems/src/app/window_layouter/window.h:375-387):
	 *
	 *   <window id="N" width="W" height="H"/>
	 *
	 * Returns true iff a <window id=id width=... height=.../> for the
	 * given id appears in the current ROM. The "new_w != cur_w ||
	 * new_h != cur_h" comparison would be cleaner but window_layouter
	 * emits this on EVERY apply_drag_operation regardless of whether
	 * the size actually changed, so we accept it always.
	 */
	struct Resize_request_entry { bool valid = false;
	                              unsigned width = 0, height = 0;
	                              unsigned id = 0; };

	Resize_request_entry _poll_resize_request_for(unsigned id,
	                                             unsigned max_iter = 300)
	{
		for (unsigned i = 0; i < max_iter; ++i) {
			_resize_request.update();
			Resize_request_entry result { };
			_resize_request.node().for_each_sub_node("window",
				[&] (Genode::Node const &win) {
					unsigned const wid = win.attribute_value("id", 0u);
					if (wid != id) return;
					result.valid = true;
					result.id    = wid;
					result.width  = win.attribute_value("width",  0u);
					result.height = win.attribute_value("height", 0u);
				});
			if (result.valid) return result;
			_timer.msleep(100);
		}
		return Resize_request_entry { };
	}

	bool _wait_for_size_change(Window_rect const &initial,
	                           Window_rect &after,
	                           unsigned max_iter = 300)
	{
		after = { };
		for (unsigned i = 0; i < max_iter; ++i) {
			Window_rect cur = _window_rect_by_title("pkg_gui_demo");
			if (cur.valid && (cur.w != initial.w || cur.h != initial.h)) {
				after = cur;
				return true;
			}
			_timer.msleep(100);
		}
		return false;
	}

	bool _size_unchanged_after_drag(Window_rect const &before)
	{
		/* After a drag, the size MUST change. Equality = fail. */
		for (unsigned i = 0; i < 50; ++i) {
			Window_rect cur = _window_rect_by_title("pkg_gui_demo");
			if (cur.valid && (cur.w != before.w || cur.h != before.h))
				return false;
			_timer.msleep(100);
		}
		return true;  /* unchanged */
	}

	bool _capture_pixel_is_green(int px_x, int px_y)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *base = _cap_ds->local_addr<Pixel>();
		return pixel_is_pkg_green(base[px_y * SCREEN_W + px_x]);
	}

	bool _capture_pixel_is_bg(int px_x, int px_y)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *base = _cap_ds->local_addr<Pixel>();
		return pixel_is_bg(base[px_y * SCREEN_W + px_x]);
	}


	/********************************************************************
	 * sponge_pkgd request/result channel
	 *******************************************************************/

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
		_result.update();
		Genode::Node const &r = _result.node();
		if (!r.has_type("result")) return Genode::String<32>();
		return r.attribute_value("status", Genode::String<32>());
	}


	/********************************************************************
	 * Main test flow
	 *******************************************************************/

	void _fail(char const *reason)
	{
		Genode::error("release-resize-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/*
	 * Per-zone observation:
	 *  - structure (a): window_layout w/h changed since the zone's press
	 *  - structure (b): the resize_request ROM carried an entry for the
	 *    window id (the layouter->wm wire is alive and reaches the
	 *    layouter's reporter)
	 *  - structure (c): green-pixel at the new content center survives
	 *    the resize (Capture sample proves the compositor actually
	 *    redrew the content at the new geometry)
	 *
	 * Returns true iff the zone is accepted. Caller skips the post-
	 * resize title-bar drag if any zone fails.
	 */
	bool _observe_zone(unsigned zone_n, char const *zone_name,
	                   Window_rect const &before)
	{
		Window_rect after { };
		bool const size_changed = _wait_for_size_change(before, after, 200);

		if (!size_changed || !after.valid) {
			Window_rect const cur = _window_rect_by_title("pkg_gui_demo");
			Genode::error("release-resize-probe: FAIL zone ", zone_name,
			              " (zone ", zone_n, "): no size change in window_layout (",
			              before.w, "x", before.h, " -> ", after.w, "x", after.h,
			              " unchanged); current=", cur.valid ? "present" : "ABSENT",
			              " ", cur.w, "x", cur.h, " at (", cur.x, ",", cur.y, ")");
			return false;
		}

		/*
		 * (b) structural: layouter->wm resize_request ROM carried an
		 * entry for our id. _poll_resize_request_for polls up to 30s.
		 */
		Resize_request_entry const rr =
			_poll_resize_request_for(after.id);
		if (!rr.valid) {
			Genode::error("release-resize-probe: FAIL zone ", zone_name,
			              ": no resize_request ROM entry for window id=",
			              after.id, " (layouter->wm wire broken)");
			return false;
		}

		/*
		 * (c) structural: pkg_gui_demo's content is still green at the
		 * new content center. (If the QPA mishandled the resize, the
		 * compositor could repaint with the wrong texture at this
		 * coordinate.)
		 */
		int const new_cx = after.x + (int)after.w / 2;
		int const new_cy = after.y + (int)after.h / 2;
		bool green = false;
		for (unsigned i = 0; i < 50; ++i) {
			if (in_bounds(new_cx, new_cy) && _capture_pixel_is_green(new_cx, new_cy)) {
				green = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!green) {
			Genode::error("release-resize-probe: FAIL zone ", zone_name,
			              ": no green pixel at new content center (",
			              new_cx, ",", new_cy, ") after resize");
			return false;
		}

		Genode::log("release-resize-probe: [observe ", zone_n + 1,
		            "] zone ", zone_name, " — size ", before.w, "x", before.h,
		            " -> ", after.w, "x", after.h,
		            "; resize_request(", rr.width, "x", rr.height,
		            ") carried; green at (", new_cx, ",", new_cy, ") confirmed");
		return true;
	}


	void _run()
	{
		/*
		 * Frame margins come from the decorator's
		 * `decorator_margins` report (motif: top=20 sides=4;
		 * themed: top=28 sides=9) so the same probe works on
		 * both decorators. Fall back to the motif constants
		 * when the ROM is not routed.
		 */
		try {
			_decorator_margins_rom.construct(_env, "decorator_margins");
			/*
			 * The decorator publishes `decorator_margins` only
			 * after its config arrives (the themed bridge sends
			 * it late in boot), so a single boot-time read sees
			 * an empty ROM. Poll until the content arrives.
			 */
			for (unsigned i = 0; i < 100; ++i) {
				_decorator_margins_rom->update();
				if (_decorator_margins_rom->valid() &&
				    _decorator_margins_rom->size() > 0)
					break;
				_timer.msleep(100);
			}
			if (_decorator_margins_rom->valid()) {
				Genode::Node const n(
				    Genode::Const_byte_range_ptr(
				        _decorator_margins_rom->local_addr<char>(),
				        _decorator_margins_rom->size()));
				/*
				 * The decorator publishes its margins in a
				 * `<floating>` sub-node (the layouter's format
				 * at window_layouter/main.cc:494). The aura
				 * (drop-shadow) margins are NOT published; the
				 * scenario passes them via the probe's inline
				 * config when they are non-zero (themed frame:
				 * aura 8 on every side).
				 */
				n.with_optional_sub_node("floating",
				    [&] (Genode::Node const &f) {
				        _margin_top    = f.attribute_value("top",    (unsigned)MOTIF_TOP);
				        _margin_left   = f.attribute_value("left",   (unsigned)MOTIF_LEFT);
				        _margin_right  = f.attribute_value("right",  (unsigned)MOTIF_RIGHT);
				        _margin_bottom = f.attribute_value("bottom", (unsigned)MOTIF_BOTTOM);
				    });
			}
		}
		catch (Genode::Rom_connection::Rom_connection_failed) { }

		/*
		 * Aura margins come from the probe's inline config
		 * (default 0; the themed scenario sets 8). The total
		 * frame inset the zone math needs is floating + aura.
		 */
		Genode::Node const cfg = _config_rom.node();
		_margin_top    += cfg.attribute_value("aura_top",    0U);
		_margin_left   += cfg.attribute_value("aura_left",   0U);
		_margin_right  += cfg.attribute_value("aura_right",  0U);
		_margin_bottom += cfg.attribute_value("aura_bottom", 0U);
		Genode::log("release-resize-probe: frame margins top=", _margin_top,
		            " left=", _margin_left, " right=", _margin_right,
		            " bottom=", _margin_bottom);

		/*
		 * Step 1: install pkg_gui_demo via sponge_pkgd's request channel.
		 */
		Genode::log("release-resize-probe: [observe 0] install pkg_gui_demo");
		if (!_send_request("install", "pkg_gui_demo")) {
			_fail("pkgd did not answer install pkg_gui_demo");
			return;
		}
		if (_request_status() != Genode::String<32>("ok")) {
			_fail("pkgd install pkg_gui_demo did not return ok");
			return;
		}

		/*
		 * Step 2: launch pkg_gui_demo.
		 */
		Genode::log("release-resize-probe: [observe 0a] launch pkg_gui_demo");
		if (!_send_request("launch", "pkg_gui_demo")) {
			_fail("pkgd did not answer launch pkg_gui_demo");
			return;
		}
		if (_request_status() != Genode::String<32>("ok")) {
			_fail("pkgd launch pkg_gui_demo did not return ok");
			return;
		}

		/*
		 * Step 3: poll the layouter's window_layout for the launched
		 * window. Up to 300s for Qt6 first paint under softpipe Mesa
		 * on seL4.
		 */
		Window_rect init_win { };
		bool        found   = false;
		for (unsigned i = 0; i < 3000; ++i) {
			init_win = _window_rect_by_title("pkg_gui_demo");
			if (init_win.valid) { found = true; break; }
			_timer.msleep(100);
		}
		if (!found) {
			_fail("pkg_gui_demo window never appeared in window_layout ROM");
			return;
		}
		Genode::log("release-resize-probe: [observe 0b] pkg_gui_demo window in "
		            "window_layout at (", init_win.x, ",", init_win.y, ") ",
		            init_win.w, "x", init_win.h, " id=", init_win.id);

		// Wait for the decor + sizer hover SEQ to settle before the
		// first zone dispatch (W3 wm_probe's proven 5 s pattern).
		Genode::log("release-resize-probe: [observe 0c] waiting 5s for "
		            "decor/sizer hover to settle before zone-1 dispatch");
		_timer.msleep(5000);

		/*
		 * Step 4: compute the 8 hit zones on the motif outer geometry.
		 * Each press lands EXACTLY on the 4-px at-border band (or
		 * the 16-px corner box for the 4 corner zones) so the
		 * PS/2-vs-tablet recipe gap is moot under the Phase 16 W9
		 * fixed tablet-dispatched flow (run/qmp.inc's qmp_tablet_drag).
		 *
		 * Coordinate contract:
		 *   outer_x = content_x - MOTIF_LEFT        = content_x - 4
		 *   outer_y = content_y - MOTIF_TOP         = content_y - 20
		 *   outer_w = content_w + MOTIF_LEFT + MOTIF_RIGHT = +8
		 *   outer_h = content_h + MOTIF_TOP + MOTIF_BOTTOM = +24
		 *   local x ranges [0..outer_w), local y ranges [0..outer_h).
		 *
		 *   decorator/window.cc:315-325 maps local (x, y) to:
		 *     at_border = x < _border_size(=4) || x >= outer_w-4
		 *              || y < 4 || y >= outer_h-4
		 *     sizer flags: x < _corner_size(=16)
		 *                   || y < 16
		 *                   || x >= outer_w - 16
		 *                   || y >= outer_h - 16
		 *   At-border + matching sizer = actionable hit zone.
		 *
		 *   Each press point sits 2 px inside the outer edge
		 * (in absolute coords: outer_+0..outer_+3 for the 4-px
		 * border band; outer_+0..outer_+15 for the 16-px corner
		 * box on each axis). Drag deltas are 50 px in the
		 * appropriate direction so the post-drag content_w/h
		 * change is unambiguously visible in window_layout.
		 */

		struct {
			char const *name;
			int px, py;     /* press point (absolute pixel on the screen) */
			int rx, ry;     /* release point (drag delta destination) */
		} zones[8];

		/*
		 * Recompute the motif outer geometry and all eight zone
		 * coordinates from the CURRENT window rect before every
		 * zone attempt. Corner drags move the anchored corner
		 * (e.g. an up-left drag shifts the window's x1/y1), so
		 * coordinates pre-computed from the initial geometry go
		 * stale after a corner zone — the W9 failure mode where a
		 * later zone's press lands outside the moved window.
		 */
		auto compute_zones = [&] (Window_rect const &cur) {
			int const ox = cur.x - (int)_margin_left;
			int const oy = cur.y - (int)_margin_top;
			int const ow = cur.w + (int)_margin_left + (int)_margin_right;
			int const oh = cur.h + (int)_margin_top + (int)_margin_bottom;
			/*
			 * All zones drag TOWARD the window interior (shrink).
			 * Outward drags compound across zones (plus retries
			 * after slow layout updates) and eventually press the
			 * window against a screen edge, where the layouter
			 * clamps the resize to zero — the observed ne/se
			 * failure mode at ~full-screen geometry. Shrinking
			 * keeps the window inside the screen at every step
			 * (240 -> ~120 px worst case) while still proving a
			 * size change per zone.
			 */
			zones[0] = { "north",  ox + ow / 2, oy + 2,
			             ox + ow / 2, oy + 2 + 30 };
			zones[1] = { "south",  ox + ow / 2, oy + oh - 2,
			             ox + ow / 2, oy + oh - 2 - 30 };
			/*
			 * Side presses sit 1 px inside the frame edge: the
			 * themed frame's side decor band is only 1 px wide,
			 * so a 2 px inset lands inside the content (themed7
			 * evidence) — while the motif's 4 px border and the
			 * themed top/bottom bands are deep enough for 2 px.
			 */
			zones[2] = { "east",   ox + ow - 1, oy + oh / 2,
			             ox + ow - 1 - 30, oy + oh / 2 };
			zones[3] = { "west",   ox + 1, oy + oh / 2,
			             ox + 1 + 30, oy + oh / 2 };
			zones[4] = { "nw",     ox + 2, oy + 2,
			             ox + 2 + 30, oy + 2 + 30 };
			zones[5] = { "ne",     ox + ow - 2, oy + 2,
			             ox + ow - 2 - 30, oy + 2 + 30 };
			zones[6] = { "sw",     ox + 2, oy + oh - 2,
			             ox + 2 + 30, oy + oh - 2 - 30 };
			zones[7] = { "se",     ox + ow - 2, oy + oh - 2,
			             ox + ow - 2 - 30, oy + oh - 2 - 30 };
		};

		Window_rect cur = init_win;
		for (unsigned z = 0; z < 8; ++z) {
			/*
			 * Emit QMP-TARGET zone <press> <release>. The host's
			 * qmp_exec_target matches and dispatches qmp_drag (single
			 * press + jiggle + drag + release).
			 *
			 * Bounded retry (up to 3 attempts per zone): the
			 * usb-tablet drag intermittently does not reach the
			 * layouter at all on this host's QEMU 11.1.1 (the
			 * window stays present at the SAME size — confirmed by
			 * the `current=present` diagnostic, i.e. not a window
			 * kill). The zone is still only counted when the size
			 * actually changed + resize_request carried + the
			 * content pixel confirmed, so a retry never weakens
			 * the assertion.
			 */
			bool zone_ok = false;
			for (unsigned attempt = 1; attempt <= 5 && !zone_ok; ++attempt) {
				if (attempt > 1)
					Genode::log("release-resize-probe: zone ", z + 1,
					            " retry ", attempt, "/5");
				compute_zones(cur);
				Genode::log("release-resize-probe: QMP-TARGET zone-", z + 1, " ",
				            zones[z].px, " ", zones[z].py, " ", zones[z].rx, " ", zones[z].ry);

				Window_rect before_zone = cur;
				zone_ok = _observe_zone(z + 1, zones[z].name, before_zone);
				cur = _window_rect_by_title("pkg_gui_demo");
			}
			if (!zone_ok) {
				_fail("zone drag verification failed after 3 attempts");
				return;
			}
		}

		/*
		 * Step 5: post-resize title-bar drag (criterion-5 regression).
		 * After 8 resizes, the title bar is still draggable.
		 *
		 * The motif title bar spans the entire top edge of the
		 * outer geometry. In local coords it sits at
		 *   y in [MOTIF_TOP (=4)..MOTIF_TOP + title_height (=20))
		 * the 16-px strip above the content's 4-px border.
		 * Mid-strip is at local y = MOTIF_TOP/2 + 4 = 14 below
		 * the outer top (absolute ty = outer_y + 14 = cur.y - 6).
		 * Picking ty close to the content keeps the press inside
		 * the title rectangle even after the previous 8 zone
		 * drags have perturbed the window position slightly.
		 *
		 * Drag delta 30 px in (+x, +y) lands the release well
		 * inside the screen (the title-bar drag in motivated
		 * directions stays at least 30 px away from edges).
		 */
		int const tx = cur.x + (int)cur.w / 2;
		int const ty = cur.y - (int)_margin_top / 2; /* mid of the title strip */
		int const drag_dx = 30;
		int const drag_dy = 30;
		bool title_drag_ok = false;
		for (unsigned attempt = 1; attempt <= 5 && !title_drag_ok; ++attempt) {
			if (attempt > 1)
				Genode::log("release-resize-probe: post-resize title drag retry ",
				            attempt, "/3");
			Genode::log("release-resize-probe: QMP-TARGET post-resize-title-drag ",
			            tx, " ", ty, " ", tx + drag_dx, " ", ty + drag_dy);
			Window_rect before_title = cur;
			for (unsigned i = 0; i < 600 && !title_drag_ok; ++i) {
				Window_rect cur2 = _window_rect_by_title("pkg_gui_demo");
				if (cur2.valid && (cur2.x != before_title.x || cur2.y != before_title.y)) {
					title_drag_ok = true;
					break;
				}
				_timer.msleep(100);
			}
		}
		if (!title_drag_ok) {
			_fail("post-resize title-bar drag did not move the window "
			      "(criterion 5 regression)");
			return;
		}
		Genode::log("release-resize-probe: [observe 9] post-resize title drag ok — "
		            "(", cur.x, ",", cur.y, ") -> (",
		            _window_rect_by_title("pkg_gui_demo").x, ",",
		            _window_rect_by_title("pkg_gui_demo").y, ")");

		Genode::log("QMP-TARGET done");
		Genode::log("release-resize-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Release_resize_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
