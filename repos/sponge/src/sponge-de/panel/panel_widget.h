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
 * Phase 11 W2 — constructor-only state was extracted into private
 * methods so live-reloadable keys (panel.height, panel.visible_widgets,
 * clock.format) reach the widget without a rebuild.
 *
 * Phase 14 W7 — adds the tasklist widget as a slot in the panel
 * QHBoxLayout between the title label and the stretch zone. The tasklist
 * is the deterministic minimize/restore path for the window stack
 * (U3 / D14.3). The widget is owned externally (defined in the
 * tasklist_widget.h header) and inserted via attach_tasklist().
 */

#pragma once

#include <QObject>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }
class LauncherMenuView;
class TasklistWidget;

class PanelWidget : public QWidget
{
	Q_OBJECT

	public:

		explicit PanelWidget(Theme::Theme const &theme, QWidget *parent = nullptr);
		~PanelWidget() override;

		/*
 * Phase 14 W11 #47: explicit destructor stops the 1 s clock timer
 * before QObject parent-child cleanup runs (the parent-owned QTimer
 * would stop on deleteChildren anyway, but the explicit stop
 * guarantees no queued timeout fires during destruction — the
 * failure-mode that surfaces as a leak-audit regression).
 */

		/*
		 * Re-apply colors/geometry/layout/visibility/clock-format from
		 * a new theme AND from the latest configd-broadcast values
		 * (cached in _height, _visible_widgets, _clock_format).
		 * Called on the GUI thread by ThemeController after a live
		 * theme reload.
		 */
		void restyle(Theme::Theme const &theme);

		/*
		 * Attach the launcher popup. Owned externally (constructed by
		 * main.cc next to the LauncherController), shown/hidden by
		 * the launcher button click.
		 */
		void set_launcher_view(LauncherMenuView *view) { _launcher_view = view; }

		/*
		 * Attach the tasklist widget (Phase 14 W7). The widget is
		 * inserted into the panel QHBoxLayout between the title
		 * label and the stretch zone. The widget is owned
		 * externally (constructed by main.cc), but its lifetime
		 * must outlive the panel.
		 *
		 * restyle() fan-out includes the tasklist widget so its
		 * colors track the active theme.
		 */
		void attach_tasklist(TasklistWidget *widget);

		/*
		 * Per-instance configd overrides (set by the apply* slots).
		 * restyle() reads these so a theme reload does NOT erase a
		 * live configd-set value. _height == 0 means "no live height
		 * override; use the theme default".
		 */
		unsigned height_override() const { return _height; }

	public slots:

		/*
		 * GUI thread ONLY (connected to ConfigController's
		 * panel_height_changed signal).
		 */
		void applyHeight(unsigned h);

		/*
		 * GUI thread ONLY. Updates the cached visible-widgets list.
		 * The list is "clock,launcher,tasklist" by default.
		 */
		void applyVisibleWidgets(QString list);

		/*
		 * GUI thread ONLY. Updates the cached clock format and
		 * refreshes the clock label.
		 */
		void applyClockFormat(QString format);

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_geometry(Theme::Theme const &theme);
		void _build_layout(Theme::Theme const &theme);
		void _apply_layout(Theme::Theme const &theme);
		void _apply_visibility();
		void _apply_clock_format(Theme::Theme const &theme);
		void _refresh_clock_text();

		/* Owned through Qt's parent-child mechanism. */
		QPushButton *_launcher_toggle { nullptr };
		QLabel      *_title_label     { nullptr };
		QLabel      *_clock_label     { nullptr };
		QTimer      *_clock_timer     { nullptr };

		LauncherMenuView *_launcher_view   { nullptr };
		TasklistWidget   *_tasklist_widget { nullptr };

		/* Cached configd overrides. */
		unsigned _height            { 0 };
		QString  _visible_widgets { QStringLiteral("clock,launcher,tasklist") };
		QString  _clock_format      { QStringLiteral("HH:mm") };
		QString  _applied_css;

		QString _warned_format;
};

}  /* namespace Sponge::Sponge_DE */
