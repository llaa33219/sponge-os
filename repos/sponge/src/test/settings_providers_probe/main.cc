/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_providers_probe — Criterion 18 settings-provider acceptance
 * probe (run/sponge-settings-providers.run).
 *
 * Drives the EXACT wire actions of sponge-de's QuickMenuController and
 * LauncherController::request_settings_open (docs/05-sponge-de.md §5.4,
 * the "Quick-menu change lifecycle") against the real sponge_pkgd /
 * settings-provider / sponge_configd chain, plus the Criterion 18
 * per-writer-label key-domain ACL:
 *
 *   phase 0  wait for the configd broadcast (the store exists)
 *   phase 1  <request op="install" pkg="sponge-de"/> on the vct
 *            `request` channel — this probe is the vct-equivalent
 *            writer for both sponge_pkgd (`request`) and
 *            sponge_configd (`config_request`, kept empty). The
 *            `installed` broadcast must then carry the pkg/sponge-de
 *            settings entries (panel -> panel_settings,
 *            theme -> theme_settings).
 *   phase 2  (a) COLD Panel quick-menu action, wire-identical to the
 *            QuickMenuController lifecycle:
 *              2a ensure-start (sponge-de, panel) over the EXISTING
 *                 launcher_request / launcher_result pair (the exact
 *                 LauncherController::request_settings_open payload:
 *                 <request op="launch" target="settings" pkg="sponge-de"
 *                 id="panel"/>). The answer MUST be action="started"
 *                 (a cold ensure-start); "focused" here would mean a
 *                 stale claim and fails loud.
 *              2b ONE closed <quick_request op="height" value="40"/> on
 *                 panel_settings_quick_request (the default panel uses
 *                 the empty quick id — no id attribute), accepted by
 *                 the QuickMenuController echo rule (ok carrying op AND
 *                 value equal to the request).
 *              2c the configd broadcast must carry panel.height=40.
 *   phase 3  a settings entry ensure-start through the SETTINGS shell
 *            wire (the settings_request / settings_result pair):
 *            <request op="launch" target="settings" pkg="sponge-de"
 *            id="theme"/> must answer action="started" — the
 *            (sponge-de, theme) entry's ensure-start is observable on
 *            the shell channel as well as on the pkgd log.
 *   phase 4  (b) per-label key-domain ACL on the theme_settings writer
 *            label (this probe drives that label — see the writer-slot
 *            note below; the phase runs after a 2 s settle so the
 *            theme provider is up before the adversarial write):
 *              4a in-domain positive control: theme.active=dark is
 *                 accepted by configd and lands in the broadcast;
 *              4b panel.height=99 is refused with
 *                 reason="key-outside-domain" (bounded English naming
 *                 writer label 'theme_settings') and the store is NOT
 *                 mutated: the broadcast stays byte-identical to its
 *                 pre-request snapshot.
 *
 * Writer-slot note (report_rom keeps a single writer per module): the
 * theme_settings configd request ROM carries THIS probe's report in
 * this scenario. configd attributes the write to the 'theme_settings'
 * label because the request arrived on that label's channel — exactly
 * the provider-label identity the Criterion 18 writer table defines
 * (sponge_configd/key_domain_acl.cc) and the enforcement under test.
 * The running theme provider's own writer slot is deliberately not
 * wired to configd here: its startup is the phase-3 ensure-start
 * witness only (its window path is what phase 3 observes).
 *
 * Redelivery note (phase 2b): report_rom has no delivery handshake, so
 * while polling for the quick result the probe redelivers the IDENTICAL
 * closed quick_request every 2 s (bounded). The provider's quick serve
 * is idempotent for op=height value=40 and the configd channel de-dups
 * the byte-identical set, so redelivery cannot corrupt the store.
 * Launcher/settings opens are NEVER redelivered: a second delivery of
 * the same open would answer "focused" and destroy the cold evidence.
 *
 * This probe never writes focus_request (sponge-de's TasklistController
 * stays its sole writer) and never touches any universal/de_config
 * label. Every failure is a bounded English FAIL line + exit(1); the
 * explicit PASS line is emitted only after every check succeeded.
 *
 * Capability surface (all routed by the scenario):
 *   Report: request, config_request (vct-slot stub, stays empty),
 *           launcher_request, settings_request,
 *           panel_settings_quick_request, theme_settings_quick_request
 *           (idle slot for provider construction),
 *           theme_settings_config_request
 *   ROM:    result, launcher_result, settings_result, installed,
 *           broadcast, panel_settings_quick_result,
 *           theme_settings_config_result
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

	Timer::Connection _timer { _env };

	/* readers */
	Genode::Attached_rom_dataspace _broadcast       { _env, "broadcast" };
	Genode::Attached_rom_dataspace _installed       { _env, "installed" };
	Genode::Attached_rom_dataspace _pkgd_result     { _env, "result" };
	Genode::Attached_rom_dataspace _launcher_result { _env, "launcher_result" };
	Genode::Attached_rom_dataspace _settings_result { _env, "settings_result" };
	Genode::Attached_rom_dataspace _panel_quick_res { _env, "panel_settings_quick_result" };
	Genode::Attached_rom_dataspace _theme_res       { _env, "theme_settings_config_result" };

	/* writers */
	Genode::Expanding_reporter _pkgd_request       { _env, "request", "request" };
	Genode::Expanding_reporter _configd_request    { _env, "request", "config_request" };
	Genode::Expanding_reporter _launcher_request   { _env, "request", "launcher_request" };
	Genode::Expanding_reporter _settings_request   { _env, "request", "settings_request" };
	Genode::Expanding_reporter _panel_quick_req    { _env, "quick_request", "panel_settings_quick_request" };
	Genode::Expanding_reporter _theme_quick_req    { _env, "quick_request", "theme_settings_quick_request" };
	Genode::Expanding_reporter _theme_config_req   { _env, "request", "theme_settings_config_request" };

	/* pre-rejection broadcast snapshot (the no-mutation witness) */
	enum { SNAP_CAP = 16 * 1024 };
	char          _snap[SNAP_CAP] { };
	Genode::size_t _snap_len { 0 };

	Probe(Genode::Env &env) : _env(env) { }

	static bool contains(char const *haystack, char const *needle)
	{
		Genode::size_t const n = Genode::strlen(needle);
		if (n == 0)
			return true;
		for (char const *p = haystack; *p; ++p) {
			Genode::size_t i = 0;
			while (i < n && p[i] && p[i] == needle[i])
				++i;
			if (i == n)
				return true;
		}
		return false;
	}

	void _fail(char const *reason)
	{
		Genode::error("settings-providers-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	void _dump_rom(char const *what, Genode::Attached_rom_dataspace &rom)
	{
		rom.update();
		if (!rom.valid()) {
			Genode::log("settings-providers-probe: [diag] ", what,
			            ": <empty>");
			return;
		}
		Genode::size_t const sz = rom.size();
		char buf[256] { };
		Genode::size_t const n = sz < sizeof(buf) - 1 ? sz : sizeof(buf) - 1;
		Genode::memcpy(buf, rom.local_addr<char>(), n);
		buf[n] = 0;
		Genode::log("settings-providers-probe: [diag] ", what, ": ",
		            (char const *)buf);
	}

	/* bounded poll: 100 ms per iteration */
	template <typename FN>
	bool _poll(FN const &check, unsigned iters)
	{
		for (unsigned i = 0; i < iters; ++i) {
			if (check())
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	static bool attr_is(Genode::Xml_node const &n, char const *name,
	                    char const *value)
	{
		try {
			return n.attribute_value(name, Genode::String<128>()) ==
			       Genode::String<128>(value);
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	/* ---- broadcast helpers ---- */

	bool _broadcast_has(char const *key, char const *value)
	{
		_broadcast.update();
		if (!_broadcast.valid())
			return false;
		try {
			Genode::Xml_node const root = _broadcast.xml();
			if (!root.has_type("config"))
				return false;
			bool found { false };
			root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
				if (!found && attr_is(k, "name", key) &&
				    attr_is(k, "value", value))
					found = true;
			});
			return found;
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _broadcast_has_pair(char const *key, char const *value)
	{
		return _poll([&] { return _broadcast_has(key, value); }, 50);
	}

	void _snapshot_broadcast()
	{
		_broadcast.update();
		_snap_len = 0;
		if (!_broadcast.valid())
			return;
		Genode::size_t const sz = _broadcast.size();
		if (sz > SNAP_CAP) {
			_fail("configd broadcast larger than snapshot buffer");
			return;
		}
		Genode::memcpy(_snap, _broadcast.local_addr<char>(), sz);
		_snap_len = sz;
	}

	bool _broadcast_unchanged()
	{
		_broadcast.update();
		if (!_broadcast.valid())
			return false;
		if (_broadcast.size() != _snap_len)
			return false;
		return Genode::memcmp(_snap, _broadcast.local_addr<char>(),
		                      _snap_len) == 0;
	}

	/* ---- phase 0: configd up ---- */

	void phase_broadcast_up()
	{
		bool up = _poll([&] {
			_broadcast.update();
			if (!_broadcast.valid())
				return false;
			try {
				return _broadcast.xml().has_type("config");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!up)
			_fail("configd broadcast never arrived");
		Genode::log("settings-providers-probe: configd broadcast up");
	}

	/* ---- phase 1: install pkg/sponge-de + entry discovery ---- */

	void phase_install()
	{
		_pkgd_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", "sponge-de");
		});

		bool ok = _poll([&] {
			_pkgd_result.update();
			if (!_pkgd_result.valid())
				return false;
			try {
				Genode::Xml_node const r = _pkgd_result.xml();
				return r.has_type("result") &&
				       attr_is(r, "status", "ok") &&
				       attr_is(r, "op",    "install") &&
				       attr_is(r, "pkg",   "sponge-de");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!ok)
			_fail("install sponge-de not answered ok on the pkgd result channel");
		Genode::log("settings-providers-probe: install sponge-de -> ok");

		bool panel_ok { false }, theme_ok { false };
		ok = _poll([&] {
			_installed.update();
			if (!_installed.valid())
				return false;
			try {
				Genode::Xml_node const root = _installed.xml();
				if (!root.has_type("installed"))
					return false;
				root.for_each_sub_node("packages",
					[&] (Genode::Xml_node const &ps) {
						ps.for_each_sub_node("package",
							[&] (Genode::Xml_node const &p) {
								if (!attr_is(p, "name", "sponge-de"))
									return;
								p.for_each_sub_node("settings",
									[&] (Genode::Xml_node const &s) {
										if (attr_is(s, "id", "panel") &&
										    attr_is(s, "target", "panel_settings"))
											panel_ok = true;
										if (attr_is(s, "id", "theme") &&
										    attr_is(s, "target", "theme_settings"))
											theme_ok = true;
									});
							});
					});
				return panel_ok && theme_ok;
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!ok)
			_fail("installed broadcast lacks the pkg/sponge-de panel/theme entries");
		Genode::log("settings-providers-probe: installed entries "
		            "discovered (panel -> panel_settings, "
		            "theme -> theme_settings)");
	}

	/* ---- phase 2: (a) cold quick-menu action ---- */

	void phase_cold_quick_height_40()
	{
		Genode::log("settings-providers-probe: quick-menu action "
		            "(height 40): cold ensure-start via launcher pair");

		/* 2a — LauncherController::request_settings_open wire shape */
		_launcher_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",     "launch");
			g.attribute("target", "settings");
			g.attribute("pkg",    "sponge-de");
			g.attribute("id",     "panel");
		});

		bool ok { false }, focused { false };
		bool answered = _poll([&] {
			_launcher_result.update();
			if (!_launcher_result.valid())
				return false;
			try {
				Genode::Xml_node const r = _launcher_result.xml();
				if (!r.has_type("result") ||
				    !attr_is(r, "status", "ok") ||
				    !attr_is(r, "op",    "launch") ||
				    !attr_is(r, "pkg",   "sponge-de") ||
				    !attr_is(r, "id",    "panel") ||
				    !attr_is(r, "target", "settings"))
					return false;
				if (attr_is(r, "action", "started")) {
					ok = true;
					return true;
				}
				if (attr_is(r, "action", "focused")) {
					focused = true;
					return true;
				}
			}
			catch (Genode::Xml_node::Invalid_syntax) { }
			return false;
		}, 100);
		if (!answered)
			_fail("launcher pair never answered the (sponge-de, panel) ensure-start");
		if (focused)
			_fail("ensure-start answered 'focused' — panel_settings was "
			      "already claimed before this cold quick-menu action");
		if (!ok)
			_fail("launcher pair answered the ensure-start with an "
			      "unexpected action");
		Genode::log("settings-providers-probe: ensure-start "
		            "sponge-de/panel -> started");

		/* 2b — one closed quick request (empty quick id = default panel),
		 * with bounded identical redelivery (see the header note) */
		_panel_quick_req.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",    "height");
			g.attribute("value", "40");
		});

		ok = false;
		for (unsigned round = 0; round < 15 && !ok; ++round) {
			ok = _poll([&] {
				_panel_quick_res.update();
				if (!_panel_quick_res.valid())
					return false;
				try {
					Genode::Xml_node const r = _panel_quick_res.xml();
					if (!r.has_type("quick_result"))
						return false;
					if (attr_is(r, "status", "ok"))
						return attr_is(r, "op",    "height") &&
						       attr_is(r, "value", "40");
					if (attr_is(r, "status", "error")) {
						_dump_rom("quick_result", _panel_quick_res);
						_fail("provider refused the height-40 quick request");
					}
				}
				catch (Genode::Xml_node::Invalid_syntax) {
					_dump_rom("quick_result (unparsable)", _panel_quick_res);
					_fail("quick_result ROM holds unparsable content");
				}
				return false;
			}, 20);
			if (!ok)
				_panel_quick_req.generate_xml([&] (Genode::Xml_generator &g) {
					g.attribute("op",    "height");
					g.attribute("value", "40");
				});
		}
		if (!ok) {
			_dump_rom("panel_settings_quick_result", _panel_quick_res);
			_fail("panel_settings_quick_result never answered "
			      "<quick_result status=\"ok\" op=\"height\" value=\"40\"/>");
		}
		Genode::log("settings-providers-probe: quick height 40 -> ok "
		            "(panel_settings_quick_result)");

		/* 2c — configd broadcast confirms the applied operation */
		if (!_broadcast_has_pair("panel.height", "40"))
			_fail("configd broadcast does not carry panel.height=40");
		Genode::log("settings-providers-probe: broadcast panel.height=40");
	}

	/* ---- per-id quick operation: widgets uses the registered key ---- */

	void phase_per_id_widgets()
	{
		_panel_quick_req.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",    "widgets");
			g.attribute("id",    "alpha");
			g.attribute("value", "clock,launcher");
		});

		bool ok { false };
		for (unsigned round = 0; round < 15 && !ok; ++round) {
			ok = _poll([&] {
				_panel_quick_res.update();
				if (!_panel_quick_res.valid())
					return false;
				try {
					Genode::Xml_node const r = _panel_quick_res.xml();
					if (!r.has_type("quick_result"))
						return false;
					if (attr_is(r, "status", "ok"))
						return attr_is(r, "op",    "widgets") &&
						       attr_is(r, "value", "clock,launcher");
					if (attr_is(r, "status", "error")) {
						_dump_rom("quick_result", _panel_quick_res);
						_fail("provider refused the per-id widgets request");
					}
				}
				catch (Genode::Xml_node::Invalid_syntax) {
					_dump_rom("quick_result (unparsable)", _panel_quick_res);
					_fail("per-id quick_result ROM holds unparsable content");
				}
				return false;
			}, 20);
			if (!ok)
				_panel_quick_req.generate_xml([&] (Genode::Xml_generator &g) {
					g.attribute("op",    "widgets");
					g.attribute("id",    "alpha");
					g.attribute("value", "clock,launcher");
				});
		}
		if (!ok)
			_fail("per-id widgets quick request was not accepted");

		if (!_broadcast_has_pair("panel.alpha.visible_widgets", "clock,launcher"))
			_fail("per-id widgets did not land on panel.alpha.visible_widgets");
		Genode::log("settings-providers-probe: per-id widgets -> ok "
		            "(panel.alpha.visible_widgets=clock,launcher)");
	}

	/* ---- phase 3: settings entry ensure-start (shell wire) ---- */

	void phase_settings_entry_ensure_start()
	{
		_settings_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",     "launch");
			g.attribute("target", "settings");
			g.attribute("pkg",    "sponge-de");
			g.attribute("id",     "theme");
		});

		bool ok = _poll([&] {
			_settings_result.update();
			if (!_settings_result.valid())
				return false;
			try {
				Genode::Xml_node const r = _settings_result.xml();
				return r.has_type("result") &&
				       attr_is(r, "status", "ok") &&
				       attr_is(r, "op",    "launch") &&
				       attr_is(r, "pkg",   "sponge-de") &&
				       attr_is(r, "id",    "theme") &&
				       attr_is(r, "target", "settings") &&
				       attr_is(r, "action", "started");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!ok)
			_fail("settings_request never answered "
			      "(sponge-de, theme) -> ok started");
		Genode::log("settings-providers-probe: settings entry "
		            "ensure-start sponge-de/theme -> started");
	}

	/* ---- phase 4: (b) theme label domain ACL ---- */

	void phase_domain_rejection()
	{
		/* 4a — in-domain positive control on the theme label */
		_theme_config_req.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   "theme.active");
			g.attribute("value", "dark");
		});

		bool ok = _poll([&] {
			_theme_res.update();
			if (!_theme_res.valid())
				return false;
			try {
				Genode::Xml_node const r = _theme_res.xml();
				return r.has_type("result") &&
				       attr_is(r, "status", "ok") &&
				       attr_is(r, "op",    "set") &&
				       attr_is(r, "key",   "theme.active") &&
				       attr_is(r, "value", "dark");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!ok) {
			_dump_rom("theme_settings_config_result", _theme_res);
			_fail("theme label in-domain set theme.active=dark not answered ok");
		}
		if (!_broadcast_has_pair("theme.active", "dark"))
			_fail("configd broadcast does not carry theme.active=dark");
		Genode::log("settings-providers-probe: theme label in-domain "
		            "write theme.active=dark -> ok");

		/* 4b — out-of-domain attempt; the store must not move */
		_snapshot_broadcast();

		_theme_config_req.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   "panel.height");
			g.attribute("value", "99");
		});

		bool rejected { false };
		ok = _poll([&] {
			_theme_res.update();
			if (!_theme_res.valid())
				return false;
			try {
				Genode::Xml_node const r = _theme_res.xml();
				if (!r.has_type("result") ||
				    !attr_is(r, "op",  "set") ||
				    !attr_is(r, "key", "panel.height"))
					return false;
				if (attr_is(r, "status", "ok")) {
					_dump_rom("theme_settings_config_result", _theme_res);
					_fail("configd ACCEPTED the out-of-domain "
					      "panel.height=99 write (domain ACL broken)");
				}
				if (!attr_is(r, "status", "error") ||
				    !attr_is(r, "reason", "key-outside-domain"))
					return false;
				Genode::String<256> const err =
					r.attribute_value("error", Genode::String<256>());
				if (!contains(err.string(), "key 'panel.height'") ||
				    !contains(err.string(), "writer label 'theme_settings'"))
					return false;
				rejected = true;
				return true;
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}, 100);
		if (!ok) {
			_dump_rom("theme_settings_config_result", _theme_res);
			_fail("theme label set panel.height=99 was not refused with "
			      "reason=key-outside-domain naming writer label "
			      "'theme_settings'");
		}
		(void)rejected;
		Genode::log("settings-providers-probe: theme label "
		            "panel.height=99 -> rejected (key-outside-domain)");

		/* store non-mutation: byte-identical broadcast + semantic checks */
		if (!_broadcast_unchanged())
			_fail("configd broadcast changed after the rejected write "
			      "(store was mutated)");
		if (!_broadcast_has("panel.height", "40"))
			_fail("broadcast lost panel.height=40 after the rejected write");
		if (_broadcast_has("panel.height", "99"))
			_fail("broadcast carries the rejected panel.height=99");
		if (!_broadcast_has("theme.active", "dark"))
			_fail("broadcast lost theme.active=dark after the rejected write");
		Genode::log("settings-providers-probe: store unmutated "
		            "(broadcast byte-identical, panel.height=40 kept)");
	}

	void run()
	{
		phase_broadcast_up();
		phase_install();
		phase_cold_quick_height_40();
		phase_per_id_widgets();
		phase_settings_entry_ensure_start();

		_timer.msleep(2000);

		phase_domain_rejection();

		Genode::log("settings-providers-probe: PASS - cold quick height-40 "
		            "round trip + broadcast + theme label key-outside-domain "
		            "rejection all witnessed");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}
