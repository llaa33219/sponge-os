/* SPDX-License-Identifier: Apache-2.0
 *
 * TasklistController — wm `window_list` report bridge for Sponge DE.
 *
 * Phase 14 W7: the panel tasklist is the deterministic
 * minimize/restore/close path for the window stack. The controller
 * watches the wm `window_list` report and the layouter's
 * `window_layout` report (both relayed by report_rom), tracks the
 * per-window state, and invokes the widget directly on changes
 * (NOT via a Qt signal — a QList<TaskInfo> signal would require
 * qRegisterMetaType which compiles fine on host Qt but generates
 * a moc template that does not exist in the Genode Qt port).
 *
 * The controller is in the GLOBAL namespace to avoid Qt's moc
 * namespace-doubling.
 */

#pragma once

#include "task_info.h"

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QList>
#include <QObject>
#include <QString>

class QTimer;

namespace Sponge { namespace Sponge_DE { class TasklistWidget; } }

class TasklistController : public QObject
{
	Q_OBJECT

	public:

		explicit TasklistController(Genode::Env &env, QObject *parent = nullptr);

		void attach_widget(Sponge::Sponge_DE::TasklistWidget *widget);

		void restyle() { }

		/* Invoked from the widget on a click. */
		void on_task_clicked(QString label);

		/* Toggle maximized. */
		void on_toggle_maximized(QString label);

		/*
		 * Real-hardware observability: count of tracked wm windows
		 * (the panel-title W: readout mirrors it).
		 */
		int tracked_window_count() const { return _tracked.size(); }

		/*
		 * Phase 16 W7 (U16.4 / D16.5) — keyboard-shortcut action
		 * slot for the `focus_next` event_filter shortcut (default
		 * binding `Alt-Tab`). Advances the focus to the next
		 * non-minimized window in `_tracked` after `_focused_label`
		 * (wrapping). Publishes a `focus_request` Report; the wm +
		 * layouter chain re-positions + raises the new window.
		 *
		 * Forward-only cycle (per the plan: "forward-only, wrapping").
		 * Skips minimized windows (focusing a minimized window would
		 * un-minimize it — out of scope for a focus-cycle, not a
		 * minimize toggle).
		 *
		 * GUI thread ONLY (failure-point 2 enforcement: marshalled
		 * from the ROM-signal handler by ShortcutController via
		 * QMetaObject::invokeMethod(... , Qt::QueuedConnection)).
		 */
		void cycle_focus();

		/*
		 * Phase 16 W6/W7 follow-up (U16.3) — show-desktop toggle
		 * minimizes or restores every visible window in `_tracked`.
		 * When `minimize=true`, every window's `minimized` flag is
		 * set to true and the layouter-rule ROM is overwritten
		 * (which causes the layouter to park every window at
		 * (-32000, -32000) — the W7 tasklist state machine's
		 * parking at tasklist_controller.cc:500-516). When
		 * `minimize=false`, every window is restored to its last
		 * non-minimized geometry (the `_tracked` Window_state
		 * cache preserves the geometry across the parked state).
		 *
		 * The ShowDesktop controller's `toggle()` slot flips the
		 * local state and calls this with the matching bool.
		 *
		 * GUI thread ONLY (failure-point 2 enforcement).
		 */
		void set_all_minimized(bool minimize);

		void set_static_rules(QString const &rules_xml);

		~TasklistController() override;

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Attached_rom_dataspace>             _window_list_rom { };
		Genode::Constructible<Genode::Signal_handler<TasklistController>> _window_list_sigh { };

		Genode::Constructible<Genode::Attached_rom_dataspace>             _window_layout_rom { };
		Genode::Constructible<Genode::Signal_handler<TasklistController>> _window_layout_sigh { };

		Genode::Constructible<Genode::Expanding_reporter>                 _focus_request { };
		Genode::Constructible<Genode::Expanding_reporter>                 _rules_reporter { };

		QTimer *_poll_timer { nullptr };

		Sponge::Sponge_DE::TasklistWidget *_widget { nullptr };

		QString _static_rules_xml;

		unsigned _focus_request_id { 0 };

		/* Q_INVOKABLE so QMetaObject::invokeMethod (from the
		 * entrypoint thread) can reach it without a slot-moc
		 * infrastructure. */
		Q_INVOKABLE void applyUpdates();

		struct Window_state {
			QString label;
			int     x              { 0 };
			int     y              { 0 };
			unsigned w             { 0 };
			unsigned h             { 0 };
			bool    focused        { false };
			bool    minimized      { false };
			bool    has_alpha      { false };
			bool    hidden         { false };
			bool    resizeable     { true };
			bool    maximized      { false };
			bool    geometry_known { false };
		};

		QList<Window_state> _tracked;

		QString _focused_label;

		bool    _window_list_dirty    { false };
		bool    _window_layout_dirty  { false };

		QStringList _last_emitted_signed;

		/*
		 * Rules-republication state: the layouter repositions every
		 * assigned window on each window_list change (dissolve +
		 * re-assign + outer_geometry), so the rules ROM must carry
		 * CURRENT window positions when the tracked SET changes (a
		 * new window) or existing windows snap back to their rule
		 * positions. Republish on set changes only (label added or
		 * removed, minimized flipped), stable for one extra poll.
		 * Geometry deltas deliberately do NOT trigger: window_layout
		 * cannot report the maximized state, so geometry feedback
		 * fights user manipulations.
		 */
		QStringList _published_rules_sig  { };
		QStringList _candidate_rules_sig  { };
		bool        _rules_sig_stable     { false };

		int _layout_origin_x { 0 };
		int _layout_origin_y { 0 };
		int _layout_area_w   { 0 };
		int _layout_area_h   { 0 };

		void _on_window_list_rom();
		void _on_window_layout_rom();
		void _poll();
		void _lazy_open();

		bool _pull_payloads();

		void _recompute_tracked();
		void _maybe_republish_rules();

		QList<TaskInfo> _build_task_infos() const;

		Window_state *_find(QString const &label);

		void _publish_focus_request(QString const &label);

		void _publish_rules_for(QString const &label);

		void _compose_rules(Genode::Xml_generator &g, QString const &target_label);

		void _emit_static_rules(Genode::Xml_generator &g) const;

		void _append_assign_for(Genode::Xml_generator &g, Window_state const &w) const;

		void _refresh_widget();

		static QStringList _signature(QList<TaskInfo> const &entries);
};
