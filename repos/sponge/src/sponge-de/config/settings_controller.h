/* SPDX-License-Identifier: Apache-2.0
 *
 * SettingsController — write-side bridge from Sponge DE to sponge_configd
 * (Phase 16 W4, D16.1 / D16.9).
 *
 * READ SIDE (broadcast consumer) lives in `ConfigController`
 * (config/config_controller.{h,cc}, Phase 11 W2). It is the
 * already-existing controller that re-publishes the four Phase 11
 * panel/launcher/clock keys as Qt signals that the panel and launcher
 * consume on the GUI thread. This controller is the WRITE side that
 * SettingsDialog needs to push user changes back into configd.
 *
 * === Channel contract (D16.1) ===
 *
 *   SettingsController --[Report "de_config_request"]-->
 *     report_rom --> sponge_configd --[ROM "de_config_request"]-->
 *   sponge_configd --[Report "config_result"]-->
 *     report_rom --> SettingsController --[ROM "de_config_result"]-->
 *
 *   Full labels include the child-name prefix:
 *
 *     policy | label: sponge-de   -> de_config_request | ...
 *     policy | label: sponge-de   -> de_config_result  | ...
 *
 * The labels are DEDICATED to the DE-side writer (mirrors the
 * Phase 14 launcher_request precedent). vct keeps its own
 * `vct -> config_request` / `vct -> config_result` labels — the
 * two writers each send full `<request op="..." .../>` payloads
 * and each receive the broadcast on its own `config_result` ROM.
 * report_rom is single-writer per label (`AGENTS.md` §1.2), so
 * the long-lived sponge-de cannot share vct's `config_request`
 * label and would have to either be demoted to a polling read-
 * only consumer (today's `ConfigController`) or get its own
 * dedicated label. Phase 16 W4 path: own dedicated label.
 *
 * === Validator parity (D16.9) ===
 *
 * SettingsController uses sponge-de's existing shared backend
 * client `Sponge::Backend::ReportRomClient`
 * (`lib/src/sponge_backend_client/`,
 * `<sponge/backend_client.h>`) with labels `de_config_request` /
 * `de_config_result`. The client mirrors vct's
 * `repos/sponge/src/vct/commands.cc:1118,1139,1417,1522`
 * instantiations exactly — same XML schema (`<request op="..."
 * key="..." value="..."/>`), same error matching
 * (`<result status="ok|error" .../>`). Every write goes through
 * the SAME closed registry in sponge_configd, so an unknown /
 * malformed key returns the same structured error to both writers.
 * Phase 16 F15 (validator parity) is enforced for free: the
 * configd backend answers the same way regardless of which label
 * the request came in on.
 *
 * === Thread model (Phase 11 risk #2) ===
 *
 * SettingsController is constructed on the GUI thread (in
 * sponge_de_main.cc's `Libc::Component::construct` lambda). The
 * shared `Sponge::Backend::ReportRomClient` client (`config_set`
 * in backend_client.cc:146-166) blocks the calling thread for up
 * to ~6s — fine for short-lived vct but WOULD block the long-
 * lived GUI thread if called too often. Therefore the dialog
 * instead emits a Qt signal carrying the (key, value) pair,
 * `request_set(key, value)` is a Qt slot (default connection is
 * direct when the caller is on the GUI thread), and the call
 * itself uses the shared client SYNCHRONOUSLY from within the
 * Apply click handler — bounded by the client's poll loop (~6s)
 * and ACCEPTABLE on the GUI thread for a single user-initiated
 * Apply click. The dialog re-emits a single write per click
 * (each control's Apply is its own click; "Apply all" is not in
 * W4 scope per the task-3 isolation).
 *
 * This matches the vct design AT A DIFFERENT SCALE: vct is
 * short-lived (entire process), SettingsController is long-lived
 * (sponge-de process) but each WRITE is still synchronous (~6s
 * worst case). The dialog's Apply buttons commit one write per
 * click (no batching), so each user interaction has bounded
 * latency (sub-50 ms when configd is healthy).
 *
 * === Failure surfacing (D16.3 + AGENTS.md §1.4) ===
 *
 * Every write's result is parsed; on `status="error"`, the
 * structured error text is forwarded to the caller via the
 * `request_failed(key, error)` signal. The dialog shows the
 * message in a `QMessageBox` — never a silent drop. Phase 16
 * F2/F3 (no silent failures in the DE-write path) is enforced.
 *
 * === open_settings_dialog() (D16.3 + W5 handler) ===
 *
 * Lazy-loads the QDialog on first *Settings* click (from the
 * panel context menu wired in W5, or a follow-up right-click
 * chord). The dialog is cached after first show (the editor's
 * tab state is preserved across invocations). The lazy-load
 * means the settings dialog has no construction cost in
 * scenarios that never open it (the bake / stable probes,
 * `run/sponge-de-sel4-interactive.run`, ...).
 *
 * The activation gate is `<de_config source="controller"/>` in
 * the component config (mirrors `<config source="configd"/>`
 * for the read-side ConfigController). In fallback mode (no
 * gate), the controller still constructs — it just never opens
 * the report_rom Report/ROM sessions (the lazy-construct
 * pattern, same as LauncherController's launcher_request).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>

#include <sponge/backend_client.h>

class QDialog;
class QTimer;

namespace Sponge::Sponge_DE {

/*
 * Read <de_config source="controller"/> from the component config.
 * Mirrors config_asks_for_configd (config_controller.cc:76) and the
 * <theme source="themed"/> / <launcher source="pkgd"/> gates. Returns
 * true ONLY when explicitly opted in. Absent (or any other value)
 * leaves the controller in fallback mode (no channels opened, no
 * writes available; the dialog still lazy-loads but shows an error
 * if invoked). Scenarios that do not wire the de_config_request
 * report_rom route boot unchanged.
 */
bool de_config_asks_for_controller(Genode::Env &env);


class SettingsController : public QObject
{
	Q_OBJECT

	public:

		/*
		 * Constructed in sponge_de_main.cc::Main's `Libc::Component
		 * ::construct`, BEFORE the panel. The constructor decides
		 * write-mode-vs-fallback based on the component config
		 * gate; in fallback mode no Report/ROM sessions are opened
		 * (settings dialog still lazy-loads, but writing fails with
		 * a Genode::warning).
		 */
		explicit SettingsController(Genode::Env &env, QObject *parent = nullptr);

		~SettingsController() override;

		/*
		 * Lazy-load the QDialog and `show()` (or, if already loaded,
		 * just raise + activate the cached instance). The dialog
		 * owns all GUI-thread work; the controller only mediates
		 * the per-tab writes through the de_config_request channel.
		 *
		 * The `QDialog` type is forward-declared; this header pulls
		 * no QtWidgets API surface. settings_controller.cc includes
		 * settings_dialog.h on first call.
		 */
		void open_settings_dialog();

		/*
		 * Phase 16 W7 (U16.4 / D16.5) — dismiss support for the
		 * `dismiss` keyboard shortcut (default Escape). Closes the
		 * settings dialog if one is currently open. No-op when the
		 * dialog is not open. The Dismisser calls this from its
		 * priority list (after launcher; before panel / bg menus).
		 *
		 * GUI thread ONLY (failure-point 2 enforcement: marshalled
		 * from the ROM-signal handler by ShortcutController via
		 * QMetaObject::invokeMethod(... , Qt::QueuedConnection)).
		 */
		void close_settings_dialog();

		/*
		 * Returns true when a settings dialog is currently open.
		 * Used by the Dismesser to check the dismiss priority list
		 * without invoking close_settings_dialog first (the
		 * priority list is launcher → settings → panel → bg; the
		 * launcher check is the first step and must NOT close the
		 * dialog if the launcher is not visible).
		 *
		 * GUI thread ONLY.
		 */
		bool dialog_is_open() const { return _dialog != nullptr; }

	signals:

		/*
		 * Emitted on the GUI thread whenever a de_config_request
		 * write completes with `status="error"`. The dialog shows
		 * a `QMessageBox` with `error` text. Never a silent drop
		 * (Phase 16 F2 enforcement).
		 */
		void request_failed(QString key, QString error);

		/*
		 * Emitted on the GUI thread when a write returns
		 * `status="ok"`. The dialog bumps its per-session success
		 * counter (used by the regression scenario's JSON knob
		 * byte-match assertion).
		 */
		void request_succeeded(QString key, QString value);

	public slots:

		/*
		 * GUI thread. Called by the SettingsDialog tabs when a
		 * control changes (Apply click, Enter, list-row toggle).
		 * Synchronously issues the write through the shared
		 * backend client (bounded ~6s worst case; sub-50 ms when
		 * configd is healthy). The result is parsed inline and
		 * one of `request_failed` / `request_succeeded` is emitted.
		 */
		void request_set(QString key, QString value);

	private:

		Genode::Env &_env;

		/*
		 * The shared backend client (mirrors vct's ReportRomClient
		 * usage). Constructed lazily on first request_set() so
		 * scenarios without the de_config_request label route
		 * never see a fatal-denied.
		 */
		Genode::Constructible<Sponge::Backend::ReportRomClient> _client { };

		/*
		 * Phase 16 W7 (U16.4 / D16.5) — the cached SettingsDialog
		 * pointer. The dialog is heap-allocated on first open so
		 * the dismiss path can close it without invalidating the
		 * pointer. The dialog owns itself; the controller releases
		 * it on close (deleteLater inside the dialog's accept()/
		 * reject() handler — the dialog's exec() returns when
		 * close_settings_dialog is called).
		 */
		class SettingsDialog *_dialog { nullptr };

		/* QTimer placeholder (reserved for future async paths;
		 * today's writes are synchronous via the shared client). */
		QTimer *_poll_timer { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
