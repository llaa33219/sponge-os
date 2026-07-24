/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * LauncherMenuView — popup app menu for Sponge DE (Phase 5c).
 *
 * The visible "launcher" UI: a frameless popup listing every installed
 * package that declares a launcher category, grouped under that
 * category. Opened by the panel's launcher button (see PanelWidget);
 * closes on click-outside or on app entry activation.
 *
 * DATA SOURCE:
 *   LauncherController owns the parsed app list and calls
 *   repopulate() whenever pkgd's `list` result changes. The view is a
 *   dumb renderer of that list — it does not talk to pkgd itself.
 *
 * THEME:
 *   All colors and the heading font come from the loaded theme
 *   (AGENTS.md §3.4). No visual element is hardcoded.
 *
 * CLICK-TO-LAUNCH IS DEFERRED:
 *   Activating an entry logs the honest not-implemented warning and
 *   closes the popup. The lifecycle glue (start-on-demand through
 *   pkg_runtime / init config) is its own phase per docs/09-roadmap.md
 *   Phase 5 notes; here we only prove the launcher populated from the
 *   pkgd rich list (Phase 5c criterion).
 *
 * No Q_OBJECT macro: all connections use functor/lambda overloads, so
 * this class needs no moc pass.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "theme/theme_loader.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Sponge::Sponge_DE {

class LauncherController;

class LauncherMenuView : public QWidget
{
	public:

		/*
		 * `controller` provides the current app list. The panel
		 * constructs the view (one per panel) but does not show it
		 * until the launcher button is clicked.
		 */
		LauncherMenuView(LauncherController &controller,
		                 Theme::Theme const &theme,
		                 QWidget *parent = nullptr);

		void restyle(Theme::Theme const &theme);

		/*
		 * Called by LauncherController on the GUI thread after a
		 * pkgd list refresh; rebuilds the menu contents in place
		 * using the last-applied theme.
		 */
		void repopulate();

	private:

		void _apply_style(Theme::Theme const &theme);

		LauncherController &_controller;

		Theme::Theme _theme;   /* cached for repopulate() */

		QVBoxLayout *_root_layout { nullptr };

		/*
		 * Per-category section. Categories are added in the order they
		 * first appear in the (name-sorted) pkgd list, which gives a
		 * stable on-screen order.
		 */
		struct Section {
			QLabel      *heading { nullptr };
			QVBoxLayout *entries_layout { nullptr };
		};
		QHash<QString, Section> _sections;

		QString _category_stylesheet() const;
		QString _entry_stylesheet() const;
};

}  /* namespace Sponge::Sponge_DE */
