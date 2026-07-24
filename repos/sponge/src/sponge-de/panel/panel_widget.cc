/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE panel.
 *
 * Colors, thickness, padding, and launcher width come from the loaded
 * theme — nothing visual is hardcoded here (AGENTS.md §3.4,
 * docs/10-theme-format.md). On-screen placement is owned by nitpicker:
 * the run scenario puts this window's session into a dedicated "panel"
 * domain, so the bar always docks to the top screen edge.
 *
 * The launcher button is a placeholder for the Phase 5 launcher: it
 * already accepts clicks (proving input reaches the widget through the
 * Gui session) but only logs that the backend is not wired yet
 * (AGENTS.md §5.3).
 */

#include "panel_widget.h"

#include <base/log.h>

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTime>
#include <QTimer>

#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

using namespace Sponge::Sponge_DE;


PanelWidget::PanelWidget(Theme::Theme const &theme, QWidget *parent)
:
	QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
	/*
	 * The window title doubles as the Gui session label (the Genode QPA
	 * plugin labels each window's session with it), and the run scenario
	 * routes "sponge-de -> Sponge Panel" into nitpicker's "panel" domain.
	 * The domain then constrains this window to the top screen band and
	 * reports the matching size back to Qt, so the bar always ends up at
	 * (0,0) with the theme's height regardless of what we set here.
	 */
	setWindowTitle(QStringLiteral("Sponge Panel"));

	_apply_style(theme);

	auto *layout = new QHBoxLayout(this);
	int const pad = (int)theme.padding();
	int const gap = (int)theme.margin();
	layout->setContentsMargins(pad, gap, pad, gap);
	layout->setSpacing(gap);

	/* Launcher entry point (placeholder backend). */
	auto *launcher = new QPushButton(QStringLiteral("S"), this);
	launcher->setFixedSize((int)theme.launcher_width(),
	                       (int)theme.panel_height() - 2 * gap);
	connect(launcher, &QPushButton::clicked, this, [] {
		Genode::log("sponge-de: launcher button clicked");
		Genode::warning("not implemented: launcher backend (Phase 5)");
	});
	layout->addWidget(launcher);

	auto *title = new QLabel(QStringLiteral("Sponge DE"), this);
	layout->addWidget(title);

	layout->addStretch();

	/* Clock: text set once at start, then refreshed every minute tick. */
	_clock_label = new QLabel(this);
	_clock_timer = new QTimer(this);
	connect(_clock_timer, &QTimer::timeout, this, [this] {
		_clock_label->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
	});
	_clock_timer->start(1000);
	_clock_label->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
	layout->addWidget(_clock_label);
}


void PanelWidget::restyle(Theme::Theme const &theme)
{
	_apply_style(theme);
	update();
}


void PanelWidget::_apply_style(Theme::Theme const &theme)
{
	QScreen *screen = QGuiApplication::primaryScreen();
	int const width = screen ? screen->geometry().width() : 1024;
	setGeometry(0, 0, width, (int)theme.panel_height());
	setFixedSize(width, (int)theme.panel_height());

	setStyleSheet(QStringLiteral(
		"QWidget { background-color: %1; color: %2; border: none; }"
		"QPushButton { background-color: %3; color: %1; border-radius: 4px; }"
		"QPushButton:pressed { background-color: %2; color: %1; }")
		.arg(Theme::to_css(theme.panel_bg()),
		     Theme::to_css(theme.panel_text()),
		     Theme::to_css(theme.accent())));
}
