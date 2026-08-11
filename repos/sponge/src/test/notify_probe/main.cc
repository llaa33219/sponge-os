/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * notify_probe — sponge_notifier acceptance probe (Phase 14 W4).
 *
 * A plain Genode component (no Qt, no libc — AGENTS.md §3.1). It exercises
 * the notification bus end-to-end:
 *
 *   1. The probe opens a Report session labeled "notif_request" and posts
 *      a sentinel notification:
 *        <notification source="notify_probe" kind="info" ttl_ms="3000">
 *          <title>Sponge Phase 14 notification sentinel</title>
 *          <body>W4 probe</body>
 *        </notification>
 *      report_rom (configured in run/sponge-notify.run) routes the
 *      probe's "notif_request" report to sponge_notifier's "notif_request"
 *      ROM. The daemon validates, assigns an id, stores in the active
 *      list, and re-emits the "notifications" ROM.
 *
 *   2. The probe reads the "notifications" ROM (relayed by report_rom
 *      from sponge_notifier's Report) and waits for the sentinel to
 *      appear — that is the daemon-end proof.
 *
 *   3. The probe opens a Capture session on the outer nitpicker and
 *      samples a fixed popover rect. The notifier_widget in sponge-de
 *      renders the popover at a known geometry (see notifier_widget.h,
 *      phase 14 W4 panel popover layout). The non-background fraction
 *      RISES when the popover opens and DROPS when it closes.
 *
 *   4. The probe waits the TTL plus a small slack, then re-samples the
 *      rect (fraction must drop back to baseline) AND re-reads the
 *      notifications ROM (sentinel must be absent).
 *
 *   5. On success, the probe logs exactly "notify-probe: PASS" and exits
 *      0. The run scenario gates on that marker.
 *
 * AGENTS.md §3.1: qualified Genode types, no exceptions. Capability
 * surface: Report (writing), ROM (reading), Capture, Timer.
 */

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
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;

/*
 * Screen geometry (mirrors run/sponge-de-test.run). The popover is
 * drawn in the panel-area, just under the top of the screen; the rect
 * below is sampled by the probe. The notifier_widget MUST paint inside
 * this rect for the probe to detect it — the geometry is documented in
 * notifier_widget.h and the widget's _apply_geometry enforces it.
 */
unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

unsigned const POPOVER_X = 700;
unsigned const POPOVER_Y = 36;
unsigned const POPOVER_W = 300;
unsigned const POPOVER_H = 60;

/* Fraction threshold: popover is "open" if at least 30 % of the popover
 * rect is non-background pixels (the popover has a solid background and
 * glyph, so the threshold is generous against font anti-aliasing). */
unsigned const POPOVER_OPEN_THRESHOLD = 300; /* per-mille */

/* Background color: nitpicker's #1e1e2e (matches the other probes). */
Genode::uint32_t const BG_PIXEL = 0x1e1e2e;

/* Bounded budgets, in milliseconds converted to 100 ms ticks. */
unsigned const NOTIF_POLL_ITERS = 60;          /* ~6 s for daemon broadcast */
unsigned const RENDER_POLL_ITERS = 600;        /* ~60 s for first paint */
unsigned const TTL_EXPIRE_ITERS = 50;          /* ~5 s after TTL boundary */
unsigned const NOTIF_ABSENT_ITERS = 30;        /* ~3 s after popover close */

unsigned const TICK_MS = 100;

char const *SENTINEL_TITLE = "Sponge Phase 14 notification sentinel";
char const *SENTINEL_BODY  = "W4 probe";


struct Notify_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "notify-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };

	/* Wire sides — the probe writes the request, reads the broadcast. */
	Genode::Expanding_reporter     _notif_request { _env, "request", "notif_request" };
	Genode::Attached_rom_dataspace _notif_rom     { _env, "notifications" };

	bool _ok { true };

	Notify_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("notify-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/*
	 * Sample the popover rect and return the non-background fraction
	 * in per-mille. A pixel is "non-background" iff it differs from
	 * the nitpicker bg by at least one channel; the popover has a
	 * solid background + glyphs, so the fraction jumps above the
	 * threshold when the widget paints and falls back to ~0 when it
	 * closes.
	 */
	unsigned _non_bg_fraction()
	{
		if (!_cap_ds.constructed())
			return 0;

		Pixel const *px = _cap_ds->local_addr<Pixel>();

		unsigned non_bg { 0 };
		unsigned const total = POPOVER_W * POPOVER_H;
		for (unsigned dy = 0; dy < POPOVER_H; ++dy) {
			for (unsigned dx = 0; dx < POPOVER_W; ++dx) {
				unsigned const x = POPOVER_X + dx;
				unsigned const y = POPOVER_Y + dy;
				Pixel const p = px[y * SCREEN_W + x];
				if (p.pixel != BG_PIXEL)
					++non_bg;
			}
		}
		return (non_bg * 1000U) / total;
	}

	/*
	 * Wait for the sentinel notification to appear in the
	 * notifications ROM. Returns true on success, false on timeout.
	 */
	bool _wait_sentinel_present()
	{
		for (unsigned i = 0; i < NOTIF_POLL_ITERS; ++i) {
			_notif_rom.update();
			if (_notif_rom.valid()) {
				try {
					Genode::Xml_node const root = _notif_rom.xml();
					if (!root.has_type("notifications"))
						continue;
					bool found { false };
					root.for_each_sub_node("notification",
						[&](Genode::Xml_node const &n) {
							if (found) return;
							Genode::String<128> const title =
								n.sub_node("title").decoded_content<Genode::String<128>>();
							if (title == Genode::String<128>(SENTINEL_TITLE))
								found = true;
						});
					if (found) {
						Genode::log("notify-probe: sentinel appears in notifications ROM (poll ", i, ")");
						return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(TICK_MS);
		}
		return false;
	}

	/*
	 * Wait for the sentinel to disappear from the notifications ROM
	 * (the TTL expired and the daemon popped it). Returns true on
	 * success, false on timeout.
	 */
	bool _wait_sentinel_absent()
	{
		for (unsigned i = 0; i < NOTIF_ABSENT_ITERS; ++i) {
			_notif_rom.update();
			if (_notif_rom.valid()) {
				try {
					Genode::Xml_node const root = _notif_rom.xml();
					bool found { false };
					if (root.has_type("notifications")) {
						root.for_each_sub_node("notification",
							[&](Genode::Xml_node const &n) {
								if (found) return;
								Genode::String<128> const title =
									n.sub_node("title").decoded_content<Genode::String<128>>();
								if (title == Genode::String<128>(SENTINEL_TITLE))
									found = true;
							});
					}
					if (!found) {
						Genode::log("notify-probe: sentinel expired from notifications ROM (poll ", i, ")");
						return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(TICK_MS);
		}
		return false;
	}

	/*
	 * Capture-poll the popover rect. Returns true when the fraction
	 * rises above the threshold within the budget, false on timeout.
	 */
	bool _wait_popover_open()
	{
		for (unsigned i = 0; i < RENDER_POLL_ITERS; ++i) {
			_capture.capture_at(Capture::Point(0, 0));
			unsigned const frac = _non_bg_fraction();
			if (frac >= POPOVER_OPEN_THRESHOLD) {
				Genode::log("notify-probe: popover visible (poll ", i,
				            ", frac=", frac, ")");
				return true;
			}
			if (i % 50 == 0)
				Genode::log("notify-probe: popover poll ", i,
				            " frac=", frac);
			_timer.msleep(TICK_MS);
		}
		return false;
	}

	/*
	 * Capture-poll the popover rect. Returns true when the fraction
	 * drops back below the threshold within the budget, false on
	 * timeout.
	 */
	bool _wait_popover_closed()
	{
		for (unsigned i = 0; i < TTL_EXPIRE_ITERS; ++i) {
			_capture.capture_at(Capture::Point(0, 0));
			unsigned const frac = _non_bg_fraction();
			if (frac < POPOVER_OPEN_THRESHOLD) {
				Genode::log("notify-probe: popover closed (poll ", i,
				            ", frac=", frac, ")");
				return true;
			}
			_timer.msleep(TICK_MS);
		}
		return false;
	}

	/*
	 * Post the sentinel notification. report_rom relays it to
	 * sponge_notifier's "notif_request" ROM.
	 */
	void _post_sentinel()
	{
		_notif_request.generate_xml([&](Genode::Xml_generator &g) {
			g.node("notif_request", [&] {
				g.node("notification", [&] {
					g.attribute("source", "notify_probe");
					g.attribute("kind",   "info");
					g.attribute("ttl_ms", "3000");
					g.node("title", [&] { g.append_sanitized(SENTINEL_TITLE); });
					g.node("body",  [&] { g.append_sanitized(SENTINEL_BODY); });
				});
			});
		});
		Genode::log("notify-probe: posted sentinel (ttl_ms=3000)");
	}

	void run()
	{
		/* Attach the capture dataspace once. */
		_capture.capture_at(Capture::Point(0, 0));
		_cap_ds.construct(_capture.dataspace());

		_timer.msleep(500);  /* let the daemon + widget boot */

		/* Step 1: post the sentinel (the request ROM update happens
		 * synchronously inside report_rom, so the daemon sees it on
		 * the next signal). */
		_post_sentinel();

		/* Step 2: wait for the daemon to re-emit the notifications ROM
		 * with the sentinel. Failure here means the daemon is missing
		 * or the wire is broken. */
		if (!_wait_sentinel_present()) {
			_fail("sentinel did not appear in notifications ROM (daemon unreachable or rejecting)");
			return;
		}

		/* Step 3: Capture-poll the popover rect. The widget in sponge-de
		 * must render the popover. Failure here means the widget is
		 * absent, the route is broken, or the rect geometry is wrong. */
		if (!_wait_popover_open()) {
			_fail("popover did not render within poll window");
			return;
		}

		/* Step 4: wait the TTL plus a small slack so the daemon's timer
		 * can pop the notification and re-emit an empty list. */
		Genode::log("notify-probe: waiting for TTL (3000 ms + slack)");
		_timer.msleep(3500);

		/* Step 5: drop the popover. The non-bg fraction must drop
		 * below the threshold. */
		if (!_wait_popover_closed()) {
			_fail("popover did not close after TTL");
			return;
		}

		/* Step 6: the sentinel must be absent from the notifications
		 * ROM (the daemon popped it). */
		if (!_wait_sentinel_absent()) {
			_fail("sentinel still in notifications ROM after TTL");
			return;
		}

		Genode::log("notify-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Notify_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
