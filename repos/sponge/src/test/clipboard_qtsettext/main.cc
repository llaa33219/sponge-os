/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * clipboard_qtsettext — Phase 14 W5 follow-on Qt-side write probe.
 *
 * The Phase 14 W5 follow-on investigation (see
 * docs/evidence/phase14-w5-qtwrite-failure.md §4) ruled out four
 * hypotheses for textedit's failing Ctrl-C → clipboard-bus write
 * (focus-domain mismatch, metadata "clipboard" attribute lost, QMP
 * Ctrl modifier delivery, observability failure on the bus). What
 * remains untested is whether QGenodeClipboard::setMimeData at all
 * fires when driven from a Qt event loop without any keyboard chain
 * in the way. An Oracle consultation settled the next decisive
 * experiment: a programmatic Qt harness that drives setText() on a
 * QTimer — no widgets, no shortcuts, no focus — and so isolates the
 * QGenodeClipboard::setMimeData path from every other Qt-side
 * variable. That is what this component is.
 *
 * Wiring (mirrors run/sponge-clipboard.run's start "sponge-de"
 * child verbatim so the upstream server treats this harness like
 * the proven PASTE-direction participant):
 *
 *   Report | label_last "clipboard" -> child clipboard
 *   ROM    | label_last "clipboard" -> child clipboard
 *   <config clipboard="yes"/>
 *
 * The harness is a direct child of init (same topology as textedit
 * in run/sponge-clipboard-qtwrite.run), so its domain is "default"
 * — the same domain the clipboard_probe's fake focus publisher
 * emits; the upstream server's write_permitted check accepts the
 * write without a "domain mismatch" warning.
 *
 * Qt on Genode requires the libc component model
 * (Libc::Component::construct) and a qpa_init() call before any
 * QGuiApplication construction. The QGenodeIntegration::clipboard()
 * plugin function lazily constructs QGenodeClipboard on the first
 * QGuiApplication::clipboard() access, reading the "config" ROM and
 * opening its Report + ROM session pair ("clipboard" label) when
 * <config clipboard="yes"/> is set; qDebug() lets us correlate the
 * marker pair (BEFORE / AFTER) with the bus observation emitted by
 * the clipboard_probe run-mate.
 *
 * Capability surface (capability-minimal per AGENTS.md §1.2):
 *   - Gui   (qpa plugins require it for the EGL/Window subsystem)
 *   - ROM at "config"                          (read-only)
 *   - Report at "clipboard_qtsettext -> clipboard"  (write)
 *   - ROM    at "clipboard_qtsettext -> clipboard"  (read)
 *
 * This component does NOT depend on Capture, Event, or a
 * framebuffer; it never renders a window — the harness exits
 * after the AFTER-marker, the run script's watchdog timer is the
 * secondary gate.
 */

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <libc/component.h>

#include <qt6_component/qpa_init.h>

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QTimer>

namespace {

/*
 * Sentinel byte string — must appear byte-for-byte in the clipboard
 * bus ROM for the probe's structural PASS gate to fire (see the
 * run script's <config qt_watch_sentinel=...> module). The string
 * deliberately avoids characters that would be problematic for any
 * future paste-direction test (no control chars, all printable ASCII,
 * all lowercase). Mirrors the Phase 14 W5 evidence log's lowercase-
 * only convention (qmp.inc's char map).
 *
 * NOTE (Phase 14 W8): the workflow scenario reuses this harness as
 * the cross-component writer (D14.2 closure). The original harness
 * exits 30 s after setText to keep the upstream clipboard server's
 * `_last_writer` alive long enough for the W5 probe's needle match.
 * The W8 workflow takes longer (install + launch packages + type
 * + paste) and needs the harness to stay alive through the paste
 * step. The W8 run scenario sets `<config keep_alive_ms="600000"/>`
 * (10 minutes) so the harness outlives the workflow. The default
 * 30000 ms is preserved for the W5 sister scenarios — the only
 * behavior change is the configurable timeout.
 */
char const *SENTINEL = "sponge qt-settext sentinel phase 14";

/*
 * The harness needs the Genode env pass-through to read its
 * "config" ROM (Genode::Attached_rom_dataspace ctor takes env).
 * Libc::Env is the bridge to Genode::Env in this component model.
 */
struct Harness
{
	Libc::Env &_env;

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	explicit Harness(Libc::Env &env) : _env(env) { }

	/*
	 * Read <config clipboard="yes"/> from the inline "config" ROM
	 * (init converts the start node's `<config>` block into a
	 * "config" ROM module). Returns true when the
	 * QGenodeClipboard bridge should be active.
	 */
	bool clipboard_opt_in() const
	{
		if (!_config_rom.valid())
			return false;

		Genode::Node const cfg = _config_rom.node();
		return cfg.attribute_value("clipboard", false);
	}

	/*
	 * Read <config keep_alive_ms="N"/> from the inline "config" ROM.
	 * The grace window after setText must keep the harness alive
	 * long enough for any reader (clipboard_probe in W5 / the W8
	 * workflow_probe) to see the bus write before the harness exits
	 * and the upstream clipboard server drops its `_last_writer`
	 * registration (the harness's Reporter session closes on exit,
	 * and the server's `read_content()` returns 0 the moment the
	 * writer unregisters — see
	 * docs/evidence/phase14-w5-qtwrite-failure.md §"timing"). The
	 * default 30 000 ms matches the original W5 behavior; the W8
	 * workflow scenario sets a longer value (10 minutes) so the
	 * paste step finds the bus still populated.
	 */
	unsigned keep_alive_ms() const
	{
		if (!_config_rom.valid())
			return 30000;

		Genode::Node const cfg = _config_rom.node();
		return cfg.attribute_value<unsigned>("keep_alive_ms", 30000);
	}

	void announce(char const *prefix) const
	{
		qDebug().noquote() << "[clipboard_qtsettext:" << prefix << "]"
		                   << "QGenodeClipboard bridge"
		                   << (clipboard_opt_in() ? "ENABLED" : "DISABLED")
		                   << "(<config clipboard=\"yes\"/> attribute honored)";
	}
};

} /* namespace */


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {

		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "clipboard_qtsettext", nullptr };

		QGuiApplication app(argc, const_cast<char **>(argv));

		Harness harness { env };

		Genode::log("clipboard_qtsettext: booting — bridge ",
		            harness.clipboard_opt_in() ? "ENABLED" : "DISABLED");

		if (!harness.clipboard_opt_in()) {
			Genode::error("clipboard_qtsettext: <config clipboard=\"yes\"/> "
			              "missing — the QGenodeClipboard bridge will no-op. "
			              "The run scenario must stage the inline `<config "
			              "clipboard=\"yes\"/>` block on this start node.");
			env.parent().exit(2);
			return;
		}

		Genode::log("clipboard_qtsettext: bridge OK, scheduling setText "
		            "after 500 ms grace period (QPA event loop idle window)");

		/*
		 * Phase A — BEFORE marker. Fires from the Qt event loop once
		 * QGuiApplication::exec() is up (clipboard bridge initialized,
		 * event dispatcher running). The qDebug() timestamp lets the
		 * probe correlate with the bus observation in the run log.
		 */
		QTimer::singleShot(500, [&] {
			harness.announce("BEFORE-setText");

			QClipboard *cb = QGuiApplication::clipboard();
			if (!cb) {
				qDebug() << "clipboard_qtsettext: QGuiApplication::clipboard()"
				         << "returned null — bridge unavailable, FAIL";
				env.parent().exit(3);
				return;
			}

			cb->setText(QString::fromUtf8(SENTINEL));

			/*
			 * Phase B — AFTER marker. Fires from the same callback
			 * AFTER the setText() call returns (the bridge's
			 * Reporter::generate has enqueued the write to the
			 * Genode clipboard server). The probe's bus observation
			 * will land at the matching marker's wall-clock time on
			 * the run log.
			 *
			 * The harness STAYS ALIVE for a generous grace window
			 * after setText — the upstream clipboard server holds
			 * `_last_writer` so the read_content() check returns 0
			 * the moment the writer unregisters (the harness's
			 * Reporter session closes on exit). The probe MUST have
			 * at least one full poll cycle to read the bytes before
			 * the harness goes away.
			 */
			harness.announce("AFTER-setText");

			unsigned const alive_ms = harness.keep_alive_ms();
			Genode::log("clipboard_qtsettext: setText() returned, keeping "
			            "Reporter session alive ", alive_ms, " ms so the probe can read "
			            "the bus before our exit cleans up the writer "
			            "(the upstream server's read_content() returns 0 "
			            "the moment the writer unregisters; the W8 workflow "
			            "scenario sets a long value via <config keep_alive_ms=\"N\"/>)");

			QTimer::singleShot(alive_ms, [alive_ms, &env] {
				Genode::log("clipboard_qtsettext: ", alive_ms, " ms grace expired, exiting "
				            "with status 0 (probe should have already PASSed)");
				env.parent().exit(0);
			});
		});

		exit(app.exec());
	});
}
