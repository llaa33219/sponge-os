/* SPDX-License-Identifier: Apache-2.0
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
 *
 * Phase 16 W5 (U16.2 / D16.2) — adds the panel context menu (right-
 * click → QMenu with Height spinbox, Visible widgets checkboxes,
 * Position radio group, Settings entry). Position radio group is
 * driven through SettingsController::request_set("panel.position",
 * "top|bottom"); the closed enum accepts left|right too but those
 * entries are disabled with a "Phase 17+" tooltip (honest disclosure
 * per AGENTS.md §1.1).
 *
 * Phase 16 W5 also adds the dual nitpicker panel-domain topology
 * (panel_top + panel_bottom). The Widget supports TWO construction
 * modes:
 *
 *   - Singleton mode (regression scenarios): one PanelWidget
 *     constructed per sponge-de process; window title "Sponge Panel".
 *     applyPosition is a no-op — the panel always shows regardless of
 *     panel.position. The single domain hosts the widget.
 *
 *   - Dual mode (W5 menu scenario): two PanelWidget instances
 *     constructed; the panel_top_widget with role=Top and title
 *     "Sponge Panel", the panel_bottom_widget with role=Bottom and
 *     title "Sponge Panel Bottom". Both subscribe to the
 *     ConfigController's panel_position_changed signal; the matching
 *     widget show()'s itself, the other hide()'s. The two pre-built
 *     widgets are routed to their own nitpicker domains via the
 *     label_last Gui session convention ("Sponge Panel" → panel_top,
 *     "Sponge Panel Bottom" → panel_bottom).
 *
 * The role is set via set_role() called immediately after construction
 * (before show()). set_sibling() links the two widgets in dual mode
 * for menu coordination (the Settings entry writes through a single
 * SettingsController; both widgets share the same pointer).
 *
 * Phase 16 W8 (U16.5 / D16.5) — multi-panel generalization. Each
 * PanelWidget instance carries:
 *
 *   - a string id (e.g. "default", "alpha", "beta"); the W8
 *     PanelCollection owns N instances keyed by id.
 *
 *   - a unique Gui session label suffix (window title doubles as
 *     the label; "Sponge Panel" for the default instance, "Sponge
 *     Panel <id>" for explicit ids). This is the F5 nitpicker
 *     label_prefix trap defense — two identically-prefixed
 *     sessions land on the same domain, so each instance must
 *     use a distinct label so nitpicker's <policy label_prefix>
 *     routes it correctly.
 *
 *   - a unique per-instance Expanding_reporter (label "panel_<id>",
 *     node type "panel") carrying a monotonically-increasing
 *     click_count attribute. mousePressEvent bumps the counter and
 *     re-emits the report. The W8 multipanel_probe subscribes to
 *     "panel_alpha" + "panel_beta" and asserts that ONLY the
 *     clicked panel's click_count increments — the F5 trap
 *     signature if both panels collapsed onto one domain.
 *
 *   - a unique panel_id for the per-id apply slots: the
 *     PanelCollection routes ConfigController's
 *     panel_height_changed_for(id, h) signal to the right
 *     PanelWidget instance via applyHeight().
 */

#pragma once

#include <QObject>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace Genode {
	class Expanding_reporter;
}

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }
class LauncherMenuView;
class SettingsController;
class TasklistWidget;

class PanelWidget : public QWidget
{
	Q_OBJECT

	public:

		enum class Role { Singleton, Top, Bottom };

		/*
		 * W8: the panel id (e.g. "default", "alpha", "beta"). Empty
		 * string falls back to the W5 legacy "default" id (kept
		 * for backward compatibility with regression scenarios
		 * that pass an empty id).
		 */
		QString id() const { return _id; }

		/*
		 * The default panel's id (a reserved token used by the
		 * regression scenarios that boot with `panel.ids=""` and
		 * rely on the implicit single bottom panel).
		 */
		static QString const &default_id() {
			static QString const s = QStringLiteral("default");
			return s;
		}

		/*
		 * The window title doubles as the Gui session label
		 * (the Genode QPA labels each window's session with it),
		 * and the run scenario routes
		 *   "sponge-de -> Sponge Panel <id>"
		 * into nitpicker's per-panel domain. Each instance's title
		 * MUST be distinct (F5 defense).
		 */
		QString label_suffix() const { return _label_suffix; }

		/*
		 * Per-instance click_count (W8). Bumped by mousePressEvent;
		 * reported through the Expanding_reporter labeled
		 * "panel_<id>". 0 means "no clicks yet".
		 */
		unsigned click_count() const { return _click_count; }

		/*
		 * Attach the per-panel click_count Expanding_reporter. The
		 * reporter is owned externally (constructed by the W8
		 * PanelCollection) and shared between the widget and the
		 * probe. nullptr disables per-panel click reporting (the
		 * legacy regression scenarios use this path — they have no
		 * `panel_<id>` Report route and never observe per-panel
		 * clicks).
		 *
		 * The setter emits the initial click_count=0 report so the
		 * probe can gate on the reporter existing before dispatching
		 * QMP clicks (the probe's
		 * `_panel_click_count_is(label, id, 0)` check is the TDD
		 * sentry for the F5 label_prefix trap defense).
		 */
		void set_click_reporter(Genode::Expanding_reporter *r);

		explicit PanelWidget(Theme::Theme const &theme,
		                     QString const &id,
		                     QString const &window_title,
		                     QWidget *parent = nullptr);
		~PanelWidget() override;

	/*
	 * Phase 14 W11 #47: explicit destructor stops the 1 s clock timer
	 * before QObject parent-child cleanup runs (the parent-owned QTimer
	 * would stop on deleteChildren anyway, but the explicit stop
	 * guarantees no queued timeout fires during destruction — the
	 * failure-mode that surfaces as a leak-audit regression).
	 */

		/*
		 * Set the role tag (singleton / top / bottom). Defaults to
		 * Singleton when the regression scenarios construct a single
		 * PanelWidget (no dual-domain wiring). Dual mode requires
		 * set_role() AND set_sibling() to be called before show().
		 */
		void set_role(Role r) { _role = r; }
		Role role() const { return _role; }

		/*
		 * Set the sibling pointer (the other PanelWidget instance
		 * in dual mode). Used by the Settings menu entry to
		 * coordinate writes through a single SettingsController,
		 * and by applyPosition to determine visibility. nullptr in
		 * singleton mode (default).
		 */
		void set_sibling(PanelWidget *s) { _sibling = s; }

		/*
		 * Inject the SettingsController pointer. The Settings menu
		 * entry calls SettingsController::open_settings_dialog().
		 * nullptr disables the entry (a no-op warning).
		 */
		void set_settings_controller(SettingsController *s) { _settings = s; }

		/*
		 * Re-apply colors/geometry/layout/visibility/clock-format from
		 * a new theme AND from the latest configd-broadcast values
		 * (cached in _height, _visible_widgets, _position,
		 * _clock_format). Called on the GUI thread by ThemeController
		 * after a live theme reload.
		 */
		void restyle(Theme::Theme const &theme);

		/*
		 * Attach the launcher popup. Owned externally (constructed by
		 * main.cc next to the LauncherController), shown/hidden by
		 * the launcher button click.
		 */
		/*
		 * Real-hardware observability: render pkgd's running set in the
		 * panel title (e.g. "Sponge DE · R:hello,debug"). The launcher
		 * popup's dot markers show the same, but only while the popup is
		 * open — the title line is always visible.
		 */
		void show_running_set(QString const &names, int window_count, int child_count, int cfg_writes, int mirror_starts, int mirror_bytes, int pr_ram_kb, int state_changes, QString const &extra_diag);

		/*
		 * Component-health surface (user directive 2026-10-05): the
		 * panel shows component anomalies surfaced by the health
		 * watcher — invisible while every watched component is
		 * healthy. Input is the system init's state report
		 * (per-child skipped_heartbeats / exit values).
		 */
		void set_health(QString const &warnings);

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
		 * Phase 16 W7 (U16.4 / D16.5) — close the active panel
		 * context menu if one is open. The QMenu is stack-
		 * allocated inside contextMenuEvent (so the menu object
		 * lives only during exec()); this slot uses Qt's global
		 * QApplication state to find and close any active modal
		 * menu — typically QApplication::activePopupWidget().
		 *
		 * Qt handles Escape for the active QMenu automatically
		 * (QMenu::keyPressEvent closes on Qt::Key_Escape). The
		 * Dismesser still calls this as a defensive belt-and-
		 * braces (the Escape key may race with the dispatcher's
		 * own forwarding in some focus topologies).
		 *
		 * GUI thread ONLY.
		 */
		void close_active_menu();

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
		 * panel_height_changed / panel_height_changed_for signals).
		 */
		void applyHeight(unsigned h);

		/*
		 * GUI thread ONLY. Updates the cached visible-widgets list.
		 * The list is "clock,launcher,tasklist" by default.
		 */
		void applyVisibleWidgets(QString list);

		/*
		 * GUI thread ONLY (Phase 16 W5, U16.2 / D16.2). Updates the
		 * cached panel.position and drives the dual nitpicker panel-
		 * domain topology. In singleton mode the widget always shows;
		 * in dual mode (set_role + set_sibling) the matching role
		 * widget show()'s and the other hide()'s.
		 *
		 * Accepts "top" / "bottom" / "left" / "right" — the closed
		 * validator emits all four. Only "top" and "bottom" map to
		 * live widget show/hide in Phase 16; "left" and "right" are
		 * accepted but treated like "bottom" (the panel stays at the
		 * bottom) because no left/right nitpicker domain exists yet.
		 * Honest disclosure per AGENTS.md §1.1 (the menu disables
		 * left/right entries so this fallback path is only reached
		 * via direct configd writes, which is the same behavior the
		 * menu would produce).
		 *
		 * Phase 16 W8: in multi-panel mode, every panel has its own
		 * domain so show/hide on position is irrelevant. The slot
		 * still updates the cached value (the menu radio reflects
		 * it) but does not call show()/hide().
		 */
		void applyPosition(QString position);

		/*
		 * GUI thread ONLY. Updates the cached clock format and
		 * refreshes the clock label.
		 */
		void applyClockFormat(QString format);

protected:

		/*
		 * Phase 16 W8: eventFilter catches mouse presses that
		 * reach the launcher button (a child widget that
		 * CONSUMES the press — Qt does NOT propagate the press
		 * to the parent panel). mousePressEvent would only fire
		 * for clicks on the panel's empty stretch / title label
		 * / clock; the click at the launcher (x=0..48) bypasses
		 * mousePressEvent. The event filter observes every press
		 * on the QApplication; we count presses whose global
		 * position is inside this panel's geometry.
		 *
		 * This is the same pattern Main::eventFilter uses for the
		 * demo window (sponge_de_main.cc).
		 */
		bool eventFilter(QObject *watched, QEvent *event) override;

		/*
		 * Phase 16 W5 panel context menu. Right-click on the panel
		 * builds a QMenu anchored to the event position (the Genode
		 * QPA returns (0,0) from QCursor::pos() — known Phase 10+
		 * lesson — so the event-local position is the only valid
		 * anchor). Each control's value-change signal routes
		 * through SettingsController::request_set, which keeps
		 * validator parity with vct's `vct config` path (D16.9).
		 *
		 * Phase 16 W8 (U16.5 / D16.5): the per-panel context menu
		 * ALSO emits per-id keys (panel.<id>.height, etc.) so a
		 * right-click on panel alpha edits alpha's keys, not the
		 * default panel's keys. The Add/Remove panel entries are
		 * added at the bottom of every per-panel menu (the
		 * SettingsController writes `panel.ids` to add / remove
		 * the corresponding id from the comma-list).
		 */
		void contextMenuEvent(QContextMenuEvent *e) override;

		/*
		 * Phase 16 W8: mousePressEvent bumps the per-panel
		 * click_count and re-emits the panel_<id> report. The W8
		 * multipanel_probe asserts that ONLY the clicked panel's
		 * click_count increments (cross-panel assertion catches the
		 * F5 label_prefix trap). The default QWidget::mousePressEvent
		 * is called first so child widgets (the launcher toggle,
		 * etc.) still receive their click signals.
		 */
		void mousePressEvent(QMouseEvent *e) override;

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_geometry(Theme::Theme const &theme);
		void _build_layout(Theme::Theme const &theme);
		void _apply_layout(Theme::Theme const &theme);
		void _apply_visibility();
		void _apply_clock_format(Theme::Theme const &theme);
		void _refresh_clock_text();
		void _report_click();

		/* Owned through Qt's parent-child mechanism. */
		QPushButton *_launcher_toggle { nullptr };
		QLabel      *_title_label     { nullptr };
		QString      _running_set;
		QLabel      *_clock_label     { nullptr };
		QLabel      *_health_label    { nullptr };
		QTimer      *_clock_timer     { nullptr };

		LauncherMenuView *_launcher_view   { nullptr };
		TasklistWidget   *_tasklist_widget { nullptr };
		SettingsController *_settings      { nullptr };

		PanelWidget *_sibling { nullptr };

		/* Dual-mode role tag. */
		Role _role { Role::Singleton };

		/*
		 * W8: per-panel identity (id + label suffix + reporter).
		 * The id is the per-id config namespace key (panel.<id>.height);
		 * the label_suffix is the Gui session title (F5 defense);
		 * the click_count reporter carries the per-instance click
		 * counter that the W8 multipanel_probe observes.
		 */
		QString _id           { default_id() };
		QString _label_suffix { QStringLiteral("Sponge Panel") };
		unsigned _click_count { 0 };

		/*
		 * W8 single-click dedup. A QPA-dispatched MouseButtonPress
		 * can land multiple times on the qApp filter chain (one to
		 * the QWidgetWindow, another to the QPushButton child). The
		 * eventFilter uses _last_click_ms to coalesce events within
		 * a 200 ms window so a single physical click increments
		 * click_count by exactly 1.
		 */
		qint64 _last_click_ms { 0 };

		/*
		 * W8: the Expanding_reporter for per-panel click_count.
		 * Injected by the W8 PanelCollection (nullptr in legacy
		 * regression scenarios where the `panel_<id>` report is
		 * not wired). The reporter's node type is "panel" and its
		 * label is "panel_<id>" (F5 defense: distinct labels per
		 * instance).
		 */
		Genode::Expanding_reporter *_click_reporter { nullptr };

		/* Cached configd overrides. */
		unsigned _height            { 0 };
		QString  _visible_widgets { QStringLiteral("clock,launcher,tasklist") };
		QString  _position        { QStringLiteral("bottom") };
		QString  _clock_format      { QStringLiteral("HH:mm") };
		QString  _applied_css;

		/*
		 * Phase 16 W8 (U16.5 / D16.5) — first-apply flag for
		 * applyPosition. The ctor's _position is "bottom" (the
		 * default). The PanelCollection sets the role BEFORE
		 * applyPosition is called; applyPosition must re-apply
		 * geometry on the FIRST call even when the broadcast value
		 * matches the default — without this flag, the first
		 * applyPosition("bottom") for a panel whose default IS
		 * "bottom" returns early and the role-derived geometry
		 * never lands on the widget.
		 */
		bool _position_default_seen { false };

		QString _warned_format;
};

}  /* namespace Sponge::Sponge_DE */
