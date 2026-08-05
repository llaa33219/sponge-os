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
 * CLICK-TO-LAUNCH (Phase 7 todo 10):
 *   Activating an entry calls LauncherController::request_launch(),
 *   which sends a `launch <name>` request to sponge_pkgd over the
 *   "launcher_request" channel and polls "launcher_result" non-
 *   blocking. The menu then closes so the launched window gets focus.
 *   Running entries render a suffix (" \342\200\242" dot) sourced from
 *   the installed broadcast's `running` attribute.
 *
 * No Q_OBJECT macro: all connections use functor/lambda overloads, so
 * this class needs no moc pass.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QDateTime>
#include <QWidget>

#include "theme/theme_loader.h"

class QLabel;
class QPushButton;
class QTimer;
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

	protected:

		/*
		 * showEvent — record the time the popup became visible. The
		 * 300ms grace period in _outside_check_timer uses this to
		 * ignore cursor-outside checks while show()/raise()/activate-
		 * Window() + the QMP closed-loop nav are settling.
		 */
		void showEvent(QShowEvent *event) override
		{
			QWidget::showEvent(event);
			_visible_since_ms = QDateTime::currentMSecsSinceEpoch();
		}

	private:

		void _apply_style(Theme::Theme const &theme);

		LauncherController &_controller;

		Theme::Theme _theme;   /* cached for repopulate() */

		QVBoxLayout *_root_layout { nullptr };

		QTimer    *_outside_check_timer { nullptr };
		qint64     _visible_since_ms     { 0 };

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
