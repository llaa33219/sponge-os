/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * LauncherController — pkgd-backed app list for the launcher (Phase 5c).
 *
 * Owns the data pipeline behind LauncherMenuView. Mirrors ThemeController's
 * thread model exactly (see theme_controller.h):
 *
 *   - The pkgd "installed" broadcast ROM is read on the GUI thread by a
 *     QTimer poll, because Qt's event loop does not drive the Genode
 *     entrypoint and the GUI thread must never block on IPC.
 *   - Each poll re-reads the broadcast ROM pkgd publishes on every
 *     install/remove (additive Phase 5c). Phase 7 todo 10 adds a
 *     write path: clicking a launcher entry sends a `launch <name>`
 *     request to pkgd over a dedicated "launcher_request" channel
 *     (report_rom is single-writer per label, so the long-lived
 *     launcher cannot share vct's "request" label). The result is
 *     polled non-blocking via a short-lived QTimer so the GUI thread
 *     never blocks on IPC (AGENTS.md §3.3 rule 5: same pkgd backend
 *     as vct launch, just a distinct transport label).
 *   - When the parsed app list actually changes, the controller emits
 *     appsChanged() on the GUI thread; LauncherMenuView repopulates.
 *
 * STATE REPORT (the verifiable artifact):
 *
 *   The controller publishes a `launcher` Reporter carrying the current
 *   app list so a headless probe (run/sponge-launcher.run) can confirm
 *   the launcher actually populated from pkgd. Shape:
 *
 *     <launcher count="N">
 *       <app name="hello" category="Utilities" binary="hello"/>
 *       ...
 *     </launcher>
 *
 *   The report is regenerated whenever the list changes, with stable
 *   attribute order for deterministic diffing.
 *
 * POLL CADENCE:
 *
 *   The launcher does not need to refresh every second. A 1.5 s poll is
 *   frequent enough to surface a freshly-installed app within a human
 *   click's reaction time, and rare enough that it does not perturb Qt's
 *   event loop on slow software rendering.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

namespace Sponge::Sponge_DE {

class LauncherMenuView;

class LauncherController : public QObject
{
	Q_OBJECT

	public:

		static constexpr unsigned MAX_APPS = 32;

		struct App {
			QString name;
			QString category;
			QString binary;
			QString description;
			bool    running { false };
		};

		explicit LauncherController(Genode::Env &env,
		                            QObject *parent = nullptr);

		QVector<App> const &apps() const { return _apps; }

		/* Wire the view. The controller calls repopulate() on it
		 * whenever the parsed list changes. */
		void attach_view(LauncherMenuView *view) { _view = view; }

		bool live_list_seen() const { return _live_list_seen; }

		/*
		 * Click-to-launch (Phase 7 todo 10). Sends `launch <name>` to
		 * pkgd over the "launcher_request" channel and starts a non-
		 * blocking QTimer poll for the matching result. Safe to call
		 * from the GUI thread (the click handler): it never blocks on
		 * IPC — the Expanding_reporter write is synchronous-but-fast
		 * and the result poll is timer-driven.
		 */
		void request_launch(QString const &name);

	signals:

		/* Emitted on the GUI thread whenever the parsed app list
		 * changed. */
		void appsChanged();

	private slots:

		void poll();
		void _poll_launch_result();

	private:

		Genode::Env &_env;

		/*
		 * pkgd's installed-set broadcast ROM (relayed by report_rom).
		 * Read-only — the launcher does NOT write requests, see the
		 * file header for why. Constructed lazily: only when the
		 * component config explicitly opts in via
		 * <launcher source="pkgd"/>, so scenarios without a pkgd
		 * route (sponge-de-test.run) are not fatal-denied.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _installed_rom { };

		/*
		 * Launch request/result channel (Phase 7 todo 10). Distinct
		 * labels from vct's "request"/"result" because report_rom is
		 * single-writer per label and a long-lived launcher coexists
		 * with short-lived vct children in the same scenario. pkgd
		 * exposes "launcher_request"/"launcher_result" as a second
		 * input/output pair feeding the same _do_launch backend.
		 */
		Genode::Constructible<Genode::Expanding_reporter>     _launch_request  { };
		Genode::Constructible<Genode::Attached_rom_dataspace> _launch_result   { };

		/* Headless-verifiability: published app list. */
		Genode::Reporter _launcher_report { _env, "launcher" };

		QTimer  *_poll_timer { nullptr };
		LauncherMenuView *_view { nullptr };

		QVector<App> _apps;
		bool _live_list_seen { false };

		QString _last_result_signature;

		/* Pending launch (non-blocking result poll). */
		QString _pending_launch_name;
		unsigned _launch_poll_count { 0 };
		static constexpr unsigned LAUNCH_POLL_MAX { 60 };

		bool _read_and_parse();
		bool _try_parse(Genode::Xml_node const &root);
		void _publish_report();
		QString _signature_of(QVector<App> const &apps) const;
};

}  /* namespace Sponge::Sponge_DE */
