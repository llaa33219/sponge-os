/* SPDX-License-Identifier: Apache-2.0
 *
 * QuickMenuController — the reusable Criterion 18 quick-settings
 * bridge (docs/05-sponge-de.md §5.4, "Quick-menu change lifecycle").
 *
 * One quick-menu change runs exactly this lifecycle, cold and warm:
 *
 *   1. Ensure-start the owning first-party settings target through
 *      LauncherController::request_settings_open() — the normal
 *      (package, settings-id) pkgd path on sponge-de's existing
 *      launcher_request / launcher_result pair. `started` and
 *      `focused` are both success; every refusal becomes a bounded
 *      quick_change_failed() message. The operation itself NEVER
 *      travels through pkgd.
 *   2. Send ONE closed <quick_request op=... [id=...] [value=...]/>
 *      on the owning provider's quick-request label
 *      (panel_settings_quick_request /
 *      background_settings_quick_request). The vocabulary below is
 *      closed — there is deliberately no key/value entry point and
 *      no operation outside the provider tables
 *      (settings_providers/{panel,background}/main.cc).
 *   3. Poll the matching quick-result label
 *      (panel_settings_quick_result /
 *      background_settings_quick_result) with a non-blocking
 *      QTimer chain (never a synchronous wait on the GUI thread),
 *      bounded by QUICK_POLL_MAX ticks.
 *
 * Quick channel sessions are used only when sponge-de's config carries
 * <quick_settings source="providers"/>. Without that opt-in, the
 * controller is not attached to any menu and no quick session is
 * attempted. Within an enabled topology, channels are lazy-opened and
 * rate-limited like the launcher channels.
 *
 * Result acceptance (the stale-answer rules; the provider is the
 * sole writer of the result label but answers of abandoned or
 * superseded requests can still be sitting in the ROM):
 *
 *   - ok is accepted only with the echoed op AND value equal to the
 *     request, so a stale ok for another value can never complete
 *     this change; an ok echoing our own value is truthful to accept
 *     even byte-identical to pre-send content (the value IS applied).
 *   - error is accepted only when it is FRESH (byte-different from
 *     the pre-send content) so an abandoned request's stale refusal
 *     never lands on a newer change.
 *   - anything else keeps polling until the budget, then fails with
 *     bounded English. Nothing here can hang the GUI thread.
 *
 * Supersede model: at most one change is in flight. A newer change
 * for the SAME target during ensure-start just swaps the queued
 * operation (the idempotent start-or-focus serve both); every other
 * combination starts a fresh ensure-start whose in-flight
 * predecessor is disambiguated by (pkg, id) matching on the launcher
 * signals.
 *
 * === Settings-open API (the panel / background Settings entries) ===
 *
 * open_panel_settings() / open_background_settings() perform ONLY
 * step 1 (the ensure-start) and report a bounded outcome on
 * quick_change_ok / quick_change_failed — they NEVER emit a
 * quick_request and never touch the quick channels. They are the
 * Criterion 18 replacement for the removed universal writer's
 * dialog opener: the provider-owned settings window is the
 * destination, so "open" means "make sure the owning provider is
 * started and focused", nothing else.
 *
 * Serve-both rule: when any operation is already in flight for the
 * SAME target, that operation's lifecycle begins with the same
 * idempotent start-or-focus ensure-start, so it already performs
 * this open and reports the shared outcome — no second launcher
 * request is queued (the launcher pair carries exactly one pending
 * request; a second would supersede and fail the first).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/env.h>
#include <os/reporter.h>
#include <util/reconstructible.h>

#include <QByteArray>
#include <QObject>
#include <QString>

namespace Sponge::Sponge_DE {

class LauncherController;

class QuickMenuController : public QObject
{
	Q_OBJECT

	public:

		explicit QuickMenuController(Genode::Env &env,
		                            QObject *parent = nullptr);

		void attach_launcher_controller(LauncherController *launcher);

	public slots:

		void change_panel_height(QString const &panel_id, int px);
		void change_panel_position(QString const &panel_id,
		                           QString const &position);
		void change_panel_widgets(QString const &panel_id,
		                           QString const &widgets);
		void change_background_color(QString const &hex);
		void change_background_image(QString const &path);

		/*
		 * Settings entries (ensure-start ONLY — see the header's
		 * settings-open API section). No quick_request, no quick
		 * channel session, just the (sponge-de, panel) /
		 * (sponge-de, background) ensure-start and a bounded
		 * success / error report.
		 */
		void open_panel_settings();
		void open_background_settings();

	signals:

		void quick_change_ok(QString const &message);
		void quick_change_failed(QString const &message);

	private slots:

		void _on_settings_open_ok(QString const &pkg, QString const &id,
		                          QString const &action);
		void _on_settings_open_failed(QString const &pkg, QString const &id,
		                              QString const &reason);
		void _poll_quick_result();

	private:

		enum class Target { PANEL, BACKGROUND };
		enum class Phase  { IDLE, ENSURING, POLLING };

		struct Change
		{
			Target    target { Target::PANEL };
			QByteArray op, id, value;
			QString    what;
		};

		struct Channel
		{
			char const *request_label { nullptr };
			char const *result_label  { nullptr };
			Genode::Constructible<Genode::Expanding_reporter>     request { };
			Genode::Constructible<Genode::Attached_rom_dataspace> result  { };
			bool   failed     { false };
			qint64 attempt_ms { 0 };
		};

		Genode::Env &_env;
		LauncherController *_launcher { nullptr };

		Channel _panel_ch, _bg_ch;

		Change   _pending;
		Phase    _phase { Phase::IDLE };
		unsigned _gen { 0 };
		unsigned _poll_count { 0 };
		QByteArray _snapshot;
		bool     _open_only { false };

		Channel &_channel(Target target);
		char const *_settings_id(Target target) const;
		bool _open_channel(Channel &ch);
		void _submit(Change const &change);
		void _submit_open(Target target);
		void _emit_superseded();
		void _request_ensure();
		void _send_request();
		void _finish_ok();
		void _fail(QString const &reason);
		void _schedule_poll();
};

}  /* namespace Sponge::Sponge_DE */
