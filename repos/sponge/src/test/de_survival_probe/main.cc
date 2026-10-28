/* SPDX-License-Identifier: Apache-2.0
 *
 * de_survival_probe — P2.1 DE survival probe: package-control failure.
 *
 * Companion of run/sponge-de-pkgd-failure.run. Contract:
 * docs/evidence/phase16-runtime-de-failure-modes.md (P0.4 failure-mode
 * audit, section 4 target degraded-state contract C1-C5, section 5
 * positive controls P1-P5 / negative controls N1-N6, section 6 open
 * questions Q1-Q3). Plan: docs/plans/phase16-runtime-overhaul.md P2.1
 * (this RED test) and P2.5 (the DE hardening that flips it green).
 *
 * What the scenario does (the run script drives QMP; this probe drives
 * the guest-side choreography and every machine-checkable assertion):
 *
 *   1. Publishes the nested `system` sandbox config (v1, healthy) as
 *      the report `system_config` -> report_rom -> ROM `system ->
 *      config` (the same config-relay wiring sponge_supervisor uses,
 *      run/supervisor.inc::sys_config_relay). The `system` sandbox
 *      hosts sponge_pkgd so its state report (`system -> state`,
 *      routed as ROM `system_state`) carries pkgd's heartbeat skips /
 *      exits - the exact input sponge-de's health watcher composes the
 *      panel label from (repos/sponge/src/sponge-de/main.cc).
 *   2. Pre-wedge positive controls (P1-P3) over the REAL QMP input
 *      path: install pkg_gui_demo on the probe channel, watch the
 *      launcher feed go live, sample first paint, click S + the first
 *      launcher entry and witness `launched 'pkg_gui_demo'` (run-script
 *      log gate) + the green first paint (pixel witness).
 *   3. Swaps the launcher menu to a NEVER-LAUNCHED package (hello) and
 *      then injects the failure: config v2 bumps pkgd's start-node
 *      version (init kills the healthy incarnation - Start_model
 *      matches name+version only) and re-routes its `request` ROM to
 *      an unresolvable label, so the replacement incarnation sleeps
 *      forever in its first entrypoint-thread ROM open ("stop because
 *      parent denied"). That is the mode-A shape from the contract:
 *      pkgd's entrypoint wedged, heartbeat skips accumulating. The
 *      heartbeat machinery is accounted for with the product-shaped
 *      `restart_after_skipped: 3 | restart_budget: 3` (churns, then
 *      settles - observed as Q2 below).
 *      WHY THE MENU SWAP: LauncherController::_poll_launch_result
 *      matches a result by (op, pkg) only - a re-launch of the SAME
 *      package during the degraded window would be answered from the
 *      STALE `launcher_result` ROM (the pre-wedge ok) and silently
 *      "succeed" (contract section 3 mode C requires the bounded
 *      timeout + structured error path to be the witnessed outcome).
 *      The degraded-window launch is therefore a package whose result
 *      never arrived.
 *   4. Degraded-window assertions: C3 first (health-label substrate),
 *      then C2 (QMP input flip: the demo `input` report press change
 *      plus fresh popup pixels; launcher_state corroborated), then C4
 *      (the DE's launch op is sent into the wedge - the run script
 *      gates the bounded `launch problem` log line). The machine-
 *      readable half of C4 is asserted against the candidate artifact
 *      ROM `de_op_error` (pre-wired empty via report_rom; see Q3
 *      below) - RED until P2.5 lands the channel.
 *   5. Q2 observation: polls `system_state` through the restart churn
 *      and records which of the three post-exhaustion shapes appears
 *      (node keeps exited(N) / disappears / freezes) - contract
 *      section 6 Q2 must be settled by observation in this run.
 *   6. Recovery (C5): config v3 revives pkgd; the feed window is
 *      measured probe-side (installed ROM republication -> launcher
 *      report update, <= 2 DE poll periods = 3000 ms), the health
 *      label is re-derived, the menu is repopulated via a re-install,
 *      and a post-recovery launch click is proven by the green pixel
 *      (pixels cannot be faked by the stale-result match - the
 *      anti-false-PASS guard for P4). The error-persistence half of
 *      C5 ("the failed operation's error stays visible until
 *      acknowledged") is asserted against `de_op_error` again - RED
 *      until P2.5 lands it.
 *
 * Marker grammar (all lines, grepped by the run script):
 *
 *   de-survival-probe: [<step>] ok|RED - <detail>     per-item outcome
 *   de-survival-probe: [q2] observed|settled|bounded  Q2 observation
 *   de-survival-probe: FAIL <reason>                  harness failure
 *   de-survival-probe: PASS                           ONLY when C1-C5
 *                                                     all pass (N1)
 *   de-survival-probe: verdict GREEN|RED (c4 c5)      final verdict
 *
 * RED is reserved for the two contract items whose mechanisms do not
 * exist yet (per P0.4 section 4: the C4 machine witness and the C5
 * error-persistence / explicit degraded-recovered state). Anything
 * else that cannot be witnessed is a HARNESS failure (FAIL + exit 1) -
 * the P2.1 gate is "RED on the documented missing mechanisms, not
 * because its harness is broken" - so the positive controls P1-P5 and
 * the C1-C3 substrate assertions are hard requirements here.
 *
 * Channel map (report_rom policies live in the run script):
 *
 *   ROM  sponge_de_input   <- sponge-de -> input          (C2 press)
 *   ROM  launcher_state    <- sponge-de -> launcher_state (C2 flip)
 *   ROM  launcher          <- sponge-de -> launcher       (feed witness)
 *   ROM  installed         <- system -> sponge_pkgd -> installed (feed)
 *   ROM  system_state      <- system -> state             (C3 label data)
 *   ROM  probe_result      <- system -> sponge_pkgd -> probe_result
 *   ROM  de_op_error       <- sponge-de -> de_op_error    (C4/C5 candidate;
 *                              empty until P2.5 writes it)
 *   REP  probe_request     -> system -> sponge_pkgd -> probe_request
 *   REP  system_config     -> system -> config            (the config relay)
 *
 * Q1 (contract section 6, probe exclusion): decided WITHOUT touching
 * product code. The probe runs OUTSIDE the `system` sandbox, so its
 * own exit can never enter the system_state label surface; and every
 * label assertion is a SUBSTRING check (N6) over a re-derivation that
 * mirrors the DE's exclusion predicate (alpha_probe / cfg_probe clean
 * exits suppressed, main.cc:652-665) exactly (N3), so the exclusion
 * lists cannot pollute the assertions either way. No edit to the
 * sponge-de or supervisor exclusion lists is made (P2.1 owns test
 * artifacts only).
 *
 * Q3 (contract section 6, operation-error channel): the machine
 * witness is asserted against the candidate ROM `de_op_error` fed by
 * the report `sponge-de -> de_op_error`. P2.5's landing shape, so
 * this probe flips green by changing nothing but the product: a
 * Node-parsable report carrying attributes `op` and `pkg` for the
 * failed operation (e.g. <op_error op="launch" pkg="hello"
 * reason="..."/>) that persists until acknowledged. If P2.5 lands a
 * different label, only the OP_ERROR wiring and the run-script policy
 * change.
 *
 * N4: this probe NEVER writes install/remove/launch on the DE's
 * `launcher_request` channel (the DE's only pkgd writer op is launch,
 * launcher_controller.cc:413-416). Package-control ops go through the
 * dedicated `probe_request` / `probe_result` transport.
 * N5: no assertion requires pkgd to answer while wedged - the in-flight
 * op is recorded as unanswered ON PURPOSE.
 *
 * It is a plain Genode component (Component::construct, no libc/Qt),
 * following AGENTS.md section 3.1 (qualified Genode types). Config is
 * read via the Genode::Node API (the sandbox delivers inline configs
 * in HID format since Genode 26.05; Xml_node would silently see
 * <empty/>).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

/* Screen geometry - matches the run script's nitpicker domains
 * (1024x768 vesa_fb, same as run/sponge-de-sel4-interactive.run). */
unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/* Nitpicker <background> (#1e1e2e) and demo window_bg (#313244,
 * default.theme). Same tolerances as test/sponge_de_probe. */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;
int const WIN_R = 0x31, WIN_G = 0x32, WIN_B = 0x44;
int const GREEN_R = 0x00, GREEN_G = 0xff, GREEN_B = 0x00;
int const COLOR_TOLERANCE = 8;
int const GREEN_TOLERANCE = 32;

struct Pt { int x, y; };
struct Rect { int x, y, w, h; };

/* Sample / click geometry - mirrors test/sponge_de_probe/main.cc
 * (the S toggle center, the doubled PS/2 entry target the launch
 * tablet helper lands at (170,120), the demo-body close point, and
 * the popup / green capture rects). */
Pt const BG_PT       { 900, 100 };
Pt const DEMO_PT     { 512, 412 };
Pt const S_TOGGLE    { 32, 14 };
Pt const CLOSE_PT    { 512, 412 };
Pt const POPUP_HEADING_PT { 170, 44 };
/*
 * Column scanned + clicked by the capture-based entry-button locator.
 * Derived from three instrumented runs: the button fill is window_bg
 * (#313244, launcher_menu_view.cc::_entry_stylesheet) but the label
 * is LEFT-aligned from x ~= 20 (shatters a center-column color run),
 * the button does not extend to the popup's right half (x=170/280
 * see through the QPA-shifted popup onto the demo window, also
 * #313244), and the demo window starts at x=192. x=14 is inside the
 * button's left fill margin (edge x:8 + border-radius corner is only
 * at the top/bottom few pixels) and cannot touch label glyphs or the
 * demo window, at any of the observed popup mappings.
 */
int const ENTRY_SCAN_X = 14;
int const ENTRY_CLICK_X = 14;
/*
 * Popup detection rect. Deliberately x:0..190: the demo window
 * (window_bg #313244) starts at x=192 and overlaps the wider popup
 * area, where it alone clears the non-bg threshold (9.2% of
 * (0,30,340,210)) and turns an absent popup into a false "open" -
 * observed across runs 8-10. The popup's heading + entry button live
 * at x:8..~120, so this rect sees popup content only.
 */
Rect const POPUP_RECT  { 0, 30, 190, 180 };
Rect const GREEN_RECT  { 80, 80, 100, 80 };
Rect const TOGGLE_RECT { 8, 4, 48, 60 };

float const POPUP_OPEN_THRESH = 0.05f;
float const POPUP_CLOSED_THRESH = 0.01f;
float const GREEN_THRESH = 0.25f;
float const TOGGLE_PRESENT_THRESH = 0.20f;

/* C5 feed-window bound: contract section 4 C5 - "within <= 2 poll
 * periods (<= 3 s at the 1500 ms cadence)". The probe polls the ROMs
 * every 100 ms and measures the window in whole poll steps. */
unsigned const C5_WINDOW_MAX_MS = 3000;

/*
 * Q2 settle bound: the heartbeat skip period is 2000 ms, so a shape
 * is only "frozen" (exhausted / stopped / vanished) after it stopped
 * moving for longer than one skip period. 30 polls x 100 ms = 3 s.
 */
unsigned const Q2_STABLE_POLLS = 30;

bool channel_near(int a, int b, int tol)
{
	return a >= b ? a - b <= tol : b - a <= tol;
}

bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R, COLOR_TOLERANCE)
	    && channel_near(p.g(), BG_G, COLOR_TOLERANCE)
	    && channel_near(p.b(), BG_B, COLOR_TOLERANCE);
}

bool pixel_is_window(Pixel const &p)
{
	return channel_near(p.r(), WIN_R, COLOR_TOLERANCE)
	    && channel_near(p.g(), WIN_G, COLOR_TOLERANCE)
	    && channel_near(p.b(), WIN_B, COLOR_TOLERANCE);
}

bool pixel_is_green(Pixel const &p)
{
	return channel_near(p.r(), GREEN_R, GREEN_TOLERANCE)
	    && channel_near(p.g(), GREEN_G, GREEN_TOLERANCE)
	    && channel_near(p.b(), GREEN_B, GREEN_TOLERANCE);
}

bool pixel_is_non_bg(Pixel const &p) { return !pixel_is_bg(p); }

/*
 * Health-label re-derivation (contract C3 / N3 / N6). The label text
 * is composed in exactly one place (sponge-de/main.cc:652-677) from
 * the `system_state` ROM: `<name> exited(<code>)` for exits (clean
 * exits of alpha_probe / cfg_probe suppressed) and `<name>
 * unresponsive(<skipped>)` for heartbeat skips, segments joined with
 * " \xc2\xb7 " (U+00B7). This probe reads the SAME ROM and mirrors the
 * predicate byte-for-byte, so a substring claim over the derived
 * aggregate is a claim about the label's content - without pixel-
 * scraping the panel and without whole-label exact matches (N6).
 */
struct Health_view
{
	bool  any_segment = false;
	bool  pkgd_unresponsive = false;
	long  pkgd_skipped = 0;
	bool  pkgd_exited = false;
	long  pkgd_exit_code = 0;
	unsigned other_segments = 0;
	/* Raw shape of the pkgd child node, for the Q2 observation. */
	bool  pkgd_seen = false;
	bool  pkgd_node_exited = false;
	long  pkgd_node_code = 0;
	long  pkgd_node_skipped = 0;

	void derive(Genode::Attached_rom_dataspace &rom)
	{
		any_segment = false;
		pkgd_unresponsive = false;
		pkgd_skipped = 0;
		pkgd_exited = false;
		pkgd_exit_code = 0;
		other_segments = 0;
		pkgd_seen = false;
		pkgd_node_exited = false;
		pkgd_node_code = 0;
		pkgd_node_skipped = 0;

		rom.update();
		if (!rom.valid() || rom.size() == 0)
			return;

		Genode::Hid_node const sstate(Genode::Const_byte_range_ptr(
			rom.local_addr<char const>(), rom.size()));

		sstate.for_each_sub_node([&](Genode::Hid_node const &c) {
			if (!c.has_type("child"))
				return;

			Genode::String<48> const name =
				c.attribute_value("name", Genode::String<48>());

			bool const probe =
				name == Genode::String<48>("alpha_probe") ||
				name == Genode::String<48>("cfg_probe");

			bool const is_pkgd =
				name == Genode::String<48>("sponge_pkgd");

			if (c.has_attribute("exited")) {
				long const code = c.attribute_value("exited", 0L);
				if (is_pkgd) {
					pkgd_seen = true;
					pkgd_node_exited = true;
					pkgd_node_code = code;
				}
				/* N3: mirror the DE's exclusion predicate. */
				if (code != 0 || !probe) {
					any_segment = true;
					if (is_pkgd) {
						pkgd_exited = true;
						pkgd_exit_code = code;
					} else {
						++other_segments;
					}
				}
				return;
			}

			long const skipped =
				c.attribute_value("skipped_heartbeats", 0L);
			if (is_pkgd) {
				pkgd_seen = true;
				pkgd_node_exited = false;
				pkgd_node_skipped = skipped;
			}
			if (skipped > 0) {
				any_segment = true;
				if (is_pkgd) {
					pkgd_unresponsive = true;
					pkgd_skipped = skipped;
				} else {
					++other_segments;
				}
			}
		});
	}

	/* Contract C3 substring targets (never whole-label text, N6). */
	bool pkgd_health_segment() const
	{
		return pkgd_unresponsive || pkgd_exited;
	}

	/* Q2 shape signature: moves as the restart churn progresses. */
	bool same_shape(Health_view const &o) const
	{
		return pkgd_seen == o.pkgd_seen
		    && pkgd_node_exited == o.pkgd_node_exited
		    && pkgd_node_code == o.pkgd_node_code
		    && pkgd_node_skipped == o.pkgd_node_skipped;
	}
};

} /* anonymous namespace */


struct De_survival_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "de-survival-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Attached_rom_dataspace _config { _env, "config" };

	/* report_rom-relayed channels (see the header channel map). */
	Genode::Attached_rom_dataspace _input_rom      { _env, "sponge_de_input" };
	Genode::Attached_rom_dataspace _launcher_state { _env, "launcher_state" };
	Genode::Attached_rom_dataspace _launcher       { _env, "launcher" };
	Genode::Attached_rom_dataspace _installed      { _env, "installed" };
	Genode::Attached_rom_dataspace _system_state   { _env, "system_state" };
	Genode::Attached_rom_dataspace _probe_result   { _env, "probe_result" };
	Genode::Attached_rom_dataspace _op_error       { _env, "de_op_error" };

	/* Writers: the probe channel to pkgd + the system-config relay.
	 * The request reporter's NODE TYPE is "request" (the report root
	 * element pkgd validates - it rejects anything else with
	 * "request root is not <request>"); the LABEL "probe_request" is
	 * what report_rom's policy matches. */
	Genode::Expanding_reporter _probe_request {
		_env, "request", "probe_request" };
	Genode::Reporter _system_config {
		_env, "config", "system_config", 16 * 1024 };

	/* Outcome ledger for the verdict. */
	bool _red_c4 = false;
	bool _red_c5 = false;

	/* Snapshot buffers for report-change detection (the C5 feed
	 * window + the recovery republication). */
	char _installed_snap[4096] { };
	Genode::size_t _installed_snap_sz = 0;
	char _launcher_snap[4096] { };
	Genode::size_t _launcher_snap_sz = 0;


	De_survival_probe(Genode::Env &env) : _env(env) { }


	void run()
	{
		Genode::log("de-survival-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		unsigned const render_iters =
			_config.valid()
			? _config.node().attribute_value("render_iters", 1800u)
			: 1800u;

		/* 1. Publish the healthy system config first so pkgd boots
		 *    early (its `installed` broadcast must be live by the end
		 *    of the first-paint wait). */
		_publish_system_config("1", false);
		Genode::log("de-survival-probe: [boot] published system config v1 "
		            "(pkgd healthy, heartbeat 2000 ms, restart 3|3)");

		/* First paint (contract C1 / D16R.11): the nitpicker background
		 * and the themed demo window are both composited. */
		if (!_wait_first_paint(render_iters))
			return;

		Health_view h { };
		h.derive(_system_state);
		for (unsigned i = 0; i < 600 && !h.pkgd_seen; ++i) {
			_timer.msleep(100);
			h.derive(_system_state);
		}
		if (!h.pkgd_seen)
			_fail("boot: system_state carries no sponge_pkgd node "
			      "(the system sandbox is not routing its state report)");
		Genode::log("de-survival-probe: [boot] ok - first paint observed; "
		            "system_state + installed feeds live");

		/* ---- P2/P3 pre-wedge positive controls ---- */

		if (!_pkg_op("install", "pkg_gui_demo", "1", "ok", 30000))
			return;
		Genode::log("de-survival-probe: [install] ok - pkgd confirmed "
		            "install pkg_gui_demo (probe channel; N4: never the "
		            "DE launcher_request)");

		if (!_wait_launcher_app("pkg_gui_demo", true, 60000))
			_fail("P2: launcher feed did not list pkg_gui_demo after the "
			      "install (feed is not live)");
		Genode::log("de-survival-probe: [p2] ok - launcher feed live "
		            "(launcher report lists pkg_gui_demo)");

		h.derive(_system_state);
		if (h.any_segment)
			_fail("P3: health label substrate is not clean before the "
			      "wedge (derived warnings non-empty)");
		Genode::log("de-survival-probe: [p3] ok - health label pre-wedge "
		            "absent (derived warnings empty)");

		if (!_sample_first_paint())
			return;

		/* ---- P1: pre-wedge launch click over the real input path ---- */

		_open_popup("p1");
		/*
		 * Informational only: the panel-button path opens the popup
		 * without publishing launcher_state (only toggle_launcher(),
		 * the keyboard-shortcut entry, and close_popup() publish -
		 * verified against the runtime trace: the report stays
		 * "open: no" across button clicks). The contract accepts the
		 * demo `input` report flip as an equal C2/P5 witness; that is
		 * the hard assertion (below and in the C2 phase).
		 */
		bool const state_flip =
			_wait_launcher_state("yes", 100);
		Genode::log("de-survival-probe: [p1] launcher_state open=yes ",
		            state_flip ? "observed" : "not published on the "
		            "button path (expected; input report is the witness)");
		Genode::log("de-survival-probe: [p1] ok - popup opened (input + "
		            "launcher_state ", state_flip ? "open=yes" : "n/a", ")");

		_click_entry("p1");
		/* Run script gates: sponge-de: launched 'pkg_gui_demo' +
		 * pkg_gui_demo: window shown. Pixels are the real-launch
		 * witness (a stale-result match cannot paint green). */
		if (!_wait_green(2000, "p1"))
			return;
		Genode::log("de-survival-probe: [p1] ok - pkg_gui_demo green first "
		            "paint sampled (real launch proven by pixels)");

		(void)_wait_popup(false, 3000, "p1 close");
		Genode::log("de-survival-probe: [p1] ok - popup closed (entry handler)");
		_done("p1");

		/* ---- Menu swap: the degraded-window launch must be FRESH ---- */

		/*
		 * Install hello WITHOUT removing pkg_gui_demo: destroying a
		 * Qt6 PD mid-run and creating hello's PD right after hits a
		 * seL4 teardown/cap overlap on this host ("null cap #304",
		 * "create_thread: invalid PD argument", "dynamic linker is
		 * an invalid dataspace" - runs 13/15), which also poisoned
		 * the later version-bump restarts. The template never
		 * destroys a Qt PD mid-run and the product's supervisor
		 * kills are small-PD restarts. With both packages installed
		 * the menu is name-sorted (pkgd's broadcast order), so the
		 * TOPMOST entry is hello - the entry scan picks the topmost
		 * button run.
		 */
		if (!_pkg_op("install", "hello", "3", "ok", 30000))
			return;
		if (!_wait_launcher_app("hello", true, 60000))
			_fail("swap: launcher feed did not list hello after the "
			      "install");
		Genode::log("de-survival-probe: [swap] ok - hello installed "
		            "(the degraded-window launch target is a package "
		            "whose launcher_result never arrived - the stale ok "
		            "would otherwise false-match; topmost entry is "
		            "hello, name-sorted)");

		_open_popup("c2");

		/* ---- The injection: kill + constructor-wedge sponge_pkgd ---- */

		_snapshot(_installed, _installed_snap, _installed_snap_sz);
		_snapshot(_launcher, _launcher_snap, _launcher_snap_sz);

		_probe_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "explain");
			g.attribute("pkg", "hello");
			g.attribute("seq", "4");
		});
		Genode::log("de-survival-probe: [wedge] in-flight explain hello "
		            "sent (will not be answered - expected, N5)");

		/*
		 * Bounded wedge-republish retries: the version-bump restart
		 * races the seL4 destroy/create cap teardown (run33: the v2
		 * incarnation failed "create_thread: invalid PD argument" /
		 * "dynamic linker is an invalid dataspace" and sat at state:
		 * incomplete with no heartbeat skips at all, while run32's
		 * identical publish worked). Each retry publishes the same
		 * wedge under a fresh version to force another restart; the
		 * C3 latch accepts the health signal from any incarnation.
		 */
		char const *const vers[] = { "2", "2r1", "2r2" };
		bool latched = false;
		for (unsigned attempt = 0; attempt < 3 && !latched; ++attempt) {
			_publish_system_config(vers[attempt], true);
			Genode::log("de-survival-probe: [wedge] injected - system config ",
			            vers[attempt], " published (denied-ROM constructor "
			            "wedge; restart_budget 3|3 accounted for)");
			latched = _wait_health_segment(45000);
		}

		/* ---- C3 first: the health-label substrate ---- */

		if (!latched)
			_fail("C3: neither 'sponge_pkgd unresponsive(' nor "
			      "'sponge_pkgd exited(' ever appeared in the derived "
			      "health label across 3 wedge publishes (harness or "
			      "platform restart failure)");
		h.derive(_system_state);
		Genode::log("de-survival-probe: [c3] ok - health label witness: '",
		            h.pkgd_unresponsive ? "sponge_pkgd unresponsive("
		                                : "sponge_pkgd exited(",
		            h.pkgd_unresponsive ? h.pkgd_skipped : h.pkgd_exit_code,
		            "' (substring; sibling segments tolerated, N3/N6; "
		            "other_segments=", h.other_segments, ")");

		/* ---- C2: input during the degraded window (the popup window
		 *     stays short so the C4 entry click follows at once) ---- */

		char press_before[32] = "";
		_read_press(press_before, sizeof(press_before));
		_click(POPUP_HEADING_PT, "c2");
		if (!_popup_is_open())
			_fail("C2: the pre-wedge launcher is not visible during the "
			      "degraded window");
		if (!_wait_new_press(press_before, 60000))
			_fail("C2: no input press arrived at sponge-de during the "
			      "degraded window (QMP input path broken)");
		_launcher_state.update();
		Genode::log("de-survival-probe: [c2] ok - QMP input reached the DE "
		            "in the degraded window (input press flip + popup "
		            "painted; C1 keeps-rendering; launcher_state logged, "
		            "button path publishes no flip - contract P5 'or "
		            "input report activity')");
		_done("c2");

		/* ---- C4: bounded structured error on the DE's launch op ---- */

		_click_entry("c4");
		/* Run script gate: sponge-de: launch problem surfaced on the
		 * panel: launch 'hello' failed: no answer from the package
		 * manager (bounded ~6 s - the designed degraded outcome). */

		if (_wait_op_error(10000)) {
			Genode::log("de-survival-probe: [c4] ok - operation-error "
			            "machine witness present (de_op_error populated)");
		} else {
			_red_c4 = true;
			Genode::log("de-survival-probe: [c4] RED - operation-error "
			            "machine witness absent: ROM de_op_error empty "
			            "10 s after the bounded launch failure (contract "
			            "section 4 C4 gap: log-only transient witness "
			            "today; Q3 candidate channel)");
		}
		_done("c4");

		/* ---- Q2: observe the restart-churn outcome shape ---- */

		_observe_q2(60000);

		/* ---- Recovery (C5) ---- */

		char const *const rec[] = { "3", "3r1", "3r2" };
		bool revived = false;
		unsigned t1_ms = 0, t2_ms = 0;
		for (unsigned attempt = 0; attempt < 3 && !revived; ++attempt) {
			_publish_system_config(rec[attempt], false);
			Genode::log("de-survival-probe: [recovery] published system "
			            "config ", rec[attempt], " (healthy route - "
			            "revival)");
			revived = _wait_snap_change(_installed, _installed_snap,
			                            _installed_snap_sz, 20000, t1_ms);
			if (!revived)
				Genode::log("de-survival-probe: [recovery] no feed yet - "
				            "republishing (seL4 destroy/create cap race?)");
		}

		/* C5 feed window: installed republication -> launcher report. */
		if (!revived)
			_fail("C5: installed was never republished after revival");
		if (!_wait_snap_change(_launcher, _launcher_snap,
		                       _launcher_snap_sz, 10000, t2_ms)) {
			_red_c5 = true;
			Genode::log("de-survival-probe: [c5] RED - installed "
			            "republicated but the launcher feed never updated "
			            "(feed is not live after revival)");
		} else {
			unsigned const window_ms =
				t2_ms >= t1_ms ? t2_ms - t1_ms : 0;
			if (window_ms > C5_WINDOW_MAX_MS) {
				_red_c5 = true;
				Genode::log("de-survival-probe: [c5] RED - installed "
				            "republicated; launcher feed live in ",
				            window_ms, " ms (> ", C5_WINDOW_MAX_MS,
				            " ms bound; contract section 4 C5 window "
				            "violated)");
			} else {
				Genode::log("de-survival-probe: [c5] ok - installed "
				            "republicated; launcher feed live in ",
				            window_ms, " ms (<= ", C5_WINDOW_MAX_MS, ")");
			}
		}

		h.derive(_system_state);
		if (h.any_segment) {
			_red_c5 = true;
			Genode::log("de-survival-probe: [c5] RED - health label did "
			            "not clear after revival (derived warnings still "
			            "non-empty: pkgd_unresponsive=",
			            h.pkgd_unresponsive ? h.pkgd_skipped : -1L,
			            " pkgd_exited=",
			            h.pkgd_exited ? h.pkgd_exit_code : -1L, ")");
		} else {
			Genode::log("de-survival-probe: [c5] ok - health label "
			            "cleared after revival (derived warnings empty)");
		}

		/* Menu repopulation + P4: post-recovery launch click. The
		 * re-install targets pkg_gui_demo again (its stale
		 * launcher_result is harmless because the REAL launch is
		 * proven by the green pixel below - the anti-false-PASS
		 * guard). */
		if (!_pkg_op("install", "pkg_gui_demo", "5", "ok", 30000))
			return;
		if (!_wait_launcher_app("pkg_gui_demo", true, 60000)) {
			_red_c5 = true;
			Genode::log("de-survival-probe: [c5] RED - menu repopulated "
			            "check failed (launcher report lacks "
			            "pkg_gui_demo after the re-install)");
		} else {
			Genode::log("de-survival-probe: [c5] ok - menu repopulated "
			            "(launcher report lists pkg_gui_demo)");
		}

		/* P4: deterministic popup state - close anything left over
		 * from the degraded window, then open + launch. */
		_open_popup("p4");
		Genode::log("de-survival-probe: [p4] ok - popup opened");

		_click_entry("p4");
		/* Run script gates: sponge-de: launched 'pkg_gui_demo' +
		 * pkg_gui_demo: window shown. */
		if (!_wait_green(2000, "p4"))
			return;
		Genode::log("de-survival-probe: [p4] ok - pkg_gui_demo green first "
		            "paint sampled post-recovery (P4 ok again; pixels "
		            "guard against the stale-result false PASS)");

		(void)_wait_popup(false, 3000, "p4 close");
		Genode::log("de-survival-probe: [p4] ok - popup closed (entry handler)");
		_done("p4");

		/* ---- C5 error persistence ("until acknowledged") ---- */

		if (_wait_op_error(2000)) {
			Genode::log("de-survival-probe: [c5] ok - operation error "
			            "still visible after recovery (persists until "
			            "acknowledged)");
		} else {
			_red_c5 = true;
			Genode::log("de-survival-probe: [c5] RED - the failed "
			            "operation's error is not persistent: de_op_error "
			            "empty after recovery (contract section 4 C5: "
			            "independent liveness is not hiding the failure - "
			            "the error must stay visible until acknowledged)");
		}

		/* ---- Verdict ---- */

		if (!_red_c4 && !_red_c5) {
			Genode::log("de-survival-probe: PASS");
			Genode::log("de-survival-probe: verdict GREEN");
		} else {
			Genode::String<32> items;
			if (_red_c4 && _red_c5)
				items = Genode::String<32>("c4 c5");
			else if (_red_c4)
				items = Genode::String<32>("c4");
			else
				items = Genode::String<32>("c5");
			Genode::log("de-survival-probe: verdict RED (", items, ")");
		}
		Genode::sleep_forever();
	}


	/* ---- system config relay (v1/v3 healthy, v2 wedged) ---- */

	void _publish_system_config(char const *version, bool wedge)
	{
		_system_config.enabled(true);

		static char const *const parent_services[] = {
			"ROM", "PD", "RM", "CPU", "LOG", "Timer", "Report"
		};

		Genode::Reporter::Result const res =
			_system_config.generate_xml([&](Genode::Xml_generator &g) {
			g.node("parent-provides", [&] {
				for (char const *s : parent_services)
					g.node("service", [&] { g.attribute("name", s); });
			});
			g.node("default", [&] {
				g.attribute("caps", "200");
				g.attribute("ram", "4M");
			});
			g.node("default-route", [&] {
				g.node("any-service", [&] {
					g.node("parent", [&] { });
				});
			});
			/* The state report carries init/child RAM + caps at a 1 s
			 * cadence - the "state-report keeps updating" side of the
			 * N2 substrate-health witness. */
			g.node("report", [&] {
				g.attribute("init_ram",   "yes");
				g.attribute("child_ram",  "yes");
				g.attribute("child_caps", "yes");
				g.attribute("delay_ms",   "1000");
			});
			g.node("heartbeat", [&] {
				g.attribute("rate_ms", "2000");
			});
			g.node("start", [&] {
				g.attribute("name", "sponge_pkgd");
				g.attribute("version", version);
				/*
				 * Cap ceiling with seL4 headroom (docs/09 section 11.1
				 * signature: ram=0/caps=N upgrades with a tight quota
				 * wedge the child silently). The product ships 200; the
				 * current tree's pkgd requests cap_quota=3 over it at
				 * startup, and init can never grant past the configured
				 * ceiling ("deferring to the next config application").
				 * 400 covers 203 + the install-time metadata ROM
				 * sessions + margin.
				 */
				g.attribute("caps", "800");
				g.attribute("ram", "16M");
				/*
				 * Product-shaped heartbeat containment
				 * (run/sponge-desktop-disk.run): 3 skipped heartbeats
				 * respawn, 3 respawns budget. The wedge incarnation is
				 * given a short startup grace so its constructor sleep
				 * shows up as skipped_heartbeats quickly; the healthy
				 * incarnations get the long grace the product uses.
				 */
				g.node("heartbeat", [&] {
					g.attribute("restart_after_skipped", "3");
					g.attribute("restart_budget", "3");
					g.attribute("startup_grace_ms",
					            wedge ? "3000" : "30000");
				});
				g.node("config", [&] {
					g.node("launcher_request", [&] { });
					g.node("probe_request", [&] { });
				});
				g.node("route", [&] {
					/*
					 * The product's section 4.6 ld.lib.so route
					 * FIRST: every child in
					 * run/sponge-desktop-disk.run carries it, and
					 * without it the v2 wedge rule broke the
					 * child's dynamic-linker load ("dynamic linker
					 * is an invalid dataspace", run13) even though
					 * the label_last matcher should not have
					 * matched it. The explicit rule makes the
					 * linker path independent of the other rules.
					 */
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_last", "ld.lib.so");
						g.node("parent", [&] { });
					});
					if (wedge) {
						/*
						 * The mode-A injection: pkgd's first
						 * entrypoint-thread session (the `request`
						 * ROM - a plain Attached_rom_dataspace
						 * member opened during Main's construction)
						 * is routed to a label no server provides -
						 * "stop because parent denied" sleeps the
						 * constructing thread forever (base Env
						 * policy), so the entrypoint never answers
						 * heartbeats and the skips accumulate.
						 * Exact `label` match (the scoped child
						 * label) - not label_last, which broke the
						 * linker load in run13.
						 */
						g.node("service", [&] {
							g.attribute("name", "ROM");
							g.attribute("label", "request");
							g.node("parent", [&] {
								g.attribute("label",
								            "sponge_pkgd_wedge_denied");
							});
						});
					}
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.node("parent", [&] { });
					});
					g.node("service", [&] {
						g.attribute("name", "Report");
						g.node("parent", [&] { });
					});
					g.node("any-service", [&] {
						g.node("parent", [&] { });
					});
				});
			});
		});

		if (res == Genode::Buffer_error::EXCEEDED)
			_fail("system config report exceeds the 16 KB buffer");
	}


	/* ---- probe-channel package-control ops (N4) ---- */

	bool _pkg_op(char const *op, char const *pkg, char const *seq,
	             char const *want_status, unsigned max_ms)
	{
		/*
		 * (Re)issue the request every 500 ms while polling. The
		 * re-issue is REQUIRED, not a nicety: pkgd arms its
		 * probe_request ROM sigh only at the end of its bootstrap
		 * (_init_launcher_channel) and never drains content that was
		 * written before the arming (only the main `request` channel
		 * gets a drain, sponge_pkgd/main.cc:_handle_request call at
		 * the bootstrap tail). A request written earlier - e.g. right
		 * at [boot], before pkgd's channels arm - is therefore lost
		 * until the ROM changes again (observed run2: pkgd ready +
		 * "probe_request channel enabled", then silence until the
		 * 30 s bound). Every retry keeps the SAME seq, so pkgd's
		 * op|pkg|seq duplicate-request replay makes the retries
		 * idempotent (a processed request is replayed, not re-run);
		 * the changing `retry` attribute guarantees a ROM content
		 * change so the sigh fires.
		 */
		unsigned retry = 0;
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			if (i % 5 == 0) {
				_probe_request.generate_xml([&](Genode::Xml_generator &g) {
					g.attribute("op",  op);
					g.attribute("pkg", pkg);
					g.attribute("seq", seq);
					g.attribute("retry", retry);
				});
				++retry;
			}
			_timer.msleep(100);
			_probe_result.update();
			if (!_probe_result.valid())
				continue;
			try {
				Genode::Xml_node const r = _probe_result.xml();
				if (!r.has_type("result"))
					continue;
				if (r.attribute_value("op", Genode::String<32>()) !=
				    Genode::String<32>(op))
					continue;
				if (r.attribute_value("pkg", Genode::String<128>()) !=
				    Genode::String<128>(pkg))
					continue;
				Genode::String<32> const status =
					r.attribute_value("status", Genode::String<32>());
				if (status == Genode::String<32>(want_status))
					return true;
				_fail("probe channel: operation answered with an "
				      "unexpected status (see serial log)");
			} catch (Genode::Xml_node::Invalid_syntax) {
				continue;
			}
		}
		_fail("probe channel: no result for the operation within bound");
		return false;
	}


	/* ---- feed / label witnesses ---- */

	template <Genode::size_t CAP>
	void _snapshot(Genode::Attached_rom_dataspace &rom,
	               char (&buf)[CAP], Genode::size_t &sz)
	{
		rom.update();
		sz = 0;
		if (!rom.valid())
			return;
		Genode::size_t const n =
			rom.size() < CAP ? rom.size() : (Genode::size_t)CAP;
		Genode::memcpy(buf, rom.local_addr<char const>(), n);
		sz = n;
	}

	template <Genode::size_t CAP>
	bool _changed(Genode::Attached_rom_dataspace &rom,
	              char const (&buf)[CAP], Genode::size_t sz)
	{
		rom.update();
		if (!rom.valid())
			return false;
		if (rom.size() != sz)
			return true;
		return Genode::memcmp(buf, rom.local_addr<char const>(), sz) != 0;
	}

	template <Genode::size_t CAP>
	bool _wait_snap_change(Genode::Attached_rom_dataspace &rom,
	                       char (&buf)[CAP], Genode::size_t &sz,
	                       unsigned max_ms, unsigned &elapsed_ms)
	{
		elapsed_ms = 0;
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			elapsed_ms += 100;
			if (_changed(rom, buf, sz)) {
				_snapshot(rom, buf, sz);
				return true;
			}
		}
		return false;
	}

	bool _wait_launcher_app(char const *want_name, bool want_present,
	                        unsigned max_ms)
	{
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			_launcher.update();
			if (!_launcher.valid())
				continue;
			bool found = false;
			try {
				Genode::Xml_node const root = _launcher.xml();
				root.for_each_sub_node("app",
					[&](Genode::Xml_node const &a) {
						if (a.attribute_value("name",
						        Genode::String<64>()) ==
						    Genode::String<64>(want_name))
							found = true;
					});
			} catch (Genode::Xml_node::Invalid_syntax) {
				continue;
			}
			if (found == want_present)
				return true;
		}
		return false;
	}

	bool _wait_health_segment(unsigned max_ms)
	{
		Health_view h { };
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			h.derive(_system_state);
			if (h.pkgd_health_segment())
				return true;
		}
		return false;
	}

	/*
	 * Q2 observer (contract section 6): watch the pkgd node shape
	 * through the heartbeat churn and record which post-exhaustion
	 * option materializes (keeps exited(N) / disappears / freezes at
	 * unresponsive(N)). Settles once the shape stops moving for longer
	 * than one heartbeat skip period; bounded at max_ms.
	 */
	void _observe_q2(unsigned max_ms)
	{
		Health_view h { }, last { };
		bool have_last = false;
		unsigned stable = 0;
		unsigned elapsed = 0;

		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			elapsed += 100;
			h.derive(_system_state);

			if (!have_last || !h.same_shape(last)) {
				last = h;
				have_last = true;
				stable = 0;
				Genode::log("de-survival-probe: [q2] observed - pkgd "
				            "node: seen=", (int)h.pkgd_seen,
				            " exited=", (int)h.pkgd_node_exited,
				            " code=", h.pkgd_node_code,
				            " skipped=", h.pkgd_node_skipped,
				            " (t=", elapsed, " ms)");
			} else {
				++stable;
			}

			bool const terminal =
				(h.pkgd_seen && h.pkgd_node_exited) ||
				!h.pkgd_seen ||
				(h.pkgd_seen && !h.pkgd_node_exited &&
				 h.pkgd_node_skipped > 0);

			if (terminal && stable >= Q2_STABLE_POLLS) {
				if (!h.pkgd_seen)
					Genode::log("de-survival-probe: [q2] settled - node "
					            "disappeared after churn (t=", elapsed,
					            " ms)");
				else if (h.pkgd_node_exited)
					Genode::log("de-survival-probe: [q2] settled - node "
					            "keeps exited(", h.pkgd_node_code,
					            ") after churn (t=", elapsed, " ms)");
				else
					Genode::log("de-survival-probe: [q2] settled - node "
					            "froze at unresponsive(",
					            h.pkgd_node_skipped, ") after churn (t=",
					            elapsed, " ms)");
				return;
			}
		}
		Genode::log("de-survival-probe: [q2] bounded - window expired (t=",
		            elapsed, " ms) with pkgd node: seen=", (int)h.pkgd_seen,
		            " exited=", (int)h.pkgd_node_exited,
		            " code=", h.pkgd_node_code,
		            " skipped=", h.pkgd_node_skipped);
	}


	/* ---- C4/C5 machine witness (the P2.5 landing spot, Q3) ---- */

	bool _wait_op_error(unsigned max_ms)
	{
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			_op_error.update();
			if (!_op_error.valid() || _op_error.size() == 0)
				continue;
			/* P2.5 landing shape (Q3): a Node-parsable artifact with
			 * op + pkg attributes for the failed operation. */
			try {
				Genode::Node const r(Genode::Const_byte_range_ptr(
					_op_error.local_addr<char const>(),
					_op_error.size()));
				Genode::String<32> const op =
					r.attribute_value("op", Genode::String<32>());
				Genode::String<128> const pkg =
					r.attribute_value("pkg", Genode::String<128>());
				if (op == Genode::String<32>("launch") &&
				    pkg == Genode::String<128>("hello"))
					return true;
			} catch (...) {
				continue;
			}
		}
		return false;
	}


	/* ---- input witnesses (C2 / P5) ---- */

	void _read_press(char *buf, Genode::size_t buf_sz)
	{
		buf[0] = 0;
		_input_rom.update();
		if (!_input_rom.valid())
			return;
		try {
			Genode::Xml_node const r = _input_rom.xml();
			if (r.has_attribute("press")) {
				Genode::String<64> const v =
					r.attribute_value("press", Genode::String<64>());
				Genode::copy_cstring(buf, v.string(), buf_sz);
			}
		} catch (Genode::Xml_node::Invalid_syntax) {
			buf[0] = 0;
		}
	}

	bool _wait_new_press(char const *last, unsigned max_ms)
	{
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			char cur[32] = "";
			_read_press(cur, sizeof(cur));
			if (cur[0] && Genode::strcmp(cur, last) != 0)
				return true;
		}
		return false;
	}

	bool _wait_launcher_state(char const *want_open, unsigned max_ms)
	{
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			_launcher_state.update();
			if (!_launcher_state.valid())
				continue;
			/* The reporter emits HID (`launcher_state | open: yes`).
			 * The Node API auto-detects the format. */
			try {
				Genode::Node const r(Genode::Const_byte_range_ptr(
					_launcher_state.local_addr<char const>(),
					_launcher_state.size()));
				if (r.has_type("launcher_state") &&
				    r.attribute_value("open", Genode::String<8>()) ==
				        Genode::String<8>(want_open))
					return true;
			} catch (...) {
				continue;
			}
		}
		return false;
	}


	/* ---- pixel witnesses (C1 / P1 / P4; anti-false-PASS) ---- */

	float _fraction(Rect r, bool (*pred)(Pixel const &))
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned long total = 0, hit = 0;
		int const y_end = r.y + r.h;
		int const x_end = r.x + r.w;
		for (int y = r.y; y < y_end && y < (int)SCREEN_H; ++y) {
			if (y < 0) continue;
			for (int x = r.x; x < x_end && x < (int)SCREEN_W; ++x) {
				if (x < 0) continue;
				++total;
				if (pred(px[y * SCREEN_W + x]))
					++hit;
			}
		}
		return total ? (float)hit / (float)total : 0.0f;
	}

	bool _wait_first_paint(unsigned max_iters)
	{
		for (unsigned i = 0; i < max_iters; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel const bg   = px[BG_PT.y  * SCREEN_W + BG_PT.x];
			Pixel const demo = px[DEMO_PT.y * SCREEN_W + DEMO_PT.x];
			if (pixel_is_bg(bg) && pixel_is_window(demo)) {
				Genode::log("de-survival-probe: first paint detected "
				            "(bg + demo window composited)");
				return true;
			}
			if (i % 50 == 0)
				Genode::log("de-survival-probe: first paint poll ", i);
		}
		_fail("first paint never appeared on nitpicker");
		return false;
	}

	bool _sample_first_paint()
	{
		_capture.capture_at(Capture::Point(0, 0));
		float const toggle = _fraction(TOGGLE_RECT, pixel_is_non_bg);
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		Pixel const demo = px[DEMO_PT.y * SCREEN_W + DEMO_PT.x];
		if (toggle < TOGGLE_PRESENT_THRESH)
			_fail("C1: panel launcher toggle not painted (toggle rect "
			      "below threshold)");
		if (!pixel_is_window(demo))
			_fail("C1: demo window color missing at the domain center");
		Genode::log("de-survival-probe: [c1] ok - panel + demo first paint "
		            "sampled (toggle_frac_per_mille=",
		            (unsigned)(toggle * 1000), ")");
		return true;
	}

	bool _wait_popup(bool want_open, unsigned max_ms, char const *phase)
	{
		float const thresh =
			want_open ? POPUP_OPEN_THRESH : POPUP_CLOSED_THRESH;
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			float const frac = _fraction(POPUP_RECT, pixel_is_non_bg);
			if (want_open ? (frac >= thresh) : (frac < thresh))
				return true;
		}
		if (want_open) {
			_capture.capture_at(Capture::Point(0, 0));
			float const frac = _fraction(POPUP_RECT, pixel_is_non_bg);
			Genode::log("de-survival-probe: popup-open diagnostic: "
			            "frac_per_mille=", (unsigned)(frac * 1000));
			_fail("popup did not open within bound (phase marker above)");
		}
		/* A close that cannot be confirmed is tolerated by callers
		 * (the compositor lags the QPA hide by 100-300 ms and the
		 * focus-out path is timer based) - never a hard gate. */
		(void)phase;
		return false;
	}

	bool _wait_green(unsigned max_iters, char const *phase)
	{
		for (unsigned i = 0; i < max_iters; ++i) {
			_timer.msleep(200);
			_capture.capture_at(Capture::Point(0, 0));
			float const frac = _fraction(GREEN_RECT, pixel_is_green);
			if (i == 0 || i % 50 == 0 || i + 1 == max_iters)
				Genode::log("de-survival-probe: green poll ", i,
				            " frac_per_mille=", (unsigned)(frac * 1000));
			if (frac >= GREEN_THRESH)
				return true;
		}
		_fail("pkg_gui_demo green pixel never appeared - click-to-launch "
		      "chain broken (phase marker above)", phase);
		return false;
	}


	/* ---- QMP-TARGET markers (the run script dispatches real input) ---- */

	/*
	 * All clicks go through the `QMP-TARGET entry <x> <y>` verb (the
	 * run script's dispatch_entry_click -> qmp_tablet_click, the
	 * usb-tablet absolute recipe). The PS/2 clamp+walk recipe is not
	 * used: its clamp burst is dropped on this host (the documented
	 * drift in qmp_ps2_click's own header - "when the clamp events
	 * drop, the cursor never reaches (0,0) and the fine walk starts
	 * from the previous position"), which mislanded the S-toggle
	 * click (runs 7-12: presses at ~(310,21)/(319,211), popup never
	 * opened). Tablet abs lands at the exact pixel (W4 evidence).
	 */
	void _click(Pt target, char const *phase)
	{
		Genode::log("de-survival-probe: [", phase, "] QMP-TARGET entry ",
		            target.x, " ", target.y);
	}

	void _done(char const *phase)
	{
		Genode::log("de-survival-probe: [", phase, "] QMP-TARGET done");
	}

	/*
	 * Topmost window_bg (#313244) run in the popup's left-fill
	 * column - the entry button, and the ONLY reliable popup-open
	 * signal: the non-bg fraction cannot be used because the green
	 * pkg_gui_demo window (also non-bg) sits in the same screen
	 * region once launched (run18/30: the state check saw the green
	 * window as an "open popup"; the entry scan then found no button
	 * and the follow-up click hit empty desktop). x=14 is inside the
	 * button's left fill, right of its x:8 edge, left of the
	 * left-aligned label glyphs (x>=20), and left of the demo window
	 * (x>=192). Returns the run length; y0/y1 its extent.
	 */
	int _entry_button_run(int &y0, int &y1)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		static int const scan_cols[] = { 14, 30, 60, 100, 170 };
		y0 = y1 = -1;
		for (unsigned c = 0; c < sizeof(scan_cols) / sizeof(scan_cols[0]); ++c) {
			int const x = scan_cols[c];
			int run_y0 = -1;
			for (int y = 30; y < 508; ++y) {
				Pixel const p = px[y * SCREEN_W + x];
				if (pixel_is_window(p)) {
					if (run_y0 < 0)
						run_y0 = y;
				} else {
					if (run_y0 >= 0) {
						int const len = y - run_y0;
						if (len >= 10) {
							y0 = run_y0;
							y1 = y - 1;
							return len;
						}
						run_y0 = -1;
					}
				}
			}
			if (run_y0 >= 0 && 508 - run_y0 >= 10) {
				y0 = run_y0;
				y1 = 507;
				return 508 - run_y0;
			}
		}
		return 0;
	}

	bool _popup_is_open()
	{
		int y0 = 0, y1 = 0;
		return _entry_button_run(y0, y1) >= 10;
	}

	/*
	 * State-driven popup clicks with VARIABLE marker counts. The S
	 * toggle is a TOGGLE (run18: a leftover-open popup was toggled
	 * closed and the open check passed on the compositor's 100-300 ms
	 * hide-lag ghost), and the demo-body focus-out close is
	 * unreliable (the Escape/dismisser path needs the
	 * ShortcutController, which stalls the DE without configd -
	 * runs 24-26). So the probe READS the popup state and emits only
	 * the clicks that state requires, then a `QMP-TARGET done`
	 * sentinel; the run script's dispatch_sequence consumes click
	 * markers until the sentinel, so the dispatch count stays
	 * deterministic at the PHASE level while the clicks adapt.
	 */
	void _open_popup(char const *phase)
	{
		bool const was_open = _popup_is_open();
		if (was_open) {
			/*
			 * Already open: a press INSIDE the popup (the heading
			 * area) is eventFilter case 1 (never closes) and still
			 * produces the input-report witness the C2 phase
			 * needs. Closing is deliberately unnecessary: the
			 * entry button's own clicked handler closes the popup
			 * (launcher_menu_view.cc case-1 comment), and every
			 * open here is state-driven, so leftover-open popups
			 * are handled without toggle-parity tricks.
			 */
			_click(POPUP_HEADING_PT, phase);
			_timer.msleep(200);
		} else {
			_click(S_TOGGLE, phase);
			bool opened = false;
			for (unsigned i = 0; i < 500 && !opened; ++i) {
				_timer.msleep(10);
				if (_popup_is_open())
					opened = true;
			}
			if (!opened)
				_fail("popup did not open after the S click "
				      "(entry button never appeared)");
		}
		_done(phase);
	}

	void _click_entry(char const *phase)
	{
		int y0 = 0, y1 = 0;
		int const len = _entry_button_run(y0, y1);

		if (len < 10) {
			float const frac = _fraction(POPUP_RECT, pixel_is_non_bg);
			Genode::log("de-survival-probe: entry-scan diagnostic: "
			            "no button run; popup_rect frac_per_mille=",
			            (unsigned)(frac * 1000));
			_fail("entry button not found in the popup columns "
			      "(popup not open or geometry unexpected)");
		}

		int const cy = (y0 + y1) / 2;
		Genode::log("de-survival-probe: [", phase, "] entry button rect y ",
		            y0, "..", y1, " (len=", len, ")");
		Genode::log("de-survival-probe: [", phase, "] QMP-TARGET entry ",
		            ENTRY_CLICK_X, " ", cy);
	}


	/* ---- harness failure: distinct from a contract RED item ---- */

	void _fail(char const *reason)
	{
		Genode::error("de-survival-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	void _fail(char const *reason, char const *detail)
	{
		Genode::error("de-survival-probe: FAIL ", reason, detail);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}
};


void Component::construct(Genode::Env &env)
{
	static De_survival_probe probe { env };
	probe.run();
}

Genode::size_t Component::stack_size() { return 64 * 1024; }
