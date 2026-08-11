/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE notification popover.
 *
 * The popover is a small themed card painted into the panel domain (run
 * scenario's domain config: `ypos: 0 | height: 96`). Geometry is
 * POPOVER_X/POPOVER_Y/POPOVER_W/POPOVER_H — those constants are the
 * SAME values the W4 notify_probe Capture-polls (see
 * test/notify_probe/main.cc), so the popover must paint exactly inside
 * the probed rect for the open/close TTL transition to be detectable.
 *
 * Rendering decisions:
 *   - The card has a solid background (theme.panel_bg) and accent
 *     border (theme.accent). The accent is the only color that
 *     reliably differs from the nitpicker background (#1e1e2e), so the
 *     probe's non-bg fraction threshold of 300/1000 is met by the
 *     background alone plus the accent border + glyphs.
 *   - The first entry's title is rendered in the top half; the body
 *     (if any) is rendered in the bottom half. Subsequent entries are
 *     stacked truncated (max 3 entries; the panel domain is only 96
 *     px tall, so anything more would clip).
 *   - Click anywhere on the popover dismisses it. Dismissal is sticky
 *     until the next non-empty broadcast (a new notification arrival
 *     re-shows the popover).
 *
 * Threading:
 *   The paint event runs on the GUI thread; the controller marshals
 *   updates via QMetaObject::invokeMethod. The widget never touches
 *   Genode::Env directly.
 */

#include "notifier_widget.h"

#include <base/log.h>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

using namespace Sponge::Sponge_DE;


NotifierWidget::NotifierWidget(Theme::Theme const &theme, QWidget *parent)
:
	QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
	/*
	 * The window title is the Genode QPA Gui session label; the run
	 * script routes "Sponge Notification Popover" into the panel
	 * domain so the popover docks at the panel's bottom band.
	 */
	setWindowTitle(QStringLiteral("Sponge Notification Popover"));

	_apply_style(theme);
	_apply_geometry();

	/* Hide at construction (no notifications yet). */
	hide();
}


void NotifierWidget::_apply_style(Theme::Theme const &theme)
{
	/*
	 * The CSS stylesheet drives the chrome (background, border, label
	 * colors). The accent border is the primary visual marker the
	 * probe's Capture check detects when the popover opens.
	 */
	QString const css = QStringLiteral(
		"QWidget { background-color: %1; color: %2; border: 2px solid %3; }")
		.arg(Theme::to_css(theme.panel_bg()),
		     Theme::to_css(theme.panel_text()),
		     Theme::to_css(theme.accent()));

	if (css != _applied_css) {
		setStyleSheet(css);
		_applied_css = css;
	}
}


void NotifierWidget::_apply_geometry()
{
	/*
	 * POPOVER_X/Y/W/H are the contract with the W4 notify_probe. The
	 * panel domain constrains the window to the panel area's content
	 * band (y >= 28), so the popover is docked just below the panel
	 * bar. Identity-check before setGeometry mirrors the panel widget
	 * W2 fix (the Genode QPA re-allocates the framebuffer on every
	 * setGeometry, even no-op).
	 */
	QRect const target(POPOVER_X, POPOVER_Y, POPOVER_W, POPOVER_H);
	if (geometry() != target) {
		setGeometry(target);
		setFixedSize(target.size());
	}
}


void NotifierWidget::applyEntries(QStringList entries)
{
	_entries = std::move(entries);

	if (_entries.isEmpty()) {
		hide();
		return;
	}

	/* A new notification arrived, so a sticky click-dismiss is cleared
	 * (the user-relevant event is the new arrival, not the previous
	 * dismiss). */
	_dismissed_by_click = false;

	show();
	raise();

	/*
	 * Repaint — the Capture probe samples the popover rect, so the
	 * pixmap must reflect the new entries within the same GUI tick.
	 */
	update();
}


void NotifierWidget::set_visible(bool visible)
{
	if (visible) {
		_dismissed_by_click = false;
		show();
		raise();
	} else {
		hide();
	}
}


void NotifierWidget::restyle(Theme::Theme const &theme)
{
	_apply_style(theme);
	_apply_geometry();
	update();
}


void NotifierWidget::paintEvent(QPaintEvent * /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);

	QFont small = p.font();
	small.setPointSizeF(small.pointSizeF() * 0.9);
	p.setFont(small);

	/*
	 * Layout: N rows stacked top-to-bottom. Each row is the title
	 * (bold) plus an optional body. With 60 px height and a 3-entry
	 * cap, each row gets ~20 px. We only show the first entry's
	 * body; the rest are title-only summaries.
	 */
	int const row_height = POPOVER_H / MAX_ENTRIES;
	QFontMetrics const fm(p.font());

	int y = 4;
	for (int i = 0; i < _entries.size() && i < MAX_ENTRIES; ++i) {
		QString const &line = _entries.at(i);
		/*
		 * The controller emits a single-line "title" string per
		 * entry (the body is concatenated with " — " separator and
		 * truncated, so the widget never has to think about XML).
		 */
		QString const text = fm.elidedText(line, Qt::ElideRight,
		                                  POPOVER_W - 8);
		p.drawText(QRect(4, y, POPOVER_W - 8, row_height - 2),
		           Qt::AlignLeft | Qt::AlignTop, text);
		y += row_height;
	}
}


void NotifierWidget::mousePressEvent(QMouseEvent * /*event*/)
{
	/*
	 * Click anywhere on the popover dismisses it. The sticky
	 * dismissal is cleared by the next applyEntries when the
	 * notification list becomes non-empty again (a new notification
	 * re-shows the popover). The TTL also triggers a hide via
	 * applyEntries(empty) on the controller side.
	 */
	_dismissed_by_click = true;
	hide();
}


void NotifierWidget::clear_dismissed()
{
	_dismissed_by_click = false;
}
