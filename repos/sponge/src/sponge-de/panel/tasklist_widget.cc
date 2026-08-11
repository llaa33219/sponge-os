/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE tasklist panel widget.
 *
 * The tasklist is a horizontal strip of fixed-width buttons (96 px each),
 * one per window known to wm. It lives inside the panel QHBoxLayout
 * (inserted between the title label and the stretch zone).
 *
 * Visual rendering — three states per entry:
 *   - Normal-Visible:        panel_bg background, accent border.
 *   - Normal-Visible-Focused: accent background, panel_text text.
 *   - Minimized:             separator background, dimmed label.
 *
 *   has_alpha: a 2 px accent strip on the left edge of the entry.
 *
 * Geometry — the widget owns no fixed width; the panel layout calculates
 * `width = max(0, parent_width - other_widgets_width)`. The widget
 * renders as many entries as fit at TASK_W each; overflow is silently
 * truncated (the panel width budget is bounded by the run script's
 * 1024 px screen reference).
 *
 * Click handling — mousePressEvent maps the click x to the entry index,
 * emits task_clicked with the entry's label. mouseDoubleClickEvent
 * emits task_toggle_maximized (the controller decides whether to use
 * the toggle path).
 *
 * Threading:
 *   All Qt APIs are GUI-thread-only. The controller marshals via
 *   QMetaObject::invokeMethod before emitting tasks_changed; the
 *   paintEvent runs on the GUI thread.
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
	/* The widget is a passive paint target inside the panel; no
	 * top-level window flags. The panel parent's layout controls the
	 * geometry. */
	_apply_style(theme);
	_apply_geometry(theme);
}


void TasklistWidget::_apply_style(Theme::Theme const &theme)
{
	/*
	 * The default stylesheet covers the entry body. The per-state
	 * colors (focused, minimized) are drawn in paintEvent because
	 * QSS state-:hover/:focus pseudo-classes don't react to our
	 * internal state model. The stylesheet handles the chrome
	 * (border, default background).
	 */
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
	/*
	 * The widget's height is the panel height; the layout controls
	 * the width. We set a fixed height and let the layout width
	 * remain unconstrained (the QHBoxLayout's stretch absorbs the
	 * slack; the widget's preferred size is TASK_W * N entries).
	 *
	 * The panel height is read from the active theme; the value is
	 * cached for paintEvent's vertical geometry.
	 */
	_panel_height = (int)theme.panel_height();
	/* Identity-check before setMinimumHeight / setMaximumHeight to
	 * avoid the QPA re-allocation bug. */
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

	/*
	 * The widget renders at the panel height. Each entry is TASK_W
	 * wide and (panel_height - 2*TASK_PAD) tall, with TASK_PAD
	 * pixels of vertical slack. The paint runs left-to-right.
	 *
	 * The theme colors are read from qApp's setProperty cache (set
	 * by the ThemeController on applyTheme) — see the notifier_widget
	 * pattern: the widget applies colors per-state in paintEvent
	 * because QSS state-pseudo-classes don't track our internal
	 * state. The QApplication cache is the simplest cross-widget
	 * transport for the active theme's palette.
	 */
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

		/*
		 * Background color per state.
		 *
		 * - Normal-Visible:        panel_bg, default text.
		 * - Normal-Visible-Focused: accent, panel_text (reversed).
		 * - Minimized:             separator, panel_text (dimmed).
		 */
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

		/* Draw the entry background. */
		p.fillRect(QRect(ex, y0, w, h), bg);

		/* Draw the has_alpha badge (2 px accent strip on the left). */
		if (t.has_alpha) {
			p.fillRect(QRect(ex, y0, 2, h), QColor(accent_str));
		}

		/*
		 * Draw the title text. The title is the layouter's
		 * <window title="..."> attribute (label + " " + Qt title).
		 * For the panel tasklist, the substring AFTER the last "->"
		 * is the user-meaningful app name. We elide to fit TASK_W - 4.
		 */
		QString const title = t.title;
		QString short_title = title;
		int const arrow = title.lastIndexOf("->");
		if (arrow >= 0)
			short_title = title.mid(arrow + 2).trimmed();
		/* Fall back to the title if no "->" is present (the sponge-de
		 * demo window's title is "Sponge DE Demo"). */
		if (short_title.isEmpty())
			short_title = title;

		QString const elided = fm.elidedText(short_title, Qt::ElideRight, w - 8);
		p.setPen(fg);
		p.drawText(QRect(ex + 4, y0, w - 8, h), Qt::AlignLeft | Qt::AlignVCenter, elided);
	}

	/* Draw a thin border around the focused entry (separates the
	 * "focused" visual from the underlying accent background). */
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
	/*
	 * Map the click x to an entry index. The click must land inside
	 * the entry rectangle (TASK_PAD < y < height - TASK_PAD).
	 */
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
