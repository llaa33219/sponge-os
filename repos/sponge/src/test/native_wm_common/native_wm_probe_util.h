/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_probe_util.h — shared probe mechanics for the native-WM
 * acceptance probes (Criterion 16, docs/plans/phase16-sponge-wm.md).
 *
 * Reusable pieces:
 *   - QMP-TARGET marker emission (the run/qmp.inc marker contract;
 *     every marker dispatched by qmp.inc's shared qmp_exec_target —
 *     the native run scenarios deliberately define NO local Tcl QMP
 *     procs, so the W3/W7 drag and click choreography stays in one
 *     place: run/qmp.inc),
 *   - sponge_pkgd request/result channel driver (install/launch/remove),
 *   - sponge_configd config_request/config_result channel driver (the
 *     backend `vct config` uses; D16W.11),
 *   - panel-tasklist click geometry (proven panel layout derivation),
 *   - native chrome geometry (decorator_margins report + the PROVISIONAL
 *     button/zone layout, TODO(native-wm)),
 *   - Capture pixel helpers (misleading_success_output defense).
 *
 * Header-only; see native_wm_protocol.h for the frozen protocol parsers.
 */

#pragma once

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_generator.h>

#include "native_wm_protocol.h"

namespace Native_wm {


/* ===================== QMP-TARGET marker emission ===================== */

/*
 * Marker contract (run/qmp.inc): the run scripts dispatch these via the
 * SHARED qmp_exec_target — click -> qmp_ps2_click (clamp+navigate+press),
 * drag -> click-at-start + PS/2 delta walk + release. Constraints the
 * probes must honor (shared-helper discipline, see run/qmp.inc):
 *   - one marker at a time, strictly serialized with the host's bounded
 *     dispatch (each action is confirmed before the next marker),
 *   - no probe-side marker re-emission (a stale marker would desync the
 *     host's fixed dispatch count),
 *   - drag deltas per axis <= 49 px so the shared drag arm walks exact
 *     rel-1 steps only (its rel-50 coarse steps map through
 *     event_filter's accelerate LUT to ~100 px and would overshoot).
 */
inline void emit_qmp_click(int x, int y)
{
	Genode::log("QMP-TARGET click ", x, " ", y);
}

inline void emit_qmp_drag(int x1, int y1, int x2, int y2)
{
	Genode::log("QMP-TARGET drag ", x1, " ", y1, " ", x2, " ", y2);
}


/* ===================== sponge_pkgd request channel ===================== */

struct Pkg_channel
{
	Genode::Env &_env;

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Timer::Connection              _timer   { _env, "native-wm-pkg" };

	explicit Pkg_channel(Genode::Env &env) : _env(env) { }

	Genode::String<32> _status(char const *op, char const *pkg)
	{
		_result.update();
		if (!_result.valid()) return Genode::String<32>();
		Genode::Node const r = _result.node();
		if (!r.has_type("result")) return Genode::String<32>();
		if (r.attribute_value("op",  Genode::String<32>())
		 != Genode::String<32>(op)) return Genode::String<32>();
		if (r.attribute_value("pkg", Genode::String<128>())
		 != Genode::String<128>(pkg)) return Genode::String<32>();
		return r.attribute_value("status", Genode::String<32>());
	}

	/*
	 * Send <request op="..." pkg="..."/> and wait for a terminal status.
	 * True when the named op/pair answers ok / not-installed /
	 * already-running (the three terminal statuses the delivered
	 * sponge_pkgd returns for install/launch/remove).
	 */
	bool request(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		for (unsigned i = 0; i < 300; ++i) {
			Genode::String<32> const s = _status(op, pkg);
			if (s == Genode::String<32>("ok")
			 || s == Genode::String<32>("not-installed")
			 || s == Genode::String<32>("already-running"))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool request_ok(char const *op, char const *pkg)
	{
		if (!request(op, pkg)) return false;
		return _status(op, pkg) == Genode::String<32>("ok");
	}
};


/* ===================== sponge_configd write channel ===================== */

/*
 * The config_request / config_result channel (the backend `vct config`
 * uses — AGENTS.md §3.3 rule 5; the user-facing spelling for these
 * writes is `vct config set <key> <value>`, D16W.11).
 *
 * TODO(native-wm/M4): `wm.layout.mode` must be registered as a
 * sponge_configd closed-registry key with a typed enum validator
 * (floating|tiling, default floating) per D16W.11 before these writes
 * answer status="ok". Until then configd refuses the unknown key and
 * the probes fail honestly at the mode-set step.
 */
struct Config_channel
{
	Genode::Env &_env;

	Genode::Expanding_reporter     _request { _env, "config_request",
	                                                 "config_request" };
	Genode::Attached_rom_dataspace _result  { _env, "config_result" };
	Genode::Attached_rom_dataspace _broadcast { _env, "configd" };
	Timer::Connection              _timer   { _env, "native-wm-config" };

	explicit Config_channel(Genode::Env &env) : _env(env) { }

	bool set(char const *key, char const *value, bool want_ok = true)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_result.valid()) {
				Genode::Node const r = _result.node();
				if (r.has_type("result")
				 && r.attribute_value("op",  Genode::String<32>())
				  == Genode::String<32>("set")
				 && r.attribute_value("key", Genode::String<128>())
				  == Genode::String<128>(key)
				 && r.attribute_value("value", Genode::String<128>())
				  == Genode::String<128>(value)) {
					bool const ok =
						r.attribute_value("status", Genode::String<32>())
						 == Genode::String<32>("ok");
					return ok == want_ok;
				}
			}
			_timer.msleep(100);
		}
		return false;
	}

	/* Poll the configd broadcast ROM for key=value. */
	bool broadcast_is(char const *key, char const *value)
	{
		for (unsigned i = 0; i < 80; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				Genode::Node const root = _broadcast.node();
				Genode::String<128> const v =
					root.attribute_value(key, Genode::String<128>());
				if (v == Genode::String<128>(value)) return true;
			}
			_timer.msleep(100);
		}
		return false;
	}
};


/* ============ panel tasklist click geometry (proven derivation) ============ */

/*
 * The panel layout derivation (run/sponge-wm-tasks.run): the panel
 * starts at (0,0); the launcher toggle is 48 px wide at the left; the
 * title label comes next (~70 px); the tasklist strip starts at
 * panel x ~= 130; each entry is a fixed 96 px button. Entry centers:
 * x = 130 + index*96 + 48, y = 18 (panel height 28). The entry INDEX is
 * the TasklistController's tracked order = window_layout iteration
 * order (see native_wm_protocol.h layout_tracked_index).
 */
inline constexpr int TASKLIST_X0      = 130;
inline constexpr int TASKLIST_ENTRY_W = 96;
inline constexpr int TASKLIST_Y       = 18;

inline int tasklist_entry_x(int index)
{
	return TASKLIST_X0 + index * TASKLIST_ENTRY_W + TASKLIST_ENTRY_W / 2;
}


/* ============ decorator margins (proven public report shape) ============ */

/*
 * sponge_decorator publishes `decorator_margins` in the same public
 * shape the two upstream decorators use (genode/repos/gems decorator
 * main.cc): <floating top=".." bottom=".." left=".." right=".."/>.
 * The native probes treat the margins as REQUIRED (the run topology
 * routes the report to them): chrome geometry cannot be derived without
 * them, and a missing report is an honest failure naming sponge_decorator.
 */
struct Chrome_margins
{
	unsigned top    { 0 };
	unsigned bottom { 0 };
	unsigned left   { 0 };
	unsigned right  { 0 };
	bool     valid  { false };
};

inline Chrome_margins read_margins(Genode::Node root, char const *tag)
{
	Chrome_margins m;
	root.with_sub_node("floating",
		[&] (Genode::Node const &f) {
			m.top    = f.attribute_value("top",    0u);
			m.bottom = f.attribute_value("bottom", 0u);
			m.left   = f.attribute_value("left",   0u);
			m.right  = f.attribute_value("right",  0u);
			m.valid  = true;
		},
		[&] {
			Genode::error(tag, ": decorator_margins has no <floating> node "
			              "(sponge_decorator contract, docs/05 §4.6)");
		});
	if (m.valid)
		Genode::log(tag, ": chrome margins top=", m.top, " bottom=", m.bottom,
		            " left=", m.left, " right=", m.right);
	return m;
}


/*
 * Native chrome geometry — PROVISIONAL run-side contract.
 *
 * TODO(native-wm): sponge_decorator ships a native title bar plus
 * native close/minimize/fullscreen buttons (D16W.1/M3). Until the
 * chrome implementation lands, the probes derive every chrome click
 * target from this layout:
 *
 *   - the title bar occupies the TOP margin band of the outer rect
 *     (height = margins.top, full outer width);
 *   - the buttons sit at the right end of the title bar, each
 *     BUTTON_W x BUTTON_H, vertically centered in the title band,
 *     BUTTON_PAD px from the outer right edge, in this left-to-right
 *     order: minimize, fullscreen, close (close is the rightmost,
 *     the common desktop convention).
 *
 * If the shipped chrome differs, update these constants in one place:
 * the resulting click-target failures are honest and localize here.
 */
inline constexpr unsigned CHROME_BUTTON_W   = 24;
inline constexpr unsigned CHROME_BUTTON_H   = 16;
inline constexpr unsigned CHROME_BUTTON_PAD = 4;

enum class Chrome_button { MINIMIZE, FULLSCREEN, CLOSE };

struct Pt { int x { 0 }; int y { 0 }; };

/* Title-bar center of a content rect (window_layout inner geometry). */
inline Pt title_center(Geom const &content, Chrome_margins const &m)
{
	return Pt { content.x + (int)content.w / 2,
	            content.y - (int)m.top / 2 };
}

inline Pt chrome_button_center(Geom const &content, Chrome_margins const &m,
                               Chrome_button which)
{
	int const outer_right = content.x + (int)content.w + (int)m.right;
	int const title_mid_y = content.y - (int)m.top / 2;

	/* rightmost button center x, then walk left by BUTTON_W */
	int x = outer_right - (int)CHROME_BUTTON_PAD - (int)CHROME_BUTTON_W / 2;
	switch (which) {
	case Chrome_button::CLOSE:      break;
	case Chrome_button::FULLSCREEN: x -= (int)CHROME_BUTTON_W; break;
	case Chrome_button::MINIMIZE:   x -= 2 * (int)CHROME_BUTTON_W; break;
	}
	return Pt { x, title_mid_y };
}


/*
 * 8-zone resize grip points (floating mode; docs/05 §4.6 / D16W.5).
 * Proven math from test/release_resize_probe (all zones drag inward so
 * deltas compound safely at screen edges), with insets derived from the
 * chrome margins so the press lands on the resize grip band of the
 * native decoration. Every drag is <= 49 px per axis (the shared
 * qmp.inc drag arm walks exact rel-1 steps below 50).
 */
inline constexpr int ZONE_DRAG_PX = 30;

struct Zone_points { int px, py, rx, ry; };

inline int grip_inset(unsigned margin)
{
	/* Target the middle of the outermost grip band, not the content
	 * boundary. The shared PS/2 walk undershoots by a few pixels; four
	 * pixels leaves room on both sides of the native 8 px edge grip. */
	unsigned const half = margin / 2;
	if (half < 1) return 1;
	if (half > 4) return 4;
	return (int)half;
}

inline int far_grip_inset(unsigned margin)
{
	/* PS/2 navigation undershoots toward smaller screen coordinates.
	 * The last pixel remains valid even without undershoot and gives
	 * south/east drags the full grip band as their error budget. */
	return margin ? 1 : 0;
}

inline Zone_points zone_points(int index /* 1=N .. 8=SE */,
                               Geom const &content, Chrome_margins const &m)
{
	int const ox = content.x - (int)m.left;
	int const oy = content.y - (int)m.top;
	int const ow = (int)content.w + (int)m.left + (int)m.right;
	int const oh = (int)content.h + (int)m.top + (int)m.bottom;

	int const it = grip_inset(m.top);
	int const ib = far_grip_inset(m.bottom);
	int const il = grip_inset(m.left);
	int const ir = far_grip_inset(m.right);

	switch (index) {
	case 1:  return { ox + ow / 2, oy + it,
	                  ox + ow / 2, oy + it + ZONE_DRAG_PX };          /* N  */
	case 2:  return { ox + ow / 2, oy + oh - ib,
	                  ox + ow / 2, oy + oh - ib - ZONE_DRAG_PX };     /* S  */
	case 3:  return { ox + ow - ir, oy + oh / 2,
	                  ox + ow - ir - ZONE_DRAG_PX, oy + oh / 2 };     /* E  */
	case 4:  return { ox + il, oy + oh / 2,
	                  ox + il + ZONE_DRAG_PX, oy + oh / 2 };          /* W  */
	case 5:  return { ox + il, oy + it,
	                  ox + il + ZONE_DRAG_PX, oy + it + ZONE_DRAG_PX };   /* NW */
	case 6:  return { ox + ow - ir, oy + it,
	                  ox + ow - ir - ZONE_DRAG_PX, oy + it + ZONE_DRAG_PX }; /* NE */
	case 7:  return { ox + il, oy + oh - ib,
	                  ox + il + ZONE_DRAG_PX, oy + oh - ib - ZONE_DRAG_PX }; /* SW */
	default: return { ox + ow - ir, oy + oh - ib,
	                  ox + ow - ir - ZONE_DRAG_PX, oy + oh - ib - ZONE_DRAG_PX }; /* SE */
	}
}


/*
 * Expected inner-geometry change per zone (all zones shrink inward by
 * ZONE_DRAG_PX). Returns the expected delta per field; the probe
 * asserts each changed field within tolerance and unchanged fields
 * EXACTLY (a zone drag may only move the dragged edge).
 */
struct Zone_delta { int dx, dy; int dw, dh; };

inline Zone_delta zone_delta(int index)
{
	switch (index) {
	case 1:  return {  0, ZONE_DRAG_PX,  0, -ZONE_DRAG_PX };             /* N  */
	case 2:  return {  0, 0,  0, -ZONE_DRAG_PX };                       /* S  */
	case 3:  return {  0, 0, -ZONE_DRAG_PX,  0 };                       /* E  */
	case 4:  return {  ZONE_DRAG_PX, 0, -ZONE_DRAG_PX,  0 };            /* W  */
	case 5:  return {  ZONE_DRAG_PX, ZONE_DRAG_PX, -ZONE_DRAG_PX, -ZONE_DRAG_PX };  /* NW */
	case 6:  return {  0, ZONE_DRAG_PX, -ZONE_DRAG_PX, -ZONE_DRAG_PX };  /* NE */
	case 7:  return {  ZONE_DRAG_PX, 0, -ZONE_DRAG_PX, -ZONE_DRAG_PX };  /* SW */
	default: return {  0, 0, -ZONE_DRAG_PX, -ZONE_DRAG_PX };            /* SE */
	}
}

/* Pointer-drag tolerance: the shared PS/2 walk is exact to +/-1 px when
 * no rel events drop; the shared drag arm's fine walk is unpaced, so a
 * few px of undershoot are tolerated (never overshoot). */
inline constexpr int DRAG_TOL = 8;


/* ===================== Capture pixel helpers ===================== */

using Pixel = Capture::Pixel;

inline bool pixel_near(Pixel const &p, int r, int g, int b, int tol = 12)
{
	auto near = [&](int a, int b_) {
		return a >= b_ ? a - b_ <= tol : b_ - a <= tol; };
	return near(p.r(), r) && near(p.g(), g) && near(p.b(), b);
}

/* nitpicker background #1e1e2e (all native scenarios set it) */
inline bool pixel_is_background(Pixel const &p)
{
	return pixel_near(p, 0x1e, 0x1e, 0x2e);
}

/* pkg_gui_demo's distinctive fill #00ff00 (Phase 7 todo 8) */
inline bool pixel_is_pkg_green(Pixel const &p)
{
	return pixel_near(p, 0x00, 0xff, 0x00);
}

/* sponge-de demo widget background #313244 (themed window_bg) */
inline bool pixel_is_demo_window(Pixel const &p)
{
	return pixel_near(p, 0x31, 0x32, 0x44);
}


/* ===================== capture buffer wrapper ===================== */

struct Capture_probe
{
	Genode::Env &_env;

	Capture::Connection _capture { _env, "native-wm-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };

	unsigned const w;
	unsigned const h;

	Capture_probe(Genode::Env &env, unsigned screen_w, unsigned screen_h)
	: _env(env), w(screen_w), h(screen_h)
	{
		_capture.buffer({ .px       = Capture::Area(w, h),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(w, h) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());
	}

	Pixel at(int x, int y)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		return px[y * (int)w + x];
	}

	bool in_bounds(int x, int y) const
	{
		return x >= 0 && x < (int)w && y >= 0 && y < (int)h;
	}
};


} /* namespace Native_wm */
