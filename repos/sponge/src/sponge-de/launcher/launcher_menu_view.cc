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
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
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
	 * Auto-close when the user clicks outside the menu. QApplication's
	 * focus-out covers alt-tab / click-into-another-window; we also
	 * catch our own app entries' clicks (below) to close on launch.
	 *
	 * Phase 10 W2 fix: the previous unconditional `hide()` on every
	 * focus change hid the popup the moment show()/raise()/
	 * activateWindow() ran, because the S button (panel.launcher)
	 * kept the input focus — it is NOT an ancestor of the popup, so
	 * the focusObjectChanged that fires on show() always saw an
	 * "outside" focus and immediately hid the popup. The user
	 * (and our QMP-driven clicker) could never click a launcher
	 * entry. The fix: debounce the hide() by FOCUS_HIDE_DEBOUNCE_MS
	 * so the focus settling that happens during show() is allowed
	 * to complete before the close decision. Any further focus
	 * change while the timer is armed resets it (the legitimate
	 * "click outside" case continues to fire because the user has
	 * to lift their hand and click somewhere, >150 ms later).
	 */
	_hide_timer = new QTimer(this);
	_hide_timer->setSingleShot(true);
	_hide_timer->setInterval(FOCUS_HIDE_DEBOUNCE_MS);
	connect(_hide_timer, &QTimer::timeout, this, [this] { hide(); });

	connect(qApp, &QApplication::focusObjectChanged, this,
	        [this](QObject *o) {
		if (!isVisible()) return;
		if (o == nullptr) return;
		if (this->isAncestorOf(qobject_cast<QWidget *>(o))) {
			_hide_timer->stop();
			return;
		}
		_hide_timer->start();
	});

	repopulate();
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
