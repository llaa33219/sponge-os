/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of Sponge_DE::Main — the Phase 3 single-window Qt widget.
 *
 * Draws one window on top of nitpicker to prove the Qt6 ↔ Genode Gui
 * bridge works. Panel, launcher, notifications, and theme application
 * are announced via Genode::warning (AGENTS.md §5.3) and land in
 * Phase 5+.
 */

#include "sponge_de_main.h"

#include <base/log.h>
#include <sponge/version.h>

#include <QLabel>
#include <QVBoxLayout>

using namespace Sponge;
using namespace Sponge::Sponge_DE;


Main::Main(Genode::Env &env, QWidget *parent)
:
	QWidget(parent),
	_env(env)
{
	setWindowTitle("Sponge DE");
	resize(640, 480);

	auto *layout = new QVBoxLayout(this);

	auto *title = new QLabel("Sponge DE", this);
	title->setAlignment(Qt::AlignCenter);
	auto font = title->font();
	font.setPointSize(24);
	font.setBold(true);
	title->setFont(font);

	auto *subtitle = new QLabel("Phase 3: single-window prototype", this);
	subtitle->setAlignment(Qt::AlignCenter);

	layout->addStretch();
	layout->addWidget(title);
	layout->addWidget(subtitle);
	layout->addStretch();

	Genode::log("Sponge DE window created (Sponge OS ",
	            Sponge::VERSION_STRING, " / ", Sponge::CODENAME, ")");

	Genode::warning("not implemented: panel (Phase 5)");
	Genode::warning("not implemented: launcher (Phase 5)");
	Genode::warning("not implemented: notifications (Phase 5)");
	Genode::warning("not implemented: theme application (Phase 5)");
}
