/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * ConfigController — live configd bridge for Sponge DE (Phase 11 W2).
 *
 * Watches sponge_configd's broadcast via a dedicated "configd"
 * ROM session (relayed by report_rom from sponge_configd's `config`
 * report) and re-publishes the four Panel/Launcher/Clock keys as Qt
 * signals that the panel and launcher consume on the GUI thread.
 * The controller is the SECOND in-sponge-de configd-broadcast
 * consumer (the first is `ThemeController`, which reads the `theme`
 * ROM — a DIFFERENT report_rom slot). Today `sponge_de_main.cc::Main`
 * does NOT directly consume the `config` broadcast; the bridge exists
 * so that a configd `set` for one of the four keys reaches the panel
 * + launcher without a restart.
 *
 * ROM LABEL:
 *
 *   The watched ROM session is labeled "configd" (NOT "config"). The
 *   "config" label is reserved by Genode init for the child's inline
 *   `<config>` block (delivered as an Inline_config_rom_service at
 *   genode/repos/os/src/lib/sandbox/child.cc:510-524); routing the
 *   configd broadcast to the "config" label would shadow the inline
 *   config that carries the activation gate. Using a distinct
 *   "configd" label sidesteps the conflict. The run script's
 *   report_rom policy is:
 *
 *     policy | label: sponge-de -> configd | report: sponge_configd -> config
 *
 *   and sponge-de's route declares:
 *
 *     service ROM | label: configd | + child report_rom
 *
 * ACTIVE KEYS (the four panel/launcher/clock keys registered by
 * sponge_configd in Phase 11 W1):
 *
 *   panel.height            uint    [16..128]   default "28"
 *   panel.visible_widgets   enum-list {clock,launcher}  default "clock,launcher"
 *   clock.format            string  (Qt QDateTime format) default "HH:mm"
 *   launcher.sort_by        enum    {manual,alpha}     default "alpha"
 *
 * THREAD MODEL (the critical invariant — failure-point 2 enforcement):
 *
 *   The ROM signal handler runs on the Genode entrypoint dispatcher
 *   thread, NOT the Qt event-loop thread blocked in QApplication::exec.
 *   Touching any QWidget or calling QApplication APIs from the signal
 *   handler is undefined behavior (Qt widgets are not thread-safe).
 *
 *   So the handler only READS the ROM (a plain shared dataspace) and
 *   copies the four key/value pairs into QStrings, then marshals the
 *   work to the GUI thread with QMetaObject::invokeMethod(...)
 *   (Qt::QueuedConnection). The actual signal emission happens in
 *   applyConfig() on the GUI thread. The actual signal emission
 *   happens in applyConfig() on the GUI thread.
 *
 * RELIABILITY NOTE (QTimer poll — mirrors ThemeController):
 *
 *   In the Qt/libc component model, arbitrary ROM signals are not
 *   guaranteed to be dispatched while QApplication::exec is blocked
 *   (the QPA event loop drives only the sessions it explicitly waits
 *   on). A short QTimer on the GUI thread re-checks the ROM and
 *   applies any change directly, so the live reload is deterministic.
 *   Both the signal (push) and the timer (pull) funnel into
 *   applyConfig(), which de-duplicates by per-key value, so they never
 *   double-apply and never cause re-entrant restyle loops.
 *
 * FALLBACK:
 *
 *   When no sponge_configd is present (e.g. run/sponge-de-test.run),
 *   opening the "configd" ROM fails (Rom_connection_failed) because no
 *   report_rom policy routes it. The controller then degrades
 *   gracefully: it does nothing — sponge-de boots exactly as before,
 *   with no configd ROM opened, no signals emitted, no errors raised.
 *   This is the mirror of `<theme source="themed"/>`; the activation
 *   gate is `<config source="configd"/>` in the component config. Both
 *   gates default to OFF, so the live configd path is an upgrade that
 *   opt-in scenarios enable explicitly.
 *
 * The four signal signatures match the exact shape Qt's auto-moc
 * generates so that QObject::connect() can bind them with the new
 * pointer-to-member-function syntax (Qt5+) at run time. The panel and
 * launcher are the only direct consumers.
 *
 * No Q_OBJECT macro is NOT an option here — signals require the moc
 * pass, so Q_OBJECT IS present (a deliberate change from
 * PanelWidget/LauncherMenuView, which use only functor/lambda
 * connections).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>

class QTimer;

namespace Sponge::Sponge_DE {

class PanelWidget;
class LauncherMenuView;

/*
 * Read <config source="configd"/> from the component config. Mirrors
 * ThemeController's <theme source="themed"/> gate (theme_controller.h)
 * and LauncherController's <launcher source="pkgd"/> gate
 * (launcher_controller.h). The same HID+XML dual-parser strategy is
 * used so that scenarios which deliver the config in either format
 * (the sandbox's inline-config ROM service emits HID by default since
 * it became the framework default) are recognized correctly.
 *
 * Returns true ONLY when explicitly opted in. Absent (or any other
 * value) leaves the controller in fallback mode (no ROM opened, no
 * signals emitted). Scenarios that do not wire the `config` report
 * route boot unchanged.
 */
bool config_asks_for_configd(Genode::Env &env);


class ConfigController : public QObject
{
	Q_OBJECT

	public:

		/*
		 * Constructed in sponge_de_main.cc::Main, BEFORE the panel.
		 * The constructor decides live-mode-vs-fallback based on the
		 * component config gate; in fallback mode no ROM session is
		 * opened at all, and `applyConfig()` is a no-op forever.
		 */
		explicit ConfigController(Genode::Env &env, QObject *parent = nullptr);

		/*
		 * The panel and launcher must be attached AFTER they are
		 * constructed but BEFORE the first configd broadcast arrives
		 * (so the very first applyConfig() can fan out to them). The
		 * wiring of the QObject signals is the controller's
		 * responsibility; the widgets just need the pointer.
		 */
		void attach_panel(PanelWidget *panel);
		void attach_launcher(LauncherMenuView *launcher);

	private slots:

		/*
		 * GUI thread. Called via Qt::QueuedConnection from the ROM
		 * signal handler AND directly by the poll timer. Parses the
		 * four key/value pairs (utf-8) out of the marshalled payload
		 * and, for each that changed since the last broadcast,
		 * emits the matching signal. De-duplicates by per-key value
		 * so identical broadcasts are no-ops (failure-point 2: no
		 * re-entrant restyle loops).
		 */
		void applyConfig(QString payload);

	private:

		Genode::Env &_env;

		/*
		 * ROM + signal-handler pair, Constructible so the
		 * fallback (no <config source="configd"/>) path never opens
		 * a session — same pattern ThemeController uses for
		 * <theme source="themed"/>.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace>            _config_rom { };
		Genode::Constructible<Genode::Signal_handler<ConfigController>>  _sigh       { };

		QTimer  *_poll_timer { nullptr };

		PanelWidget     *_panel    { nullptr };
		LauncherMenuView *_launcher { nullptr };

		/*
		 * Last-applied values (one per key). Used by applyConfig to
		 * suppress no-op re-applications — identical bytes do not
		 * re-emit the signal.
		 */
		QString _last_panel_height;
		QString _last_panel_visible_widgets;
		QString _last_clock_format;
		QString _last_launcher_sort_by;

		bool _read_payload(QString &payload);
		void _on_rom();    /* entrypoint thread: read ROM, marshal */
		void _poll();      /* GUI thread: pull + apply */

		/*
		 * Returns true if the four values in `payload` differ from
		 * the corresponding `_last_*` field; on a true return, the
		 * `_last_*` fields are updated AND the matching
		 * panel_height_changed / panel_visible_widgets_changed /
		 * clock_format_changed / launcher_sort_by_changed signal is
		 * emitted exactly once. The matching `applyConfig()` slot
		 * (panel/launcher) reads the value from the signal arg, so
		 * we do NOT call any QWidget method directly here.
		 */
		void _emit_changed(QString const &panel_height,
		                   QString const &panel_visible_widgets,
		                   QString const &clock_format,
		                   QString const &launcher_sort_by);

	signals:

		/* panel.height (uint [16..128], default 28). The panel's
		 * applyHeight slot calls setFixedSize + resize. */
		void panel_height_changed(unsigned h);

		/* panel.visible_widgets (enum-list, default "clock,launcher").
		 * The panel parses on ',' and hides the launcher toggle or
		 * clock label whose token is absent. */
		void panel_visible_widgets_changed(QString list);

		/* clock.format (string, default "HH:mm"). The panel's
		 * applyClockFormat slot runs the format with try/catch
		 * around QDateTime::toString, falling back to "HH:mm" with a
		 * single Genode::warning per bad value (failure-point 9). */
		void clock_format_changed(QString format);

		/* launcher.sort_by (enum {manual,alpha}, default "alpha").
		 * The launcher's applySortBy slot re-runs repopulate(). */
		void launcher_sort_by_changed(QString sort);
};

}  /* namespace Sponge::Sponge_DE */