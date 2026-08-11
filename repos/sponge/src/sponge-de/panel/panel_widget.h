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
 * clock.format) reach the widget without a rebuild:
 *
 *   _build_layout(theme)         — creates launcher toggle, title, clock QLabel
 *   _apply_style(theme)          — restyles stylesheet
 *   _apply_geometry(theme,h)     — sets fixed-size + outer geometry
 *   _apply_layout(theme)         — re-applies margins/spacing/sizes
 *   _apply_visibility(list)      — hides launcher toggle / clock label
 *   _apply_clock_format(theme,f) — re-applies the clock format
 *
 * restyle() re-applies everything via these helpers; the apply* slots
 * are bound to ConfigController signals (panel_height_changed,
 * panel_visible_widgets_changed, clock_format_changed). The
 * ConfigController marshals all configd sigh callbacks to the GUI
 * thread via QMetaObject::invokeMethod, so the apply* slots are
 * guaranteed to run on the GUI thread — never call them from a
 * non-GUI thread.
 *
 * Q_OBJECT IS used (a deliberate change from the pre-W2 version, which
 * avoided moc via functor/lambda connections) — the three apply* slots
 * need to be connectable from ConfigController's Qt-5-style
 * pointer-to-member connect() and require the moc pass.
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

		/*
		 * Re-apply colors/geometry/layout/visibility/clock-format from
		 * a new theme AND from the latest configd-broadcast values
		 * (cached in _height, _visible_widgets, _clock_format).
		 * Called on the GUI thread by ThemeController after a live
		 * theme reload — never from a ROM signal handler. Rebuilding
		 * the widget stylesheet and calling update() repaints without
		 * recreating the window.
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
		 * must outlive the panel (the panel's layout will paint
		 * the widget when the layout is updated).
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
		 * panel_height_changed signal, which is emitted on the GUI
		 * thread via QMetaObject::invokeMethod marshalling — failure-
		 * point 2 enforcement). Updates the cached override and
		 * reapplies geometry.
		 */
		void applyHeight(unsigned h);

		/*
		 * GUI thread ONLY. Updates the cached visible-widgets list and
		 * hides whichever child is no longer in the list. The list is
		 * "clock,launcher" by default.
		 */
		void applyVisibleWidgets(QString list);

		/*
		 * GUI thread ONLY. Updates the cached clock format and
		 * refreshes the clock label. SEMANTIC fallback here (Qt
		 * side): if the format string produces garbage
		 * (QDateTime::toString yields empty or the previous text
		 * unchanged after substitution), fall back to "HH:mm" and
		 * emit Genode::warning ONCE per bad value (failure-point 9).
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

		LauncherMenuView *_launcher_view { nullptr };
		TasklistWidget   *_tasklist_widget { nullptr };

		/* Cached configd overrides. */
		unsigned _height            { 0 };   /* 0 = use theme.panel_height() */
		QString  _visible_widgets { QStringLiteral("clock,launcher") };
		QString  _clock_format      { QStringLiteral("HH:mm") };
		QString  _applied_css;             /* stylesheet currently on the widget */

		/*
		 * De-dup for the clock-format fallback warning: one warning
		 * per bad format value (failure-point 9: do not spam the log
		 * every minute on the QTimer tick).
		 */
		QString _warned_format;
};

}  /* namespace Sponge::Sponge_DE */