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
 * Phase 3 scope: open one Qt Widgets window on top of nitpicker. Panel,
 * launcher, notifications, and theme application arrive in later phases.
 */

#include <base/log.h>
#include <libc/component.h>

#include <QApplication>

#include <qt6_component/qpa_init.h>

#include "sponge_de_main.h"

using namespace Sponge::Sponge_DE;


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {

		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "sponge-de", nullptr };

		QApplication app(argc, const_cast<char **>(argv));

		Main main_window(env);
		main_window.show();

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
