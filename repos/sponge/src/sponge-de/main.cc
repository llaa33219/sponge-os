/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Entry point for the sponge-de component.
 *
 * Sponge DE is Sponge OS's desktop environment — a long-lived Genode
 * component that boots with the system and stays alive until shutdown.
 *
 * Qt on Genode requires the libc component model (Libc::Component::construct)
 * rather than the plain Component::construct used by non-Qt components like
 * vct. The QPA plugin that bridges Qt to Genode's Gui session is loaded by
 * qpa_init(), which must run before QApplication is constructed.
 *
 * Phase 5c: panel + launcher. The launcher button opens an app menu
 * populated from sponge_pkgd's rich `list` result (only packages that
 * declare a launcher category). The LauncherController polls pkgd on a
 * QTimer (same non-blocking pattern as ThemeController) and publishes a
 * `launcher` report for headless verification. Click-to-launch itself
 * is intentionally deferred (see launcher_menu_view.cc).
 *
 * Phase 14 W4: notifications. The NotifyPoster owns the "notif_request"
 * Report session and is the in-sponge-de writer of the notification
 * bus. NotifierController subscribes to the daemon's "notifications"
 * ROM and pushes the active list to NotifierWidget (the themed popover).
 * ThemeController / ConfigController / LauncherController each emit
 * event notifications through the poster on theme apply, config change,
 * and package install.
 */

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <libc/component.h>
#include <rom_session/connection.h>

#include <QApplication>
#include <QFont>

#include <qt6_component/qpa_init.h>

#include "config/config_controller.h"
#include "launcher/launcher_controller.h"
#include "launcher/launcher_menu_view.h"
#include "panel/notifier_controller.h"
#include "panel/notifier_widget.h"
#include "panel/notify_poster.h"
#include "panel/panel_widget.h"
#include "sponge_de_main.h"
#include "theme/theme_controller.h"
#include "theme/theme_loader.h"

using namespace Sponge::Sponge_DE;


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {

		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "sponge-de", nullptr };

		QApplication app(argc, const_cast<char **>(argv));

		/*
		 * ThemeController owns the live theme pipeline. In live mode it
		 * watches sponge_themed's "theme" report and re-styles the panel,
		 * demo window, and launcher in place when it changes; in fallback
		 * mode (no sponge_themed, e.g. run/sponge-de-test.run) it reads
		 * default.theme once. Either way, initial() is the theme used to
		 * construct the widgets below.
		 */
		ThemeController theme_ctrl(env);

		app.setFont(QFont(theme_ctrl.initial().default_font().family.string(),
		                  (int)theme_ctrl.initial().default_font().size));

		/*
		 * Phase 11 W2: ConfigController — bridges sponge_configd's
		 * `config` ROM to Qt signals for panel.height,
		 * panel.visible_widgets, clock.format, launcher.sort_by.
		 * Activation is gated by <config source="configd"/> in the
		 * component config (mirrors <theme source="themed"/>); in
		 * fallback mode the controller never opens a ROM session
		 * and never emits a signal (sponge-de boots unchanged).
		 *
		 * Constructed BEFORE the panel so the very first applyConfig()
		 * has a target to fan out to; the panel's apply* slots are
		 * private and connected by attach_panel() below.
		 */
		ConfigController config_ctrl(env);

		/* Notification daemon bridge (Phase 14 W4). */
		NotifyPoster       notify_poster(env);
		NotifierController notifier_ctrl(env);

		/*
		 * Launcher data path. Constructed before the panel because the
		 * panel's launcher button shows/hides its popup. The view is
		 * constructed with the initial theme so the popup renders
		 * correctly even before the first pkgd poll completes.
		 */
		LauncherController launcher_ctrl(env);
		LauncherMenuView   launcher_view(launcher_ctrl, theme_ctrl.initial());
		launcher_ctrl.attach_view(&launcher_view);

		NotifierWidget notifier_popover(theme_ctrl.initial());
		notifier_ctrl.attach_widget(&notifier_popover);

		PanelWidget panel(theme_ctrl.initial());
		panel.show();
		panel.set_launcher_view(&launcher_view);
		theme_ctrl.attach_panel(&panel);
		config_ctrl.attach_panel(&panel);
		config_ctrl.attach_launcher(&launcher_view);
		Genode::log("sponge-de: panel shown");

		Main main_window(env, theme_ctrl.initial());
		main_window.show();
		theme_ctrl.attach_main(&main_window);
		Genode::log("sponge-de: window shown");

		theme_ctrl.attach_launcher(&launcher_view);

		/* Wire the notify poster to every event-emitting controller. */
		theme_ctrl.attach_notify_poster(&notify_poster);
		config_ctrl.attach_notify_poster(&notify_poster);
		launcher_ctrl.attach_notify_poster(&notify_poster);

		/* Marker matched by run/sponge-de.run for automated verification. */
		Genode::log("sponge-de: panel and window shown");

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
