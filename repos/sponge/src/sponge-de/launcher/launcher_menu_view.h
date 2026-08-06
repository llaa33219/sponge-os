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
 * SORT (Phase 11 W2):
 *   The view's `applySortBy` slot honours configd's `launcher.sort_by`
 *   (via ConfigController's launcher_sort_by_changed signal). When the
 *   value is "alpha" (default), entries are sorted alphabetically by
 *   name within each category. When the value is "manual", the entries
 *   are emitted in the pkgd "installed" broadcast order (which itself
 *   is name-sorted by pkgd, so today `manual` ≈ `alpha` for a single
 *   installed set — the comparator still matters when a probe writes a
 *   value and then re-reads it). A named comparator
 *   `launcher_alpha_less_than` is exported so the panel-config probe
 *   can assert ordering behavior without poking into the view's
 *   internals.
 *
 * Q_OBJECT IS used (a deliberate change from the pre-W2 version) —
 * the applySortBy slot must be connectable from ConfigController's
 * pointer-to-member-function syntax. The other behavior (click-
 * outside filter, etc.) still uses functor/lambda connections.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QWidget>

#include "launcher/launcher_controller.h"
#include "theme/theme_loader.h"

class QEvent;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Sponge::Sponge_DE {

/*
 * Named comparator used by LauncherMenuView when sort_by == "alpha".
 * Exported (free function) so the panel-config probe can assert that
 * a launcher.sort_by=alpha round-trip yields alphabetical order
 * without poking into the view's internals. Manual sort uses pkgd's
 * broadcast order verbatim; alpha sort applies this comparator to the
 * app list before insertion.
 *
 * Returns true iff `a.name` precedes `b.name` in ascending order under
 * QString::compare (locale-independent, the same comparator QList uses
 * by default — a probe can replicate the assertion in pure C++).
 */
bool launcher_alpha_less_than(LauncherController::App const &a,
                              LauncherController::App const &b);

class LauncherMenuView : public QWidget
{
	Q_OBJECT

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
		 * using the last-applied theme. Honors the cached
		 * _sort_by: "alpha" resorts via launcher_alpha_less_than;
		 * "manual" preserves the pkgd broadcast order.
		 */
		void repopulate();

	public slots:

		/*
		 * GUI thread ONLY (connected to ConfigController's
		 * launcher_sort_by_changed signal — failure-point 2
		 * enforcement: marshalled via QMetaObject::invokeMethod).
		 * Updates the cached value and rebuilds the menu. No-op
		 * when the value is byte-identical to the cached one.
		 */
		void applySortBy(QString sort);

	protected:

		/*
		 * qApp-level event filter. See class comment for the
		 * click-outside contract. Installed in the constructor.
		 */
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_layout(Theme::Theme const &theme);

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

		/*
		 * Cached configd sort override. "alpha" = alphabetical by
		 * name (the default per the Phase 11 W2 plan); "manual" =
		 * preserve the pkgd broadcast order. An empty value
		 * preserves the pre-W2 behavior (no resort — pkgd's name-
		 * sorted broadcast order is used verbatim).
		 */
		QString _sort_by;

		QString _category_stylesheet() const;
		QString _entry_stylesheet() const;
};

}  /* namespace Sponge::Sponge_DE */