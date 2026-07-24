/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of Sponge_DE::Main — the Phase 3 demo window.
 *
 * Draws a themed window on top of nitpicker, reports the theme values
 * in use (so the visual check doubles as a theme-pipeline check), and
 * provides a button whose clicks are logged — proving that keyboard /
 * mouse input reaches the widget through the Gui session's input
 * channel (Phase 3 completion criterion 3).
 */

#include "sponge_de_main.h"

#include <base/log.h>
#include <util/xml_generator.h>
#include <sponge/version.h>

#include <cstdio>

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

using namespace Sponge;
using namespace Sponge::Sponge_DE;

namespace {

char const *position_name(Theme::Theme::PanelPosition position)
{
	using Position = Theme::Theme::PanelPosition;
	switch (position) {
	case Position::TOP:    return "top";
	case Position::BOTTOM: return "bottom";
	case Position::LEFT:   return "left";
	case Position::RIGHT:  return "right";
	}
	return "unknown";
}

} /* namespace */


Main::Main(Genode::Env &env, Theme::Theme const &theme, QWidget *parent)
:
	QWidget(parent),
	_env(env),
	_input_report(_env, "input")
{
	setWindowTitle("Sponge DE Demo");

	_input_report.enabled(true);

	/*
	 * Observe input at the application level so presses reaching any
	 * child widget (the demo button included) are surfaced as a report.
	 */
	qApp->installEventFilter(this);

	/*
	 * Placement is owned by nitpicker: the run scenario routes this
	 * window's session ("sponge-de -> Sponge DE Demo") into the "demo"
	 * domain, whose xpos/ypos/width/height pin it to a fixed floating
	 * rectangle. Qt coordinates are domain-relative, so (0,0) lands the
	 * window exactly on the domain's screen position.
	 */
	setGeometry(0, 0, 640, 480);

	/* Styling entirely from the theme (AGENTS.md §3.4). */
	setStyleSheet(QStringLiteral(
		"QWidget { background-color: %1; color: %2; }"
		"QPushButton { background-color: %3; color: %1; border-radius: 6px; padding: 6px 12px; }"
		"QPushButton:pressed { background-color: %2; color: %1; }")
		.arg(Theme::to_css(theme.window_bg()),
		     Theme::to_css(theme.title_text()),
		     Theme::to_css(theme.accent())));

	auto *layout = new QVBoxLayout(this);

	auto *title = new QLabel("Sponge DE", this);
	title->setAlignment(Qt::AlignCenter);
	auto font = title->font();
	font.setPointSize((int)theme.title_font().size + 12);
	font.setBold(true);
	title->setFont(font);

	auto *subtitle = new QLabel("Panel + window on nitpicker (Phase 3)", this);
	subtitle->setAlignment(Qt::AlignCenter);

	auto *theme_info = new QLabel(
		QStringLiteral("theme: format v%1, panel %2/%3px")
			.arg(theme.format_version())
			.arg(position_name(theme.panel_position()))
			.arg(theme.panel_height()),
		this);
	theme_info->setAlignment(Qt::AlignCenter);

	auto *input_check = new QPushButton("Input check — click me", this);
	connect(input_check, &QPushButton::clicked, this, [] {
		Genode::log("sponge-de: input event received (button clicked)");
	});

	layout->addStretch();
	layout->addWidget(title);
	layout->addWidget(subtitle);
	layout->addWidget(theme_info);
	layout->addWidget(input_check, 0, Qt::AlignCenter);
	layout->addStretch();

	Genode::log("Sponge DE window created (Sponge OS ",
	            Sponge::VERSION_STRING, " / ", Sponge::CODENAME, ")");
}


void Main::_report_press(QPoint pos)
{
	char buf[32];
	int const n = snprintf(buf, sizeof(buf), "%d,%d",
	                       pos.x(), pos.y());

	(void)_input_report.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("press", buf, (Genode::size_t)(n > 0 ? n : 0));
	});
}


void Main::mousePressEvent(QMouseEvent *e)
{
	_report_press(e->pos());
	QWidget::mousePressEvent(e);
}


bool Main::eventFilter(QObject * /*watched*/, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		_report_press(me->pos());
	}
	return false;  /* never consume — only observe */
}
