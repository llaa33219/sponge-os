/* SPDX-License-Identifier: Apache-2.0
 *
 * configd_registry_probe — sponge_configd registry-extension acceptance
 * probe (Phase 16 W2).
 *
 * Plain Genode component (no libc, no Qt — AGENTS.md §3.1). It exercises
 * sponge_configd's Phase-16 W2 registry extensions end-to-end:
 *
 *   - Pattern keys (U16.5 / D16.5): the probe writes `panel.ids` then
 *     `panel.<id>.height` with a valid id, asserts the broadcast carries
 *     the per-id key (proves the pattern registry instantiated the slot
 *     on first write), then writes a `panel.<id>.height` whose id
 *     violates the `[a-z0-9_-]{1,16}` charset and asserts the error
 *     reply mentions the charset rule.
 *
 *   - Structured shortcuts key (U16.4 / D16.5/D16.10): the probe writes
 *     a well-formed multi-line binding list (action<TAB>sequence per
 *     line), asserts the broadcast carries the value, then writes a
 *     line with an unknown action token and asserts the error.
 *
 *   - Validator parity (F15): the probe writes a typo'd
 *     `panel.visble_widgets=clock` and asserts the structured error
 *     reply suggests `panel.visible_widgets` (the established
 *     validator parity path).
 *
 * One probe serves all three scenarios (selected via
 * `<config phase="..."/>`). Each scenario gates on its specific PASS
 * marker:
 *
 *   run/sponge-configd-pattern-keys.run  -> "pattern-keys-probe: PASS"
 *   run/sponge-configd-shortcuts.run    -> "shortcuts-probe: PASS"
 *   run/sponge-configd-badkey.run        -> "badkey-probe: PASS"
 *
 * Capability surface: Report (config_request) + ROM (config_result,
 * broadcast) + Timer. No Capture, no GUI.
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
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Expanding_reporter     _request   { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace _result    { _env, "config_result" };
	Genode::Attached_rom_dataspace _broadcast { _env, "broadcast" };

	Genode::String<32> _phase { };

	bool _ok { true };

	Probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *probe_name, char const *reason)
	{
		_ok = false;
		Genode::error(probe_name, ": FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/*
	 * Wait until sponge_configd has emitted its first broadcast. The
	 * probe's first request may race the daemon's signal wiring if
	 * the broadcast isn't present yet.
	 */
	bool _wait_for_broadcast()
	{
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
					if (root.has_type("config"))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Send a <request op="set" key="K" value="V"/> via the
	 * config_request Report. Polls for the matching config_result
	 * with status="ok" + op/key/value. Returns true on success.
	 */
	bool _send_set(char const *key, char const *value)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_result.valid()) {
				try {
					Genode::Xml_node const r = _result.xml();
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
	 * Like _send_set but expects status="error" + the error text
	 * containing the substring `expect_substr`. Used to assert that
	 * a write is rejected AND that the structured error mentions the
	 * right diagnosis (charset rule, unknown action, suggested key).
	 */
	bool _send_set_expect_error(char const *key, char const *value,
	                            char const *expect_substr)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_result.valid()) {
				try {
					Genode::Xml_node const r = _result.xml();
					if (r.attribute_value("status", Genode::String<32>()) ==
					    Genode::String<32>("error") &&
					    r.attribute_value("op",    Genode::String<32>()) ==
					    Genode::String<32>("set") &&
					    r.attribute_value("key",   Genode::String<128>()) ==
					    Genode::String<128>(key)) {
						Genode::String<256> const err =
							r.attribute_value("error", Genode::String<256>());
						if (err.length() > 0 && expect_substr != nullptr &&
						    Genode::strcmp(err.string(), "") != 0) {
							/* substring search */
							Genode::size_t const n = err.length();
							Genode::size_t const m = Genode::strlen(expect_substr);
							for (Genode::size_t k = 0; k + m <= n; ++k)
								if (Genode::strcmp(err.string() + k,
								                    expect_substr, m) == 0)
									return true;
						}
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Verify the broadcast ROM carries a <key name="K" value="V"/>
	 * entry. Polls for a bounded budget.
	 */
	bool _broadcast_has(char const *key, char const *value)
	{
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
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


	/* ============== phase pattern-keys ============== */

	/*
	 * The plan (W2 #3 step 1):
	 *   1. write panel.ids=alpha,beta
	 *   2. write panel.alpha.height=40 → broadcast carries it
	 *   3. write panel.bogus!!id.height=40 → error mentioning charset
	 *
	 * Final gate: "pattern-keys-probe: PASS".
	 */
	void _run_phase_pattern_keys()
	{
		char const *const PROBE = "pattern-keys-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}

		/* step 1: declare the active panel ids (comma-list) */
		if (!_send_set("panel.ids", "alpha,beta")) {
			_fail(PROBE, "set panel.ids=alpha,beta not answered");
			return;
		}
		if (!_broadcast_has("panel.ids", "alpha,beta")) {
			_fail(PROBE, "broadcast missing panel.ids=alpha,beta");
			return;
		}
		Genode::log(PROBE, ": panel.ids=alpha,beta broadcast ok");

		/*
		 * step 2: instantiate the per-id pattern key. The pattern
		 * registry walks the templates, validates 'alpha' against
		 * [a-z0-9_-]{1,16}, clones panel.height's Key_def into
		 * the instantiated slot, runs the cloned validator (uint
		 * 16..128), and persists the value.
		 */
		if (!_send_set("panel.alpha.height", "40")) {
			_fail(PROBE, "set panel.alpha.height=40 not answered");
			return;
		}
		if (!_broadcast_has("panel.alpha.height", "40")) {
			_fail(PROBE, "broadcast missing panel.alpha.height=40 "
			              "(pattern registry did not instantiate)");
			return;
		}
		Genode::log(PROBE, ": panel.alpha.height=40 broadcast ok");

		/*
		 * step 3: reject an invalid id (contains '!!' which is not
		 * in [a-z0-9_-]). The charset check runs BEFORE the value
		 * validator (plan W2 #3 step b); the structured error
		 * mentions the charset rule.
		 */
		if (!_send_set_expect_error("panel.bogus!!id.height", "40",
		                            "charset")) {
			_fail(PROBE, "panel.bogus!!id.height=40 was accepted "
			              "or error did not mention charset");
			return;
		}
		Genode::log(PROBE, ": panel.bogus!!id.height charset error ok");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}


	/* ============== phase shortcuts ============== */

	/*
	 * The plan (W2 #3 step 2):
	 *   1. write shortcuts.bindings=<3 lines action<TAB>sequence>
	 *   2. broadcast carries the value verbatim
	 *   3. write shortcuts.bindings=garbage_action\tSuper → error
	 *
	 * The value is a multi-line string. Each line is one binding;
	 * the line separator is \n (encoded as &#10; in XML when sent).
	 * Each line is <action_token>\t<key_sequence> where the action
	 * token must be a member of the closed enum {launcher,
	 * focus_next, dismiss} and the key sequence is a list of Genode
	 * Input-event keycodes separated by '-'.
	 *
	 * For the XML transport we need the value to round-trip through
	 * the report_rom report channel unchanged; we encode the
	 * newlines as raw "\n" characters in the attribute and rely on
	 * the XML attribute parser preserving them. (XML 1.0 attribute
	 * values allow the literal LF character; Genode's report_rom
	 * passes the value through unmodified.)
	 *
	 * Final gate: "shortcuts-probe: PASS".
	 */
	void _run_phase_shortcuts()
	{
		char const *const PROBE = "shortcuts-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}

		char const bindings[] = "launcher\tSuper"
		                        "\nfocus_next\tAlt-Tab"
		                        "\ndismiss\tEscape";

		/* step 1 + 2: write and verify */
		if (!_send_set("shortcuts.bindings", bindings)) {
			_fail(PROBE, "set shortcuts.bindings=<3 lines> not answered");
			return;
		}
		if (!_broadcast_has("shortcuts.bindings", bindings)) {
			_fail(PROBE, "broadcast missing shortcuts.bindings=<3 lines>");
			return;
		}
		Genode::log(PROBE, ": shortcuts.bindings=<3 lines> broadcast ok");

		/*
		 * step 3: unknown action token "garbage_action" is not a
		 * member of {launcher, focus_next, dismiss}. The validator
		 * emits Genode::warning AND a structured error with the
		 * action token + the expected enum.
		 */
		char const bad[] = "garbage_action\tSuper";
		if (!_send_set_expect_error("shortcuts.bindings", bad, "garbage_action")) {
			_fail(PROBE, "shortcuts.bindings=<bad action> was accepted "
			              "or error did not mention garbage_action");
			return;
		}
		Genode::log(PROBE, ": shortcuts.bindings=<bad action> error ok");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}


	/* ============== phase badkey ============== */

	/*
	 * The plan (W2 #6 validator parity): write a typo'd key
	 * `panel.visble_widgets=clock` and assert the structured error
	 * mentions the correct key `panel.visible_widgets` as the
	 * suggestion (mirrors the established vct validator parity).
	 *
	 * Final gate: "badkey-probe: PASS".
	 */
	void _run_phase_badkey()
	{
		char const *const PROBE = "badkey-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}

		/*
		 * The validator parity contract: an unknown key returns an
		 * error mentioning the suggestion (the closest known key).
		 * The current registry sees `panel.visble_widgets` as a
		 * completely unknown key (no levenshtein matching in the
		 * current validator), so the structured error must at least
		 * mention that the key is unknown AND offer
		 * `panel.visible_widgets` as the suggestion (F15 parity).
		 */
		if (!_send_set_expect_error("panel.visble_widgets", "clock",
		                            "panel.visible_widgets")) {
			_fail(PROBE, "panel.visble_widgets=clock was accepted "
			              "or error did not suggest panel.visible_widgets");
			return;
		}
		Genode::log(PROBE, ": panel.visble_widgets validator parity error ok");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}


	/* ============== driver ============== */

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid()) {
			_fail("configd-registry-probe", "no probe config ROM");
			return;
		}

		_phase = _config_rom.node().attribute_value("phase",
		                                            Genode::String<32>());

		if (_phase == "pattern-keys") { _run_phase_pattern_keys(); return; }
		if (_phase == "shortcuts")   { _run_phase_shortcuts();   return; }
		if (_phase == "badkey")      { _run_phase_badkey();      return; }

		_fail("configd-registry-probe",
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