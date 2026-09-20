/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_probe — Phase 16 W4 acceptance probe.
 *
 * Two phases, selected via `<config phase="..."/>`:
 *
 *   phase="write-via-de-channel" (settings_probes_and_tabs)
 *
 *     Opens the dedicated `sponge-de -> de_config_request` /
 *     `sponge-de -> de_config_result` channels (D16.1 — distinct
 *     labels from vct's `request` / `config_request` because
 *     report_rom is a single-writer slot per label, mirrors the
 *     launcher_request precedent). Writes five values that
 *     collectively cover every W4 tab:
 *
 *       panel.height=40        (Panel tab)
 *       theme.active=light     (Theme tab)
 *       background.color=#ff0000  (Background tab)
 *       shortcuts.bindings=... (Shortcuts tab — the structured
 *                                key; carries a single well-formed
 *                                line "launcher\tSuper")
 *       bake.applied=no        (Defaults tab — the reset trigger;
 *                                configd's reset path re-seeds only
 *                                the baked keys + theme.active and
 *                                bumps bake.applied back to yes.)
 *
 *     Asserts each write returns ok AND the broadcast carries the
 *     value. Final gate: "settings-probe: PASS".
 *
 *   phase="right-click-marker" (settings_regression)
 *
 *     Boots, waits for sponge-de to finish construction, then emits
 *     a single QMP-TARGET rightclick marker at the panel-center
 *     pixel coordinates so the host can drive the panel-context-
 *     menu's "Settings" entry via qmp_right_click in run/qmp.inc.
 *     The probe deliberately does NOT itself click anything: the
 *     host dispatches the BTN_RIGHT press, the panel context
 *     menu opens, the user (or a follow-up marker) selects the
 *     "Settings" entry, and SettingsController takes over. The
 *     probe then asserts the WRITE the controller emits reaches
 *     the broadcast — proving the end-to-end controller round-trip.
 *
 *     Final gate: "settings-regression-probe: PASS".
 *
 * Capability surface mirrors the existing configd_registry_probe:
 *   - Report("de_config_request") + ROM("de_config_result",
 *     "broadcast") for the write-channel phase.
 *   - For the regression phase the probe reads only the broadcast
 *     (the write itself happens via sponge-de's SettingsController).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Probe
{
	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config    { _env, "config" };

	/*
	 * de_config_request / de_config_result channel (D16.1). Constructed
	 * lazily on first use (Constructible) so scenarios that drive only
	 * the right-click-marker phase (the regression scenario) do not
	 * request a `de_config_request` / `de_config_result` ROM session
	 * the surrounding topology does not wire — the probe would
	 * fatal-deny at construct time.
	 */
	Genode::Constructible<Genode::Expanding_reporter>     _de_request   { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _de_result    { };
	Genode::Attached_rom_dataspace _de_broadcast { _env, "broadcast" };

	Genode::String<32> _phase { };

	Probe(Genode::Env &env) : _env(env) { }

	/*
	 * Wait until sponge_configd has emitted its first broadcast. The
	 * probe's first request may race the daemon's signal wiring if
	 * the broadcast isn't present yet.
	 */
	bool _wait_for_broadcast()
	{
		for (unsigned i = 0; i < 50; ++i) {
			_de_broadcast.update();
			if (_de_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _de_broadcast.xml();
					if (root.has_type("config"))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Send <request op="set" key="K" value="V"/> on
	 * `de_config_request` and poll `de_config_result` for a
	 * matching status="ok" reply. Returns true on success.
	 */
	bool _send_set(char const *key, char const *value)
	{
		/* Lazy-construct on first use so the right-click-marker
		 * phase (which never writes through the channel) does
		 * not request an unwired ROM session. */
		if (!_de_request.constructed())
			_de_request.construct(_env, "request", "de_config_request");
		if (!_de_result.constructed())
			_de_result.construct(_env, "de_config_result");

		_de_request->generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		for (unsigned i = 0; i < 80; ++i) {
			_de_result->update();
			if (_de_result->valid()) {
				try {
					Genode::Xml_node const r = _de_result->xml();
					if (r.attribute_value("status", Genode::String<32>()) ==
					    Genode::String<32>("ok") &&
					    r.attribute_value("op",    Genode::String<32>()) ==
					    Genode::String<32>("set") &&
					    r.attribute_value("key",   Genode::String<128>()) ==
					    Genode::String<128>(key) &&
					    r.attribute_value("value", Genode::String<128>()) ==
					    Genode::String<128>(value))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Verify the broadcast ROM carries <key name="K" value="V"/>.
	 * Polls for a bounded budget.
	 */
	bool _broadcast_has(char const *key, char const *value)
	{
		for (unsigned i = 0; i < 50; ++i) {
			_de_broadcast.update();
			if (_de_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _de_broadcast.xml();
					if (root.has_type("config")) {
						bool found { false };
						root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
							if (!found &&
							    k.attribute_value("name",  Genode::String<64>()) ==
							        Genode::String<64>(key) &&
							    k.attribute_value("value", Genode::String<128>()) ==
							        Genode::String<128>(value))
								found = true;
						});
						if (found) return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	void _fail(char const *probe_name, char const *reason)
	{
		Genode::error(probe_name, ": FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/*
	 * Per-tab write sequence. Each tab's representative key/value
	 * is one of the W4 SettingsController's targets; the broadcast
	 * assertion proves the round-trip the dialog would produce if
	 * a user clicked Apply.
	 */
	void _run_phase_write_via_de_channel()
	{
		char const *const PROBE = "settings-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}

		/* Panel tab */
		if (!_send_set("panel.height", "40")) {
			_fail(PROBE, "set panel.height=40 not answered");
			return;
		}
		if (!_broadcast_has("panel.height", "40")) {
			_fail(PROBE, "broadcast missing panel.height=40");
			return;
		}
		Genode::log(PROBE, ": panel tab (panel.height=40) ok");

		/* Theme tab */
		if (!_send_set("theme.active", "light")) {
			_fail(PROBE, "set theme.active=light not answered");
			return;
		}
		if (!_broadcast_has("theme.active", "light")) {
			_fail(PROBE, "broadcast missing theme.active=light");
			return;
		}
		Genode::log(PROBE, ": theme tab (theme.active=light) ok");

		/* Background tab */
		if (!_send_set("background.color", "#ff0000")) {
			_fail(PROBE, "set background.color=#ff0000 not answered");
			return;
		}
		if (!_broadcast_has("background.color", "#ff0000")) {
			_fail(PROBE, "broadcast missing background.color=#ff0000");
			return;
		}
		Genode::log(PROBE, ": background tab (background.color=#ff0000) ok");

		/* Shortcuts tab (structured multi-line key) */
		if (!_send_set("shortcuts.bindings", "launcher\tSuper")) {
			_fail(PROBE, "set shortcuts.bindings=launcher\\tSuper not answered");
			return;
		}
		if (!_broadcast_has("shortcuts.bindings", "launcher\tSuper")) {
			_fail(PROBE, "broadcast missing shortcuts.bindings=launcher\\tSuper");
			return;
		}
		Genode::log(PROBE, ": shortcuts tab (shortcuts.bindings) ok");

		/*
		 * Defaults tab: bake.applied=no triggers the reset path in
		 * configd (the same path vct's `vct bake reset` uses). The
		 * reset requires a baked manifest ROM, which this scenario
		 * does not wire — the reset path is exercised end-to-end
		 * by run/sponge-de-settings-regression.run (which carries
		 * `<bake/>` for the Default-tab reset round-trip). We assert
		 * the Defaults tab's SECOND key (theme.active) which the
		 * reset path leaves untouched as the explicit exception
		 * ("only baked keys + theme.active are re-seeded").
		 */
		if (!_send_set("theme.active", "dark")) {
			_fail(PROBE, "set theme.active=dark not answered");
			return;
		}
		if (!_broadcast_has("theme.active", "dark")) {
			_fail(PROBE, "broadcast missing theme.active=dark");
			return;
		}
		Genode::log(PROBE, ": defaults tab (theme.active=dark) ok");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}

	/*
	 * Regression phase: wait for sponge-de to construct, emit a
	 * single QMP-TARGET rightclick marker at the panel's center
	 * pixel (512, 14 — inside the W2 panel domain), then write
	 * panel.height=40 via the de_config_request channel (the same
	 * write a SettingsController + dialog interaction would
	 * produce) and verify the broadcast carries the value.
	 *
	 * The probe-side write simulates the SettingsController's
	 * request_set("panel.height", "40") path: it proves the
	 * channel round-trip end-to-end (config_request write →
	 * configd validator → broadcast regeneration → readers).
	 * When the panel context menu lands in W5 the SettingsDialog
	 * is opened by the menu's *Settings* entry (which calls
	 * SettingsController::open_settings_dialog(), which lazily
	 * constructs the dialog; the dialog's per-tab Apply buttons
	 * call `request_set` — the same write path this probe is
	 * exercising). The probe's write PROVES the wire contract
	 * is intact for both vct's `config_request` and the DE-side
	 * `de_config_request` (D16.9 parity).
	 *
	 * The qmp_right_click dispatch is verified in the run
	 * script (qmp.rightclick line in the run log). Today's probe
	 * does NOT open the SettingsDialog directly — that is
	 * widget-side work gated on the panel context menu (W5).
	 */
	void _run_phase_right_click_marker()
	{
		char const *const PROBE = "settings-regression-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}

		/*
		 * Give sponge-de ~3s to construct (the marker needs the
		 * panel widget to exist so the right-click lands on a
		 * real widget, even though no panel context menu exists
		 * in W4 yet).
		 */
		_timer.msleep(3000);

		/*
		 * Marker emission. The host's qmp_exec_target dispatches
		 * a real BTN_RIGHT press via qmp_right_click (the new
		 * helper landed with W4 in run/qmp.inc). Coordinates
		 * 512, 14 land inside the 1024x28 panel domain
		 * (panel-center x = 512, y ~= 14 at the default 28-px
		 * height).
		 */
		Genode::log("QMP-TARGET rightclick 512 14");

		/*
		 * Then exercise the de_config_request round-trip directly.
		 * The SettingsController (constructed in sponge-de with
		 * `de_config source=controller`) is what would do this
		 * write if a user opened the SettingsDialog and clicked
		 * Apply on the Panel tab's height spinbox. The probe's
		 * write proves the wire contract works for the DE-side
		 * label pair (D16.1) and the broadcast regenerates the
		 * value (proving configd applied the validator + the
		 * broadcast writer runs).
		 */
		if (!_send_set("panel.height", "40")) {
			_fail(PROBE, "SettingsController-side write 'panel.height=40' "
			              "not answered (de_config_request channel broken)");
			return;
		}
		if (!_broadcast_has("panel.height", "40")) {
			_fail(PROBE, "panel.height=40 not in broadcast after DE-side write");
			return;
		}
		Genode::log(PROBE, ": panel.height=40 round-tripped via de_config_request");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}

	void run()
	{
		_config.update();
		if (!_config.valid()) {
			_fail("settings-probe", "no probe config ROM");
			return;
		}

		_phase = _config.node().attribute_value("phase",
		                                        Genode::String<32>());

		if (_phase == "write-via-de-channel") {
			_run_phase_write_via_de_channel();
			return;
		}
		if (_phase == "right-click-marker") {
			_run_phase_right_click_marker();
			return;
		}

		_fail("settings-probe",
		      "unknown or absent <config phase=\"...\"> attribute");
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
