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
#include <QPoint>
#include <QPushButton>
#include <QRect>
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
	 * Auto-close when the user clicks outside the menu. We install a
	 * 50ms timer that checks QCursor::pos() — if the global cursor
	 * position is outside the popup's screen rect (the launcher domain:
	 * x:0-341, y:28-508, panel-popup domain), we hide the popup.
	 *
	 * Phase 10 W2 fix (supersedes the earlier focus-out debounce
	 * and the qApp eventFilter attempt): neither focus-object-changed
	 * nor the QPA plugin's qApp eventFilter reliably fires MouseButton-
	 * Press for the QMP-driven PS/2 click path on this host — the
	 * QPA plugin dispatches Input::Relative_motion for pointer motion
	 * but the button press/release events are consumed before reaching
	 * the qApp event filter (verified empirically — see docs/evidence/
	 * task-2-phase10-interactive.md "Known issues"). A periodic
	 * cursor-position poll is the simplest reliable primitive: it reads
	 * the same global cursor state the QPA plugin uses internally, and
	 * the 50ms cadence is fast enough for a "click outside" to feel
	 * human-imperceptible but slow enough that the popup stays open
	 * during show()/raise()/activateWindow() (which takes <50ms on this
	 * host with the PS/2 closed-loop navigation).
	 *
	 * The grace period (2000ms after show) prevents the cursor at the
	 * S button (which is OUTSIDE the popup's domain — y:14 is above
	 * the popup's y:28) from immediately hiding the popup while the
	 * QMP-driven click chain runs. The full chain is:
	 *   S click (~1.2s dispatch) → first entry click (~1.2s dispatch)
	 *   = ~2.4s total. 2000ms covers the S click + the start of the
	 * entry click; the entry click moves the cursor INTO the popup
	 * domain (y:65) and the timer becomes a no-op.
	 */
	_outside_check_timer = new QTimer(this);
	_outside_check_timer->setInterval(50);
	connect(_outside_check_timer, &QTimer::timeout, this, [this] {
		if (!isVisible()) return;
		/* Grace period: don't check while the cursor is at the S
		 * button and the popup is just opening. 2000ms covers the
		 * full QMP click dispatch chain (S click + first entry click). */
		if (QDateTime::currentMSecsSinceEpoch() - _visible_since_ms < 2000)
			return;
		QPoint const g = QCursor::pos();
		QRect const r(0, 28, 341, 480);
		if (!r.contains(g)) {
			Genode::log("sponge-de: launcher cursor-outside at (",
			            g.x(), ",", g.y(), ") — hiding popup");
			hide();
		}
	});
	_outside_check_timer->start();

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
