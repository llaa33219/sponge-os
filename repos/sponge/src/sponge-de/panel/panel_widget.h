/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Sponge DE panel widget.
 *
 * The panel is a frameless top-level Qt window docked to one screen
 * edge (position and thickness come from the loaded theme). It hosts
 * the launcher button and the clock — nothing more (docs/05-sponge-de.md
 * §5.1). It is one module inside the single sponge-de component and is
 * kept deliberately self-contained so it can be split into its own
 * Genode component later (docs/05-sponge-de.md §3).
 *
 * No Q_OBJECT macro: all connections use functor/lambda overloads, so
 * this class needs no moc pass.
 */

#pragma once

#include <QWidget>

class QLabel;
class QTimer;

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }

class PanelWidget : public QWidget
{
	public:

		explicit PanelWidget(Theme::Theme const &theme, QWidget *parent = nullptr);

		/*
		 * Re-apply colors/geometry from a new theme. Called on the GUI
		 * thread by ThemeController after a live theme reload — never
		 * from the ROM signal handler. Rebuilding the widget stylesheet
		 * and calling update() repaints without recreating the window.
		 */
		void restyle(Theme::Theme const &theme);

	private:

		void _apply_style(Theme::Theme const &theme);

		/* Owned through Qt's parent-child mechanism. */
		QLabel *_clock_label;
		QTimer *_clock_timer;
};

}  /* namespace Sponge::Sponge_DE */
