/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * NotifierWidget — themed notification popover for Sponge DE.
 *
 * A frameless top-level Qt window docked to the bottom of the panel
 * domain (just under the bar). It is the front-end counterpart of
 * `sponge_notifier` (Phase 14 W4): the widget subscribes to the
 * daemon's "notifications" ROM via NotifierController and renders the
 * top-N active entries as a compact card.
 *
 * Geometry (mirrors test/notify_probe/main.cc POPOVER_* constants):
 *   x = 700, y = 36, w = 300, h = 60
 *   The run config's panel domain is `ypos: 0 | height: 96`, so the
 *   popover fits inside the panel area. The capture probe polls this
 *   exact rect (it must remain byte-identical across the daemon +
 *   widget changes).
 *
 * Lifecycle:
 *   - Hidden when the active list is empty
 *   - Visible when the active list is non-empty
 *   - Auto-hides when the list goes empty (handled by the controller;
 *     the widget only paints the current set)
 *
 * Threading:
 *   Like every other sponge-de widget, the rendering is on the GUI
 *   thread. The NotifierController marshals ROM content (Genode
 *   entrypoint thread) to the GUI thread via QMetaObject::invokeMethod
 *   (failure-point 2 enforcement, mirrors ConfigController and
 *   ThemeController).
 *
 * Q_OBJECT is NOT used — the widget itself is purely a paint target;
 *   the controller carries the QObject signals. The widget just
 *   depends on Qt's standard QWidget updates.
 */

#pragma once

#include <QString>
#include <QWidget>

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }

class NotifierWidget : public QWidget
{
	Q_OBJECT

	public:

		explicit NotifierWidget(Theme::Theme const &theme, QWidget *parent = nullptr);

		/* Re-style the popover from the active theme. Re-applies the
		 * stylesheet only when it actually changes (the Genode QPA
		 * perturbs paint/flush timing on every redundant top-level
		 * mutation, so identity-check before setStyleSheet is the
		 * standard W2 fix). */
		void restyle(Theme::Theme const &theme);

		/*
		 * Replace the rendered list. `entries` is a JSON-ish line per
		 * entry (kind|icon|primary|secondary). The widget renders up
		 * to MAX_ENTRIES from the top, ellipsizing the rest.
		 * Called on the GUI thread (via controller's
		 * QMetaObject::invokeMethod marshalling).
		 *
		 * Each entry's title is truncated to MAX_TITLE_CHARS chars
		 * (the daemon already caps at 96; the widget further trims
		 * to fit the 300-px popover at the default font size).
		 */
		void applyEntries(QStringList entries);

		/*
		 * Show / hide the popover. The widget calls show() / hide()
		 * itself from applyEntries (empty list → hide), so this is
		 * only for external callers (e.g. a forced dismiss).
		 */
		void set_visible(bool visible);

		bool is_dismissed_by_click() const { return _dismissed_by_click; }
		void clear_dismissed();

	private:

		void _apply_style(Theme::Theme const &theme);
		void _apply_geometry();

		/* Stable geometry constants — mirrors notify_probe/main.cc. */
		static constexpr int POPOVER_X = 700;
		static constexpr int POPOVER_Y = 36;
		static constexpr int POPOVER_W = 300;
		static constexpr int POPOVER_H = 60;
		static constexpr int MAX_TITLE_CHARS = 40;
		static constexpr int MAX_ENTRIES = 3;

		QStringList _entries;
		QString     _applied_css;
		bool        _dismissed_by_click { false };

	protected:

		void paintEvent(QPaintEvent *event) override;
		void mousePressEvent(QMouseEvent *event) override;
};

}  /* namespace Sponge::Sponge_DE */
