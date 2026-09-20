/* SPDX-License-Identifier: Apache-2.0
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
 *
 * Phase 16 W5 (U16.2 / D16.2): the panel context menu (right-click
 * → QMenu with Height spinbox, Visible widgets checkboxes, Position
 * radio group, Settings entry) lives in contextMenuEvent below. Each
 * control's value-change signal routes through SettingsController::
 * request_set, which keeps the wire contract identical to vct's
 * `vct config` path (D16.9 validator parity).
 *
 * The dual nitpicker panel-domain topology (panel_top + panel_bottom)
 * is driven by the ConfigController::panel_position_changed signal
 * → applyPosition slot. In singleton mode applyPosition is a no-op
 * (the single panel stays visible). In dual mode the matching-role
 * widget show()'s and the sibling hide()'s.
 *
 * Phase 16 W8 (U16.5 / D16.5): multi-panel generalization. Each
 * PanelWidget carries a per-instance id, a distinct Gui session
 * label suffix (the F5 nitpicker label_prefix trap defense), and
 * a unique per-panel click_count Expanding_reporter. The
 * contextMenuEvent emits per-id keys (panel.<id>.height, etc.)
 * when the controller is wired to a SettingsController.
 */

#include "panel_widget.h"

#include <base/log.h>
#include <os/reporter.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTime>
#include <QTimer>
#include <QWidgetAction>

#include "config/settings_controller.h"
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


PanelWidget::PanelWidget(Theme::Theme const &theme,
                             QString const &id,
                             QString const &window_title,
                             QWidget *parent)
:
	QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
	_id(id.isEmpty() ? default_id() : id),
	_label_suffix(window_title.isEmpty() ? QStringLiteral("Sponge Panel")
	                                     : window_title)
{
	/*
	 * The window title doubles as the Gui session label (the Genode QPA
	 * plugin labels each window's session with it), and the run scenario
	 * routes "sponge-de -> Sponge Panel" into nitpicker's "panel_top"
	 * domain and "sponge-de -> Sponge Panel Bottom" into "panel_bottom".
	 * The matching domain constrains the window to the top or bottom
	 * screen band and reports the matching size back to Qt, so the bar
	 * always ends up docked to the right edge regardless of what we set
	 * here.
	 *
	 * Singleton-mode regression scenarios pass "Sponge Panel" (the
	 * legacy title); the single existing route in their topology
	 * carries them to whatever single panel domain they wire.
	 * Dual-mode W5 scenarios pass "Sponge Panel" for the top widget
	 * and "Sponge Panel Bottom" for the bottom widget, which the run
	 * script's two label_last policies route to the matching domain.
	 *
	 * Phase 16 W8 (U16.5 / D16.5): the multi-panel scenario passes
	 * "Sponge Panel Alpha" / "Sponge Panel Beta" so the two Gui
	 * sessions route to their own nitpicker domains via the F5
	 * label_prefix defense. The PanelWidget id (_id) carries the
	 * same identity for the configd per-id namespace.
	 */
	setWindowTitle(_label_suffix);

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

/*
	 * W8: install an event filter on qApp so clicks that
	 * land on the launcher button (a CHILD widget that consumes the
	 * press — Qt does NOT propagate the press to the parent panel)
	 * still bump this panel's click_count. The filter observes every
	 * MouseButtonPress in the QApplication; we count presses whose
	 * target is this panel OR any descendant whose geometry overlaps
	 * this panel's window rect.
	 */
	qApp->installEventFilter(this);
	/*
	 * Also install on `this` (the panel widget itself) as a fallback —
	 * if the click reaches the panel widget's child chain first, this
	 * filter catches it before child dispatch.
	 */
	installEventFilter(this);

	/*
	 * W8: emit the initial click_count=0 report so the probe can gate
	 * on the reporter existing before dispatching QMP clicks. The
	 * reporter is constructed lazily on the first paint (rather than
	 * at PanelCollection construction time) because the PanelWidget
	 * itself is constructed before main.cc has a chance to wire the
	 * Report session — the lazy construct picks up the wired session
	 * on the first click. The initial paint path emits click_count=0
	 * so the probe's `_panel_click_count_is(... 0)` gate can verify
	 * the reporter exists without waiting for a click.
	 *
	 * The reporter's label is "panel_<id>" and its node type is
	 * "panel". The F5 trap defense: two same-id widgets would
	 * collide on the single-writer rule (AGENTS.md §1.2). Each
	 * PanelWidget instance MUST have a distinct id (the W8
	 * PanelCollection enforces this at panel-add time).
	 */
	_report_click();
}


PanelWidget::~PanelWidget()
{
	/*
	 * Phase 14 W11 #47: stop the 1 s clock timer explicitly before
	 * QObject's parent-child cleanup runs. The `new QTimer(this)`
	 * ownership would stop it on deleteChildren anyway, but the
	 * explicit stop closes the failure-mode where a queued timeout
	 * fires during destruction (the leak-audit regression).
	 */
	if (_clock_timer) {
		_clock_timer->stop();
		_clock_timer->deleteLater();
		_clock_timer = nullptr;
	}
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
	 * In dual mode (panel_top + panel_bottom), each widget's
	 * screen y coordinate is set to match its role so the two
	 * widgets have distinct view positions in the nitpicker
	 * scene graph. The nitpicker domain config clips each view
	 * to its pre-declared band (panel_top y=0..28 / panel_bottom
	 * y=740..768), but the distinct view positions avoid the
	 * QGenodePlatformWindow _adjust_and_set_geometry conflict
	 * where two views at the same screen rect fight over the
	 * same framebuffer allocation. Role::Singleton keeps the
	 * legacy single-domain (0, 0) position.
	 *
	 * NOTE: the constructor calls _apply_geometry with role
	 * still Singleton (default) because set_role is called
	 * AFTER the ctor returns. applyPosition() re-runs this
	 * method with the role set so the geometry snaps to the
	 * role-matched y on the first position broadcast.
	 */
	int const widget_y = (_role == Role::Bottom) ? 740 : 0;

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
	QRect const target(0, widget_y, width, h);
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
		/*
		 * Phase 16 W8 (U16.5 / D16.5): the per-panel click_count is
		 * incremented ONCE per physical click via the eventFilter
		 * (which catches the MouseButtonPress before any child
		 * widget consumes it). The launcher's clicked() signal also
		 * fires on every press-release, so an earlier version of this
		 * lambda bumped _click_count a SECOND time here — that double-
		 * counting made a single QMP click increment click_count by 2
		 * (eventFilter) + 1 (release signal), so the W8 probe never
		 * observed click_count=1.
		 *
		 * DO NOT add `_click_count++; _report_click();` here — the
		 * eventFilter is the single source of truth. This lambda is
		 * responsible ONLY for showing/hiding the launcher popup.
		 */
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
		/*
		 * Do NOT call widget->show() here. The widget is shown
		 * automatically when inserted into the panel's layout
		 * (the panel is already shown at this point). An explicit
		 * show() race-conditions with the panel's pending first
		 * paint and can delay the demo window's first paint, which
		 * breaks the Phase-7 sponge-de-test acceptance probe.
		 */
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
	 * Phase 14 W7 reserved the "tasklist" token in the panel-side
	 * parser but did NOT extend the configd validator — the write
	 * would have been rejected before the broadcast ever saw it
	 * (a latent F15-style bug). Phase 16 W2 #7 extends the
	 * validator to accept "tasklist" alongside {clock, launcher};
	 * the panel-side parse is unchanged. The default theme still
	 * ships "clock,launcher" (tasklist is opt-in via the panel
	 * context menu's checkbox in W5).
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


void PanelWidget::applyPosition(QString position)
{
	/*
	 * The position-vs-cache check skips identical writes (no-op
	 * restyle). Phase 16 W8 multi-panel mode requires a first-time
	 * geometry re-apply even when position matches the ctor
	 * default — the role has been set by the PanelCollection but
	 * the ctor's _apply_geometry ran with role=Singleton. We use
	 * the _position_default_seen flag to gate this: the FIRST
	 * applyPosition always runs (even if position matches the
	 * default "bottom"); subsequent applies use the dedup check.
	 */
	bool const first_apply = !_position_default_seen;
	if (position == _position && !first_apply) return;

	QString const prev = _position;
	_position = position;
	_position_default_seen = true;

	/*
	 * Phase 16 W8 (U16.5 / D16.5): the multi-panel mode sets the
	 * panel's role via set_role() before applyPosition. Each panel
	 * has its OWN role (Top / Bottom) — there is no sibling. The
	 * legacy W5 dual-mode relied on sibling-pointer toggling
	 * (one widget shows, the sibling hides); multi-panel mode
	 * relies on per-panel show/hide based on its own position
	 * match.
	 *
	 * Regression scenarios (panel_bottom_enabled = true) still use
	 * the W5 dual-mode: a panel_top widget (role Top) and a
	 * panel_bottom widget (role Bottom) connected via set_sibling.
	 * The sibling-based show/hide remains correct for that path.
	 *
	 * Multi-panel mode (W8): each panel has its own role. There
	 * is no sibling. The role-based show/hide is correct.
	 *
	 * Legacy default (single panel widget, role Singleton, no
	 * sibling): the no-op path is correct.
	 */
	bool const has_role = (_role == Role::Top || _role == Role::Bottom);
	if (_role == Role::Singleton && !has_role) {
		Genode::log("sponge-de: panel.position=", position.toUtf8().constData(),
		            " (singleton; position change only affects menu radio "
		            "selection, no show/hide)");
		return;
	}

	/*
	 * Re-apply geometry on the first broadcast so the widget's
	 * screen-y matches its role (the ctor's _apply_geometry runs
	 * before set_role is called, so it used the Singleton (0, 0)
	 * default).
	 */
	Theme::Theme const no_theme;
	_apply_geometry(no_theme);

	bool show_self = false;
	if (_role == Role::Top) {
		show_self = (position == QStringLiteral("top"));
	} else {
		show_self = (position == QStringLiteral("bottom")
		         || position == QStringLiteral("left")
		         || position == QStringLiteral("right"));
	}

	if (show_self) {
		show();
		raise();
	} else {
		hide();
	}

	Genode::log("sponge-de: panel.position=", position.toUtf8().constData(),
	            " role=", (_role == Role::Top ? "top" : "bottom"),
	            " show=", (show_self ? "yes" : "no"),
	            " prev=", prev.toUtf8().constData());
}


void PanelWidget::contextMenuEvent(QContextMenuEvent *e)
{
	if (!_settings) {
		/*
		 * Settings menu entry is gated on SettingsController
		 * (the de_config_request label must be wired in the run
		 * topology). Without it, fall back to the default menu
		 * (none today — the panel has no other context menu).
		 */
		Genode::warning("sponge-de: panel context menu requested but "
		                "SettingsController is not attached; the menu "
		                "is unavailable in this topology");
		QWidget::contextMenuEvent(e);
		return;
	}

	/*
	 * Anchor the menu to the event-local position. The Genode QPA
	 * returns (0,0) from QCursor::pos() (Phase 10+ lesson — the
	 * pointer-position ROM is not propagated to the QPA); without
	 * the event-local position the menu would always pop up at the
	 * origin regardless of where the user clicked.
	 */
	QPoint const anchor = e->globalPos();

	QMenu menu;
	/*
	 * W8: the menu title carries the per-panel identity so a user
	 * right-clicking panel "alpha" sees "Sponge Panel alpha" in the
	 * menu's title bar — the visible cue that the controls below
	 * edit alpha's keys, not the default panel's.
	 */
	menu.setTitle(QStringLiteral("Sponge Panel ") + _id);

	/*
	 * W8: per-id key prefix. The legacy flat panel.* keys still
	 * work for the default panel (the W2 validator only accepted
	 * them on the default id; the W2 instantiates pattern keys on
	 * first write of a per-id name). For the default panel we keep
	 * the flat key path; for explicit ids we emit per-id keys.
	 *
	 * Backward-compat note: the W2 `panel.ids` key bootstraps the
	 * pattern-key set; when `panel.ids=""` (the regression-
	 * scenario default), the only valid instance is the implicit
	 * "default" panel whose writes go through the flat panel.* keys.
	 * The W2 pattern-key scan instantiates a per-id entry on first
	 * write of `panel.<id>.height` (and similarly for the other
	 * suffixes), but the default id is special — its flat writes
	 * are mirrored to the per-id namespace for the broadcast fan-out.
	 *
	 * For multi-panel (panel.ids != ""), every menu emits per-id
	 * keys. The SettingsController::request_set is unaware of the
	 * id; it writes whatever key it gets.
	 */
	QString const k_height   = (QStringLiteral("panel.") + _id +
	                            QStringLiteral(".height"));
	QString const k_visible  = (QStringLiteral("panel.") + _id +
	                            QStringLiteral(".visible_widgets"));
	QString const k_position = (QStringLiteral("panel.") + _id +
	                            QStringLiteral(".position"));

	/*
	 * Section 1: Height spinbox (range 16..128, 4 px steps).
	 * QWidgetAction embeds a custom widget inside the menu; the
	 * spinbox emits valueChanged → we read the integer and forward
	 * to SettingsController via QMetaObject::invokeMethod with
	 * QueuedConnection (Phase 11 risk #2 — GUI-thread invariant).
	 */
	{
		auto *spin = new QSpinBox(&menu);
		spin->setRange(16, 128);
		spin->setSingleStep(4);
		spin->setValue((int)(_height > 0 ? _height : 28));
		spin->setSuffix(QStringLiteral(" px"));
		QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged),
		                 this, [this, k_height, spin](int v) {
			if (_settings) {
				QMetaObject::invokeMethod(_settings, "request_set",
				                          Qt::QueuedConnection,
				                          Q_ARG(QString, k_height),
				                          Q_ARG(QString, QString::number(v)));
			}
			(void)spin;
		});
		auto *height_action = new QWidgetAction(&menu);
		height_action->setDefaultWidget(spin);
		height_action->setText(QStringLiteral("Height"));
		menu.addAction(height_action);
	}

	menu.addSeparator();

	/*
	 * Section 2: Visible widgets checkboxes (clock / launcher /
	 * tasklist). Each toggle rebuilds the canonical comma-list and
	 * writes it through SettingsController. Validator parity:
	 * panel.<id>.visible_widgets accepts the same shape the panel
	 * has always parsed (enum-list of {clock, launcher, tasklist}).
	 */
	auto write_visible = [this, k_visible](QString list) {
		if (!_settings) return;
		QMetaObject::invokeMethod(_settings, "request_set",
		                          Qt::QueuedConnection,
		                          Q_ARG(QString, k_visible),
		                          Q_ARG(QString, list));
	};

	QStringList const current = _visible_widgets.split(QLatin1Char(','));
	auto is_on = [&current](QString const &tok) {
		for (QString const &t : current)
			if (t.trimmed() == tok) return true;
		return false;
	};

	auto make_cb = [&](QString const &label, QString const &token) {
		auto *a = menu.addAction(label);
		a->setCheckable(true);
		a->setChecked(is_on(token));
		return a;
	};

	QAction *cb_clock    = make_cb(QStringLiteral("Clock"),    QStringLiteral("clock"));
	QAction *cb_launcher = make_cb(QStringLiteral("Launcher"), QStringLiteral("launcher"));
	QAction *cb_tasklist = make_cb(QStringLiteral("Tasklist"), QStringLiteral("tasklist"));

	/*
	 * Single-shot writer: after every toggle we rebuild the canonical
	 * comma-list from the three checkboxes' current state and write
	 * it through SettingsController. This avoids the per-action lambda
	 * state-drift hazard (a per-action lambda would need to read
	 * sibling state, which is harder to keep consistent).
	 *
	 * The button's clicked() signal also bumps _click_count (the
	 * launcher is the most reliable click target in the panel; the
	 * eventFilter catches non-button clicks but Qt's button consumes
	 * its own click before the parent's mousePressEvent fires). The
	 * connection is per-instance so each panel's launcher increments
	 * THAT panel's counter, never another panel's.
	 */
	auto sync_visible = [cb_clock, cb_launcher, cb_tasklist, write_visible]() {
		QStringList list;
		if (cb_clock->isChecked())    list << QStringLiteral("clock");
		if (cb_launcher->isChecked()) list << QStringLiteral("launcher");
		if (cb_tasklist->isChecked()) list << QStringLiteral("tasklist");
		write_visible(list.join(QLatin1Char(',')));
	};
	QObject::connect(cb_clock,    &QAction::toggled, this, sync_visible);
	QObject::connect(cb_launcher, &QAction::toggled, this, sync_visible);
	QObject::connect(cb_tasklist, &QAction::toggled, this, sync_visible);

	menu.addSeparator();

	/*
	 * Section 3: Position radio group. top + bottom enabled, left +
	 * right disabled with "Phase 17+" tooltip per D16.2.
	 */
	auto *position_group = new QActionGroup(&menu);
	position_group->setExclusive(true);

	auto add_pos = [&](QString const &label, QString const &token, bool enabled) {
		auto *a = menu.addAction(label);
		a->setCheckable(true);
		a->setActionGroup(position_group);
		a->setChecked(_position == token);
		a->setEnabled(enabled);
		if (!enabled) {
			a->setToolTip(QStringLiteral("Phase 17+"));
		}
		QObject::connect(a, &QAction::triggered, this,
		                 [this, k_position, token, a]() {
			if (!a->isChecked()) return;
			if (!_settings) return;
			QMetaObject::invokeMethod(_settings, "request_set",
			                          Qt::QueuedConnection,
			                          Q_ARG(QString, k_position),
			                          Q_ARG(QString, token));
		});
		return a;
	};

	add_pos(QStringLiteral("Top"),    QStringLiteral("top"),    true);
	add_pos(QStringLiteral("Bottom"), QStringLiteral("bottom"), true);
	add_pos(QStringLiteral("Left"),   QStringLiteral("left"),   false);
	add_pos(QStringLiteral("Right"),  QStringLiteral("right"),  false);

	menu.addSeparator();

	/*
	 * Section 4: Settings entry — opens the §5.4 dialog via
	 * SettingsController. The dialog is lazily constructed on the
	 * first click and cached afterward.
	 */
	auto *settings_action = menu.addAction(QStringLiteral("Settings..."));
	QObject::connect(settings_action, &QAction::triggered, this, [this]() {
		if (!_settings) return;
		QMetaObject::invokeMethod(_settings, "open_settings_dialog",
		                          Qt::QueuedConnection);
	});

	/*
	 * exec() blocks until the user dismisses the menu. Anchored to
	 * the event-global position (the Genode QPA returns (0,0) from
	 * QCursor::pos(), so the event-local position is the only valid
	 * anchor — Phase 10+ lesson).
	 */
	menu.exec(anchor);
}


/*
 * Phase 16 W7 — close the active panel context menu if one is open.
 * The QMenu is stack-allocated inside contextMenuEvent (so the menu
 * object lives only during exec()); the only way to close it from
 * outside is via Qt's active-popup widget state. The Dismisser calls
 * this from its priority list; the slot is a no-op when no menu is
 * active (the common case — the user pressed Escape without an open
 * menu).
 *
 * Qt handles Escape for the active QMenu automatically (QMenu's
 * keyPressEvent closes on Qt::Key_Escape). This slot is the
 * belt-and-braces defensive path for races where the Escape key
 * propagation is delayed (e.g. focus is held by a non-Qt widget).
 */
void PanelWidget::close_active_menu()
{
	QWidget *active_popup = QApplication::activePopupWidget();
	if (active_popup) {
		Genode::log("sponge-de: panel close_active_menu (dismiss path)");
		active_popup->close();
	}
}


/*
 * Phase 16 W8 (U16.5 / D16.5): per-panel click_count report. The
 * Expanding_reporter's label is "panel_<id>" (F5 defense: distinct
 * per instance); the node type is "panel"; the body's only
 * attribute is click_count. The report carries
 *
 *   <panel id="<id>" click_count="N"/>
 *
 * and bumps on every mousePressEvent (QWidget's default routing
 * forwards to QApplication first, then to the widget).
 *
 * The reporter is owned externally (the W8 PanelCollection
 * constructs it and shares the pointer with the widget). If the
 * run scenario does not wire the per-panel Report (the legacy
 * regression scenarios), `_click_reporter` is nullptr and this
 * method is a silent no-op. This keeps the regression scenarios
 * byte-compatible: they keep the legacy single panel without
 * per-panel Report routes and without any F5 trap exposure.
 */
void PanelWidget::_report_click()
{
	if (!_click_reporter) return;

	_click_reporter->generate_xml([this](Genode::Xml_generator &g) {
		g.attribute("id",          _id.toUtf8().constData());
		g.attribute("click_count", (int)_click_count);
	});
}


/*
 * Phase 16 W8 — setter for the per-panel click_count reporter.
 * After setting the pointer, immediately emit the initial
 * click_count=0 report so the probe can gate on the reporter
 * existing (the F5 label_prefix trap defense: a freshly-
 * instantiated panel must report click_count=0 within the
 * same broadcast cycle as the panel.ids=alpha,beta write).
 */
void PanelWidget::set_click_reporter(Genode::Expanding_reporter *r)
{
	_click_reporter = r;
	_report_click();
}


/*
 * Phase 16 W8 mousePressEvent. The click_count is incremented
 * exclusively via the eventFilter (which catches the MouseButtonPress
 * before any child widget consumes it - deduplicated via
 * _last_click_ms). mousePressEvent no longer bumps the counter; the
 * launcher button's clicked() signal also no longer bumps it (see
 * _build_layout). The default QWidget::mousePressEvent is called so
 * child widgets still receive their click signals.
 */
void PanelWidget::mousePressEvent(QMouseEvent *e)
{
	QWidget::mousePressEvent(e);
}


/*
 * Phase 16 W8 eventFilter. Catches MouseButtonPress events on the
 * QApplication for clicks landing on the launcher button (a child
 * widget that CONSUMES the press). The filter is the W8 click
 * counter for the F5 cross-panel assertion — clicks on the launcher
 * button bypass mousePressEvent (Qt does not propagate from child to
 * parent), but the eventFilter sees the press before Qt dispatches it
 * to the button.
 *
 * The filter only counts presses whose target window is THIS panel's
 * Gui session (the global mouse position is in the panel's screen
 * rect); presses on other panels (panel_beta vs panel_alpha) are
 * ignored, so the F5 cross-panel assertion still catches the F5 trap.
 */
bool PanelWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		QRect const g  = geometry();
		QRect const gr(g.x(), g.y(), g.width(), g.height());
		if (gr.contains(me->globalPos())) {
			/*
			 * Phase 16 W8 (U16.5 / D16.5) dedup. A single physical
			 * click can produce multiple QEvent::MouseButtonPress
			 * events on the qApp filter chain (QPA dispatches one to
			 * the QWidgetWindow and another to the QPushButton child).
			 * Without dedup click_count jumps by >=2 per click and the
			 * W8 probe never sees click_count=1. The 200 ms window is
			 * well below the human double-click interval (~500 ms).
			 */
			static constexpr qint64 DEDUP_WINDOW_MS = 200;
			qint64 const now_ms = QDateTime::currentMSecsSinceEpoch();
			if (now_ms - _last_click_ms >= DEDUP_WINDOW_MS) {
				_last_click_ms = now_ms;
				_click_count++;
				_report_click();
			}
		}
	}
	return false;  /* never consume */
}