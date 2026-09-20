/* SPDX-License-Identifier: Apache-2.0
 *
 * ShortcutController — extensible keyboard-shortcut framework
 * (Phase 16 W7, U16.4 / D16.5).
 *
 * Reads `shortcuts.bindings` from sponge_configd's broadcast and
 * dynamically configures event_filter's `<report>` source with
 * `<shortcut>` entries for the closed action enum
 * (`launcher` / `focus_next` / `dismiss`). Subscribes to the
 * event_filter's per-action shortcut Reports (one Report per
 * closed action) and dispatches the action via
 * QMetaObject::invokeMethod(Qt::QueuedConnection) on the GUI
 * thread.
 *
 * === Architecture (D16.5) ===
 *
 *   sponge_configd --[Report "config"]--> report_rom
 *                                     --[ROM "configd"]--> ShortcutController
 *
 *   ShortcutController parses `shortcuts.bindings`, reads the
 *   static base from the `event_filter.config` boot module
 *   (single source of truth, mirrors the drivers_interactive-pc
 *   recipe), injects a `<report>` source carrying the parsed
 *   `<shortcut>` children, and emits the merged event_filter
 *   config to a Report labeled `event_filter_config`:
 *
 *   ShortcutController --[Report "event_filter_config"]--> report_rom
 *                                              --[ROM "event_filter.config"]--> event_filter
 *
 *   event_filter --[Report "shortcut_launcher"|...]--> report_rom
 *                                              --[ROM "shortcut_hit_launcher"|...]--> ShortcutController
 *
 *   ShortcutController --[Report "shortcut_hit"]--> report_rom
 *                                              --[ROM "shortcut_hit"]--> shortcuts_probe
 *                                              (carries the action name on dispatch:
 *                                               <shortcut_hit action="X" hit="yes"/>)
 *
 *   ShortcutController dispatches the action via QMetaObject:
 *
 *     `launcher`   -> LauncherController::toggle_launcher
 *     `focus_next` -> TasklistController::cycle_focus
 *     `dismiss`    -> Dismisser::dismiss_topmost
 *
 * The framework is extensible: writing a new
 * `shortcuts.bindings` value through de_config_request
 * re-emits the event_filter config without code change.
 *
 * === Single-listener (F9 defense) ===
 *
 * There is exactly ONE event_filter instance per topology
 * (the run script's single `event_filter` child), exactly ONE
 * `<report>` source in the dynamic config, and exactly ONE
 * subscriber per action (`shortcut_hit_<action>` ROM). No
 * double-fire: each captured keypress produces exactly one
 * shortcut_hit_<action>="yes" Report AND one
 * shortcut_hit="yes" Report (the consolidated probe observation
 * that carries the action name on dispatch).
 *
 * === Activation gate ===
 *
 * The controller is constructed only when the component config
 * carries `<shortcuts source="controller"/>`. Mirrors the W4
 * <de_config source="controller"/> + W5 <panel_bottom source=
 * "dual"/> + W6 <background source="controller"/> gates.
 *
 * === Threading contract (failure-point 2 enforcement) ===
 *
 * The ROM signal handler runs on the Genode entrypoint dispatcher
 * thread, NOT the Qt event-loop thread. The handler only reads
 * the ROM (a plain shared dataspace) and copies the broadcast
 * payload into a QString, then marshals to the GUI thread via
 * QMetaObject::invokeMethod(Qt::QueuedConnection). The actual
 * config-d regeneration + event_filter config emission +
 * Report<shortcut_hit_<action>> subscription happens in
 * applyBindings() on the GUI thread.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>

class QTimer;

namespace Sponge::Sponge_DE {

class LauncherController;
class SettingsController;
class BackgroundWidget;
class PanelWidget;
class Dismisser;

}  /* namespace Sponge::Sponge_DE */

class TasklistController;

namespace Sponge::Sponge_DE {

bool shortcuts_asks_for_controller(Genode::Env &env);


class ShortcutController : public QObject
{
	Q_OBJECT

	public:

		explicit ShortcutController(Genode::Env &env, QObject *parent = nullptr);

		~ShortcutController() override;

		/*
		 * Wire the action-target controllers. The Dismesser
		 * is constructed by main.cc and receives its popover
		 * pointers through the existing W6 setters; the
		 * ShortcutController only needs the Dismesser pointer
		 * for the `dismiss` dispatch path.
		 */
		void set_launcher_controller(LauncherController *l) { _launcher = l; }
		void set_tasklist_controller(TasklistController *t) { _tasklist = t; }
		void set_dismisser(Sponge::Sponge_DE::Dismisser *d) { _dismisser = d; }

	signals:

		/*
		 * Emitted on the GUI thread whenever the configd
		 * broadcast carries a new `shortcuts.bindings` value.
		 * The W7 controller's internal signal — main.cc does
		 * NOT need to subscribe; the controller self-connects
		 * to its own slot via QMetaObject::invokeMethod so the
		 * signal-thread boundary is GUI-thread-safe by
		 * construction.
		 */
		void shortcuts_bindings_changed(QString value);

	private slots:

		/*
		 * GUI thread (failure-point 2 enforcement: marshalled
		 * from the ROM signal handler by QMetaObject::invoke
		 * Method(... , Qt::QueuedConnection)). Parses the
		 * payload, regenerates the dynamic event_filter config,
		 * and updates the per-action subscriber ROMs.
		 */
		void applyBindings(QString payload);

	private:

		Genode::Env &_env;

		/*
		 * `configd` ROM — sponge_configd's broadcast relayed by
		 * report_rom. The "configd" label is used (NOT "config")
		 * to avoid the init-inline-config shadow.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace>            _config_rom { };
		Genode::Constructible<Genode::Signal_handler<ShortcutController>> _sigh       { };

		/*
		 * Dynamic event_filter config reporter. The label
		 * `event_filter_config` is mapped by report_rom policy
		 * to the event_filter child's `event_filter.config`
		 * ROM label.
		 *
		 * The W7 live-capture proof (Phase 16 W7 final commit,
		 * 2026-09-19): the emit body is no longer a hardcoded
		 * static base + shortcuts — the controller READS the
		 * `event_filter.config` boot module as a static base
		 * (single source of truth, the upstream drivers_interactive-pc
		 * recipe) and INJECTS a `<report>` source carrying the
		 * dynamic `<shortcut>` children. The merged config is
		 * what event_filter loads — the upstream recipe does
		 * not need to be patched.
		 */
		Genode::Constructible<Genode::Expanding_reporter> _event_filter_reporter { };

		/*
		 * Consolidated `shortcut_hit` reporter — the W7 live-capture
		 * proof's single observation point. Every action dispatch
		 * emits `<shortcut_hit action="X" hit="yes"/>` (action
		 * token + the same yes-flag as the per-action reporters).
		 * The probe (shortcuts_probe) subscribes to this ONE ROM
		 * to detect which action fired; the per-action reporters
		 * remain for the structural test side.
		 *
		 * Constructible — opened lazily on the first configd
		 * broadcast (same lifecycle as the per-action reporters)
		 * so topologies without the policy route do not fatal-
		 * deny at construct time.
		 */
		Genode::Constructible<Genode::Expanding_reporter> _shortcut_hit_reporter { };

		/*
		 * Per-action shortcut-hit reporters (one Report per
		 * closed action). The label `shortcut_hit_<action>`
		 * is mapped by report_rom policy to the
		 * ShortcutController's own `shortcut_hit_<action>`
		 * ROM label — the controller SUBSCRIBES to its own
		 * publishers via the report_rom loop. The
		 * subscribers use the attached_rom_dataspace path
		 * with a Signal_handler that invokes the action slot
		 * on the GUI thread.
		 *
		 * Constructible so the action slots can be lazy-
		 * constructed on the first configd broadcast (the
		 * controller does NOT block boot waiting for the
		 * broadcast — it opens the ROM subscribers as soon as
		 * the actions are known).
		 */
		struct HitReporter {
			Genode::Constructible<Genode::Expanding_reporter> reporter { };
			QString action;
		};
		HitReporter _hit_launcher;
		HitReporter _hit_focus_next;
		HitReporter _hit_dismiss;

		/*
		 * Subscribers for event_filter's `<report>` source —
		 * when the user presses Super / Alt-Tab / Escape,
		 * event_filter publishes
		 * `<shortcut name="<action>" serial="N"/>` on a PER-
		 * ACTION reporter (one reporter per closed action,
		 * created by Report_source::Shortcut's constructor:
		 * `Expanding_reporter(env, "shortcut", name)` with
		 * `name = "launcher" | "focus_next" | "dismiss"`).
		 *
		 * The controller subscribes via ONE ROM session per
		 * action (the report_rom policy maps each
		 * `sponge-de -> shortcut_<action>` request to the
		 * matching `drivers -> event_filter -> shortcut ->
		 * <action>` report). event_filter publishes at most
		 * ONE of these per captured keypress, so each
		 * subscriber observes at most one hit per cycle.
		 *
		 * The ROM sigh handler parses the report's `<shortcut>`
		 * children, identifies which action fired (via the
		 * `name` attribute), dispatches the action slot via
		 * QMetaObject::invokeMethod(Qt::QueuedConnection),
		 * AND publishes the consolidated `shortcut_hit`
		 * report with the action name (the structural shape
		 * the probe polls for).
		 */
		struct HitSubscriber {
			Genode::Constructible<Genode::Attached_rom_dataspace>          rom { };
			Genode::Constructible<Genode::Signal_handler<ShortcutController>> sigh { };
			unsigned                                                     last_serial { 0 };
			QString                                                       action;
		};
		HitSubscriber _hit_sub_launcher;
		HitSubscriber _hit_sub_focus_next;
		HitSubscriber _hit_sub_dismiss;

		QTimer *_poll_timer { nullptr };

		/*
		 * Action targets. main.cc wires these after
		 * construction. Absent any pointer, the corresponding
		 * action is a no-op with a Genode::warning.
		 */
		LauncherController *_launcher { nullptr };
		TasklistController *_tasklist  { nullptr };
		Sponge::Sponge_DE::Dismisser          *_dismisser { nullptr };

		/*
		 * Last-applied value (per-key de-dup). Avoids re-emit
		 * on identical broadcasts.
		 */
		QString _last_bindings;

		bool _reemit_pending { false };

		/*
		 * Parse the structured `action<tab>key_sequence` value
		 * into the XML body for the `<report>` source's
		 * `<shortcut>` children. Returns the inner XML body
		 * (the `<shortcut>` elements — no top-level wrapper;
		 * the caller injects them inside `<report>`).
		 */
		QString _build_shortcuts_xml(QString const &value);

		/*
		 * Emit the FULL event_filter config (static base +
		 * dynamic `<shortcut>` children) to the reporter.
		 * The static base is read from the boot module's
		 * `event_filter.config` (the drivers_interactive-pc
		 * recipe staged into bin/ by the run script). A
		 * `<report>` source is injected carrying the parsed
		 * shortcuts; if the base already has one (a future
		 * recipe variant), its contents are replaced.
		 *
		 * If the boot module is unavailable, falls back to the
		 * hardcoded inline base (same content as the recipe).
		 */
		void _emit_event_filter_config(QString const &shortcuts_xml);

		/*
		 * Open the consolidated `shortcut_hit` reporter
		 * (lazy, same lifecycle as the per-action reporters).
		 * Initial structural state is `<shortcut_hit hit=
		 * "ready"/>` so the probe can detect the controller
		 * wiring on first update.
		 */
		void _open_shortcut_hit_reporter();

		/*
		 * Publish to the consolidated `shortcut_hit` reporter
		 * — carries the action name (`<shortcut_hit action=
		 * "X" hit="yes"/>`). Called from `_publish_hit` on
		 * every successful dispatch.
		 */
		void _publish_shortcut_hit(char const *action);

		void _on_rom();
		void _poll();

		/*
		 * Open the hit reporters + the per-action event_filter
		 * shortcut ROM subscribers. Called from applyBindings()
		 * on the GUI thread.
		 */
		void _open_hit_reporter(HitReporter &hr, char const *action);
		void _open_shortcut_subscriber();
		void _open_hit_subscriber(HitSubscriber &hs, char const *action);
		void _on_hit_subscriber_rom_launcher();
		void _on_hit_subscriber_rom_focus_next();
		void _on_hit_subscriber_rom_dismiss();
		void _publish_hit(char const *action);

		/*
		 * Shared per-action ROM dispatch body. Called from
		 * the three `_on_hit_subscriber_rom_*` member
		 * functions (Genode's Signal_handler API requires
		 * member-function pointers, so we cannot pass an
		 * action discriminator via lambda capture).
		 */
		void _dispatch_hit_subscriber(HitSubscriber &hs);
};

}  /* namespace Sponge::Sponge_DE */
