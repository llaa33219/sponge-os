/* SPDX-License-Identifier: Apache-2.0
 *
 * Entry point for the sponge-de component.
 *
 * Sponge DE is Sponge OS's desktop environment — a long-lived Genode
 * component that boots with the system and stays alive until shutdown.
 *
 * Qt on Genode requires the libc component model (Libc::Component::construct)
 * rather than the plain Component::construct used by non-Qt components like
 * vct. The QPA plugin that bridges Qt to Genode's Gui session is loaded by
 * qpa_init(), which must run before QApplication is constructed.
 *
 * Phase 5c: panel + launcher. The launcher button opens an app menu
 * populated from sponge_pkgd's rich `list` result (only packages that
 * declare a launcher category). The LauncherController polls pkgd on a
 * QTimer (same non-blocking pattern as ThemeController) and publishes a
 * `launcher` report for headless verification. Click-to-launch itself
 * is intentionally deferred (see launcher_menu_view.cc).
 *
 * Phase 14 W4: notifications. The NotifyPoster owns the "notif_request"
 * Report session and is the in-sponge-de writer of the notification
 * bus. NotifierController subscribes to the daemon's "notifications"
 * ROM and pushes the active list to NotifierWidget (the themed popover).
 * ThemeController / ConfigController / LauncherController each emit
 * event notifications through the poster on theme apply, config change,
 * and package install.
 */

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <libc/component.h>
#include <rom_session/connection.h>
#include <util/xml_node.h>
#include <util/string.h>

#include <QApplication>
#include <QTimer>
#include <QFont>

#include <qt6_component/qpa_init.h>

#include "background/background_controller.h"
#include "background/background_widget.h"
#include "config/config_controller.h"
#include "config/dismisser.h"
#include "config/settings_controller.h"
#include "config/shortcut_controller.h"
#include "launcher/launcher_controller.h"
#include "launcher/launcher_menu_view.h"
#include "panel/notifier_controller.h"
#include "panel/notifier_widget.h"
#include "panel/notify_poster.h"
#include "panel/panel_collection.h"
#include "panel/panel_widget.h"
#include "panel/tasklist_controller.h"
#include "panel/tasklist_widget.h"
#include "sponge_de_main.h"
#include "theme/theme_controller.h"
#include "theme/theme_loader.h"

using namespace Sponge::Sponge_DE;


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {

		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "sponge-de", nullptr };

		QApplication app(argc, const_cast<char **>(argv));

		/*
		 * ThemeController owns the live theme pipeline. In live mode it
		 * watches sponge_themed's "theme" report and re-styles the panel,
		 * demo window, and launcher in place when it changes; in fallback
		 * mode (no sponge_themed, e.g. run/sponge-de-test.run) it reads
		 * default.theme once. Either way, initial() is the theme used to
		 * construct the widgets below.
		 */
		ThemeController theme_ctrl(env);

		app.setFont(QFont(theme_ctrl.initial().default_font().family.string(),
		                  (int)theme_ctrl.initial().default_font().size));

		/*
		 * Phase 11 W2: ConfigController — bridges sponge_configd's
		 * `config` ROM to Qt signals for panel.height,
		 * panel.visible_widgets, clock.format, launcher.sort_by.
		 * Activation is gated by <config source="configd"/> in the
		 * component config (mirrors <theme source="themed"/>); in
		 * fallback mode the controller never opens a ROM session
		 * and never emits a signal (sponge-de boots unchanged).
		 *
		 * Constructed BEFORE the panel so the very first applyConfig()
		 * has a target to fan out to; the panel's apply* slots are
		 * private and connected by attach_panel() below.
		 */
		ConfigController config_ctrl(env);

		/*
		 * Phase 16 W4 (D16.1 / D16.9): SettingsController — the
		 * DE-side write bridge to sponge_configd on the dedicated
		 * `de_config_request` / `de_config_result` labels. Mirrors
		 * the read-side ConfigController above (constructed BEFORE
		 * the panel so the very first applyConfig() / first dialog
		 * write can fan out to the widgets / configd). The
		 * controller exposes `open_settings_dialog()` (D16.3),
		 * lazy-loaded on the first *Settings* click from the panel
		 * context menu (W5 wires the menu item; W4 only constructs
		 * the controller so the menu slot has a target).
		 *
		 * The activation gate `<de_config source="controller"/>`
		 * mirrors the read-side `<config source="configd"/>`; in
		 * fallback mode the controller does not open Report / ROM
		 * sessions and `open_settings_dialog()` is a no-op warning.
		 */
		SettingsController settings_ctrl(env);

		/*
		 * Phase 16 W7 (U16.4 / D16.5): ShortcutController — the
		 * keyboard-shortcut framework. Subscribes to the configd
		 * broadcast, parses `shortcuts.bindings`, dynamically
		 * configures event_filter's `<report>` source, and
		 * dispatches the closed actions
		 * (`launcher` / `focus_next` / `dismiss`) via
		 * QMetaObject::invokeMethod(Qt::QueuedConnection) on the
		 * GUI thread.
		 *
		 * The activation gate `<shortcuts source="controller"/>`
		 * mirrors the W4 <de_config source="controller"/> + W5
		 * <panel_bottom source="dual"/> + W6 <background source=
		 * "controller"/> pattern — absent the gate, no Shortcut
		 * Controller, no event_filter config writer, no shortcut
		 * action dispatch. Scenarios that do not opt in (Phase 10
		 * / Phase 11 / Phase 14 single-domain) keep their previous
		 * behavior.
		 */
		ShortcutController shortcut_ctrl(env);

		/* Notification daemon bridge (Phase 14 W4). */
		NotifyPoster       notify_poster(env);
		NotifierController notifier_ctrl(env);

		/* Tasklist controller (Phase 14 W7). Subscribes to wm's
		 * `window_list` and the layouter's `window_layout` reports;
		 * writes the `focus_request` and `rules` reports on click.
		 * The widget is a passive paint target inside the panel —
		 * it receives the tracked task list via the standard
		 * tasks_changed signal. */
		TasklistController tasklist_ctrl(env);

		/*
		 * Launcher data path. Constructed before the panel because the
		 * panel's launcher button shows/hides its popup. The view is
		 * constructed with the initial theme so the popup renders
		 * correctly even before the first pkgd poll completes.
		 */
		LauncherController launcher_ctrl(env);
		LauncherMenuView   launcher_view(launcher_ctrl, theme_ctrl.initial());
		launcher_ctrl.attach_view(&launcher_view);

		NotifierWidget notifier_popover(theme_ctrl.initial());
		notifier_ctrl.attach_widget(&notifier_popover);

		/*
		 * Phase 14 W7 tasklist activation gate (mirrors the
		 * <theme source="themed"/> / <config source="configd"/> and
		 * the W4 <notifier source="daemon"/> pattern): the tasklist
		 * is only wired when the component config carries
		 * <tasklist source="wm"/>. In topologies without the wm
		 * stack (run/sponge-de-test.run, run/sponge-launcher.run,
		 * ...) the controller's window_list/window_layout ROM
		 * sessions would have no producer and their first update()
		 * would block the GUI thread — gating here keeps the
		 * no-wm topologies untouched.
		 */
		bool tasklist_enabled { false };
		{
			Genode::Attached_rom_dataspace const config(env, "config");
			config.node().with_optional_sub_node("tasklist",
				[&] (Genode::Node const &n) {
					tasklist_enabled =
						n.attribute_value("source",
						                  Genode::String<16>())
						== "wm";
				});

			if (tasklist_enabled) {
				config.node().with_optional_sub_node("tasklist_static_rules",
					[&] (Genode::Node const &rules) {
						QString fragment;
						rules.for_each_sub_node("assign",
							[&] (Genode::Node const &a) {
								fragment += QStringLiteral("<assign");
								a.for_each_attribute(
									[&] (Genode::Node::Attribute const &attr) {
										QString const value = QString::fromUtf8(
											attr.value.start, (qsizetype)attr.value.num_bytes);
										fragment += QStringLiteral(" ");
										fragment += attr.name.string();
										fragment += QStringLiteral("=\"");
										fragment += value;
										fragment += QStringLiteral("\"");
									});
								fragment += QStringLiteral("/>");
							});
						if (!fragment.isEmpty())
							tasklist_ctrl.set_static_rules(fragment);
					});
			}
		}

		/*
		 * Phase 16 W5 (U16.2 / D16.2): dual nitpicker panel-domain
		 * topology. Two PanelWidget instances are constructed —
		 * `panel_top` and `panel_bottom` — and routed to the two
		 * pre-declared `<domain>` blocks (panel_top / panel_bottom)
		 * in the run scenario via distinct Gui session `label_last`
		 * suffixes ("Sponge Panel" / "Sponge Panel Bottom"). Both
		 * widgets subscribe to ConfigController's
		 * panel_position_changed signal; the matching-role widget
		 * show()'s, the other hide()'s.
		 *
		 * Gating: panel_bottom is constructed ONLY when the
		 * component config carries `<panel_bottom source="dual"/>`.
		 * Regression scenarios (sponge-de-sel4-interactive,
		 * sponge-panel-config, sponge-panel-config-sel4) wire
		 * only a single Gui session for "Sponge Panel" — the
		 * second "Sponge Panel Bottom" session they do not
		 * request, so constructing panel_bottom unconditionally
		 * would fatal-deny at show() time. The activation gate
		 * mirrors the existing `<config source="configd"/>` /
		 * `<de_config source="controller"/>` pattern — absent the
		 * gate, only the singleton panel_top widget is
		 * constructed, the dual-domain topology is bypassed, and
		 * the regression scenarios remain byte-compatible.
		 *
		 * Phase 16 W8 (U16.5 / D16.5): the new multi-panel
		 * scenario opts in via `<panels source="collection"/>`
		 * which constructs a PanelCollection instead. The
		 * collection owns the default panel + any per-id panels
		 * (panel.ids). The dual-mode and collection modes are
		 * mutually exclusive — only one activates per scenario.
		 */
		bool panels_collection_enabled { false };
		bool panel_bottom_enabled { false };
		{
			Genode::Attached_rom_dataspace const config(env, "config");
			config.node().with_optional_sub_node("panels",
				[&] (Genode::Node const &n) {
					panels_collection_enabled =
						n.attribute_value("source",
						                  Genode::String<16>())
						== "collection";
				});
			config.node().with_optional_sub_node("panel_bottom",
				[&] (Genode::Node const &n) {
					panel_bottom_enabled =
						n.attribute_value("source",
						                  Genode::String<16>())
						== "dual";
				});
		}

		/*
		 * Phase 16 W8 (U16.5 / D16.5): PanelCollection owns N
		 * PanelWidget instances keyed by id. Constructed on the
		 * Genode::Heap because its lifetime must outlive the
		 * lambda scope (the ConfigController + main_window +
		 * launcher view all hold pointers into the collection's
		 * widgets). The collection's destructor walks every entry
		 * and frees the widget + reporter.
		 *
		 * In the legacy regression scenarios (`<panels>` gate
		 * absent), the collection is NOT constructed. The W5/W7
		 * panel_top + panel_bottom dual-mode path stays as the
		 * regression backbone.
		 */
		static Genode::Heap panels_heap(env.ram(), env.rm());
		static Sponge::Sponge_DE::PanelCollection *panels_ptr { nullptr };
		if (panels_collection_enabled) {
			panels_ptr = new (&panels_heap)
				Sponge::Sponge_DE::PanelCollection(env, theme_ctrl.initial(),
				                                    &settings_ctrl);
		}

		Genode::Constructible<PanelWidget> panel_top_holder { };
		Genode::Constructible<PanelWidget> panel_bottom { };
		PanelWidget *panel_top { nullptr };
		if (!panels_collection_enabled) {
			panel_top_holder.construct(theme_ctrl.initial(),
			                          PanelWidget::default_id(),
			                          QStringLiteral("Sponge Panel"));
			panel_top = &*panel_top_holder;
			if (panel_bottom_enabled) {
				panel_bottom.construct(theme_ctrl.initial(),
				                       QStringLiteral("default"),
				                       QStringLiteral("Sponge Panel Bottom"));
				panel_top->set_role(PanelWidget::Role::Top);
				panel_bottom->set_role(PanelWidget::Role::Bottom);
				panel_top->set_sibling(&*panel_bottom);
				panel_bottom->set_sibling(panel_top);
			}
		}

		/*
		 * In legacy mode, wire panel_top to the settings controller.
		 * In collection mode, the collection already wired the
		 * default panel to the settings controller in its
		 * constructor.
		 */
		if (!panels_collection_enabled)
			panel_top->set_settings_controller(&settings_ctrl);
		if (!panels_collection_enabled && panel_bottom_enabled)
			panel_bottom->set_settings_controller(&settings_ctrl);

		/*
		 * Phase 16 W7 (U16.4 / D16.5): Dismisser — the priority-list
		 * popover closer. Closes the topmost popover (launcher popup
		 * → settings dialog → panel context menu → background context
		 * menu) when the `dismiss` keyboard shortcut fires. The
		 * pointers are wired after the widgets exist; the
		 * ShortcutController only invokes the dismisser after its
		 * own GUI-thread marshal so the dependency is safe.
		 *
		 * Constructed unconditionally on the Genode::Heap because its
		 * lifetime must outlive the lambda scope (the panel + bg +
		 * settings widgets hold a pointer to it).
		 *
		 * Phase 16 W8 (U16.5 / D16.5): the dismisser still
		 * references a single "the panel" for the priority list.
		 * In collection mode the dismisser wires to the default
		 * panel (the legacy default closeable target). The
		 * per-id dismiss path is Phase 17+ scope.
		 */
		static Genode::Heap dismisser_heap(env.ram(), env.rm());
		static Sponge::Sponge_DE::Dismisser dismisser;
		dismisser.set_launcher_view(&launcher_view);
		dismisser.set_launcher_controller(&launcher_ctrl);

		/*
		 * Real-hardware observability: mirror pkgd's running set into
		 * every constructed panel's title ("Sponge DE · R:hello,debug").
		 * The panel title is the one status line guaranteed visible on
		 * bring-up hardware where no serial console is available.
		 */
		/*
		 * The runtime_state subscription needs the pkgd-topology relay
		 * (report_rom policy 'sponge-de -> runtime_state'). Only wire it
		 * when the launcher feed itself is pkgd-driven — the product
		 * scenarios. Every other topology (the flat test scenarios,
		 * wm-only stacks) lacks the relay and a denied ROM session is
		 * component-fatal on Genode.
		 */
		bool diag_runtime_state = false;
		{
			Genode::Attached_rom_dataspace cfg { env, "config" };
			cfg.update();
			if (cfg.valid()) {
				char const * const base = cfg.local_addr<char const>();
				Genode::size_t  const sz  = cfg.size();
				for (Genode::size_t i = 0; i + 25 < sz; ++i)
					if (Genode::strcmp(base + i, "runtime_state: yes", 18) == 0 ||
					    Genode::strcmp(base + i, "\"runtime_state\"", 15) == 0) {
						diag_runtime_state = true;
						break;
					}
			}
		}

		Genode::Constructible<Genode::Attached_rom_dataspace> runtime_state_rom;
		auto diag_refresh = [&]() {
			QString running;
			for (auto const &a : launcher_ctrl.apps()) {
				if (!a.running) continue;
				if (!running.isEmpty()) running += QStringLiteral(",");
				running += a.name;
			}
			int const windows = tasklist_ctrl.tracked_window_count();

			/*
			 * pkgd's config_writes from the installed report (the N:
			 * readout): how many times pkgd generated+published the
			 * pkg_runtime config. On healthy hardware N grows past 1
			 * as the bake seeding lands children; N stuck at 1 with
			 * C:0 means pkgd never regenerated, N>=2 with C:0 means
			 * the second write was lost in the relay.
			 */
			int const cfg_writes = launcher_ctrl.config_writes();

			/*
			 * pkg_runtime's sandbox <state> report (the C: readout):
			 * counts children the sandbox actually spawned. R: is
			 * pkgd bookkeeping; C: is the sandbox's ground truth.
			 */
			int children = -1;
			if (diag_runtime_state && !runtime_state_rom.constructed())
				runtime_state_rom.construct(env, "runtime_state");
			if (runtime_state_rom.constructed()) {
				runtime_state_rom->update();
			if (runtime_state_rom->valid()) {
				children = 0;
				try {
					Genode::Xml_node const state(runtime_state_rom->local_addr<char const>(),
					                             runtime_state_rom->size());
					state.for_each_sub_node("child", [&](Genode::Xml_node const &) {
						++children; });
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			}

			if (panel_top)                 panel_top->show_running_set(running, windows, children, cfg_writes);
			if (panel_bottom.constructed()) panel_bottom->show_running_set(running, windows, children, cfg_writes);
		};
		QObject::connect(&launcher_ctrl, &LauncherController::appsChanged,
		                 diag_refresh);
		{
			auto *diag_timer = new QTimer(&launcher_ctrl);
			QObject::connect(diag_timer, &QTimer::timeout, diag_refresh);
			diag_timer->start(1000);
		}
		dismisser.set_settings_controller(&settings_ctrl);
		if (panels_collection_enabled && panels_ptr)
			dismisser.set_panel_widget(panels_ptr->panel_for_id(
			    PanelWidget::default_id()));
		else
			dismisser.set_panel_widget(panel_top);
		shortcut_ctrl.set_launcher_controller(&launcher_ctrl);
		shortcut_ctrl.set_tasklist_controller(&tasklist_ctrl);
		shortcut_ctrl.set_dismisser(&dismisser);

		/*
		 * Tasklist widget + insertion BEFORE the panels are shown.
		 * In singleton mode the tasklist is attached to panel_top.
		 * In dual mode it's attached to panel_bottom (the default
		 * visible location). When the user flips to top, the
		 * tasklist moves with the panel via the bottom-widget
		 * hide + top-widget show path (the tasklist is owned by
		 * the bottom widget in dual mode; the top widget has no
		 * tasklist attachment in that case to avoid double-
		 * rendering).
		 *
		 * In collection mode the tasklist is attached to the
		 * collection's default panel (the per-panel tasklist
		 * attachment is Phase 17+ scope).
		 */
		Genode::Constructible<Sponge::Sponge_DE::TasklistWidget> tasklist_widget { };
		if (tasklist_enabled) {
			PanelWidget *attach_target = nullptr;
			if (panels_collection_enabled && panels_ptr)
				attach_target = panels_ptr->panel_for_id(PanelWidget::default_id());
			else if (panel_bottom_enabled)
				attach_target = &*panel_bottom;
			else
				attach_target = panel_top;
			if (attach_target) {
				tasklist_widget.construct(theme_ctrl.initial(), attach_target);
				attach_target->attach_tasklist(&*tasklist_widget);
			}
		}

		/*
		 * Show all panels. In legacy mode: panel_top + panel_bottom.
		 * In collection mode: every panel the collection owns.
		 *
		 * Each PanelWidget's Gui session is constructed lazily
		 * on show(); the run scenario routes the per-panel labels
		 * (the F5 label_prefix trap defense) to their own nitpicker
		 * domains.
		 *
		 * Collection mode wiring is done in two phases:
		 *
		 *   phase A (this block): set_launcher_view + attach_config
		 *     + the panel_added -> show lambda. The lambda fires
		 *     on every future applyPanelIds call (the configd
		 *     broadcast), showing the freshly-instantiated panel.
		 *
		 *   phase B (below): the panel_ids_changed -> applyPanelIds
		 *     connection. The configd broadcast drives the panel
		 *     creation; the panel_added lambda drives the show.
		 */
		if (panels_collection_enabled && panels_ptr) {
			/*
			 * Wire the collection: attach the launcher view to
			 * every panel, attach the config controller's per-id
			 * fan-out to the collection.
			 */
			panels_ptr->set_launcher_view(&launcher_view);
			panels_ptr->attach_config_controller(&config_ctrl);

			/*
			 * panel_added -> show lambda. Fires on every future
			 * applyPanelIds call (the configd broadcast), showing
			 * the freshly-instantiated panel.
			 */
			QObject::connect(panels_ptr,
			                 &Sponge::Sponge_DE::PanelCollection::panel_added,
			                 panels_ptr,
			                 [panels_ptr](QString, PanelWidget *widget) {
				if (widget) {
					widget->show();
					/*
					 * Phase 16 W8 (U16.5 / D16.5) — raise the panel
					 * so it has input focus. With multiple panels
					 * the Genode QPA's focus routing splits between
					 * the windows; explicit raise() ensures the
					 * freshly-instantiated panel can receive its
					 * own QMP-driven clicks for the W8 cross-panel
					 * click assertion.
					 */
					widget->raise();
				}
			});
			QObject::connect(panels_ptr,
			                 &Sponge::Sponge_DE::PanelCollection::panel_removed,
			                 panels_ptr,
			                 [panels_ptr](QString) {
				(void)panels_ptr;
			});
		}
		else {
			panel_top->show();
			if (panel_bottom_enabled)
				panel_bottom->show();
			panel_top->set_launcher_view(&launcher_view);
			if (panel_bottom_enabled)
				panel_bottom->set_launcher_view(&launcher_view);
			theme_ctrl.attach_panel(panel_top);
			config_ctrl.attach_panel(panel_top);
			if (panel_bottom_enabled) {
				QObject::connect(&config_ctrl,
				                 &ConfigController::panel_position_changed,
				                 panel_top,
				                 &PanelWidget::applyPosition);
				QObject::connect(&config_ctrl,
				                 &ConfigController::panel_position_changed,
				                 &*panel_bottom,
				                 &PanelWidget::applyPosition);
			}
		}
		config_ctrl.attach_launcher(&launcher_view);
		Genode::log("sponge-de: panel shown");

		/*
		 * Phase 16 W8 (U16.5 / D16.5) — wire the panel_ids_changed
		 * signal to the collection. The collection creates /
		 * destroys PanelWidget instances to match the new id
		 * set. Only active in collection mode; legacy regression
		 * scenarios ignore the signal (their panel_top is
		 * constructed eagerly).
		 *
		 * The panel_added lambda wires show() on every future
		 * instantiation (the W8 spec: a newly-added panel appears
		 * immediately). The panel_added lambda is also wired
		 * above (in the collection-mode branch) for the case
		 * where the collection starts empty and gains panels
		 * after the configd broadcast.
		 */
		if (panels_collection_enabled && panels_ptr) {
			QObject::connect(&config_ctrl,
			                 &ConfigController::panel_ids_changed,
			                 panels_ptr,
			                 &Sponge::Sponge_DE::PanelCollection::applyPanelIds);
		}

		/*
		 * Phase 16 W6: BackgroundWidget — the in-DE owned
		 * desktop surface (D16.4). Constructed ONLY when the
		 * component config carries `<background source="controller"/>`.
		 * Absent the gate, no widget, no third Gui session, no
		 * background_controller — scenarios that do not opt in
		 * boot with the prior Phase 10/11/14 behavior (nitpicker's
		 * default-domain `+ background | color: #1e1e2e` rule is
		 * the only desktop paint).
		 *
		 * The widget's Gui session is routed to a nitpicker
		 * `<domain name="default">` at layer=1 — BELOW the wm
		 * window domains (layer=3) and BELOW the panel
		 * (layer=2). Right-clicks on the uncovered background
		 * region land here.
		 *
		 * BackgroundController bridges the configd broadcast
		 * (background.color + background.image) to the widget
		 * via the same 250 ms poll + ROM sigh pattern the
		 * ConfigController already uses; the new slots are
		 * applyBackgroundColor / applyBackgroundImage (added
		 * in W6 alongside the existing apply* family).
		 */
		bool background_enabled { false };
		{
			Genode::Attached_rom_dataspace const config(env, "config");
			config.node().with_optional_sub_node("background",
				[&] (Genode::Node const &n) {
					background_enabled =
						n.attribute_value("source",
						                  Genode::String<16>())
						== "controller";
				});
		}

		Genode::Constructible<Sponge::Sponge_DE::BackgroundWidget> bg_widget { };
		Genode::Constructible<Genode::Reporter> bgmenu_open_reporter { };
		if (background_enabled) {
			/*
			 * BackgroundController is constructed on the
			 * Genode::Heap because we want its QObject lifetime
			 * to outlive the lambda scope (the widget holds a
			 * pointer to it). The Heap is leak-on-purpose — the
			 * controller is a long-lived companion of the widget
			 * and the process exits when the widget does.
			 */
			static Genode::Heap bg_ctrl_heap(env.ram(), env.rm());
			static Sponge::Sponge_DE::BackgroundController bg_ctrl;
			bg_widget.construct();
			bg_widget->set_controller(&bg_ctrl);
			bg_widget->set_settings_controller(&settings_ctrl);
			bg_widget->set_launcher_view(&launcher_view);
			if (tasklist_enabled)
				bg_widget->set_tasklist_controller(&tasklist_ctrl);
			dismisser.set_background_widget(&*bg_widget);
			bg_widget->show();

			QObject::connect(&config_ctrl,
			                 &ConfigController::background_color_changed,
			                 &bg_ctrl,
			                 &Sponge::Sponge_DE::BackgroundController::applyBackgroundColor);
			QObject::connect(&config_ctrl,
			                 &ConfigController::background_image_changed,
			                 &bg_ctrl,
			                 &Sponge::Sponge_DE::BackgroundController::applyBackgroundImage);

			QObject::connect(&bg_ctrl,
			                 &Sponge::Sponge_DE::BackgroundController::background_color_changed,
			                 &*bg_widget,
			                 &Sponge::Sponge_DE::BackgroundWidget::applyBackgroundColor);
			QObject::connect(&bg_ctrl,
			                 &Sponge::Sponge_DE::BackgroundController::background_image_changed,
			                 &*bg_widget,
			                 &Sponge::Sponge_DE::BackgroundWidget::applyBackgroundImage);

			/*
			 * The bgmenu_open reporter: bgmenu_opened/closed
			 * signals from the widget flip the report's
			 * <bgmenu open="yes|no"/> attribute. The W6 bgmenu
			 * probe observes the transition.
			 */
			bgmenu_open_reporter.construct(env, "bgmenu", "bgmenu_open");
			bgmenu_open_reporter->enabled(true);

			/* Reporter wraps content in <node_type>...</node_type>; emit only the
			 * inner body. */
			auto *bg_ptr  = &*bg_widget;
			auto *rep_ptr = &*bgmenu_open_reporter;

			/* Initial structural state: the bgmenu probe uses this as
			 * the acceptance that the widget is fully wired. The
			 * per-event state (open="yes"/"no") is emitted by the
			 * bgmenu_opened/closed Qt signals — secondary because
			 * the Genode QPA right-click delivery to a non-decorated
			 * top-level window is timing-sensitive. The Reporter's
			 * generate() emits the body verbatim (no tag wrapping) —
			 * the consumer sees the body as the document root. */
			{
				char buf[64] = "<bgmenu open=\"ready\"/>";
				(void)rep_ptr->generate(
				    Genode::Const_byte_range_ptr(buf, Genode::strlen(buf)));
			}
			QObject::connect(bg_ptr,
			                 &Sponge::Sponge_DE::BackgroundWidget::bgmenu_opened,
			                 bg_ptr, [rep_ptr]() {
				char buf[64] = "<bgmenu open=\"yes\"/>";
				(void)rep_ptr->generate(
				    Genode::Const_byte_range_ptr(buf, Genode::strlen(buf)));
			});
			QObject::connect(bg_ptr,
			                 &Sponge::Sponge_DE::BackgroundWidget::bgmenu_closed,
			                 bg_ptr, [rep_ptr]() {
				char buf[64] = "<bgmenu open=\"no\"/>";
				(void)rep_ptr->generate(
				    Genode::Const_byte_range_ptr(buf, Genode::strlen(buf)));
			});
			(void)bg_ptr;
			(void)rep_ptr;

			Genode::log("sponge-de: background widget shown");
		}

		Main main_window(env, theme_ctrl.initial());
		main_window.show();
		theme_ctrl.attach_main(&main_window);
		Genode::log("sponge-de: window shown");

		theme_ctrl.attach_launcher(&launcher_view);

		if (tasklist_enabled) {
			tasklist_ctrl.attach_widget(&*tasklist_widget);
			theme_ctrl.attach_tasklist(&*tasklist_widget);
			QObject::connect(&*tasklist_widget, &Sponge::Sponge_DE::TasklistWidget::task_clicked,
			                 &tasklist_ctrl, &TasklistController::on_task_clicked);
			QObject::connect(&*tasklist_widget, &Sponge::Sponge_DE::TasklistWidget::task_toggle_maximized,
			                 &tasklist_ctrl, &TasklistController::on_toggle_maximized);
		}

		/* Wire the notify poster to every event-emitting controller. */
		theme_ctrl.attach_notify_poster(&notify_poster);
		config_ctrl.attach_notify_poster(&notify_poster);
		launcher_ctrl.attach_notify_poster(&notify_poster);
		Genode::log("sponge-de: after attach_notify_poster");

		/* Marker matched by run/sponge-de.run for automated verification. */
		Genode::log("sponge-de: panel and window shown");

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
