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
 * Phase 3 scope: a themed panel docked to the screen edge plus one demo
 * window, both drawn by Qt on top of nitpicker. The theme is loaded from
 * the "default.theme" ROM module (system layer, docs/10-theme-format.md
 * §7); launcher backend and notifications remain Phase 5 stubs.
 */

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <libc/component.h>
#include <rom_session/connection.h>

#include <QApplication>
#include <QFont>

#include <qt6_component/qpa_init.h>

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
		 * watches sponge_themed's "theme" report and re-styles the panel
		 * and window in place when it changes; in fallback mode (no
		 * sponge_themed, e.g. run/sponge-de-test.run) it reads
		 * default.theme once. Either way, initial() is the theme used to
		 * construct the widgets below.
		 */
		ThemeController theme_ctrl(env);

		app.setFont(QFont(theme_ctrl.initial().default_font().family.string(),
		                  (int)theme_ctrl.initial().default_font().size));

		PanelWidget panel(theme_ctrl.initial());
		panel.show();
		theme_ctrl.attach_panel(&panel);
		Genode::log("sponge-de: panel shown");

		Main main_window(env, theme_ctrl.initial());
		main_window.show();
		theme_ctrl.attach_main(&main_window);
		Genode::log("sponge-de: window shown");

		/* Marker matched by run/sponge-de.run for automated verification. */
		Genode::log("sponge-de: panel and window shown");

		Genode::warning("not implemented: notifications (Phase 5)");

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
