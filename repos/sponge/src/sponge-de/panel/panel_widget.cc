/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE panel.
 *
 * Colors, thickness, padding, and launcher width come from the loaded
 * theme AND the latest configd broadcast (panel.height,
 * panel.visible_widgets, clock.format) — nothing visual is hardcoded
 * here (AGENTS.md §3.4, docs/10-theme-format.md). On-screen placement
 * is owned by nitpicker: the run scenario puts this window's session
 * into a dedicated "panel" domain, so the bar always docks to the top
 * screen edge.
 *
 * The launcher button opens / hides the popup; the popup itself is
 * owned by LauncherMenuView.
 *
 * Phase 11 W2: layout / launcher-toggle-size / clock-format / height
 * are now live-reloadable. The ConfigController signal handlers
 * (applyHeight / applyVisibleWidgets / applyClockFormat) update
 * instance state; restyle() reads the same instance state so a theme
 * reload preserves a live configd-set value (failure-point 3).
 */

#include "panel_widget.h"

#include <base/log.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QTime>
#include <QTimer>

#include "launcher/launcher_menu_view.h"
#include "tasklist_widget.h"
#include "theme/theme_loader.h"
#include "theme/theme_qt.h"

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Qt-side SEMANTIC fallback for an invalid clock.format string. The
 * configd FormatString validator accepts up to 64 printable ASCII
 * characters but cannot reason about Qt's QDateTime format syntax
 * (escape sequences, literal-text quoting). When the validator accepts
 * a format that turns out to be garbage at the panel side, we fall
 * back to "HH:mm" here. See Phase 11 plan W2 §4: SEMANTIC fallback in
 * the Qt side of the boundary, structural validation in the
 * configd side.
 *
 * Returns the (possibly-replaced) format string. The boolean `&used`
 * is set to true when a fallback was applied.
 */
QString semantic_format_fallback(QString const &format, bool *used = nullptr)
{
	if (used) *used = false;
	if (format.isEmpty()) {
		if (used) *used = true;
		return QStringLiteral("HH:mm");
	}
	/*
	 * Probe with the current time; if the result is empty, the format
	 * has no Qt time-field specifiers at all (e.g. "bogus" → "").
	 * Drop it.
	 */
	QString const probe = QDateTime::currentDateTime().toString(format);
	if (probe.isEmpty()) {
		if (used) *used = true;
		return QStringLiteral("HH:mm");
	}
	return format;
}

}  /* namespace */


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

	/* First paint: theme-derived geometry + ctor-time default layout. */
	_apply_geometry(theme);
	_build_layout(theme);

	/*
	 * Clock timer: 1s tick keeps the displayed time fresh. The actual
	 * format is owned by _clock_format (configd-overridable); the
	 * initial value is the theme/conventional "HH:mm" until
	 * ConfigController emits clock_format_changed.
	 */
	_clock_timer = new QTimer(this);
	connect(_clock_timer, &QTimer::timeout, this, [this] {
		_refresh_clock_text();
	});
	_clock_timer->start(1000);
	_refresh_clock_text();
}


void PanelWidget::_apply_style(Theme::Theme const &theme)
{
	QString const css = QStringLiteral(
		"QWidget { background-color: %1; color: %2; border: none; }"
		"QPushButton { background-color: %3; color: %1; border-radius: 4px; }"
		"QPushButton:pressed { background-color: %2; color: %1; }")
		.arg(Theme::to_css(theme.panel_bg()),
		     Theme::to_css(theme.panel_text()),
		     Theme::to_css(theme.accent()));

	/*
	 * Re-applying an identical stylesheet re-polishes the whole widget
	 * tree for no visual change; skip it. (On the Genode QPA each
	 * redundant top-level mutation also perturbs the paint/flush
	 * timing — see the alpha black-panel analysis in
	 * docs/evidence/task-6-phase11-alpha-flake.md.)
	 */
	if (css != _applied_css) {
		setStyleSheet(css);
		_applied_css = css;
	}
}


void PanelWidget::_apply_geometry(Theme::Theme const &theme)
{
	/*
	 * The Genode QPA reports a degenerate 1x1 screen geometry until
	 * nitpicker's panorama info arrives (QGenodeScreen ctor maps
	 * Gui::Undefined to Area{1,1}). Whether the info has arrived by
	 * the time the panel constructs is boot-timing dependent — a
	 * 1-px-wide panel shows as a black/unpainted band (the alpha
	 * flake documented in docs/evidence/task-6-phase11-alpha-flake.md).
	 * Never trust an implausible width; fall back to the scenario's
	 * reference width (the run scripts all use 1024).
	 */
	QScreen *screen = QGuiApplication::primaryScreen();
	int const screen_w = screen ? screen->geometry().width() : 0;
	int const width = screen_w > 64 ? screen_w : 1024;
	int const h     = _height > 0 ? (int)_height : (int)theme.panel_height();

	/*
	 * setGeometry/setFixedSize on the Genode QPA reach
	 * QGenodePlatformWindow::setGeometry -> _adjust_and_set_geometry,
	 * which re-allocates the Gui framebuffer session on EVERY call —
	 * even a no-op one. An unchanged-size restyle would therefore
	 * rotate the panel's buffer out from under nitpicker (the new
	 * dataspace is zero-filled), which produced the flaky black panel
	 * band in run/sponge-alpha.run on base-sel4. Only touch the
	 * window geometry when it actually changes.
	 */
	QRect const target(0, 0, width, h);
	if (geometry() != target) {
		setGeometry(target);
		setFixedSize(target.size());
	}
}


void PanelWidget::_build_layout(Theme::Theme const &theme)
{
	/*
	 * The horizontal box: [launcher toggle] [title] [tasklist] [stretch] [clock].
	 * _apply_layout re-applies margins/spacing/sizes without
	 * recreating the children.
	 *
	 * Phase 14 W7: the tasklist is inserted between the title label
	 * and the stretch zone. The tasklist absorbs the slack so the
	 * clock stays right-aligned. The widget is attached later via
	 * attach_tasklist(); the layout slot is reserved here so the
	 * stretch behaviour is correct from the first paint.
	 */
	auto *layout = new QHBoxLayout(this);
	int const pad = (int)theme.padding();
	int const gap = (int)theme.margin();
	layout->setContentsMargins(pad, gap, pad, gap);
	layout->setSpacing(gap);

	/* Launcher button: toggles the popup. */
	_launcher_toggle = new QPushButton(QStringLiteral("S"), this);
	_launcher_toggle->setObjectName(QStringLiteral("launcherToggle"));
	connect(_launcher_toggle, &QPushButton::clicked, this, [this] {
		if (_launcher_view) {
			if (_launcher_view->isVisible()) {
				_launcher_view->hide();
				return;
			}
			/* Refresh on open: a pkgd update may have landed between opens. */
			_launcher_view->repopulate();
			_launcher_view->show();
			_launcher_view->raise();
			_launcher_view->activateWindow();
		} else {
			Genode::warning("not implemented: launcher view not attached");
		}
	});

	_title_label = new QLabel(QStringLiteral("Sponge DE"), this);

	_clock_label = new QLabel(this);

	_apply_layout(theme);
	_apply_visibility();

	layout->addWidget(_launcher_toggle);
	layout->addWidget(_title_label);
	layout->addStretch();  /* The tasklist absorbs the slack; the
	                          stretch ensures the clock stays at
	                          the right edge even when the tasklist
	                          is empty. */
	layout->addWidget(_clock_label);
}


void PanelWidget::attach_tasklist(TasklistWidget *widget)
{
	_tasklist_widget = widget;

	if (!widget) return;

	if (auto *layout = qobject_cast<QHBoxLayout *>(this->layout())) {
		widget->setParent(this);
		layout->insertWidget(2, widget);
		widget->setMinimumHeight(_height > 0 ? (int)_height : 28);
		widget->show();
	}
}


void PanelWidget::_apply_layout(Theme::Theme const &theme)
{
	/*
	 * Re-apply launcher-button size (panel_height - 2*margin) and the
	 * panel layout margins/spacing. Per panel_widget.cc:61-62 (the
	 * pre-W2 source), the toggle height tracks panel_height - 2*gap —
	 * so growing the panel visibly grows the toggle rect (this is the
	 * P1 subphase assertion target in the panel-config probe).
	 *
	 * Look up the root layout via the QWidget's layout() accessor —
	 * there is only ever one top-level QHBoxLayout, attached in
	 * _build_layout().
	 */
	int const pad = (int)theme.padding();
	int const gap = (int)theme.margin();
	int const h   = _height > 0 ? (int)_height : (int)theme.panel_height();

	if (auto *layout = qobject_cast<QHBoxLayout *>(this->layout())) {
		layout->setContentsMargins(pad, gap, pad, gap);
		layout->setSpacing(gap);
	}

	if (_launcher_toggle)
		_launcher_toggle->setFixedSize((int)theme.launcher_width(),
		                               h - 2 * gap);
}


void PanelWidget::_apply_visibility()
{
	/*
	 * Parse the cached list on ',' and hide the widgets whose token
	 * is absent. Order-insensitive, whitespace-trimmed.
	 *
	 * Default list ("clock,launcher") keeps both visible; setting
	 * "launcher" hides the toggle, setting "clock" hides the label,
	 * setting "tasklist" hides the tasklist widget. An empty list
	 * hides everything (validator rejects empty lists at the
	 * configd side, so this is a defensive default).
	 *
	 * Phase 14 W7: the tasklist visibility token is added; the
	 * validator sponge_configd accepts "tasklist" as a valid
	 * token. The default theme ships "clock,launcher,tasklist" so
	 * the tasklist is visible by default.
	 */
	bool show_launcher { false };
	bool show_clock    { false };
	bool show_tasklist { false };

	QStringList tokens = _visible_widgets.split(QLatin1Char(','));
	for (QString &t : tokens) {
		QString const tok = t.trimmed();
		if      (tok == QLatin1String("launcher")) show_launcher = true;
		else if (tok == QLatin1String("clock"))    show_clock    = true;
		else if (tok == QLatin1String("tasklist")) show_tasklist = true;
	}

	if (_launcher_toggle)
		_launcher_toggle->setVisible(show_launcher);
	if (_title_label)
		_title_label->setVisible(show_launcher || show_clock || show_tasklist);
	if (_clock_label)
		_clock_label->setVisible(show_clock);
	if (_tasklist_widget)
		_tasklist_widget->setVisible(show_tasklist);
}


void PanelWidget::_apply_clock_format(Theme::Theme const & /*theme*/)
{
	bool replaced = false;
	QString const format = semantic_format_fallback(_clock_format, &replaced);

	if (replaced && _clock_format != _warned_format) {
		Genode::warning("sponge-de: clock.format: invalid '",
		                _clock_format.toUtf8().constData(),
		                "', falling back to HH:mm");
		_warned_format = _clock_format;
	}

	/*
	 * Re-apply the font/colors via the style sheet is unnecessary —
	 * the format string only affects text content, and _apply_style
	 * has already set up the colors. We just refresh the displayed
	 * text now.
	 */
	_refresh_clock_text();
}


void PanelWidget::_refresh_clock_text()
{
	if (!_clock_label) return;

	QString const format = semantic_format_fallback(_clock_format);
	_clock_label->setText(QTime::currentTime().toString(format));
}


void PanelWidget::restyle(Theme::Theme const &theme)
{
	/*
	 * Order matters: style (sheet + bg), geometry (window rect),
	 * layout (margins/spacing/launcher size — uses _height if set),
	 * visibility (per the latest visible_widgets), clock format (per
	 * the latest _clock_format), then update() repaints.
	 *
	 * Phase 14 W7: the tasklist widget is restyled alongside the
	 * panel so its colors track the active theme.
	 */
	_apply_style(theme);
	_apply_geometry(theme);
	_apply_layout(theme);
	_apply_visibility();
	_apply_clock_format(theme);
	if (_tasklist_widget)
		_tasklist_widget->restyle(theme);
	update();
}


/* ============================================================
 * apply* slots — GUI thread ONLY.
 *
 * These are connected (by ConfigController::attach_panel) to the
 * panel_height_changed / panel_visible_widgets_changed /
 * clock_format_changed signals. The ConfigController emits them
 * from applyConfig() which runs on the GUI thread (marshalled by
 * QMetaObject::invokeMethod from the ROM signal handler). Qt
 * dispatches signal-connected slots on the emitting thread by
 * default, so the slot body here is guaranteed GUI-thread.
 *
 * Failure-point 2 enforcement: NEVER call these from a non-GUI
 * thread; the underlying QWidget mutations would be undefined
 * behavior.
 * ============================================================ */

void PanelWidget::applyHeight(unsigned h)
{
	if (h == 0 || h == _height) return;

	_height = h;

	Theme::Theme empty;  /* unused: restyle is called from the caller */

	/*
	 * Repaint at the new height. We DO NOT call restyle() because the
	 * theme didn't change — only the configd-driven height override.
	 * applyStyle is cheap, applyLayout reuses the cached height.
	 */
	_apply_geometry(empty);
	_apply_layout(empty);
	update();
}


void PanelWidget::applyVisibleWidgets(QString list)
{
	if (list == _visible_widgets) return;
	_visible_widgets = list;
	_apply_visibility();

	/*
	 * Force a repaint — setVisible(false) on a child does not always
	 * trigger an immediate paint on the parent QWidget (Qt coalesces
	 * update() calls during the same event-loop iteration). Without
	 * the explicit update() the panel may keep showing the toggle's
	 * accent background for one frame, which the panel-config probe
	 * sees as "toggle still rendered" in subphase P4.
	 */
	update();
}


void PanelWidget::applyClockFormat(QString format)
{
	if (format == _clock_format) return;
	_clock_format = format;
	_apply_clock_format(Theme::Theme{});
}