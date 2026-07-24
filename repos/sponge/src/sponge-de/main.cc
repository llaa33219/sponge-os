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
		 * Load the system theme (best-effort). A missing or malformed
		 * theme never blocks the desktop: the Theme struct carries the
		 * compiled-in defaults, so the panel and windows always come up
		 * (automation is the default), while the ROM module remains the
		 * inspectable, replaceable customization point (control).
		 */
		Theme::Theme theme;
		try {
			Genode::Attached_rom_dataspace rom(env, "default.theme");
			if (rom.valid()) {
				Theme::ThemeLoader loader;
				bool const clean = loader.load(rom.local_addr<char const>(),
				                               rom.size(), theme);
				Genode::log("sponge-de: theme loaded: default.theme",
				            clean ? "" : " (with warnings, see above)");
			} else {
				Genode::warning("sponge-de: default.theme invalid, using built-in defaults");
			}
		} catch (Genode::Rom_connection::Rom_connection_failed) {
			Genode::warning("sponge-de: default.theme ROM unavailable, using built-in defaults");
		}

		app.setFont(QFont(theme.default_font().family.string(),
		                  (int)theme.default_font().size));

		PanelWidget panel(theme);
		panel.show();
		Genode::log("sponge-de: panel shown");

		Main main_window(env, theme);
		main_window.show();
		Genode::log("sponge-de: window shown");

		/* Marker matched by run/sponge-de.run for automated verification. */
		Genode::log("sponge-de: panel and window shown");

		Genode::warning("not implemented: notifications (Phase 5)");

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
