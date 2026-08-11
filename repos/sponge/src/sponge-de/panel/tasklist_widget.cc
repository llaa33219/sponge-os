/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE tasklist panel widget.
 */

#include "tasklist_widget.h"

#include <base/log.h>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

using namespace Sponge::Sponge_DE;


TasklistWidget::TasklistWidget(Theme::Theme const &theme, QWidget *parent)
:
	QWidget(parent),
	_panel_height { (int)theme.panel_height() }
{
	_apply_style(theme);
	_apply_geometry(theme);
}


void TasklistWidget::_apply_style(Theme::Theme const &theme)
{
	QString const css = QStringLiteral(
		"QWidget { background-color: %1; color: %2; border: none; }")
		.arg(Theme::to_css(theme.panel_bg()),
		     Theme::to_css(theme.panel_text()));

	if (css != _applied_css) {
		setStyleSheet(css);
		_applied_css = css;
	}
}


void TasklistWidget::_apply_geometry(Theme::Theme const &theme)
{
	_panel_height = (int)theme.panel_height();
	if (minimumHeight() != _panel_height)
		setMinimumHeight(_panel_height);
}


void TasklistWidget::restyle(Theme::Theme const &theme)
{
	_apply_style(theme);
	_apply_geometry(theme);
	update();
}


void TasklistWidget::applyEntries(QList<TaskInfo> entries)
{
	_entries = std::move(entries);
	update();
}


void TasklistWidget::paintEvent(QPaintEvent * /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);

	auto const cached = [](char const *key, char const *dflt) -> QString {
		QString const v = qApp->property(key).toString();
		return v.isEmpty() ? QString::fromUtf8(dflt) : v;
	};

	QString const panel_bg_str   = cached("theme_panel_bg",   "#1e1e2e");
	QString const panel_text_str = cached("theme_panel_text", "#cdd6f4");
	QString const accent_str     = cached("theme_accent",     "#89b4fa");
	QString const separator_str  = cached("theme_separator",  "#45475a");

	int const x0 = 0;
	int const y0 = TASK_PAD;
	int const h  = height() - 2 * TASK_PAD;
	int const w  = TASK_W;

	QFont const font = p.font();
	QFontMetrics const fm(font);

	for (int i = 0; i < _entries.size(); ++i) {
		TaskInfo const &t = _entries.at(i);
		int const ex = x0 + i * w;

		QColor bg;
		QColor fg;
		if (t.focused) {
			bg = QColor(accent_str);
			fg = QColor(panel_text_str);
		} else if (t.minimized) {
			bg = QColor(separator_str);
			fg = QColor(panel_text_str);
		} else {
			bg = QColor(panel_bg_str);
			fg = QColor(panel_text_str);
		}

		p.fillRect(QRect(ex, y0, w, h), bg);

		if (t.has_alpha) {
			p.fillRect(QRect(ex, y0, 2, h), QColor(accent_str));
		}

		QString const title = t.title;
		QString short_title = title;
		int const arrow = title.lastIndexOf("->");
		if (arrow >= 0)
			short_title = title.mid(arrow + 2).trimmed();
		if (short_title.isEmpty())
			short_title = title;

		QString const elided = fm.elidedText(short_title, Qt::ElideRight, w - 8);
		p.setPen(fg);
		p.drawText(QRect(ex + 4, y0, w - 8, h), Qt::AlignLeft | Qt::AlignVCenter, elided);
	}

	for (int i = 0; i < _entries.size(); ++i) {
		TaskInfo const &t = _entries.at(i);
		if (!t.focused) continue;
		int const ex = x0 + i * w;
		p.setPen(QPen(QColor(accent_str), 1));
		p.drawRect(QRect(ex + 1, y0 + 1, w - 2, h - 2));
	}
}


void TasklistWidget::mousePressEvent(QMouseEvent *event)
{
	int const x = event->x();
	int const y = event->y();
	if (y < TASK_PAD || y > height() - TASK_PAD) return;

	int const idx = x / TASK_W;
	if (idx < 0 || idx >= _entries.size()) return;

	emit task_clicked(_entries.at(idx).label);
}


void TasklistWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	int const x = event->x();
	int const y = event->y();
	if (y < TASK_PAD || y > height() - TASK_PAD) return;

	int const idx = x / TASK_W;
	if (idx < 0 || idx >= _entries.size()) return;

	emit task_toggle_maximized(_entries.at(idx).label);
}
