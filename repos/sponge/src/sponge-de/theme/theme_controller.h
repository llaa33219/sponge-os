/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * ThemeController — live theme reload bridge for Sponge DE (Phase 5b).
 *
 * Watches sponge_themed's "theme" ROM and re-applies the resolved theme
 * to the panel and demo window in place, without restarting the
 * component. It also publishes an "applied_theme" report carrying the
 * name of the theme currently in use, so a verification probe (and
 * Leitzentrale) can confirm the desktop actually adopted it.
 *
 * THREAD MODEL (the critical invariant):
 *
 *   The ROM signal handler runs on the Genode entrypoint dispatcher
 *   thread, NOT the Qt event-loop thread blocked in QApplication::exec.
 *   Touching any QWidget or calling QApplication APIs from the signal
 *   handler is undefined behavior (Qt widgets are not thread-safe).
 *
 *   So the handler only READS the ROM (a plain shared dataspace) and
 *   copies the theme name + raw content into QStrings, then marshals
 *   the work to the GUI thread with QMetaObject::invokeMethod(...,
 *   Qt::QueuedConnection). The actual re-style happens in applyTheme()
 *   on the GUI thread. The actual re-style happens in applyTheme()
 *   on the GUI thread.
 *
 * RELIABILITY NOTE (QTimer poll):
 *
 *   In the Qt/libc component model, arbitrary ROM signals are not
 *   guaranteed to be dispatched while QApplication::exec is blocked
 *   (the QPA event loop drives only the sessions it explicitly waits
 *   on). A short QTimer on the GUI thread re-checks the ROM and applies
 *   any change directly, so the live reload is deterministic. Both the
 *   signal (push) and the timer (pull) funnel into applyTheme(), which
 *   de-duplicates by theme name, so they never double-apply.
 *
 * FALLBACK:
 *
 *   When no sponge_themed is present (e.g. run/sponge-de-test.run),
 *   opening the "theme" ROM fails (Rom_connection_failed) because no
 *   report_rom policy routes it. The controller then degrades
 *   gracefully: it reads the "default.theme" ROM once, applies it, and
 *   publishes applied_theme="default". No live reload in that mode —
 *   the desktop comes up with the system default theme (automation is
 *   the default; the live themed path is the upgrade).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>

#include "theme_loader.h"

class QTimer;

namespace Sponge::Sponge_DE {

class PanelWidget;
class Main;
class LauncherMenuView;

class ThemeController : public QObject
{
	Q_OBJECT

	public:

		explicit ThemeController(Genode::Env &env, QObject *parent = nullptr);

		/*
		 * The theme used for the initial widget construction (before any
		 * live update arrives). In fallback mode this is the parsed
		 * default.theme; in live mode it is the compiled-in defaults
		 * until the first themed report is processed.
		 */
		Sponge_DE::Theme::Theme const &initial() const { return _theme; }

		/* Wire a widget for live re-style. The controller calls
		 * restyle() on each whenever a new theme is applied. */
		void attach_panel(PanelWidget *panel);
		void attach_main(Main *main);
		void attach_launcher(LauncherMenuView *launcher);

		/* True once at least one themed report has been applied. */
		bool live_applied() const { return _live_applied; }

	private slots:

		/* GUI thread: parse the marshalled INI and re-style. Invoked via
		 * Qt::QueuedConnection from the ROM signal handler, and directly
		 * by the poll timer. */
		void applyTheme(QString name, QString ini);

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Attached_rom_dataspace>            _theme_rom { };
		Genode::Constructible<Genode::Signal_handler<ThemeController>>   _sigh      { };

		Genode::Reporter _applied_report { _env, "applied_theme" };

		Sponge_DE::Theme::Theme _theme;       /* currently applied (GUI-thread owned) */

		PanelWidget       *_panel    { nullptr };
		Main               *_main     { nullptr };
		LauncherMenuView   *_launcher { nullptr };

		bool     _live         { false };
		bool     _live_applied { false };
		QString  _last_applied;   /* de-dup: skip re-applying the same name */
		QTimer  *_poll_timer  { nullptr };

		void _on_rom();             /* entrypoint thread: read ROM, marshal */
		bool _read_theme(QString &name, QString &ini);
		void _poll();               /* GUI thread: pull + apply */
		void _publish_applied(char const *name);
		void _fallback_default_theme();
};

}  /* namespace Sponge::Sponge_DE */
