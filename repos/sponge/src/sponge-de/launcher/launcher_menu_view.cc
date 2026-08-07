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


namespace {

/*
 * Local comparator used when sort_by == "alpha". Keeps the standard
 * QString::compare semantics (locale-independent codepoint order)
 * so the panel-config probe can replicate the assertion in pure
 * standard-library code if it needs to.
 */
bool less_than_alpha(QString const &a, QString const &b)
{
	return a.compare(b, Qt::CaseInsensitive) < 0;
}

}  /* namespace */


bool Sponge::Sponge_DE::launcher_alpha_less_than(LauncherController::App const &a,
                                                 LauncherController::App const &b)
{
	return less_than_alpha(a.name, b.name);
}


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

	_apply_layout(theme);
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
	_apply_layout(theme);
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

	/*
	 * Phase 11 W2: honor launcher.sort_by.
	 *
	 *   "manual" (default if configd is OFF) — preserve the pkgd
	 *     "installed" broadcast order verbatim. pkgd's broadcast is
	 *     already name-sorted (sponge_pkgd/main.cc:1329-1376), so
	 *     for a single installed set this matches alpha.
	 *
	 *   "alpha" — re-sort the apps by name before insertion. This
	 *     gives a stable order even when pkgd's order changes (e.g.
	 *     a future pkgd optimization, or a probe writing a manual
	 *     install order); the comparator is exported as
	 *     launcher_alpha_less_than for assertion by the panel-config
	 *     probe.
	 */
	QVector<LauncherController::App> apps = _controller.apps();

	if (_sort_by == QLatin1String("alpha")) {
		std::stable_sort(apps.begin(), apps.end(),
		                 [](LauncherController::App const &a,
		                    LauncherController::App const &b) {
			                 return less_than_alpha(a.name, b.name);
		                 });
	}
	/* _sort_by == "manual" or empty: leave the pkgd order untouched. */

	/* For each app, append to (or create) its category section. */
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
	QRect sg = screen ? screen->geometry() : QRect(0, 0, 1024, 768);
	/* The Genode QPA reports a degenerate 1x1 screen until nitpicker's
	 * panorama info arrives; never trust an implausible size (see
	 * panel_widget.cc::_apply_geometry). */
	if (sg.width() < 64 || sg.height() < 64)
		sg = QRect(0, 0, 1024, 768);

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


void LauncherMenuView::_apply_layout(Theme::Theme const &theme)
{
	/*
	 * Phase 11 W2: margins/spacing now extracted into _apply_layout so
	 * restyle() can re-apply them after a theme reload (mirrors the
	 * PanelWidget::_apply_layout migration). The pre-W2 ctor-only
	 * code lives here verbatim.
	 */
	if (!_root_layout) return;

	int const pad = (int)theme.padding();
	int const gap = (int)theme.margin();
	_root_layout->setContentsMargins(pad, pad, pad, pad);
	_root_layout->setSpacing(gap);
}


QString LauncherMenuView::_category_stylesheet() const
{
	return QStringLiteral(
		"QLabel { color: %1; font-weight: bold; padding: 2px 0; }")
		.arg(Theme::to_css(_theme.panel_text()));
}


QString LauncherMenuView::_entry_stylesheet() const
{
	/* Vertical padding 16 px + min-height 50 px: bigger hit target
	 * (AGENTS.md §1.1 convenience — UX rationale in commit body). */
	return QStringLiteral(
		"QPushButton { background-color: %1; color: %2; "
		"border: none; border-radius: %3px; padding: 16px 12px; "
		"min-height: 50px; text-align: left; }"
		"QPushButton:hover { background-color: %4; }"
		"QPushButton:pressed { background-color: %4; }")
		.arg(Theme::to_css(_theme.window_bg()),
		     Theme::to_css(_theme.title_text()),
		     QString::number(_theme.border_radius()),
		     Theme::to_css(_theme.accent()));
}


/* ============================================================
 * applySortBy slot — GUI thread ONLY.
 *
 * Connected (by ConfigController::attach_launcher) to the
 * launcher_sort_by_changed signal. The ConfigController emits from
 * applyConfig() on the GUI thread; Qt dispatches signal-connected
 * slots on the emitting thread, so this slot is guaranteed
 * GUI-thread.
 *
 * Failure-point 2 enforcement: NEVER call this from a non-GUI
 * thread.
 * ============================================================ */

void LauncherMenuView::applySortBy(QString sort)
{
	/*
	 * Accept only the two registry-valid values ("alpha", "manual").
	 * The configd Enum validator rejects unknown tokens before
	 * broadcast, so defensive clamping here keeps a corrupted
	 * broadcast from scrambling the menu.
	 */
	if (sort != QLatin1String("alpha") && sort != QLatin1String("manual"))
		sort = QStringLiteral("alpha");

	if (sort == _sort_by) return;
	_sort_by = sort;

	/*
	 * Rebuild the menu with the new sort. If the popup is currently
	 * visible, the rebuild is visible immediately; if it is hidden,
	 * the rebuild happens lazily on the next show()/repopulate()
	 * call from the panel's launcher-button click.
	 */
	repopulate();
}