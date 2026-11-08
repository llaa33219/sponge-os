/*
 * prod_launch_probe — per-app fresh-boot replay of the launcher's
 * click-to-launch backend on the PRODUCT media topology, with
 * wm window-registration proof in the grouped_ui_probe's proven
 * heavy-closure envelope.
 *
 * The user-facing launch flow: Qt menu click → LauncherController
 * posts <request op="launch" pkg="X"/> on launcher_request →
 * sponge_pkgd _do_launch → the pkg_runtime config regains the
 * package's start node → the package boots and its window appears.
 * The Qt pixel side was proven by the Phase-10 interactive
 * scenarios; this probe replays the BACKEND identically (same
 * request shape, pkgd's shared handle body, over the dedicated
 * probe_request transport because report_rom is single-writer per
 * label and sponge-de owns launcher_request here) for the
 * user-reported packages: terminal, files, falkon.
 *
 * PER-APP FRESH-BOOT MATRIX (why exactly one app per boot): every
 * sequential multi-app experiment in ONE boot fails for a different
 * CNode reason — falkon's ~509 MiB WebEngine closure starves core's
 * initial-PD CNode construction around its residency (the R29/R30
 * failure class that grouped_ui_probe's sanctioned pkgd teardowns
 * exist for — docs/evidence/phase16-p51-grouped-app-matrix.md), and
 * the initial-PD budget does not recover after a falkon teardown
 * well enough to create another runtime (a terminal runtime started
 * AFTER the teardown was measured to fail initial-PD creation).
 * Each package's window has ALREADY been proven individually in its
 * own fresh boot. The honest regression scope for the original
 * "clicked the launcher, no window" bug is therefore ONE selected
 * package per boot — the case matrix
 *
 *   SPONGE_LAUNCH_CASE=terminal|files|falkon|multi
 *
 * delivered through this probe's config (`+ config | case: <value>`,
 * validated and injected by
 * run/sponge-desktop-disk-uefi-usb.run). The three single cases are
 * run as three serial fresh boots by the harness (parent runs the
 * three cases in sequence); cross-app CNode pressure is GONE BY
 * CONSTRUCTION there — only one package runtime is ever created per
 * boot. The fourth case `multi` deliberately BREAKS that
 * construction: it launches terminal, files and falkon in one boot
 * and requires their windows to co-reside in ONE wm window_list
 * snapshot — the RED-first regression for concurrent-window
 * validation. Every sequential multi-app experiment so far failed
 * with CNode errors (the R29/R30 failure class above), so `multi` is
 * expected to reproduce that RED for diagnosis; it is NOT a claim
 * that multi-app residence works.
 *
 * SEQUENCE (every wait bounded):
 *   1. parse the case from the probe config with the Node API
 *      (Attached_rom_dataspace::node) and log the received config
 *      verbatim (`+ config | case: <value>`); a missing/invalid
 *      case fails loudly (Genode::error + `windows FAIL`), launches
 *      nothing, and NEVER falls back to a default app
 *   2. launch the package(s) — the retained request marker
 *      `prod-launch-probe: launch <pkg> -> ok` / `-> FAIL` (ok
 *      requires an ANSWERED request whose status is ok; unanswered
 *      or an error status is a request failure). Single cases launch
 *      the one selected package; multi launches terminal, files and
 *      falkon in that order (the _apps table order), 6 s pacing
 *      between the requests.
 *   3. bounded window wait — single: the app's exact label prefix
 *      → the per-app marker `prod-launch-probe: window <case> ->
 *      registered` exactly once (plus a label/id evidence line).
 *      multi: ONE window_list snapshot per scan (ONE
 *      Attached_rom_dataspace::update + ONE Node traversal) must
 *      carry ALL THREE label prefixes AT THE SAME TIME — sequential
 *      registration across scans is not a witness — logging each
 *      registration once plus `prod-launch-probe: multi simultaneous
 *      n=3` once; a second stable scan ~5 s later must still carry
 *      all three (`prod-launch-probe: multi siblings held n=3`), and
 *      a registered label missing from any later snapshot is a
 *      vanish (`prod-launch-probe: window <name> -> vanished`, one
 *      line per app, sticky — a disappeared window FAILs the
 *      verdict).
 *   4. ONE final verdict over those registrations:
 *      `prod-launch-probe: windows PASS` or
 *      `prod-launch-probe: windows FAIL`, preceded by
 *      `prod-launch-probe: missing label prefix:` lines when a
 *      window never registered. ANY step failure (request error,
 *      missing window, vanished window, missing simultaneity) ends
 *      in windows FAIL even if windows somehow registered.
 *
 * WINDOW TRUTH (the regression net): a pkgd launch acknowledgement
 * is not a window. The window watch polls wm's window_list report
 * (report_rom relay `wm -> window_list`; the report content is
 * HID-format, so it is parsed with the Node API —
 * Attached_rom_dataspace::node(), never .xml(), which sees HID as
 * invalid XML and silently yields <empty/>) and matches the
 * selected window's label by prefix:
 *
 *   pkg_runtime_terminal -> terminal  (case=terminal, case=multi)
 *   pkg_runtime_simple   -> files     (case=files,    case=multi)
 *   pkg_runtime_browser  -> falkon    (case=falkon,   case=multi)
 *
 * case=multi matches all three in the SAME snapshot per scan (a
 * single Node traversal checks every window node against all three
 * prefixes) and additionally witnesses the co-residency
 * (`prod-launch-probe: multi simultaneous n=3`) and its stability
 * (`prod-launch-probe: multi siblings held n=3`).
 *
 * The scenario enforces the final verdict
 * (run/sponge-desktop-disk-uefi-usb.run): a launch ack without a
 * registered window must FAIL the gate, and case=multi additionally
 * enforces the simultaneous witness and terminal's vfs boot marker.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

/* Sponge includes */
#include <sponge/backend_client.h>

namespace Prod_launch_probe { struct Main; }

struct Prod_launch_probe::Main
{
	Main(Main const &) = delete;
	Main &operator=(Main const &) = delete;

	Genode::Env &_env;

	Timer::Connection _timer { _env };

	/*
	 * wm's window_list via report_rom (route: ROM window_list ->
	 * child report_rom; policy: prod_launch_probe -> window_list |
	 * report: wm -> window_list). HID-format content — Node API
	 * only (see the header comment).
	 */
	Genode::Attached_rom_dataspace _window_list { _env, "window_list" };

	/*
	 * The launch case from the parent-provided config
	 * (`+ config | case: <value>` in this probe's start node,
	 * run/sponge-desktop-disk-uefi-usb.run). Parsed with the Node
	 * API (Attached_rom_dataspace::node), like the window_list
	 * report.
	 */
	Genode::Attached_rom_dataspace _config { _env, "config" };

	/* 30s result budget: a launch answer can come only after the
	 * package runtimes' boot path on the storage chain has processed
	 * the request, which on product media may exceed the 6s default.
	 * A spent budget still FAILs loudly below; the budget is a wait,
	 * not a mask. */
	Sponge::Backend::ReportRomClient _launcher {
		_env, "probe_request", "probe_result", 30000 };

	/*
	 * Window budget for the one selected app: a bounded wait, never
	 * a mask. 300 s is the probe's established generous window
	 * budget — room for falkon's ~509 MiB WebEngine closure
	 * resolving over the USB chain (grouped_ui_probe gives the
	 * browser class its longest first-paint budget for exactly this
	 * reason). A spent budget is a wait that ran out: the missing
	 * label is reported at the verdict and the verdict FAILs.
	 */
	static constexpr unsigned WINDOW_BUDGET_MS { 300000 };

	/*
	 * The case table — the matrix definition. Exactly one entry is
	 * selected per boot for the single cases; the others are never
	 * launched, so no cross-app CNode pressure can arise. `multi`
	 * launches ALL THREE (this table's order is the mandated launch
	 * order: terminal, files, falkon) and is the deliberate
	 * exception — the RED simultaneous-residence regression.
	 */
	struct App_window {
		char const *name;          /* marker token + case value */
		char const *label_prefix;  /* wm window label prefix */
		bool        registered;    /* seen in a window_list snapshot */
		bool        vanished;      /* registered, then absent later */
	};

	App_window _apps[3] {
		{ "terminal", "pkg_runtime_terminal -> terminal", false, false },
		{ "files",    "pkg_runtime_simple -> files",      false, false },
		{ "falkon",   "pkg_runtime_browser -> falkon",    false, false },
	};

	/* The one selected case — never more than one app per single boot. */
	App_window *_app { nullptr };

	/* case=multi: all three apps run in this one boot. */
	bool _multi { false };

	/* multi witnesses: one snapshot carried all three prefixes, and a
	 * later stable scan still carried all three. Both markers are
	 * logged exactly once by the scan. */
	bool _simultaneous { false };
	bool _siblings_held { false };

	/* Sequence health for the final verdict: the request must be
	 * answered with status ok. A step failure ends in
	 * `windows FAIL` even if a window somehow registered. */
	bool _steps_ok { true };

	Main(Genode::Env &env) : _env(env)
	{
		/*
		 * 0. Case from config. Fail loudly on a missing/invalid
		 * case (no silent default): the verdict below FAILs and
		 * nothing is launched.
		 */
		if (!_select_case()) {
			_verdict();
			return;
		}

		/* Let the system settle after boot before the launch. */
		_settle(3000);

		if (_multi) {
			/*
			 * 1. multi: launch terminal, files, falkon in that
			 * order — the retained ack markers (`prod-launch-
			 * probe: launch <pkg> -> ok` / `-> FAIL`) with the
			 * single-case 6 s pacing between requests. This
			 * deliberately recreates sequential multi-app
			 * residence in ONE boot (the RED CNode-pressure
			 * regression).
			 */
			for (App_window const &app : _apps) {
				_launch(app.name);
				_settle(6000);
			}

			/*
			 * 2. Bounded window wait: co-residency of all three
			 * label prefixes in ONE window_list snapshot, plus
			 * the ~5 s stable sibling scan (see _wait_multi).
			 */
			_wait_multi(WINDOW_BUDGET_MS);
		} else {
			/*
			 * 1. Launch ONLY the selected package — the matrix
			 * proves the three user-reported packages as three
			 * serial fresh boots, so nothing else is ever
			 * launched in this boot. The request marker is
			 * retained unchanged in shape:
			 * `prod-launch-probe: launch <case> -> ok` /
			 * `-> FAIL`.
			 */
			_launch(_app->name);
			_settle(6000);

			/*
			 * 2. Bounded window wait for the app's exact label
			 * prefix (its registration proof; the per-app
			 * marker is logged exactly once by the scan).
			 */
			_wait_window(WINDOW_BUDGET_MS);
		}

		/*
		 * 3. ONE final verdict over the registrations.
		 */
		_verdict();
	}

	/*
	 * Parse `+ config | case: <value>` from the probe config (Node
	 * API) and select what this boot proves. The received config is
	 * logged verbatim as evidence. A missing or unknown case is a
	 * loud failure: error log + no launch + `windows FAIL` at the
	 * verdict — never a silent default. `multi` selects the
	 * simultaneous-residence regression (all three apps).
	 */
	bool _select_case()
	{
		Genode::String<16> case_value;

		if (_config.valid() && _config.size() > 0) {
			Genode::Node const config = _config.node();
			case_value = config.attribute_value(
				"case", Genode::String<16>());
		}

		Genode::log("+ config | case: ", case_value);

		if (Genode::strcmp(case_value.string(), "multi") == 0) {
			_multi = true;
			return true;
		}

		for (App_window &app : _apps) {
			if (Genode::strcmp(case_value.string(), app.name) != 0)
				continue;
			_app = &app;
			return true;
		}

		Genode::error("prod-launch-probe: missing or invalid case: '",
		              case_value, "' — expected one of "
		              "terminal, files, falkon, multi");
		return false;
	}

	void _settle(unsigned ms)
	{
		unsigned const step { 100 };
		for (unsigned done = 0; done < ms; done += step)
			_timer.msleep(step);
	}

	/* ---- request step (marker retained) ---- */

	/*
	 * Post one launch request for the selected package and log its
	 * retained marker (`prod-launch-probe: launch <pkg> -> ok` /
	 * `-> FAIL`). "ok" requires an ANSWERED request whose status is
	 * ok — an unanswered request or an error status (not-installed,
	 * insufficient-resources, ...) is a request failure and FAILs
	 * the final verdict even if a window somehow registers.
	 */
	void _launch(char const *pkg)
	{
		bool ok = _launcher.request("launch", pkg);
		if (ok) {
			Genode::String<32> const status =
				_launcher.result_xml().attribute_value(
					"status", Genode::String<32>());
			ok = (status == Genode::String<32>("ok"));
		}
		if (!ok)
			_steps_ok = false;
		Genode::log("prod-launch-probe: launch ", pkg, " -> ",
		            ok ? "ok" : "FAIL");
	}

	/* ---- wm window_list: registration truth ---- */

	static bool _label_has_prefix(char const *label, char const *prefix)
	{
		return Genode::strcmp(label, prefix, Genode::strlen(prefix)) == 0;
	}

	/*
	 * Scan wm's window_list once. When the selected app's window
	 * registers, log its bounded marker exactly once (the
	 * registered flag makes the marker poll-independent — one
	 * marker, ever) plus one label/id evidence line. Returns
	 * whether the selected app is registered so far.
	 */
	bool _scan_window()
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0)
			return false;

		Genode::Node const root = _window_list.node();
		root.for_each_sub_node("window", [&] (Genode::Node const &w) {
			if (_app->registered)
				return;
			Genode::String<160> label =
				w.attribute_value("label", Genode::String<160>());
			if (!_label_has_prefix(label.string(), _app->label_prefix))
				return;
			_app->registered = true;
			Genode::log("prod-launch-probe: window ", _app->name,
			            " -> registered");
			Genode::log("prod-launch-probe: window ", _app->name,
			            " id=", w.attribute_value("id", 0u),
			            " label=", label.string());
		});
		return _app->registered;
	}

	/*
	 * Bounded window wait: poll wm's window_list every step until
	 * the selected app's exact label prefix registers — its marker
	 * is logged exactly once by the scan — or the budget is spent.
	 * A spent budget is a wait that ran out, not a mask: the
	 * missing label is reported at the verdict.
	 */
	bool _wait_window(unsigned budget_ms)
	{
		unsigned const step_ms { 500 };
		for (unsigned done = 0; done < budget_ms; done += step_ms) {
			if (_scan_window())
				return true;
			_timer.msleep(step_ms);
		}
		/* One final scan so a window registered during the last
		 * sleep still counts. */
		return _scan_window();
	}

	/* ---- multi: simultaneous co-residency truth ---- */

	/*
	 * Scan wm's window_list ONCE — exactly ONE update() + ONE Node
	 * traversal — and derive per-app presence from THAT single
	 * snapshot. All three prefixes must co-occur in the same
	 * snapshot for the simultaneous witness; sequential
	 * registration across scans is deliberately not sufficient.
	 * Each registration marker is logged exactly once (the
	 * registered flag). A registered app missing from a later valid
	 * snapshot is a vanish: logged once, sticky — a disappeared
	 * window FAILs the verdict. An empty/invalid window_list is not
	 * a snapshot, so it proves nothing and marks nothing vanished.
	 * Returns whether THIS snapshot carried all three prefixes.
	 */
	bool _scan_multi()
	{
		_window_list.update();

		bool present[3] { false, false, false };

		bool const have_snapshot {
			_window_list.valid() && _window_list.size() > 0 };

		if (have_snapshot) {
			Genode::Node const root = _window_list.node();
			root.for_each_sub_node("window", [&] (Genode::Node const &w) {
				Genode::String<160> label =
					w.attribute_value("label", Genode::String<160>());
				for (unsigned i = 0; i < 3; i++) {
					if (!_label_has_prefix(label.string(),
					                       _apps[i].label_prefix))
						continue;
					present[i] = true;
					if (_apps[i].registered)
						continue;
					_apps[i].registered = true;
					Genode::log("prod-launch-probe: window ",
					            _apps[i].name, " -> registered");
					Genode::log("prod-launch-probe: window ",
					            _apps[i].name,
					            " id=", w.attribute_value("id", 0u),
					            " label=", label.string());
				}
			});

			for (unsigned i = 0; i < 3; i++) {
				if (_apps[i].registered && !present[i]
				 && !_apps[i].vanished) {
					_apps[i].vanished = true;
					Genode::log("prod-launch-probe: window ",
					            _apps[i].name, " -> vanished");
				}
			}
		}

		bool const all_present {
			present[0] && present[1] && present[2] };

		if (all_present && !_simultaneous) {
			_simultaneous = true;
			Genode::log("prod-launch-probe: multi simultaneous n=3");
		}
		return all_present;
	}

	/*
	 * Bounded multi wait: poll (one snapshot per scan) until ONE
	 * snapshot carries all three label prefixes — the simultaneous
	 * witness, logged once by the scan — or the budget is spent.
	 * Then a second stable scan ~5 s later must still carry all
	 * three (siblings held). Vanish tracking runs on every scan;
	 * a spent budget is a wait that ran out, not a mask.
	 */
	void _wait_multi(unsigned budget_ms)
	{
		unsigned const step_ms { 500 };
		bool witnessed { false };
		for (unsigned done = 0; done < budget_ms && !witnessed;
		     done += step_ms) {
			witnessed = _scan_multi();
			if (!witnessed)
				_timer.msleep(step_ms);
		}
		/* One final scan so a co-resident snapshot arriving during
		 * the last sleep still counts. */
		if (!witnessed)
			_scan_multi();

		if (!_simultaneous)
			return;

		_settle(5000);
		if (_scan_multi()) {
			_siblings_held = true;
			Genode::log("prod-launch-probe: multi siblings held n=3");
		}
	}

	/*
	 * ONE final verdict over the registrations. Failure names the
	 * label prefixes never seen; any step failure (see _steps_ok)
	 * ends in windows FAIL even when windows registered. For multi
	 * the PASS additionally requires the simultaneous witness, the
	 * held sibling scan, and that no registered window vanished. A
	 * missing/invalid case reaches here with no app selected and
	 * FAILs outright.
	 */
	void _verdict()
	{
		if (_multi) {
			for (App_window const &app : _apps)
				if (!app.registered)
					Genode::log("prod-launch-probe: missing label prefix: ",
					            app.label_prefix);
			bool ok { _steps_ok && _simultaneous && _siblings_held };
			for (App_window const &app : _apps)
				if (!app.registered || app.vanished)
					ok = false;
			if (ok)
				Genode::log("prod-launch-probe: windows PASS");
			else
				Genode::log("prod-launch-probe: windows FAIL");
			return;
		}
		if (_app && !_app->registered)
			Genode::log("prod-launch-probe: missing label prefix: ",
			            _app->label_prefix);
		if (_app && _app->registered && _steps_ok)
			Genode::log("prod-launch-probe: windows PASS");
		else
			Genode::log("prod-launch-probe: windows FAIL");
	}
};

void Component::construct(Genode::Env &env)
{
	static Prod_launch_probe::Main main(env);
}
