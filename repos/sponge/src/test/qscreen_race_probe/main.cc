/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * qscreen_race_probe — focused single-frame cold-boot probe for the
 * Phase 14 W3 / D14.8(b) QGenodeScreen 1x1-stale race investigation.
 *
 * The Phase 11 alpha-flake review
 * (docs/evidence/task-6-phase11-alpha-flake.md) suspected a race where
 * QGenodeScreen publishes a 1x1 screen geometry until nitpicker's
 * panorama info arrives — late on heavy seL4 topologies — and
 * sponge-de's PanelWidget trusts that geometry. The pre-W11 panel code
 * constructed a 1x28 panel buffer mapped into the full-width panel
 * domain, which read as 0x0 (black) at the alpha_probe sample point.
 * The Phase 11 fix added a width-floor guard in panel_widget.cc
 * (_apply_geometry: width = screen_w > 64 ? screen_w : 1024), and
 * alpha subsequently went 6/6 green.
 *
 * D14.8(b) gates any follow-up vendored patch on a focused reproducer
 * showing the race in >= 3 of 10 cold-boot trials. This probe is the
 * ONLY variable-under-test: every other condition (driver stack, build
 * flags, theme, configd wiring, sponge-de binary) is held constant.
 *
 * Probe protocol (single-frame, bounded):
 *
 *   1. Allocate the capture buffer (1024x768) as early as possible
 *      after component construction. This makes the probe's capture
 *      session define nitpicker's panorama — the SAME role fb_sdl plays
 *      in the interactive scenario. No warm-up, no render-paint
 *      priming: the probe does NOT wait for any other component to be
 *      ready before its first capture.
 *   2. First capture attempt (poll 0): read TWO sample pixels:
 *        - PANEL_PT (512, 14)   : the panel band center
 *        - DEMO_PT  (512, 412)  : the demo-domain center
 *      The Phase 11 evidence named the 0x0 (black) panel reading as the
 *      race signature. Default theme panel_bg is #1e1e2e, which equals
 *      the nitpicker <background> color, so a NON-raced panel band
 *      reads as 0x1e1e2e (nitpicker bg, never painted yet) OR the
 *      themed color after paint; a RACED panel reads as 0x0 (black,
 *      1x28 degenerate buffer never painted).
 *   3. Subsequent polls (bounded by POLL_BUDGET x 200 ms = 60s
 *      wall-clock cold-boot window): poll the demo-domain center for
 *      the themed window_bg color (#313244, from default.theme). The
 *      window_bg is unambiguous: it ONLY appears after sponge-de paints
 *      its themed demo window. A race-affected boot leaves the demo-
 *      domain center at nitpicker bg (#1e1e2e) forever — sponge-de's
 *      first paint never happens because the panel construction already
 *      pinned the dataspace to a 1x1 screen and Qt's repaint path
 *      requires a follow-up geometry change to recover.
 *   4. Verdict line:
 *        - "qscreen-race-probe: trial verdict clean" — the demo-domain
 *          window_bg was observed within the bounded window. The race
 *          did NOT reproduce this boot.
 *        - "qscreen-race-probe: trial verdict race"  — the demo-domain
 *          center stayed at nitpicker bg / 0x0 for the full bounded
 *          window. The race DID reproduce this boot. The first-capture
 *          pixel readings are also logged so the Phase 15 reviewer can
 *          distinguish a "1x1 degenerate" race from a generic
 *          "sponge-de never booted" hang.
 *
 * Failure path: bounded timeouts everywhere. After the verdict line
 * the probe sleeps 1 frame, then exits 0. The run script gates on
 * the verdict regex (not on the exit code — exit 0 either way is
 * intentional; this is an investigation tool, not a pass/fail gate;
 * D14.8(b) is the policy that consumes the verdict).
 *
 * Plain Genode component following AGENTS.md §3.1 (qualified Genode
 * types, no exceptions, Component::construct exactly as the framework
 * expects).
 */

#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>

namespace {

using Pixel = Capture::Pixel;

/*
 * Screen geometry — must match the run scenario's domain layout
 * (panel domain 0,0,1024,28; demo domain 192,172,640,480; default
 * domain 0,28,1024,-28 for app windows). The probe's capture buffer
 * defines nitpicker's panorama because there is no fb driver in the
 * scenario; 1024x768 matches the vesa_fb mode set by the driver
 * stack. Mirrors the reference probe (sponge_de_probe SCREEN_W/H).
 */
unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e) from the run scenario. The
 * panel domain outside any painted client buffer reads as this color.
 * A 1x1-degenerate panel buffer (the race signature) reads as 0x0
 * (black) because the QPA allocated a fresh framebuffer that no
 * paint event has ever touched.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Themed window_bg color (#313244, default.theme). Only appears on
 * the demo-domain center after sponge-de paints its themed demo
 * window. This is the positive signal the race did NOT reproduce.
 */
int const WIN_R = 0x31, WIN_G = 0x32, WIN_B = 0x44;

/*
 * Sample points (absolute screen coords).
 *
 *   PANEL_PT (512, 14)  : panel band center. Reading 0x0 indicates
 *                         the panel buffer is mapped but never painted
 *                         (1x28-degenerate race signature, per Phase
 *                         11 evidence). Reading 0x1e1e2e is nitpicker
 *                         bg (panel not painted yet, normal cold-boot
 *                         early state — race NOT yet decided).
 *   DEMO_PT  (512, 412) : demo-domain center. Themed window_bg
 *                         #313244 only appears after sponge-de first-
 *                         paints. Stays at nitpicker bg / 0x0 forever
 *                         if the panel-construction race pinned Qt to
 *                         a 1x1 screen and the follow-up geometry
 *                         change never arrives.
 */
struct Pt { int x, y; };
Pt const PANEL_PT { 512, 14  };
Pt const DEMO_PT  { 512, 412 };

/*
 * Color tolerance — blit's <copy> on capture buffers can shift a
 * channel by a couple of units depending on the source. 8-bit slack
 * per channel is enough for clean comparisons and tight enough to
 * never false-positive nitpicker bg as themed window_bg (they differ
 * by 19, 20, 22 per channel).
 */
int const COLOR_TOLERANCE = 8;

bool channel_near(int a, int b, int tol) {
	return a >= b ? a - b <= tol : b - a <= tol;
}

bool pixel_is_window(Pixel const &p) {
	return channel_near(p.r(), WIN_R, COLOR_TOLERANCE)
	    && channel_near(p.g(), WIN_G, COLOR_TOLERANCE)
	    && channel_near(p.b(), WIN_B, COLOR_TOLERANCE);
}

/*
 * Bounded cold-boot window. 60s is generous — a clean boot paints the
 * demo-domain window_bg within ~5-10s on this host (proven by
 * alpha_probe's RENDER_POLL_ITERS=1200 cap of ~120s; the W3 probe
 * intentionally tightens the budget to surface the race signature
 * within the test budget, not to be a substitute for alpha). 200ms
 * per poll keeps the probe's own CPU profile negligible.
 */
unsigned const POLL_BUDGET = 300;

} /* anonymous namespace */


struct Qscreen_race_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "qscreen-race-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Qscreen_race_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("qscreen-race-probe: starting (cold-boot single-frame probe)");

		/*
		 * Allocate the shared capture buffer. With nitpicker's empty
		 * <capture/> config the probe receives an unconstrained policy,
		 * so its buffer dimensions define the panorama. This is the
		 * same role fb_sdl plays in the interactive scenario. The
		 * 1024x768 match vesa_fb's set mode and the run scenario's
		 * domain layout — NO warm-up delay before this call.
		 */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Poll 0: the very first capture, no warm-up. Read both sample
		 * points and log the raw pixel values so the Phase 15 reviewer
		 * can distinguish the race signatures:
		 *
		 *   panel=0x1e1e2e demo=0x1e1e2e   (poll 0, normal: panel not
		 *                                    yet painted, demo-domain
		 *                                    empty — neither signal
		 *                                    is decisive yet)
		 *   panel=0x0      demo=0x1e1e2e   (poll 0, race: 1x28 panel
		 *                                    buffer mapped but never
		 *                                    painted; demo-domain
		 *                                    empty as expected)
		 *   panel=0x313244 demo=0x1e1e2e   (poll 0, lucky: panel
		 *                                    already painted — the
		 *                                    race did not strike)
		 */
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px0 = _cap_ds->local_addr<Pixel>();
		Pixel const p0_panel = px0[PANEL_PT.y * SCREEN_W + PANEL_PT.x];
		Pixel const p0_demo  = px0[DEMO_PT.y  * SCREEN_W + DEMO_PT.x];
		Genode::log("qscreen-race-probe: first_capture ",
		            "panel_pixel=0x", Genode::Hex(p0_panel.pixel),
		            " demo_pixel=0x", Genode::Hex(p0_demo.pixel));

		/*
		 * Polls 1..POLL_BUDGET: poll the demo-domain center for the
		 * themed window_bg color. This is the conclusive race signal:
		 *
		 *   - observed within the bounded window -> the demo-domain
		 *     painted, sponge-de's first frame reached nitpicker, the
		 *     race did NOT reproduce this boot.
		 *   - never observed within the bounded window -> sponge-de
		 *     never painted the demo window; combined with a 0x0 panel
		 *     reading at poll 0 (or any poll), this is the race
		 *     signature.
		 *
		 * The Phase 11 fix added the width-floor guard in
		 * panel_widget.cc that prevents the 1x1-stale symptom from
		 * reaching the panel's window dimensions, so a clean boot
		 * should ALWAYS paint within the budget on this topology. A
		 * race verdict here would mean either (a) the guard is
		 * incomplete on this host, or (b) a different 1x1-stale path
		 * exists that was not covered by the Phase 11 fix.
		 */
		bool clean = false;
		unsigned race_panel_black_streak = 0; /* polls with panel=0x0 */
		unsigned race_panel_black_max    = 0; /* longest such streak */
		for (unsigned i = 0; i < POLL_BUDGET; ++i) {
			_timer.msleep(200);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel const panel = px[PANEL_PT.y * SCREEN_W + PANEL_PT.x];
			Pixel const demo  = px[DEMO_PT.y  * SCREEN_W + DEMO_PT.x];

			if (panel.pixel == 0x0u) {
				++race_panel_black_streak;
				if (race_panel_black_streak > race_panel_black_max)
					race_panel_black_max = race_panel_black_streak;
			} else {
				race_panel_black_streak = 0;
			}

			if (i % 10 == 0)
				Genode::log("qscreen-race-probe: poll ", i,
				            " panel=0x", Genode::Hex(panel.pixel),
				            " demo=0x", Genode::Hex(demo.pixel));

			if (pixel_is_window(demo)) {
				clean = true;
				Genode::log("qscreen-race-probe: demo-domain painted ",
				            "at poll ", i, " (race did not reproduce)");
				break;
			}
		}

		/*
		 * Verdict. The Phase 11 race signature was: panel=0x0 from
		 * poll 20 forever. The width-floor guard makes that
		 * extremely unlikely (verified 6/6 + sweep in Phase 11). The
		 * probe logs BOTH the verdict line AND the supporting raw
		 * readings so the policy in D14.8(b) ("race if >= 3 of 10
		 * cold-boot trials reproduce") can be applied by counting the
		 * verdict lines in the evidence log.
		 */
		if (clean) {
			Genode::log("qscreen-race-probe: trial verdict clean ",
			            "race_panel_black_max=", race_panel_black_max,
			            " (consecutive black-streak; 0 = never black)");
		} else {
			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel const final_panel = px[PANEL_PT.y * SCREEN_W + PANEL_PT.x];
			Pixel const final_demo  = px[DEMO_PT.y  * SCREEN_W + DEMO_PT.x];
			Genode::log("qscreen-race-probe: trial verdict race ",
			            "final_panel=0x", Genode::Hex(final_panel.pixel),
			            " final_demo=0x", Genode::Hex(final_demo.pixel),
			            " race_panel_black_max=", race_panel_black_max);
		}

		/*
		 * The probe ALWAYS exits 0 (the run script does not gate on
		 * exit code — it gates on the verdict regex above). Exiting
		 * non-zero on race would make the scenario FAIL on a race
		 * verdict, which would force D14.8(b)'s >= 3/10 reproduction
		 * threshold to be enforced as a hard gate; D14.8(b) instead
		 * records the verdict line and lets the evidence log decide.
		 */
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Qscreen_race_probe inst(env);
}