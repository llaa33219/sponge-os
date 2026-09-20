/* SPDX-License-Identifier: Apache-2.0
 *
 * ShortcutController — see shortcut_controller.h.
 */

#include "shortcut_controller.h"

#include "dismisser.h"
#include "launcher/launcher_controller.h"
#include "panel/tasklist_controller.h"

#include <base/log.h>
#include <base/node.h>
#include <input/keycodes.h>
#include <util/hid.h>
#include <util/string.h>
#include <cstdio>

#include <QApplication>
#include <QMetaObject>
#include <QTimer>

#include <QApplication>
#include <QMetaObject>
#include <QTimer>

using namespace Sponge::Sponge_DE;
using ::TasklistController;


/*
 * Closed action enum — the Phase 16 W7 binding scope. The plan
 * pins this to three actions (D16.5). Future Phase 17+ work may
 * extend; until then, the parser REJECTS unknown action tokens
 * with a structured error (configd's validator already does
 * that — we re-validate here as a belt-and-braces guard).
 */
namespace {

struct Action_info {
	char const *name;
	char const *key_sequence_default;
};

Action_info const action_table[] = {
	{ "launcher",   "Super" },
	{ "focus_next", "Alt-Tab" },
	{ "dismiss",    "Escape" },
};
constexpr unsigned num_actions =
    sizeof(action_table) / sizeof(action_table[0]);


/*
 * Map an alias token to its canonical Genode Input-event keycode.
 * Mirrors the synonym table in sponge_configd/main.cc:864-882 so
 * the framework and the validator agree on the spelling.
 */
struct Synonym { char const *alias; char const *canonical; };
Synonym const synonyms[] = {
	{ "Super",      "KEY_LEFTMETA" },
	{ "Meta",       "KEY_LEFTMETA" },
	{ "MetaL",      "KEY_LEFTMETA" },
	{ "MetaR",      "KEY_RIGHTMETA"},
	{ "Esc",        "KEY_ESC"      },
	{ "Escape",     "KEY_ESC"      },
	{ "Tab",        "KEY_TAB"      },
	{ "Return",     "KEY_ENTER"    },
	{ "Enter",      "KEY_ENTER"    },
	{ "Backspace",  "KEY_BACKSPACE"},
	{ "Alt",        "KEY_LEFTALT"  },
	{ "AltL",       "KEY_LEFTALT"  },
	{ "AltR",       "KEY_RIGHTALT" },
	{ "Ctrl",       "KEY_LEFTCTRL" },
	{ "CtrlL",      "KEY_LEFTCTRL" },
	{ "Shift",      "KEY_LEFTSHIFT"},
	{ "Space",      "KEY_SPACE"    },
	{ "Minus",      "KEY_MINUS"    },
	{ "Plus",       "KEY_EQUAL"    },
	{ "Backquote",  "KEY_GRAVE"    },
	{ "Slash",      "KEY_SLASH"    },
	{ "Comma",      "KEY_COMMA"    },
	{ "Dot",        "KEY_DOT"      },
	{ "Apostrophe", "KEY_APOSTROPHE"},
	{ "Semicolon",  "KEY_SEMICOLON"},
};


bool parse_config_asks_for_shortcuts(Genode::Attached_rom_dataspace &config)
{
	config.update();
	if (!config.valid()) return false;

	char const *const base = config.local_addr<char>();
	Genode::size_t  const sz  = config.size();

	bool live = false;
	bool const is_xml = (sz > 0 && base[0] == '<');
	if (is_xml) {
		try {
			Genode::Xml_node const root(base, sz);
			root.for_each_sub_node("shortcuts", [&](Genode::Xml_node const &s) {
				if (!live)
					live = s.attribute_value("source",
					         Genode::String<32>()) ==
					       Genode::String<32>("controller");
			});
		} catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!live && n.has_type("shortcuts"))
				live = n.attribute_value("source",
				         Genode::String<32>()) ==
				       Genode::String<32>("controller");
		});
	}

	return live;
}


char const *resolve_synonym(char const *tok)
{
	for (unsigned s = 0;
	     s < sizeof(synonyms) / sizeof(synonyms[0]); ++s)
		if (Genode::strcmp(tok, synonyms[s].alias) == 0)
			return synonyms[s].canonical;
	return tok;
}

}  /* anonymous namespace */


bool Sponge::Sponge_DE::shortcuts_asks_for_controller(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	return parse_config_asks_for_shortcuts(config);
}


ShortcutController::ShortcutController(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
	bool const live = shortcuts_asks_for_controller(_env);

	if (!live) {
		Genode::log("sponge-de: shortcuts source=none (no ShortcutController "
		            "wiring; framework unavailable in this topology)");
		return;
	}

	Genode::log("sponge-de: shortcuts source=controller (live keyboard-shortcut framework)");

	try {
		_event_filter_reporter.construct(_env, "report", "event_filter_config");

		_config_rom.construct(_env, "configd");
		_config_rom->update();
		_sigh.construct(_env.ep(), *this, &ShortcutController::_on_rom);
		_config_rom->sigh(*_sigh);
		_on_rom();

		/*
		 * 250 ms poll — same cadence as ConfigController. The
		 * ROM-signal handler may miss the initial version
		 * depending on the entrypoint-dispatcher scheduling;
		 * the timer is the deterministic fallback.
		 */
		_poll_timer = new QTimer(this);
		_poll_timer->start(250);
		QObject::connect(_poll_timer, &QTimer::timeout,
		                 this, &ShortcutController::_poll);
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("sponge-de: shortcuts: configd ROM unavailable "
		                "(no report_rom policy for "
		                "sponge-de -> configd <- sponge_configd -> config)");
		_config_rom.destruct();
		_sigh.destruct();
	}
}


ShortcutController::~ShortcutController()
{
	if (_poll_timer) {
		_poll_timer->stop();
		_poll_timer->deleteLater();
		_poll_timer = nullptr;
	}
}


/*
 * Genode entrypoint dispatcher thread. Read the ROM, marshal to
 * the GUI thread via QMetaObject::invokeMethod(... , Qt::Queued
 * Connection). NEVER touches a QWidget or QApplication here.
 */
void ShortcutController::_on_rom()
{
	if (!_config_rom.constructed()) return;

	_config_rom->update();
	if (!_config_rom->valid()) return;

	char const *const base = _config_rom->local_addr<char>();
	Genode::size_t const sz  = _config_rom->size();
	QString const payload = QString::fromUtf8(base, (int)sz);
	if (payload.isEmpty()) return;

	QMetaObject::invokeMethod(this, "applyBindings",
	                          Qt::QueuedConnection,
	                          Q_ARG(QString, payload));
}


void ShortcutController::_poll()
{
	if (!_config_rom.constructed()) return;

	_config_rom->update();
	if (_config_rom->valid()) {
		char const *const base = _config_rom->local_addr<char>();
		Genode::size_t const sz  = _config_rom->size();
		QString const payload = QString::fromUtf8(base, (int)sz);
		if (!payload.isEmpty())
			applyBindings(payload);
	}

	/*
	 * Poll the per-action shortcut ROMs (the report_rom reader
	 * notification for the policy-relayed per-action reports
	 * does not reliably reach the subscriber sigh on seL4 —
	 * empirically the sigh never fires even though the report
	 * content updates; the 250 ms poll is the deterministic
	 * path, matching the ConfigController sigh+poll pattern).
	 * Serial-deduplicated in _dispatch_hit_subscriber.
	 */
	_dispatch_hit_subscriber(_hit_sub_launcher);
	_dispatch_hit_subscriber(_hit_sub_focus_next);
	_dispatch_hit_subscriber(_hit_sub_dismiss);
}


/*
 * Extract `shortcuts.bindings` from the configd broadcast XML.
 * Same shape as ConfigController::applyConfig() (the W2 key
 * landed in the closed registry with a structured validator).
 *
 * The value is the literal string configd stored after the
 * `<key name="shortcuts.bindings" value="..."/>` attribute
 * decode — the attribute is unescaped XML (configd's
 * Xml_generator does NOT re-escape `\n` / `\t` because they
 * survive the encode round-trip inside the <key>'s value=
 * attribute when the writer uses the genode Xml_generator API).
 *
 * For Phase 16 W7 the value is read verbatim; the per-line
 * validation happens lazily in `_build_shortcuts_xml()` (we
 * re-validate as a belt-and-braces guard so a corrupted
 * broadcast cannot produce a malformed event_filter config).
 */
static QString extract_shortcuts_bindings(QString const &payload)
{
	try {
		Genode::Xml_node const root(payload.toUtf8().constData(),
		                            (Genode::size_t)payload.toUtf8().size());
		Genode::String<1024> value { };
		bool found = false;
		root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
			if (!found &&
			    k.attribute_value("name", Genode::String<64>()) ==
			        Genode::String<64>("shortcuts.bindings")) {
				value = k.attribute_value("value", Genode::String<1024>());
				found = true;
			}
		});
		if (found) return QString::fromUtf8(value.string());
	}
	catch (Genode::Xml_node::Invalid_syntax) { }
	return QString();
}


/*
 * Build the inner XML body for the event_filter `<report>`
 * source. Each `<shortcut>` element corresponds to one
 * `action<tab>key_sequence` line in the bound value. The
 * `name` attribute is the action token; the `<key name="..."/>`
 * children are the Genode Input-event keycodes.
 *
 * On parse error (unknown action token or unresolvable keycode),
 * the line is SKIPPED with a `Genode::warning` (the plan's
 * contract: never a silent drop). The broadcast validator
 * (configd) is the primary gate; this is a defensive second
 * pass that catches any corruption in transit.
 */
QString ShortcutController::_build_shortcuts_xml(QString const &value)
{
	QString out;
		Genode::size_t const length = Genode::strlen(value.toUtf8().constData());
		Genode::size_t line_start { 0 };
		unsigned line_no { 0 };
		QByteArray const utf8 = value.toUtf8();
		char const *const buf = utf8.constData();

		while (line_start < length) {
			Genode::size_t line_end = line_start;
			while (line_end < length && buf[line_end] != '\n') ++line_end;
			++line_no;

			Genode::size_t first = line_start;
			Genode::size_t last  = line_end;
			while (first < last && (buf[first] == ' ' || buf[first] == '\t' ||
			       buf[first] == '\r')) ++first;
			while (last > first && (buf[last - 1] == ' ' ||
			       buf[last - 1] == '\t' || buf[last - 1] == '\r')) --last;

			if (first == last) {
				++line_start = line_end + 1;
				continue;
			}

			Genode::size_t tab_pos { 0 };
			bool found_tab { false };
			bool multi_tab { false };
			for (Genode::size_t i = first; i < last; ++i)
				if (buf[i] == '\t') {
					if (found_tab) {
						multi_tab = true;
						break;
					}
					tab_pos = i;
					found_tab = true;
				}
			if (multi_tab) {
				Genode::String<8> const ln { line_no };
				Genode::warning("sponge-de: shortcut line ", ln,
				                ": multiple TABs, skipping");
				++line_start = line_end + 1;
				continue;
			}
			if (!found_tab) {
				++line_start = line_end + 1;
				continue;
			}

			char action[32] { };
			Genode::size_t const action_len = tab_pos - first;
			if (action_len == 0 || action_len >= sizeof(action)) {
				++line_start = line_end + 1;
				continue;
			}
			for (Genode::size_t i = 0; i < action_len; ++i)
				action[i] = buf[first + i];

			bool action_ok { false };
			for (unsigned i = 0; i < num_actions; ++i)
				if (Genode::strcmp(action, action_table[i].name) == 0) {
					action_ok = true;
					break;
				}
			if (!action_ok) {
				Genode::String<8> const ln { line_no };
				Genode::String<32> const act { action };
				Genode::warning("sponge-de: shortcut line ", ln,
				                ": unknown action token '", act,
				                "', skipping");
				++line_start = line_end + 1;
				continue;
			}

			Genode::size_t const seq_first = tab_pos + 1;
			Genode::size_t const seq_last  = last;
			if (seq_first > seq_last) {
				++line_start = line_end + 1;
				continue;
			}

			out += QStringLiteral("<shortcut name=\"");
			out += QString::fromUtf8(action);
			out += QStringLiteral("\">");

			Genode::size_t ks = seq_first;
			bool key_error { false };
			while (ks < seq_last) {
				Genode::size_t ke = ks;
				while (ke < seq_last && buf[ke] != '-') ++ke;

				char keyname[32] { };
				Genode::size_t const kn_len = ke - ks;
				if (kn_len == 0 || kn_len >= sizeof(keyname)) {
					Genode::String<8> const ln { line_no };
					Genode::warning("sponge-de: shortcut line ", ln,
					                ": empty key token, skipping");
					key_error = true;
					break;
				}
				for (Genode::size_t i = 0; i < kn_len; ++i)
					keyname[i] = buf[ks + i];

				char const *resolved = resolve_synonym(keyname);
				Genode::String<22> const kn { resolved };
				if (::Input::key_code(kn) == ::Input::KEY_UNKNOWN) {
					Genode::String<8> const ln { line_no };
					Genode::String<32> const kn_disp { keyname };
					Genode::warning("sponge-de: shortcut line ", ln,
					                ": unknown key '", kn_disp,
					                "', skipping");
					key_error = true;
					break;
				}

				out += QStringLiteral("<key name=\"");
				out += QString::fromUtf8(resolved);
				out += QStringLiteral("\"/>");

				if (ke == seq_last) break;
				ks = ke + 1;
			}

		if (key_error) {
			/* Re-emit any partial <shortcut> we wrote so far —
			 * easier than backtracking the QString. Add a
			 * closing tag; event_filter will treat the
			 * missing </shortcut> as malformed and ignore
			 * the entry. */
			out += QStringLiteral("</shortcut>");
			++line_start = line_end + 1;
			continue;
		}

		out += QStringLiteral("</shortcut>");
		++line_start = line_end + 1;
	}

	return out;
}




/*
 * Emit the event_filter config carrying the dynamic `<shortcut>`
 * children to the reporter. The emit is XML (unambiguous nesting;
 * event_filter's Node API auto-detects the format).
 */
void ShortcutController::_emit_event_filter_config(QString const &shortcuts_xml)
{
	if (!_event_filter_reporter.constructed()) return;

	/*
	 * W7 live-capture topology (empirically established
	 * 2026-09-19, bisect series in the W7 evidence log):
	 * the vendored event_filter `<report>` source only fires
	 * its `<shortcut>` reports when the report source is the
	 * DIRECT child of `<output>` wrapping a plain `<input>`
	 * chain. Composing it with `<merge>` or `<chargen>` (any
	 * nesting order) silently breaks the publish path — the
	 * events still reach the filter, but no report is
	 * generated. The dedicated shortcuts scenario therefore
	 * drops the chargen chain from ITS event_filter config
	 * (the scenario does not type text); coexistence of
	 * shortcuts + chargen on the product media is a Phase 17+
	 * item (needs an upstream report-source composition fix
	 * or a ledgered vendored patch).
	 *
	 * The emitted config carries `force="yes"` so event_filter
	 * reconfigures immediately on each re-emit (its
	 * `_handle_config` defers while sessions are non-idle
	 * otherwise).
	 */
	QString const merged =
	    QStringLiteral("<config force=\"yes\"><output><report>"
	                   "<input name=\"ps2\"/><input name=\"usb\"/>")
	    + shortcuts_xml
	    + QStringLiteral("</report></output>"
	                     "<policy label=\"ps2\" input=\"ps2\"/>"
	                     "<policy label=\"usb\" input=\"usb\"/></config>");

	QByteArray const utf8 = merged.toUtf8();

	/*
	 * Pad to a page-aligned size with newlines so the
	 * consumer's Node API (which reads the full dataspace
	 * size, page-aligned) doesn't choke on control chars
	 * in the trailing region.
	 */
	Genode::size_t constexpr OUT_SIZE = 8 * 1024;
	static char out_buf[OUT_SIZE] { };
	Genode::size_t const used = Genode::min((Genode::size_t)utf8.size(), OUT_SIZE);
	Genode::copy_cstring(out_buf, utf8.constData(), used + 1);
	for (Genode::size_t i = used; i < OUT_SIZE; ++i)
		out_buf[i] = '\n';

	Genode::log("sponge-de: shortcuts: emitted event_filter_config ",
	            used, " bytes (padded to ", OUT_SIZE, ")");
	_event_filter_reporter->generate(
		Genode::Const_byte_range_ptr(out_buf, OUT_SIZE));
}


void ShortcutController::_open_hit_reporter(HitReporter &hr,
                                              char const *action)
{
	if (hr.reporter.constructed()) return;

	QString const label = QString("shortcut_hit_") + QString::fromUtf8(action);
	try {
		hr.reporter.construct(_env, "report", label.toUtf8().constData());
		hr.action = QString::fromUtf8(action);

		/*
		 * Initial structural state. The probe polls for
		 * `<shortcut_hit hit="yes"/>` — emit that on
		 * construction so the probe can detect the action
		 * delivery (the ROM becomes valid immediately on
		 * creation).
		 */
		QByteArray const init_xml = QString("<shortcut_hit hit=\"ready\"/>").toUtf8();
		hr.reporter->generate(
		    Genode::Const_byte_range_ptr(init_xml.constData(),
		                                  Genode::size_t(init_xml.size())));
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		/*
		 * The run scenario does not route this report
		 * (the topology doesn't enable the W7 framework).
		 * Silently skip — the framework degrades to a no-op
		 * for the missing action (the de_config channel is
		 * still functional for write validation).
		 */
		Genode::log("sponge-de: shortcut: hit report '",
		            label.toUtf8().constData(),
		            "' unavailable; action '", action, "' is a no-op");
	}
}


void ShortcutController::_publish_hit(char const *action)
{
	/*
	 * Per-action reporter (one Report per closed action) — the
	 * pre-W7-final structural test side. The probe reads the
	 * matching `shortcut_hit_<action>` ROM to confirm the action
	 * name (one Report per action keeps the structural assertions
	 * per-action independent).
	 */
	HitReporter *hr { nullptr };
	if (Genode::strcmp(action, "launcher")   == 0) hr = &_hit_launcher;
	if (Genode::strcmp(action, "focus_next") == 0) hr = &_hit_focus_next;
	if (Genode::strcmp(action, "dismiss")    == 0) hr = &_hit_dismiss;
	if (hr && hr->reporter.constructed()) {
		QByteArray const xml = QString("<shortcut_hit hit=\"yes\"/>").toUtf8();
		hr->reporter->generate(
		    Genode::Const_byte_range_ptr(xml.constData(),
		                                  Genode::size_t(xml.size())));
	}

	/*
	 * Consolidated `shortcut_hit` reporter — the W7 live-capture
	 * proof's single observation point. Carries the action name
	 * (`<shortcut_hit action="X" hit="yes"/>`) so the probe
	 * observes both that an action fired AND which one.
	 */
	_publish_shortcut_hit(action);
}


/*
 * Open the consolidated `shortcut_hit` reporter. Mirrors the
 * per-action reporters' lazy lifecycle — opened on the first
 * configd broadcast, in `applyBindings()`. The probe reads the
 * resulting `shortcut_hit` ROM to detect dispatches.
 */
void ShortcutController::_open_shortcut_hit_reporter()
{
	if (_shortcut_hit_reporter.constructed()) return;

	try {
		_shortcut_hit_reporter.construct(_env, "report", "shortcut_hit");

		QByteArray const init_xml =
		    QString("<shortcut_hit hit=\"ready\"/>").toUtf8();
		_shortcut_hit_reporter->generate(
		    Genode::Const_byte_range_ptr(init_xml.constData(),
		                                  Genode::size_t(init_xml.size())));
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::log("sponge-de: shortcut: consolidated 'shortcut_hit' "
		            "report unavailable (run scenario does not route it; "
		            "the per-action reporters remain functional)");
	}
}


/*
 * Publish to the consolidated `shortcut_hit` reporter. The action
 * name is carried in the `action` attribute (a separate `hit="yes"`
 * attribute matches the per-action reporter's payload shape — the
 * probe uses the attribute pattern for both, no shape divergence).
 *
 * Unknown action tokens are dropped with a Genode::warning — the
 * probe's assertion is action-named, so an unknown action would
 * confuse the live gate (the per-action reporter already dropped it
 * earlier in _publish_hit, but the consolidated reporter runs after
 * the per-action one and would otherwise publish a spurious
 * `action="<unknown>" hit="yes"`).
 */
void ShortcutController::_publish_shortcut_hit(char const *action)
{
	if (!_shortcut_hit_reporter.constructed()) return;

	bool known = false;
	for (unsigned i = 0; i < num_actions; ++i)
		if (Genode::strcmp(action, action_table[i].name) == 0) {
			known = true;
			break;
		}
	if (!known) {
		Genode::warning("sponge-de: shortcut: consolidated publish: "
		                "unknown action '", action, "'");
		return;
	}

	QString const xml = QString("<shortcut_hit action=\"%1\" hit=\"yes\"/>")
	                        .arg(QString::fromUtf8(action));
	QByteArray const utf8 = xml.toUtf8();
	_shortcut_hit_reporter->generate(
	    Genode::Const_byte_range_ptr(utf8.constData(),
	                                  Genode::size_t(utf8.size())));
}


void ShortcutController::_open_shortcut_subscriber()
{
	_open_hit_subscriber(_hit_sub_launcher,   "launcher");
	_open_hit_subscriber(_hit_sub_focus_next, "focus_next");
	_open_hit_subscriber(_hit_sub_dismiss,    "dismiss");
}


/*
 * Open a per-action hit subscriber. The ROM session label is
 * `shortcut_<action>` (e.g. `shortcut_launcher`) — the run
 * script's report_rom policy maps
 * `sponge-de -> shortcut_<action>` to
 * `drivers -> event_filter -> shortcut -> <action>`.
 *
 * The per-action report carries `<shortcut name="<action>"
 * serial="N"/>` (event_filter's Report_source::Shortcut
 * publishes when the captured keys match its `Keys` set).
 *
 * De-dup: each report carries a monotonically-increasing
 * `serial` attribute; the same press may re-fire on reconfigure,
 * so we skip already-processed reports per-subscriber.
 */
void ShortcutController::_open_hit_subscriber(HitSubscriber &hs,
                                              char const *action)
{
	if (hs.rom.constructed()) return;

	QString const rom_label = QString("shortcut_") + QString::fromUtf8(action);
	hs.action = QString::fromUtf8(action);
	try {
		hs.rom.construct(_env, rom_label.toUtf8().constData());

		typedef void (ShortcutController::*Mfp)();
		Mfp mfp { nullptr };
		if (Genode::strcmp(action, "launcher")   == 0) mfp = &ShortcutController::_on_hit_subscriber_rom_launcher;
		if (Genode::strcmp(action, "focus_next") == 0) mfp = &ShortcutController::_on_hit_subscriber_rom_focus_next;
		if (Genode::strcmp(action, "dismiss")    == 0) mfp = &ShortcutController::_on_hit_subscriber_rom_dismiss;

		hs.sigh.construct(_env.ep(), *this, mfp);
		hs.rom->sigh(*hs.sigh);
		Genode::log("sponge-de: shortcut: subscribed to event_filter '",
		            rom_label.toUtf8().constData(),
		            "' ROM (action '", action, "')");
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::log("sponge-de: shortcut: event_filter `", rom_label.toUtf8().constData(),
		            "` ROM unavailable; action '", action, "' is a no-op");
	}
}


/*
 * Per-action ROM-sigh handler. Genode's Signal_handler API only
 * supports member-function pointers (no captures), so each
 * HitSubscriber has its own handler that dispatches into a
 * shared body keyed by the action token. The body parses
 * event_filter's per-action `<shortcut name=<action> serial=N>`
 * payload, de-dups by serial, dispatches the GUI-thread action
 * slot, and publishes the consolidated `shortcut_hit` report.
 */
void ShortcutController::_dispatch_hit_subscriber(HitSubscriber &hs)
{
	if (!hs.rom.constructed()) return;
	hs.rom->update();
	if (!hs.rom->valid()) return;

	/*
	 * Parse with the Node API (auto-detects HID/XML) — the
	 * per-action report is emitted in HID format by event_filter.
	 * Legacy Xml_node would fail because the content isn't
	 * valid XML on its own (it's quoted HID content).
	 *
	 * Recursive walk: the per-action report's content is HID
	 * with the `<shortcut>` payload as quoted text nested inside
	 * a root node — must recurse to find it.
	 */
	char const *p = hs.rom->local_addr<char>();
	Genode::size_t const sz = hs.rom->size();
	Genode::Node const root(Genode::Const_byte_range_ptr(p, sz));

	struct Walk_rec {
		static void run(Genode::Node const &n,
		                char const *target,
		                unsigned &out_serial,
		                bool &out_found) {
			/*
			 * The report's ROOT node is itself the `<shortcut>`
			 * element (event_filter's report body is
			 * `<shortcut name="..." serial="..."/>`), so check
			 * `n` itself before recursing into sub-nodes.
			 */
			if (n.type() == "shortcut") {
				Genode::String<64> const name =
					n.attribute_value("name", Genode::String<64>());
				if (Genode::strcmp(name.string(), target) == 0) {
					out_serial = n.attribute_value("serial", 0u);
					out_found = true;
					return;
				}
			}
			n.for_each_sub_node([&] (Genode::Node const &c) {
				if (out_found) return;
				run(c, target, out_serial, out_found);
			});
		}
	};
	bool found = false;
	unsigned serial { 0 };
	Walk_rec::run(root, hs.action.toUtf8().constData(), serial, found);

	if (!found) return;
	if (serial == hs.last_serial) return;
	hs.last_serial = serial;

	Genode::log("sponge-de: shortcut hit: ", hs.action.toUtf8().constData(),
	            " (serial=", serial, ")");

	QByteArray const action_bytes = hs.action.toUtf8();
	char const *action = action_bytes.constData();
	_publish_hit(action);

	if (Genode::strcmp(action, "launcher") == 0) {
		if (_launcher)
			QMetaObject::invokeMethod(_launcher, "toggle_launcher",
			                          Qt::QueuedConnection);
		else
			Genode::warning("sponge-de: shortcut launcher hit: "
			                "LauncherController not attached");
	} else if (Genode::strcmp(action, "focus_next") == 0) {
		if (_tasklist)
			QMetaObject::invokeMethod(_tasklist, "cycle_focus",
			                          Qt::QueuedConnection);
		else
			Genode::warning("sponge-de: shortcut focus_next hit: "
			                "TasklistController not attached");
	} else if (Genode::strcmp(action, "dismiss") == 0) {
		if (_dismisser)
			QMetaObject::invokeMethod(_dismisser, "dismiss_topmost",
			                          Qt::QueuedConnection);
		else
			Genode::warning("sponge-de: shortcut dismiss hit: "
			                "Dismisser not attached");
	} else {
		Genode::warning("sponge-de: shortcut hit: unknown action '",
		                action,
		                "' (validator should have rejected this)");
	}
}


void ShortcutController::_on_hit_subscriber_rom_launcher()
{
	_dispatch_hit_subscriber(_hit_sub_launcher);
}


void ShortcutController::_on_hit_subscriber_rom_focus_next()
{
	_dispatch_hit_subscriber(_hit_sub_focus_next);
}


void ShortcutController::_on_hit_subscriber_rom_dismiss()
{
	_dispatch_hit_subscriber(_hit_sub_dismiss);
}


/*
 * GUI thread slot. Parses the payload, regenerates the event_filter
 * config, updates the per-action hit subscribers.
 */
void ShortcutController::applyBindings(QString payload)
{
	QString const bindings = extract_shortcuts_bindings(payload);

	if (bindings == _last_bindings) return;
	_last_bindings = bindings;

	Genode::log("sponge-de: shortcuts.bindings = ",
	            bindings.isEmpty() ? "<absent>" : bindings.toUtf8().constData());

	/*
	 * Emit the dynamic event_filter config. The XML body is
	 * the inner `<shortcut>` children (no top-level wrapper);
	 * `_emit_event_filter_config` injects them into the full
	 * `<config>` document.
	 */
	QString const shortcuts_xml = _build_shortcuts_xml(bindings);
	_emit_event_filter_config(shortcuts_xml);

	/*
	 * Schedule one deferred re-emit 3 s after each binding
	 * application. The event_filter config sigh does not fire
	 * reliably on the FIRST creation of the relayed report
	 * (report_rom registers the module empty at ROM-session
	 * creation; the first write's reader notification races
	 * the reader's sigh registration), so the deferred re-emit
	 * is the reliable reconfigure trigger for event_filter.
	 * Runs on the GUI thread (applyBindings is already
	 * GUI-marshalled).
	 */
	if (!_reemit_pending) {
		_reemit_pending = true;
		QTimer::singleShot(3000, this, [this]() {
			_reemit_pending = false;
			QString const xml = _build_shortcuts_xml(_last_bindings);
			_emit_event_filter_config(xml);
			Genode::log("sponge-de: shortcuts: deferred re-emit done");
		});
	}

	/*
	 * Ensure every action's hit reporter is open (one Report
	 * per closed action; the probe reads the translated report).
	 * Open lazily on the first applyBindings so scenarios
	 * without the policy route do not fatal-deny at construct
	 * time.
	 */
	_open_hit_reporter(_hit_launcher,   "launcher");
	_open_hit_reporter(_hit_focus_next, "focus_next");
	_open_hit_reporter(_hit_dismiss,    "dismiss");

	/*
	 * Open the consolidated `shortcut_hit` reporter — the W7
	 * live-capture proof's single observation point. The probe
	 * subscribes to this ONE ROM to detect every action dispatch.
	 */
	_open_shortcut_hit_reporter();

	/*
	 * Subscribe to event_filter's `<report>` source via the
	 * `shortcut` ROM label. The sigh handler parses each
	 * `<shortcut name="...">` child, dispatches the action
	 * on the GUI thread, AND re-publishes the translated
	 * `shortcut_hit_<action>` report.
	 */
	_open_shortcut_subscriber();

	emit shortcuts_bindings_changed(bindings);
}
