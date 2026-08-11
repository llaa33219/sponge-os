/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * TasklistWidget — horizontal tasklist strip for the Sponge DE panel.
 *
 * Phase 14 W7: the tasklist is the deterministic restoration path
 * for the window stack (U3 — no decorative-only minimize). The
 * widget is a horizontal strip that lives inside the panel's
 * QHBoxLayout (between the title label and the stretch zone).
 *
 * Geometry (mirrors the panel pattern):
 *   - The widget is a QWidget, not a top-level window — it lives
 *     inside the panel's Gui session.
 *   - Each task entry is a fixed-width button (TASK_W ×
 *     theme.panel_height). The widget adaptively renders as many
 *     entries as fit; overflow is hidden (the layout window is
 *     bounded by the panel's available width).
 *   - The widget subscribes to the TasklistController's
 *     tasks_changed signal and re-paints when the set changes.
 *
 * Visual state per entry:
 *   - Normal-Visible:        themed accent background, themed text.
 *   - Normal-Visible-Focused: theme.accent background, theme.panel_text
 *                            text (reversed colors — the "focused" body).
 *   - Minimized:             theme.separator background, dimmed text
 *                            (theme.separator is the visually muted
 *                            theme key).
 *   - Has-Alpha:             a small left-edge badge to distinguish
 *                            alpha windows (Qt apps) from opaque
 *                            windows. The badge is a 2px-wide
 *                            theme.accent strip.
 *
 * Click handling:
 *   - Each task entry is a clickable region. mousePressEvent maps
 *     the click x to the entry index, then emits the task_clicked
 *     signal with the entry's label. The TasklistController handles
 *     the click (focus_request + rules update).
 *
 * Threading:
 *   The widget is paint-only. The controller marshals the task list
 *   from the entrypoint thread via QMetaObject::invokeMethod before
 *   emitting tasks_changed.
 *
 * The widget re-styles on theme reload via the standard restyle()
 * call path (mirrors PanelWidget::restyle).
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }

/*
 * POD struct for a single task entry. The controller owns the
 * state; the widget receives a QList<TaskInfo> in applyEntries().
 *
 * `label` is the layouter's Window::Label (the wm session label,
 * e.g. "pkg_runtime -> pkg_gui_demo"). It is the stable identifier
 * the controller uses for focus_request and rules updates.
 *
 * `title` is the layouter's <window title="..."> attribute (the
 * concat of label + " " + the Qt window title). It's the
 * user-visible label rendered on the entry.
 *
 * `x`, `y`, `w`, `h` are the tracked window's geometry (from the
 * window_layout report). Used by the controller only — the widget
 * does not paint the geometry but may render it as a tooltip.
 */
struct TaskInfo {
	QString  label;
	QString  title;
	int      x      { 0 };
	int      y      { 0 };
	unsigned w      { 0 };
	unsigned h      { 0 };
	bool     focused   { false };
	bool     minimized { false };
	bool     has_alpha { false };
};

class TasklistWidget : public QWidget
{
	Q_OBJECT

	public:

		explicit TasklistWidget(Theme::Theme const &theme, QWidget *parent = nullptr);

		/*
		 * Re-style the widget from the active theme. The widget
		 * reads theme.panel_bg, theme.panel_text, theme.accent,
		 * theme.separator, theme.panel_height, theme.padding,
		 * theme.margin. Identity-checked before reusing the cached
		 * CSS (the Genode QPA perturbs paint/flush timing on every
		 * redundant top-level mutation).
		 */
		void restyle(Theme::Theme const &theme);

		/*
		 * Replace the rendered task list. Called on the GUI thread
		 * (via the controller's QMetaObject::invokeMethod marshalling).
		 * Empty list renders an empty strip.
		 */
		void applyEntries(QList<TaskInfo> entries);

		/* The number of currently visible tasks. */
		int task_count() const { return _entries.size(); }

	signals:

		/* Emitted from the GUI thread when the user clicks an
		 * entry. The label is the TaskInfo::label of the clicked
		 * entry; the controller decides what to do based on the
		 * window's tracked state. */
		void task_clicked(QString label);

		/* Emitted when the user double-clicks an entry — the
		 * controller calls this the toggle-maximized action. */
		void task_toggle_maximized(QString label);

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_geometry(Theme::Theme const &theme);

		/* The fixed task-entry width. 96 px is enough for
		 * "pkg_gui_demo" + the "Focused" text. */
		static constexpr int TASK_W = 96;

		/* Task entry vertical padding (top + bottom). The widget
		 * entry draws inside the panel's height minus 2*pad. */
		static constexpr int TASK_PAD = 2;

		QList<TaskInfo> _entries;
		QString         _applied_css;
		int             _panel_height { 28 };

	protected:

		void paintEvent(QPaintEvent *event) override;
		void mousePressEvent(QMouseEvent *event) override;
		void mouseDoubleClickEvent(QMouseEvent *event) override;
};

}  /* namespace Sponge::Sponge_DE */
