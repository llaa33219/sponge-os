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
 * CLICK-OUTSIDE (Phase 10 W2 final):
 *   The earlier QTimer + QCursor::pos() mechanism is fundamentally
 *   broken on the Genode QPA — QCursor::pos() never reflects the real
 *   nitpicker pointer (always reports (0,0)) and the timer would
 *   fire the moment its grace period elapsed, hiding the popup
 *   between the S-click and the entry click in the launch phase.
 *   The replacement is event-driven: the view installs a qApp-level
 *   event filter and hides itself on any QEvent::MouseButtonPress
 *   whose target is NOT the panel's launcher toggle button (object-
 *   Name "launcherToggle") and NOT inside the popup itself. The
 *   toggle button is special-cased because its release-time `clicked`
 *   handler toggles the popup — hiding on PRESS would race the
 *   release and re-open the popup. Presses INSIDE the popup (entry
 *   buttons, etc.) are ignored because the entry button's own
 *   `clicked` handler closes the popup on success. Presses at the
 *   toggle button itself are ignored for the same reason.
 *
 *   This is deterministic, race-free, and independent of the cursor
 *   position reported by the QPA — the QMP-driven PS/2 click chain
 *   (S click → entry click) keeps the popup open across the chain
 *   because no press target falls outside the toggle/popup allowlist.
 *
 * No Q_OBJECT macro: all connections use functor/lambda overloads, so
 * this class needs no moc pass.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "theme/theme_loader.h"

class QEvent;
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

	protected:

		/*
		 * qApp-level event filter. See class comment for the
		 * click-outside contract. Installed in the constructor.
		 */
		bool eventFilter(QObject *watched, QEvent *event) override;

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
