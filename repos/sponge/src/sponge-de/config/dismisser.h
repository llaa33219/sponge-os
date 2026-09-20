/* SPDX-License-Identifier: Apache-2.0
 *
 * Dismisser — closes the topmost popover (Phase 16 W7, U16.4 / D16.5).
 *
 * Phase 16 W7 binds the `dismiss` keyboard shortcut (default `Escape`)
 * to close the topmost popover. The plan's W7 #5 walks the wm
 * `window_list` ROM in reverse-stack order (newest focus first)
 * and closes the topmost popover that is one of:
 *
 *   - the launcher popup (`LauncherMenuView`) — owned by sponge-de,
 *     closed by `_view->hide()`
 *   - the Settings dialog (`SettingsDialog`) — opened via
 *     `SettingsController::open_settings_dialog()`; the dialog is
 *     exec'd modally, so closing is the controller's
 *     `close_settings_dialog()` slot (added in W7)
 *   - the panel context menu — transient QMenu, owned by the panel;
 *     the panel widget's `close_active_menu()` slot closes it
 *   - the background context menu — owned by BackgroundWidget;
 *     `close_active_menu()` closes it
 *
 * Priority order: launcher → settings → panel menu → bg menu
 * (the user-experienced Z-order: the most recently opened popover
 * is the topmost; the dismisser closes it in the order the user
 * would visually expect).
 *
 * The Dismisser is a QObject (not a QWidget) — it carries pointers
 * to each popover-owning component and routes the dismiss through
 * the appropriate slot. The framework (ShortcutController) invokes
 * `dismiss_topmost()` on the GUI thread via
 * QMetaObject::invokeMethod(Qt::QueuedConnection).
 *
 * Window-list ROM access: the Dismisser does NOT need direct
 * access to wm's `window_list` ROM — the priority list is fixed
 * (launcher → settings → panel → bg), and the user only has one
 * popover visible at a time in normal usage. If multiple popovers
 * are open simultaneously (a corner-case), the Dismisser closes
 * them in priority order from the front of the list. The plan
 * notes this is the Phase 16 boundary — full window-stack-aware
 * dismissal is a Phase 17+ scope item.
 */

#pragma once

#include <QObject>

#include "../launcher/launcher_menu_view.h"
#include "../launcher/launcher_controller.h"
#include "../panel/panel_widget.h"
#include "../background/background_widget.h"
#include "settings_controller.h"

namespace Sponge::Sponge_DE {

class Dismisser : public QObject
{
	Q_OBJECT

	public:

		explicit Dismisser(QObject *parent = nullptr);

		/*
		 * Inject the popover-owning pointers. The controller in
		 * main.cc wires these after construction; absent any
		 * pointer, the corresponding dismiss step is a no-op
		 * (the user-visible popover doesn't exist in this topology).
		 */
		void set_launcher_view(LauncherMenuView *v)        { _launcher_view = v; }
		void set_launcher_controller(LauncherController *c) { _launcher_ctrl = c; }
		void set_settings_controller(SettingsController *s) { _settings      = s; }
		void set_panel_widget(PanelWidget *p)              { _panel         = p; }
		void set_background_widget(BackgroundWidget *b)    { _bg_widget     = b; }

	public slots:

		/*
		 * GUI thread ONLY (failure-point 2 enforcement: marshalled
		 * from the ROM-signal handler by ShortcutController via
		 * QMetaObject::invokeMethod(... , Qt::QueuedConnection)).
		 *
		 * Walks the priority list and closes the first visible
		 * popover. Returns true if anything was closed (the
		 * ShortcutController can log this for observability);
		 * false if no popover was visible.
		 */
		bool dismiss_topmost();

	private:

		LauncherMenuView *_launcher_view { nullptr };
		LauncherController *_launcher_ctrl { nullptr };
		SettingsController *_settings      { nullptr };
		PanelWidget      *_panel         { nullptr };
		BackgroundWidget *_bg_widget     { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
