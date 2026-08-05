/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of LauncherMenuView. See launcher_menu_view.h.
 */

#include "launcher_menu_view.h"

#include "launcher_controller.h"
#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

#include <base/log.h>

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


LauncherMenuView::LauncherMenuView(LauncherController &controller,
                                   Theme::Theme const &theme,
                                   QWidget *parent)
:
	QWidget(parent,
	        Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
	_controller(controller),
	_theme(theme)
{
	setWindowTitle(QStringLiteral("Sponge Launcher"));
	setAttribute(Qt::WA_QuitOnClose, false);

	_root_layout = new QVBoxLayout(this);
	int const pad = (int)theme.padding();
	int const gap = (int)theme.margin();
	_root_layout->setContentsMargins(pad, pad, pad, pad);
	_root_layout->setSpacing(gap);

	_apply_style(theme);

	/*
	 * Event-driven click-outside: install a qApp-level event filter
	 * (see eventFilter()). qApp-level filters see every event before
	 * the QPA dispatches it to its target widget, which is what we
	 * need for a deterministic press-time "click outside" decision
	 * that does not depend on QCursor::pos() (broken on the Genode
	 * QPA — always reports (0,0)). The filter is installed in the
	 * constructor; the popup is a long-lived child of the panel, so
	 * the filter outlives every show()/hide() cycle.
	 */
	qApp->installEventFilter(this);

	repopulate();
}


bool LauncherMenuView::eventFilter(QObject *watched, QEvent *event)
{
	/*
	 * Click-outside: only act on QEvent::MouseButtonPress while the
	 * popup is visible. Three allowlist cases (do nothing, let the
	 * widget's own handler take it):
	 *
	 *  (1) the press is on the popup itself or any of its children
	 *      (entry buttons, etc.) — the entry button's own clicked
	 *      handler closes the popup on success; we must not race it
	 *      by hiding on press.
	 *  (2) the press is on the panel's launcher toggle button
	 *      (objectName "launcherToggle") — its clicked handler
	 *      TOGGLES the popup on release (hide+show). Hiding on press
	 *      would race the release: the click toggle would re-show
	 *      what we just hid, and the user would see a glitch.
	 *  (3) the watched object is not a QWidget (defensive — QPA
	 *      delivers some press events to non-widget objects too;
	 *      we don't care about those).
	 *
	 * Anything else (press on the demo body, the panel background,
	 * the desktop, etc.) is "click outside" — hide.
	 */
	if (event->type() != QEvent::MouseButtonPress) return false;
	if (!isVisible()) return false;

	QWidget *w = qobject_cast<QWidget *>(watched);
	if (w == nullptr) return false;

	/* Case 1: press on popup itself or any child. */
	if (w == this || isAncestorOf(w)) return false;

	/* Case 2: press on the launcher toggle button. Identified by
	 * the objectName set in PanelWidget's constructor. */
	if (w->objectName() == QLatin1String("launcherToggle"))
		return false;

	/* Case 3 is the catch-all: click outside, hide. */
	Genode::log("sponge-de: launcher click-outside on ",
	            w->metaObject()->className(),
	            " cursor=", QCursor::pos().x(), ",", QCursor::pos().y(),
	            " — hiding popup");
	hide();
	return false;
}


void LauncherMenuView::restyle(Theme::Theme const &theme)
{
	_theme = theme;
	_apply_style(theme);

	/* Re-apply per-section stylesheets since they carry theme colors. */
	for (auto it = _sections.begin(); it != _sections.end(); ++it) {
		if (it.value().heading)
			it.value().heading->setStyleSheet(_category_stylesheet());
	}

	/* Walk child entries (QPushButtons under each section layout) and
	 * re-apply the entry stylesheet. */
	QList<QPushButton *> entries = findChildren<QPushButton *>();
	for (QPushButton *btn : entries)
		btn->setStyleSheet(_entry_stylesheet());

	update();
}


void LauncherMenuView::repopulate()
{
	/* Clear existing sections. */
	QList<QObject *> children_copy = children();
	for (QObject *c : children_copy) {
		if (auto *w = qobject_cast<QWidget *>(c))
			w->deleteLater();
	}
	_sections.clear();

	int const gap = 4;

	/* For each app, append to (or create) its category section. */
	QVector<LauncherController::App> const &apps = _controller.apps();
	for (LauncherController::App const &a : apps) {
		if (!_sections.contains(a.category)) {
			auto *heading = new QLabel(a.category, this);
			heading->setStyleSheet(_category_stylesheet());
			heading->show();
			_root_layout->addWidget(heading);

			auto *entries_layout = new QVBoxLayout();
			entries_layout->setSpacing(gap);
			_root_layout->addLayout(entries_layout);

			_sections.insert(a.category, { heading, entries_layout });
		}

		Section &sec = _sections[a.category];
		QString const label = a.running
		    ? a.name + QStringLiteral(" \342\200\242")
		    : a.name;
		auto *entry = new QPushButton(label, this);
		entry->setStyleSheet(_entry_stylesheet());
		if (!a.description.isEmpty())
			entry->setToolTip(a.description);

		/*
		 * Click-to-launch (Phase 7 todo 10): the controller sends a
		 * `launch <name>` request to sponge_pkgd over the
		 * "launcher_request" channel (AGENTS.md §3.3 rule 5: same
		 * pkgd backend as `vct launch`). The menu closes so the
		 * launched window can take focus.
		 */
		connect(entry, &QPushButton::clicked, this, [this, a] {
			_controller.request_launch(a.name);
			hide();
		});

		entry->show();
		sec.entries_layout->addWidget(entry);
	}

	/* If no apps have launchers yet, show a placeholder line so the
	 * popup is not a confusingly-empty rectangle. */
	if (_sections.isEmpty()) {
		auto *empty = new QLabel(QStringLiteral("No apps installed yet"), this);
		empty->setAlignment(Qt::AlignCenter);
		empty->setStyleSheet(QStringLiteral(
			"QLabel { color: %1; padding: 12px; }")
			.arg(Theme::to_css(_theme.separator())));
		empty->show();
		_root_layout->addWidget(empty);
	}

	adjustSize();
}


void LauncherMenuView::_apply_style(Theme::Theme const &theme)
{
	QScreen *screen = QGuiApplication::primaryScreen();
	QRect const sg = screen ? screen->geometry() : QRect(0, 0, 1024, 768);

	int const menu_w = sg.width() / 3;
	/* Guard against the pre-paint phase when Qt hasn't yet been told
	 * the screen geometry by the QPA plugin: a negative max-height
	 * would be clipped to 0 and produce a useless popup. */
	int const menu_max_h = sg.height() > 64 ? sg.height() - 64 : 200;

	setMinimumWidth(menu_w);
	setMaximumWidth(menu_w);
	setMaximumHeight(menu_max_h);

	setStyleSheet(QStringLiteral(
		"QWidget#launcherRoot { background-color: %1; border: %3px solid %2; }")
		.arg(Theme::to_css(theme.panel_bg()),
		     Theme::to_css(theme.separator()),
		     QString::number(theme.border_width())));
	setObjectName(QStringLiteral("launcherRoot"));
}


QString LauncherMenuView::_category_stylesheet() const
{
	return QStringLiteral(
		"QLabel { color: %1; font-weight: bold; padding: 2px 0; }")
		.arg(Theme::to_css(_theme.panel_text()));
}


QString LauncherMenuView::_entry_stylesheet() const
{
	return QStringLiteral(
		"QPushButton { background-color: %1; color: %2; "
		"border: none; border-radius: %3px; padding: 6px 10px; "
		"text-align: left; }"
		"QPushButton:hover { background-color: %4; }"
		"QPushButton:pressed { background-color: %4; }")
		.arg(Theme::to_css(_theme.window_bg()),
		     Theme::to_css(_theme.title_text()),
		     QString::number(_theme.border_radius()),
		     Theme::to_css(_theme.accent()));
}
