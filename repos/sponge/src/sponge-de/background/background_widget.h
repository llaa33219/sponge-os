/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundWidget — the in-DE owned desktop surface (Phase 16 W6,
 * criteria 10 + 11).
 *
 * The widget is a fullscreen frameless QWidget that paints the
 * background color (background.color) or the background image
 * (background.image). It owns the previously empty desktop region —
 * nitpicker's default-domain background colour (the platform-level
 * `+ background | color: #1e1e2e` rule) was the only paint there
 * before W6. Right-clicks on the uncovered background region land
 * here; contextMenuEvent builds a QMenu (Settings / Launch / Show
 * desktop) anchored to the event-local position (the Genode QPA
 * returns (0,0) from QCursor::pos() — Phase 10+ lesson).
 *
 * Widget is constructed only when the component config carries
 * `<background source="controller"/>` (mirrors the W4 de_config
 * + W5 panel_bottom gates). Scenarios that do not opt in keep
 * the Phase 10/11/14 behavior unchanged — no widget, no extra
 * Gui session, no extra ROM session.
 *
 * Three pieces:
 *
 *   - The QWidget itself (this file) — paint + contextMenu + lifecycle.
 *   - BackgroundController — subscribes to configd's background.color
 *     + background.image broadcasts and forwards the live update
 *     to the widget via the QMetaObject::invokeMethod GUI-thread
 *     marshal.
 *   - ShowDesktop — the minimize-all / restore-all state machine
 *     (U16.3), reusing the Phase 14 W7 tasklist layouter-rule ROM
 *     overwrite (tasklist_controller.cc:500-516 — the off-screen
 *     (-32000,-32000) parking).
 *
 * Layer ordering: the widget's Gui session routes to the
 * nitpicker `<domain name="default">` at layer=1 (BELOW the
 * wm window domains at layer=3, BELOW the panel at layer=2).
 * The default-domain background color rule is removed by the
 * W6 implementation (nitpicker's `+ background | color: #1e1e2e`
 * would paint underneath the widget anyway, but the widget
 * paints the entire screen so the nitpicker background colour
 * is unreachable).
 */

#pragma once

#include <QImage>
#include <QPoint>
#include <QString>
#include <QWidget>

class QMenu;

namespace Sponge::Sponge_DE {

class BackgroundController;
class SettingsController;
class LauncherMenuView;

}

namespace Sponge::Sponge_DE {

class BackgroundWidget : public QWidget
{
	Q_OBJECT

	public:

		explicit BackgroundWidget(QWidget *parent = nullptr);
		~BackgroundWidget() override;

		void set_controller(BackgroundController *c) { _controller = c; }

		void set_settings_controller(SettingsController *s) { _settings = s; }
		void set_launcher_view(LauncherMenuView *v) { _launcher_view = v; }
		void set_tasklist_controller(void *t) { _tasklist = t; }

		/*
		 * Phase 16 W7 (U16.4 / D16.5) — close the active background
		 * context menu if one is open. Same QApplication::active
		 * PopupWidget() pattern as PanelWidget::close_active_menu
		 * (the menu is stack-allocated inside contextMenuEvent).
		 *
		 * GUI thread ONLY.
		 */
		void close_active_menu();

	public slots:

		void applyBackgroundColor(QString color);
		void applyBackgroundImage(QString path);

	signals:

		/* contextMenuEvent lifecycle. sponge_de_main.cc connects
		 * these to the bgmenu_open reporter the W6 probe observes. */
		void bgmenu_opened();
		void bgmenu_closed();

	protected:

		bool event(QEvent *e) override;
		void paintEvent(QPaintEvent *e) override;
		void mousePressEvent(QMouseEvent *e) override;
		void mouseMoveEvent(QMouseEvent *e) override;
		void contextMenuEvent(QContextMenuEvent *e) override;

	private:

		BackgroundController *_controller  { nullptr };
		SettingsController    *_settings    { nullptr };
		LauncherMenuView      *_launcher_view { nullptr };
		void                 *_tasklist    { nullptr };

		QString _color    { QStringLiteral("#1e1e2e") };
		QString _image_path { QStringLiteral("/system/background/default.png") };

		QImage _image;

		void _reload_image();
		void show_context_menu(QPoint const &global_pos);
};

}  /* namespace Sponge::Sponge_DE */