/* SPDX-License-Identifier: Apache-2.0
 *
 * shortcuts_probe — Phase 16 W7 acceptance probe (U16.4 / D16.5).
 *
 * Two phases, selected via `<config phase="..."/>`:
 *
 *   phase="default-bindings" (shortcuts / sponge-de-shortcuts.run)
 *
 *     Boots, waits for the configd broadcast. Writes the W7 initial
 *     binding list `launcher\tSuper\nfocus_next\tAlt-Tab\n
 *     dismiss\tEscape` via the dedicated `sponge-de ->
 *     de_config_request` channel (D16.1 / D16.9). The configd
 *     closed-registry validator (configd/main.cc:752 _shortcuts_valid)
 *     accepts the value (the W2 deliverable). The broadcast
 *     regenerates with the new value; the in-DE ShortcutController
 *     subscribes to the `configd` ROM and re-emits a full
 *     `event_filter.config` Report (the W7 plan's dynamic
 *     config). The probe verifies:
 *
 *       (a) the broadcast carries the written bindings (write
 *           path round-trip)
 *       (b) the dynamic event_filter_config report was emitted with
 *           the parsed `<shortcut>` children (controller publish
 *           path)
 *       (c) the LIVE keypress path runs end-to-end:
 *             - QMP-TARGET key super -> shortcut_hit action=launcher
 *               yes AND launcher popup non-bg fraction rises
 *             - QMP-TARGET key escape -> shortcut_hit action=dismiss
 *               yes AND popup non-bg fraction falls
 *             - QMP-TARGET key alt-tab -> shortcut_hit action=focus_next
 *               yes
 *
 *     Final gate: "shortcuts-probe: PASS".
 *
 *   phase="override-bindings" (shortcuts-extend / sponge-de-shortcuts-extend.run)
 *
 *     Same flow but writes the override binding list:
 *     `launcher\tCtrl-Alt-Tab\nfocus_next\tAlt-Tab\ndismiss\tEscape`.
 *     The configd validator accepts the override (Ctrl / Alt / Tab
 *     are all in the synonym table at configd/main.cc:864-882; the
 *     plan's Ctrl-Alt-T example hits `T` as a single-letter token
 *     which the validator correctly rejects — single letters are NOT
 *     in the synonym table; only the multi-letter names are). The
 *     broadcast regenerates; the in-DE controller re-emits
 *     event_filter.config with the new triple `<key KEY_LEFTCTRL/>
 *     <key KEY_LEFTALT/><key KEY_TAB/>` for launcher. The live
 *     capture path then drives `QMP-TARGET key ctrl-alt-tab` (the
 *     qcode-object form, sent as a single send-key call) and
 *     verifies `shortcut_hit action=launcher yes`.
 *
 *     Final gate: "shortcuts-extend-probe: PASS".
 *
 * Capability surface:
 *   - Report("de_config_request") + ROM("de_config_result",
 *     "broadcast") for the write channel.
 *   - ROM("event_filter_config") for the dynamic event_filter config
 *     report (verifies the controller emitted the parsed <shortcut>
 *     children).
 *   - ROM("shortcut_hit") for the consolidated action-dispatch
 *     observation (carries the action name on every dispatch).
 *   - ROM("launcher_state") for the launcher-popup open/close
 *     observation (the deterministic reporter written by
 *     LauncherController on every toggle).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <os/reporter.h>
#include <rom_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {


struct Probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	Genode::Constructible<Genode::Expanding_reporter>     _de_request   { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _de_result    { };
	Genode::Attached_rom_dataspace _de_broadcast { _env, "broadcast" };

	Genode::Constructible<Genode::Attached_rom_dataspace> _event_filter_config { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _shortcut_hit        { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _launcher_state     { };

	Genode::String<32> _phase { };



	Probe(Genode::Env &env) : _env(env) { }

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

	bool _send_set(char const *key, char const *value)
	{
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
					Genode::String<32> const status =
						r.attribute_value("status", Genode::String<32>());
					Genode::String<32> const op =
						r.attribute_value("op", Genode::String<32>());
					Genode::String<128> const k =
						r.attribute_value("key", Genode::String<128>());
					Genode::String<128> const v =
						r.attribute_value("value", Genode::String<128>());
					if (status == Genode::String<32>("ok") &&
					    op == Genode::String<32>("set") &&
					    k == Genode::String<128>(key) &&
					    v == Genode::String<128>(value))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

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

	bool _wait_for_shortcut_hit(char const *expected_action,
	                            unsigned max_ms = 8000)
	{
		if (!_shortcut_hit.constructed()) {
			Genode::error("shortcuts-probe: shortcut_hit ROM not opened");
			return false;
		}
		unsigned const step_ms = 100;
		for (unsigned i = 0; i < (max_ms / step_ms); ++i) {
			_shortcut_hit->update();
			if (_shortcut_hit->valid()) {
				try {
					Genode::Xml_node const r = _shortcut_hit->xml();
					if (r.has_type("shortcut_hit")) {
						Genode::String<8> const hit =
							r.attribute_value("hit", Genode::String<8>());
						Genode::String<32> const action =
							r.attribute_value("action", Genode::String<32>());
						if (hit == Genode::String<8>("yes") &&
						    action == Genode::String<32>(expected_action))
							return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(step_ms);
		}
		return false;
	}


	/* the launcher_state ROM is opened lazily (scenarios without the
	 * report_rom policy must not fatal-deny the probe). */
	void _open_launcher_state_rom()
	{
		if (_launcher_state.constructed()) return;
		try {
			_launcher_state.construct(_env, "launcher_state");
		}
		catch (Genode::Rom_connection::Rom_connection_failed) { }
	}

	bool _wait_launcher_state(char const *want_open, unsigned max_ms = 20000)
	{
		if (!_launcher_state.constructed()) {
			Genode::error("shortcuts-probe: launcher_state ROM not opened");
			return false;
		}
		for (unsigned i = 0; i < max_ms / 100; ++i) {
			_launcher_state->update();
			if (_launcher_state->valid()) {
				/*
				 * The reporter's lambda-generate emits HID
				 * (`launcher_state | open: yes`), not XML — the
				 * Node API auto-detects the format; the legacy
				 * Xml_node API would return <empty/>.
				 */
				char const *p = _launcher_state->local_addr<char>();
				Genode::size_t const sz = _launcher_state->size();
				Genode::Node const r(Genode::Const_byte_range_ptr(p, sz));
				if (r.has_type("launcher_state") &&
				    r.attribute_value("open", Genode::String<8>()) ==
				        Genode::String<8>(want_open))
					return true;
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

	void _read_phase()
	{
		Genode::Attached_rom_dataspace config(_env, "config");
		for (unsigned i = 0; i < 50 && _phase.length() == 0; ++i) {
			config.update();
			if (!config.valid()) { _timer.msleep(100); continue; }
			_phase = config.node().attribute_value("phase",
			                                        Genode::String<32>());
			if (_phase.length() > 0) return;
			_timer.msleep(100);
		}
	}

	void run()
	{
		_read_phase();

		if (_phase == Genode::String<32>("default-bindings")) {
			_run_default();
		} else if (_phase == Genode::String<32>("override-bindings")) {
			_run_override();
		} else {
			Genode::error("shortcuts_probe: unknown phase '", _phase.string(), "'");
			_env.parent().exit(1);
			Genode::sleep_forever();
		}
	}

	/*
	 * Wait ~4s for sponge-de to construct (the ShortcutController
	 * needs to be live before the QMP-TARGET key markers fire; the
	 * controller's first read of the configd broadcast + the first
	 * event_filter_config emit happen on attach).
	 */
	void _wait_for_sponge_de()
	{
		_timer.msleep(4000);
	}

	void _open_dynamic_config_rom()
	{
		if (!_event_filter_config.constructed()) {
			try {
				_event_filter_config.construct(_env, "event_filter_config");
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::log("shortcuts-probe: event_filter_config ROM unavailable "
				            "(the run scenario did not stage the dynamic-config "
				            "report_rom policy; this is OK — the probe will only "
				            "verify the broadcast write path)");
			}
		}
	}

	void _open_shortcut_hit_rom()
	{
		if (!_shortcut_hit.constructed()) {
			try {
				_shortcut_hit.construct(_env, "shortcut_hit");
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::error("shortcuts-probe: shortcut_hit ROM unavailable "
				              "(the run scenario did not route the consolidated "
				              "report — W7 live gate cannot run)");
			}
		}
	}

	bool _wait_dynamic_config_has_shortcut(char const *name,
	                                     unsigned max_ms = 10000)
	{
		if (!_event_filter_config.constructed()) {
			Genode::log("shortcuts-probe: event_filter_config ROM not opened");
			return false;
		}
		unsigned const step_ms = 100;
		for (unsigned i = 0; i < (max_ms / step_ms); ++i) {
			_event_filter_config->update();
			if (_event_filter_config->valid()) {
				/*
				 * Use the Node API (auto-detects HID/XML) —
				 * the controller emits HID; the legacy
				 * Xml_node API would fail to parse.
				 */
				char const *p = _event_filter_config->local_addr<char>();
				Genode::size_t const sz = _event_filter_config->size();
				Genode::Node const root(Genode::Const_byte_range_ptr(p, sz));

				bool found = false;
				if (i == 0)
					Genode::log("shortcuts-probe: scanning for '", name, "'");

				/*
				 * Recursive walk (the lambda captures `found`,
				 * `expected`, `name` via reference). The
				 * `<shortcut>` children live deep under
				 * `<output> > <merge> > <report>`, several levels
				 * below the config root.
				 */
				struct Walk_rec {
					static void run(Genode::Node const &n,
					                char const *target_name,
					                bool &out_found) {
						n.for_each_sub_node([&] (Genode::Node const &c) {
							if (out_found) return;
							if (c.type() == "shortcut") {
								Genode::String<64> const nm =
									c.attribute_value("name", Genode::String<64>());
								if (Genode::strcmp(nm.string(), target_name) == 0) {
									out_found = true;
									return;
								}
							}
							run(c, target_name, out_found);
						});
					}
				};

				Walk_rec::run(root, name, found);
				if (found) return true;
				if (found) return true;
			}
			_timer.msleep(step_ms);
		}
		return false;
	}

	/*
	 * W7 live gate — drive a single QMP `send-key` and verify
	 * BOTH the consolidated `shortcut_hit` observation AND the
	 * user-visible side effect (popup open / close / focus change).
	 *
	 * The probe emits the `QMP-TARGET key <spec>` marker; the
	 * run script dispatches a real QMP send-key (qcode-object
	 * form for QEMU 11). The dispatched keypress traverses:
	 *   QMP -> emulated PS/2 keyboard -> ps2 -> event_filter
	 *   (the merged config's <report> source) -> <shortcut name=X>
	 *   hit -> sponge-de's ShortcutController -> action dispatch
	 *   AND `shortcut_hit` re-publish + GUI-thread action slot.
	 *
	 * The probe gates on (a) the consolidated hit (proves the
	 * event_filter capture + the controller dispatch) and (b)
	 * the side effect (proves the action slot actually ran).
	 * Both must succeed — failure of either is a HARD FAIL.
	 *
	 * Side-effect expectation by action:
	 *   launcher   — popup non-bg fraction rises (popover opens)
	 *   dismiss    — popup non-bg fraction falls (popover closes)
	 *   focus_next — no observable state change in this topology
	 *                (no wm, no windows to cycle between); the hit
	 *                alone is the proof.
	 */
	bool _run_live_action(char const *key_spec, char const *expected_action,
	                      bool check_popup_open)
	{
		Genode::log("QMP-TARGET key ", key_spec);

		if (!_wait_for_shortcut_hit(expected_action)) {
			_fail("shortcuts-probe", "shortcut_hit action != expected");
			return false;
		}
		Genode::log("shortcuts-probe: shortcut_hit action=", expected_action,
		            " hit=yes observed (W7 live keypress path)");

		if (Genode::strcmp(expected_action, "focus_next") == 0) {
			/*
			 * focus_next cycles focus between tracked windows; this
			 * topology has no wm / no windows, so cycle_focus()
			 * returns with a Genode::warning. The shortcut_hit
			 * observation above is the proof; no side-effect
			 * assertion.
			 */
			return true;
		}

		/*
		 * Popup side-effect proof via the `launcher_state` report
		 * (deterministic; the popup's on-screen position varies
		 * with the panel position, so a fixed Capture rect is
		 * unreliable — W7 evidence).
		 */
		_open_launcher_state_rom();
		char const *const want = check_popup_open ? "yes" : "no";
		if (!_wait_launcher_state(want)) {
			_fail("shortcuts-probe",
			      check_popup_open
			          ? "launcher_state open=yes never observed after launcher hit"
			          : "launcher_state open=no never observed after dismiss hit");
			return false;
		}
		Genode::log("shortcuts-probe: launcher popup ",
		            check_popup_open ? "open" : "close",
		            " observed via launcher_state");
		return true;
	}

	void _run_default()
	{
		char const *const PROBE = "shortcuts-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}
		Genode::log(PROBE, ": configd broadcast arrived");

		_wait_for_sponge_de();
		_open_dynamic_config_rom();
		_open_shortcut_hit_rom();

		/*
		 * STEP 1: write the W7 initial binding list.
		 */
		char const *const BINDINGS =
			"launcher\tSuper\nfocus_next\tAlt-Tab\ndismiss\tEscape";
		if (!_send_set("shortcuts.bindings", BINDINGS)) {
			_fail(PROBE, "set shortcuts.bindings=<initial> not answered");
			return;
		}
		if (!_broadcast_has("shortcuts.bindings", BINDINGS)) {
			_fail(PROBE, "broadcast missing shortcuts.bindings=<initial>");
			return;
		}
		Genode::log(PROBE, ": shortcuts.bindings=<initial> round-tripped");

		/*
		 * STEP 2: verify the dynamic event_filter_config report
		 * was emitted with the parsed `<shortcut>` children (the
		 * structural exerciser side — the controller re-parses
		 * configd, resolves synonyms, and re-emits).
		 */
		_open_dynamic_config_rom();
		bool config_ok = true;
		if (_wait_dynamic_config_has_shortcut("launcher")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"launcher\"/>");
		} else {
			Genode::log(PROBE, ": dynamic event_filter_config MISSING <shortcut name=\"launcher\"/>");
			config_ok = false;
		}
		if (_wait_dynamic_config_has_shortcut("focus_next")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"focus_next\"/>");
		} else {
			config_ok = false;
		}
		if (_wait_dynamic_config_has_shortcut("dismiss")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"dismiss\"/>");
		} else {
			config_ok = false;
		}
		if (!config_ok) {
			_fail(PROBE, "dynamic event_filter_config missing one or more shortcuts");
			return;
		}

		/*
		 * STEP 3: LIVE keypress gate. Three markers, dispatched
		 * by the run script via qmp_exec_target
		 * (`qmp_send_combination` under the hood). For each
		 * marker the probe verifies:
		 *   - shortcut_hit action=<X> hit=yes observed (the
		 *     controller's consolidated publisher proves the
		 *     event_filter capture + GUI-thread dispatch fired)
		 *   - the user-visible side effect for launcher (popup
		 *     open) and dismiss (popup close); focus_next has
		 *     no observable state change in this no-wm topology.
		 *
		 * Polling windows: the run script dispatches one QMP
		 * send-key per QMP-TARGET marker; the probe's hit poll
		 * is bounded by 8s (sufficient on this host; the QMP
		 * dispatch + GUI-thread marshal together take <500 ms
		 * in the panel-phase evidence).
		 */
		if (!_run_live_action("super", "launcher", true)) {
			return;
		}
		if (!_run_live_action("escape", "dismiss", false)) {
			return;
		}
		if (!_run_live_action("alt-tab", "focus_next", false)) {
			return;
		}

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}

	void _run_override()
	{
		char const *const PROBE = "shortcuts-extend-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}
		Genode::log(PROBE, ": configd broadcast arrived");

		_wait_for_sponge_de();
		_open_dynamic_config_rom();
		_open_shortcut_hit_rom();

		/*
		 * Override the launcher's Super binding with Ctrl-Alt-Tab.
		 * The configd validator accepts the override because
		 * Ctrl / Alt / Tab are all in the synonym table at
		 * configd/main.cc:864-882.
		 */
		char const *const BINDINGS =
			"launcher\tCtrl-Alt-Tab\nfocus_next\tAlt-Tab\ndismiss\tEscape";
		if (!_send_set("shortcuts.bindings", BINDINGS)) {
			_fail(PROBE, "set shortcuts.bindings=<override> not answered");
			return;
		}
		if (!_broadcast_has("shortcuts.bindings", BINDINGS)) {
			_fail(PROBE, "broadcast missing shortcuts.bindings=<override>");
			return;
		}
		Genode::log(PROBE, ": shortcuts.bindings=<override> round-tripped");

		_open_dynamic_config_rom();
		bool config_ok = true;
		if (_wait_dynamic_config_has_shortcut("launcher")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"launcher\"/>");
		} else {
			config_ok = false;
		}
		if (_wait_dynamic_config_has_shortcut("focus_next")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"focus_next\"/>");
		} else {
			config_ok = false;
		}
		if (_wait_dynamic_config_has_shortcut("dismiss")) {
			Genode::log(PROBE, ": dynamic event_filter_config carries <shortcut name=\"dismiss\"/>");
		} else {
			config_ok = false;
		}
		if (!config_ok) {
			_fail(PROBE, "dynamic event_filter_config missing one or more shortcuts");
			return;
		}

		/*
		 * W7 live gate for the override: dispatch the new
		 * binding (`ctrl-alt-tab`) and verify the launcher
		 * action fires. focus_next and dismiss keep their
		 * default bindings (Alt-Tab / Escape); we dispatch
		 * Alt-Tab (focus_next) as a sanity check and Escape
		 * (dismiss) to verify the dismisser still works after
		 * the launcher override.
		 *
		 * The 3 s settle waits out the reconfigure latency:
		 * configd write -> broadcast -> controller re-emit ->
		 * report_rom relay -> event_filter sigh -> chain rebuild.
		 * Without it the keypress lands before event_filter
		 * applies the new binding and the shortcut never fires.
		 */
		_timer.msleep(3000);

		if (!_run_live_action("ctrl-alt-tab", "launcher", true)) {
			return;
		}
		if (!_run_live_action("escape", "dismiss", false)) {
			return;
		}
		if (!_run_live_action("alt-tab", "focus_next", false)) {
			return;
		}

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }