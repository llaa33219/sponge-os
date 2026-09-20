/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundWidget — see background_widget.h.
 */

#include "background_widget.h"

#include "background_controller.h"
#include "show_desktop.h"

#include <base/log.h>

#include "config/settings_controller.h"
#include "launcher/launcher_menu_view.h"

/*
 * Forward declare the global TasklistController here (rather
 * than in the header) so the header stays free of cross-
 * namespace dependency. background_widget.h's `void *`
 * member avoids the include cycle.
 */
namespace Sponge { namespace Sponge_DE { class TasklistWidget; } }
class TasklistController;

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>

using namespace Sponge::Sponge_DE;


BackgroundWidget::BackgroundWidget(QWidget *parent)
:
	QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
	/*
	 * The window title is the Gui session label — run scripts
	 * route "sponge-de -> Sponge Background" to the default
	 * nitpicker domain at layer=1 (BELOW windows, BELOW the
	 * panel). The title is the only contract between run
	 * scripts and the in-DE widget.
	 */
	setWindowTitle(QStringLiteral("Sponge Background"));

	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true);

	/*
	 * Fill the screen. Same Genode-QPA caveat as PanelWidget:
	 * the primaryScreen geometry may be degenerate (1x1) until
	 * nitpicker's panorama info arrives. Fall back to the
	 * scenario's reference size (1024x768).
	 */
	QScreen *screen = QGuiApplication::primaryScreen();
	int const sw = screen ? screen->geometry().width()  : 0;
	int const sh = screen ? screen->geometry().height() : 0;
	int const w = sw > 64 ? sw : 1024;
	int const h = sh > 64 ? sh : 768;
	setGeometry(0, 0, w, h);
}


BackgroundWidget::~BackgroundWidget()
{
}


void BackgroundWidget::applyBackgroundColor(QString color)
{
	if (color == _color) return;
	_color = color;
	update();
}


void BackgroundWidget::applyBackgroundImage(QString path)
{
	if (path == _image_path) return;
	_image_path = path;
	_reload_image();
	update();
}


void BackgroundWidget::_reload_image()
{
	_image = QImage();

	if (_image_path.isEmpty()) return;

	/*
	 * The image path comes from the configd allowlist validator
	 * (sponge_configd/main.cc:422-425). The path is one of
	 * the closed set, served by the run scenario as a ROM
	 * boot module (e.g. /system/background/default.png →
	 * bin/default.png). On the Genode QPA, Qt's QImage can
	 * load PNG bytes from any QIODevice; the path is loaded
	 * via QFile. (The closed allowlist means the path is
	 * guaranteed safe — no path traversal possible.)
	 */
	QImage img;
	if (!img.load(_image_path)) {
		Genode::warning("sponge-de: background.image: failed to load '",
		                _image_path.toUtf8().constData(),
		                "' — falling back to solid colour");
		return;
	}
	_image = img;
}


void BackgroundWidget::paintEvent(QPaintEvent * /*e*/)
{
	QPainter p(this);

	if (!_image.isNull()) {
		int w = _image.width();
		int h = _image.height();
		for (int y = 0; y < this->height(); y += h) {
			for (int x = 0; x < this->width(); x += w) {
				p.drawImage(QPoint(x, y), _image);
			}
		}
		return;
	}

	p.fillRect(rect(), QColor(_color));
}


bool BackgroundWidget::event(QEvent *e)
{
	return QWidget::event(e);
}


void BackgroundWidget::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::RightButton) {
		show_context_menu(e->globalPosition().toPoint());
	}
	QWidget::mousePressEvent(e);
}


void BackgroundWidget::mouseMoveEvent(QMouseEvent *e)
{
	QWidget::mouseMoveEvent(e);
}


void BackgroundWidget::contextMenuEvent(QContextMenuEvent *e)
{
	show_context_menu(e->globalPos());
}


void BackgroundWidget::show_context_menu(QPoint const &global_pos)
{
	emit bgmenu_opened();

	QMenu menu;
	menu.setTitle(QStringLiteral("Sponge Desktop"));

	auto *settings_action = menu.addAction(QStringLiteral("Settings..."));
	QObject::connect(settings_action, &QAction::triggered, this, [this]() {
		if (!_settings) return;
		QMetaObject::invokeMethod(_settings, "open_settings_dialog",
		                          Qt::QueuedConnection);
	});

	auto *launcher_action = menu.addAction(QStringLiteral("Launch"));
	QObject::connect(launcher_action, &QAction::triggered, this, [this]() {
		if (_launcher_view) {
			if (_launcher_view->isVisible())
				_launcher_view->hide();
			else {
				_launcher_view->repopulate();
				_launcher_view->show();
				_launcher_view->raise();
				_launcher_view->activateWindow();
			}
		} else {
			Genode::warning("not implemented: launcher view not attached");
		}
	});

	auto *show_desktop_action = menu.addAction(QStringLiteral("Show desktop"));
	show_desktop_action->setCheckable(true);
	if (_tasklist) {
		ShowDesktop sd;
		sd.set_tasklist_controller(static_cast<TasklistController *>(_tasklist));
		show_desktop_action->setChecked(sd.is_active());
	}
	QObject::connect(show_desktop_action, &QAction::triggered, this, [this]() {
		if (!_tasklist) return;
		ShowDesktop sd;
		sd.set_tasklist_controller(static_cast<TasklistController *>(_tasklist));
		sd.toggle();
	});

	menu.exec(global_pos);

	emit bgmenu_closed();
}


/*
 * Phase 16 W7 (U16.4 / D16.5) — close the active background context
 * menu. Same QApplication::activePopupWidget() defensive path as
 * PanelWidget::close_active_menu. The QMenu is stack-allocated
 * inside contextMenuEvent (so the menu object lives only during
 * exec()); Qt handles Escape for the active QMenu automatically
 * (QMenu's keyPressEvent closes on Qt::Key_Escape) — this slot
 * is the belt-and-braces path for races where the Escape key
 * propagation is delayed.
 */
void BackgroundWidget::close_active_menu()
{
	QWidget *active_popup = QApplication::activePopupWidget();
	if (active_popup) {
		Genode::log("sponge-de: bg close_active_menu (dismiss path)");
		active_popup->close();
	}
}