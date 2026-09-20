/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Dismisser. See dismisser.h.
 */

#include "dismisser.h"

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
 *   2. settings dialog (SettingsController) — opened via the
 *      panel context menu's Settings entry or via the bg context
 *      menu's Settings entry. Heap-allocated (W7 refactor) so
 *      close() can dismiss it from outside.
 *   3. panel context menu (QApplication::activePopupWidget via
 *      PanelWidget::close_active_menu) — Qt's built-in Escape
 *      already closes the menu; this slot is the defensive
 *      belt-and-braces path for races.
 *   4. background context menu (QApplication::activePopupWidget
 *      via BackgroundWidget::close_active_menu) — same defensive
 *      path as panel.
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

	if (_settings && _settings->dialog_is_open()) {
		Genode::log("sponge-de: dismisser: closing settings dialog");
		_settings->close_settings_dialog();
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
