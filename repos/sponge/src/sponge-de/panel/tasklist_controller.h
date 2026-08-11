/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * TasklistController — wm `window_list` report bridge for Sponge DE.
 *
 * Phase 14 W7: the panel tasklist is the deterministic
 * minimize/restore/close path for the window stack. The controller
 * watches the wm `window_list` report and the layouter's
 * `window_layout` report (both relayed by report_rom) and
 * republishes the active task list as a Qt signal that the
 * TasklistWidget consumes on the GUI thread.
 *
 * On user click, the controller writes two Genode reports:
 *
 *   1. `focus_request` — a single-element ROM the layouter reads via
 *      its `focus_request` label. The layouter brings the matching
 *      window to front and focuses it (see
 *      genode/repos/gems/src/app/window_layouter/main.cc:787-822).
 *      The `id` attribute is a monotonic counter the layouter uses
 *      to dedupe repeated broadcasts.
 *
 *   2. `rules` — a `<rules>` XML document the layouter reads in
 *      `rules="rom"` mode. The controller maintains the COMPLETE
 *      rules set (the static initial layout AND any per-window
 *      dynamic updates) so the layouter sees a self-contained
 *      document on every emission.
 *
 *      State transitions (matching docs/plans/wm-state-table.md):
 *
 *        Normal-Visible-Focused -> Minimized
 *            rules: <assign label="..." xpos="-32000" ypos="-32000"
 *                          width="<saved>" height="<saved>"/>
 *        Normal-Visible         -> Minimized
 *            Same as above (minimize does not require focus).
 *        Minimized              -> Normal-Visible-Focused
 *            rules: <assign label="..." xpos="<saved>" ypos="<saved>"
 *                          width="<saved>" height="<saved>"/>
 *            focus_request: <focus_request id="<N>" label="<label>"/>
 *                           (focus-after-restore per U3).
 *        Normal-Visible(-Focused) -> Maximized
 *        Maximized              -> Normal-Visible
 *            rules: <assign label="..." xpos="<saved>" ypos="<saved>"
 *                          width="<saved>" height="<saved>"
 *                          maximized="yes|no"/>
 *
 *      The off-screen parking coordinates (x=-32000, y=-32000) are
 *      well outside nitpicker's int32 view space.
 *
 * CHANNEL OWNERSHIP (AGENTS.md §1.2):
 *
 *   The controller is the SOLE writer of the `focus_request` and
 *   `rules` reports. report_rom is single-writer per label; no
 *   other component may write to these labels. The static rules
 *   (initial placement of the demo + pkg_runtime windows) are
 *   mirrored from the layouter's inline rules — the controller
 *   supersedes the inline rules when the layouter is configured
 *   with `rules="rom"`.
 *
 * THREAD MODEL (the critical invariant — failure-point 2):
 *
 *   The ROM signal handler runs on the Genode entrypoint dispatcher
 *   thread, NOT the Qt event-loop thread. Touching any QWidget or
 *   calling QApplication APIs from the signal handler is undefined
 *   behavior.
 *
 *   The handler reads the ROM (a plain shared dataspace), parses the
 *   <window_list> / <window_layout> XML, and marshals the entry list
 *   to the GUI thread via QMetaObject::invokeMethod(...)
 *   (Qt::QueuedConnection). A 250 ms QTimer poll acts as a safety
 *   net for the same reason ConfigController uses one.
 *
 * WIRE FORMAT (window_list):
 *
 *   The wm publishes <window_list> with N children:
 *     <window id="<int>" label="<wm-session-label>"
 *            title="<decor-title>" width="<effective-client-w>"
 *            height="<effective-client-h>" has_alpha="yes" hidden="yes"
 *            resizeable="yes"/>
 *   See genode/repos/gems/src/server/wm/window_registry.h:63-118.
 *
 *   Geometric attributes (x, y) are NOT in window_list; the
 *   controller derives them from the layouter's `window_layout`
 *   report via a separate subscription.
 *
 * ACTIVATION:
 *
 *   The controller is constructed unconditionally in sponge_de_main.cc.
 *   If the `window_list` ROM cannot be opened (no wm in the topol-
 *   ogy, or no route for the `window_list` label), the controller
 *   degrades to no-op: no ROM is opened, no signals are emitted, no
 *   errors raised. The widget still constructs (it's a passive
 *   paint target) and the tasklist stays empty.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QList>
#include <QObject>
#include <QString>

class QTimer;

namespace Sponge::Sponge_DE {

#include "tasklist_widget.h"  /* for TaskInfo */

class TasklistWidget;

class TasklistController : public QObject
{
	Q_OBJECT

	public:

		explicit TasklistController(Genode::Env &env, QObject *parent = nullptr);

		/* Attach the widget AFTER it is constructed but BEFORE the
		 * first broadcast arrives. The controller's first marshalled
		 * signal fans out to the widget's applyEntries(). */
		void attach_widget(TasklistWidget *widget);

		/* Restyle callback (ThemeController fan-out). The widget
		 * restyles itself; the controller has no theme-derived state
		 * to update. Provided for symmetry with the other
		 * controllers. */
		void restyle() { }

		/*
		 * Invoked from the widget on a click. The label is the
		 * layouter's Window::Label (the wm session label, e.g.
		 * "pkg_runtime -> pkg_gui_demo"). The controller decides
		 * whether to minimize or restore by looking at the tracked
		 * state for the label.
		 *
		 * On click:
		 *   - If the window is currently minimized, write a rule
		 *     with the saved (x, y, w, h) AND emit a focus_request.
		 *     The state transitions to Normal-Visible-Focused
		 *     (focus-after-restore per U3).
		 *   - If the window is currently Normal-Visible or
		 *     Normal-Visible-Focused, write a rule with
		 *     (x=-32000, y=-32000, w, h). The window is parked
		 *     off-screen.
		 */
		void on_task_clicked(QString label);

		/*
		 * Toggle the maximized state for a window. The controller
		 * writes a rule with the saved (x, y, w, h) and
		 * maximized flipped from the current state.
		 */
		void on_toggle_maximized(QString label);

		/*
		 * Initialize the static rules document. The host run
		 * script's inline `<rules>` is replaced by the controller's
		 * `rules` ROM when the layouter is configured with
		 * `rules="rom"`. The controller needs to know the
		 * placeholder rules (the ones the layouter would have used
		 * if no ROM were provided) so it can mirror them on every
		 * re-emit.
		 *
		 * Format: each QString is a literal XML string that the
		 * controller pastes into the `<rules>` document. The widget
		 * has no notion of these — they are pure run-time policy.
		 *
		 * Called once at startup before the first window_list
		 * arrives.
		 */
		void set_static_rules(QStringList static_assigns);

	private slots:

		/* GUI thread. Pulls the latest payloads, recomputes the
		 * tracked state, then emits tasks_changed with the new
		 * TaskInfo list. */
		void applyUpdates();

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Attached_rom_dataspace>             _window_list_rom { };
		Genode::Constructible<Genode::Signal_handler<TasklistController>> _window_list_sigh { };

		Genode::Constructible<Genode::Attached_rom_dataspace>             _window_layout_rom { };
		Genode::Constructible<Genode::Signal_handler<TasklistController>> _window_layout_sigh { };

		Genode::Constructible<Genode::Expanding_reporter>                 _focus_request { };
		Genode::Constructible<Genode::Expanding_reporter>                 _rules_reporter { };

		QTimer *_poll_timer { nullptr };

		TasklistWidget *_widget { nullptr };

		QStringList _static_rules;  /* placeholder <assign> nodes from the run script */

		unsigned _focus_request_id { 0 };  /* monotonic counter for the focus_request ROM */

		/* The tracked state per window. The struct is private because
		 * the widget only needs the public TaskInfo view. */
		struct Window_state {
			QString label;
			int     x                { 0 };
			int     y                { 0 };
			unsigned w               { 0 };
			unsigned h               { 0 };
			bool    focused          { false };
			bool    minimized        { false };
			bool    has_alpha        { false };
			bool    hidden           { false };
			bool    resizeable       { true };
			bool    maximized        { false };
			bool    geometry_known   { false };  /* true once window_layout has set x,y,w,h */
		};

		/* Tracked per-window state. Keyed by the wm session label. */
		QList<Window_state> _tracked;

		/* The focused label (last layouter focus report). */
		QString _focused_label;

		/* Cached IO state. The entrypoint-thread handlers stash
		 * payload strings here (the strings live as long as the
		 * ROM dataspace); the GUI-thread applyUpdates() reads them. */
		bool    _window_list_dirty    { false };
		bool    _window_layout_dirty  { false };

		/* De-dup: remember the last emitted task list signature. */
		QStringList _last_emitted_signed;

		void _on_window_list_rom();   /* entrypoint thread */
		void _on_window_layout_rom(); /* entrypoint thread */
		void _poll();                  /* GUI thread */
		void _lazy_open();             /* GUI thread */

		/* Read the latest payloads (marshalled onto the GUI thread by
		 * the entrypoint handlers). Returns true if any payload was
		 * updated. */
		bool _pull_payloads();

		/* Apply the latest payloads to the tracked state. GUI thread. */
		void _recompute_tracked();

		/* Build the TaskInfo list for the widget from the tracked state. */
		QList<TaskInfo> _build_task_infos() const;

		/* Look up a tracked window by label. Returns nullptr if not found. */
		Window_state *_find(QString const &label);

		/* Publish the focus_request report. */
		void _publish_focus_request(QString const &label);

		/* Publish the rules report with the per-window override. */
		void _publish_rules_for(QString const &label);

		/* Compose the full rules document. Called on the GUI thread. */
		void _compose_rules(Genode::Xml_generator &g, QString const &target_label);

		/* Build the assign XML for a single tracked window. */
		void _append_assign_for(Genode::Xml_generator &g, Window_state const &w) const;

		/* Compute a stable signature for the emitted task list. Used
		 * for de-dup. */
		static QStringList _signature(QList<TaskInfo> const &entries);

	signals:

		/* The tracked task list. The widget renders it. */
		void tasks_changed(QList<TaskInfo> entries);
};

}  /* namespace Sponge::Sponge_DE */
