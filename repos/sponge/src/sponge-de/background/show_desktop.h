/* SPDX-License-Identifier: Apache-2.0
 *
 * ShowDesktop — minimize-all / restore-all state machine (Phase 16 W6,
 * U16.3).
 *
 * Reuses the Phase 14 W7 tasklist state machine (the off-screen
 * (-32000,-32000) parking at tasklist_controller.cc:500-516) but
 * applies it to every focused/visible window at once. The toggle
 * is two-state (show vs restore); the menu shows the current
 * state and the click flips it.
 *
 * State is a single boolean `_shown` (false = "all visible",
 * true = "all minimized"). On the first click after a "restored"
 * state, every visible window's geometry is saved (the
 * Window_state cache inside TasklistController) and the layouter
 * rule ROM is overwritten with the off-screen parking for each.
 * On the second click, the saved geometry is restored.
 *
 * The implementation piggybacks on the tasklist controller's
 * `_publish_rules_for(label)` path (tasklist_controller.cc:424-431)
 * by walking the `_tracked` list and applying the rules to
 * every window. The tasklist is the established source of truth
 * for window state — ShowDesktop reuses its data path, no
 * parallel state.
 *
 * No new wiring beyond the existing wm `focus_request` report
 * (which the tasklist controller already publishes) — the
 * layouter reads the rules ROM and repositions every assigned
 * window automatically (genode/repos/gems/src/app/window_layouter/
 * main.cc:332-362 + 364-406).
 */

#pragma once

#include <QObject>

namespace Sponge { namespace Sponge_DE { class TasklistWidget; } }

class TasklistController;

namespace Sponge::Sponge_DE {

class ShowDesktop : public QObject
{
	Q_OBJECT

	public:

		explicit ShowDesktop(QObject *parent = nullptr);

		void set_tasklist_controller(TasklistController *t) { _tasklist = t; }

		bool is_active() const { return _active; }

	public slots:

		/*
		 * GUI thread ONLY (failure-point 2 enforcement: marshalled
		 * from the background context menu via
		 * QMetaObject::invokeMethod(... , Qt::QueuedConnection) when
		 * the menu's Show desktop entry is triggered from a non-GUI
		 * thread).
		 *
		 * Implements U16.3 — minimize-all / restore-all via the
		 * tasklist's layouter-rule ROM overwrite (Phase 14 W7 state
		 * machine, tasklist_controller.cc:500-516). The first
		 * click after a "restored" state saves every visible
		 * window's geometry and minimizes them by parking at
		 * (-32000, -32000) via _publish_rules_for; the second click
		 * restores.
		 */
		void toggle();

	private:

		TasklistController *_tasklist { nullptr };
		bool                _active   { false };
};

}  /* namespace Sponge::Sponge_DE */