/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Dismisser. See dismisser.h.
 */

#include "dismisser.h"

#include <base/log.h>

#include <QApplication>
#include <QWidget>

using namespace Sponge::Sponge_DE;


Dismisser::Dismisser(QObject *parent) : QObject(parent)
{
}


/*
 * Phase 16 W7 (U16.4 / D16.5) — close the topmost popover in
 * priority order. Returns true if anything was closed (used by
 * the ShortcutController for observability).
 *
 * Priority order (matches the plan's W7 #5 spec):
 *   1. launcher popup (LauncherMenuView) — most recently opened
 *      by the Super shortcut or by a panel-click; the dismisser
 *      closes it via hide().
 *   2. panel context menu (QApplication::activePopupWidget via
 *      PanelWidget::close_active_menu) — Qt's built-in Escape
 *      already closes the menu; this slot is the defensive
 *      belt-and-braces path for races.
 *   3. background context menu (QApplication::activePopupWidget
 *      via BackgroundWidget::close_active_menu) — same defensive
 *      path as panel.
 *
 * Provider-owned settings windows are not touched (Criterion 18):
 * they are the providers' own top-level windows, closed through
 * their own chrome — the dismisser only closes sponge-de popovers.
 *
 * Returns after the first successful close (only one popover is
 * visible at a time in normal usage).
 */
bool Dismisser::dismiss_topmost()
{
	if (_launcher_view && _launcher_view->isVisible()) {
		Genode::log("sponge-de: dismisser: closing launcher popup");
		/* route through the controller so `launcher_state` publishes */
		if (_launcher_ctrl)
			_launcher_ctrl->close_popup();
		else
			_launcher_view->hide();
		return true;
	}

	if (_panel) {
		/* close_active_menu returns when no popup is active; the
		 * Dismesser treats the absence of a closed popup as "not
		 * dismissed at this step, try the next". */
		QWidget *before = QApplication::activePopupWidget();
		_panel->close_active_menu();
		QWidget *after = QApplication::activePopupWidget();
		if (before != after) {
			Genode::log("sponge-de: dismisser: closing panel context menu");
			return true;
		}
	}

	if (_bg_widget) {
		QWidget *before = QApplication::activePopupWidget();
		_bg_widget->close_active_menu();
		QWidget *after = QApplication::activePopupWidget();
		if (before != after) {
			Genode::log("sponge-de: dismisser: closing background context menu");
			return true;
		}
	}

	return false;
}
