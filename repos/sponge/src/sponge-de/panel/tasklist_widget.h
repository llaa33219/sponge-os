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
 *     entries as fit; overflow is hidden.
 *   - The widget subscribes to the TasklistController's
 *     tasks_changed signal and re-paints when the set changes.
 *
 * Visual state per entry:
 *   - Normal-Visible:        themed panel_bg background, themed text.
 *   - Normal-Visible-Focused: themed accent background, panel_text
 *                            text (reversed).
 *   - Minimized:             themed separator background, dimmed text.
 *   - Has-Alpha:             a small left-edge badge (2 px accent stripe).
 *
 * Click handling:
 *   - mousePressEvent maps the click x to the entry index, then
 *     emits task_clicked with the entry's label.
 *   - mouseDoubleClickEvent emits task_toggle_maximized.
 */

#pragma once

#include "task_info.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace Sponge::Sponge_DE { namespace Theme { struct Theme; } }

namespace Sponge::Sponge_DE {

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

		/* Replace the rendered task list. GUI-thread only. */
		void applyEntries(QList<TaskInfo> entries);

		/* The number of currently visible tasks. */
		int task_count() const { return _entries.size(); }

	signals:

		/* Emitted from the GUI thread when the user clicks an entry. */
		void task_clicked(QString label);

		/* Emitted when the user double-clicks an entry. */
		void task_toggle_maximized(QString label);

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_geometry(Theme::Theme const &theme);

		static constexpr int TASK_W = 96;
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
