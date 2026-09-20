/* SPDX-License-Identifier: Apache-2.0
 *
 * ShowDesktop — see show_desktop.h.
 *
 * Phase 16 W7 closes the W6 deviation: the toggle now drives the
 * tasklist's `set_all_minimized(bool)` slot which publishes the
 * layouter-rule ROM overwrite (the W7 state-machine path at
 * tasklist_controller.cc:500-516). The first click minimizes
 * every visible window by parking them at (-32000, -32000); the
 * second click restores.
 *
 * State is a single boolean `_active`. The menu's
 * `show_desktop_action->setChecked(sd.is_active())` reflects the
 * current state at the time the menu opens; the click handler
 * flips it.
 */

#include "show_desktop.h"

#include <base/log.h>

#include "../panel/tasklist_controller.h"

using namespace Sponge::Sponge_DE;


ShowDesktop::ShowDesktop(QObject *parent)
:
	QObject(parent)
{
}


/*
 * Phase 16 W7 — drive the tasklist's set_all_minimized(bool)
 * via the W6/W7 follow-up entry. The toggle is two-state (show
 * vs restore); the menu shows the current state via the
 * show_desktop_action's checkable flag and the click flips it.
 *
 * GUI thread ONLY (the menu slot dispatches via the QMenu's
 * triggered signal which Qt delivers on the GUI thread).
 */
void ShowDesktop::toggle()
{
	if (!_tasklist) {
		Genode::warning("sponge-de: show-desktop toggle: TasklistController not attached");
		return;
	}

	_active = !_active;

	Genode::log("sponge-de: show-desktop toggle: ",
	            _active ? "ON (minimize all)" : "OFF (restore all)");

	_tasklist->set_all_minimized(_active);
}