/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of TasklistController. See tasklist_controller.h for
 * the wire contract and the threading model.
 */

#include "tasklist_controller.h"

#include "tasklist_widget.h"

#include <base/log.h>
#include <base/node.h>
#include <util/string.h>

#include <QMetaObject>
#include <QTimer>


TasklistController::TasklistController(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
}


void TasklistController::attach_widget(Sponge::Sponge_DE::TasklistWidget *widget)
{
	_widget = widget;

	_lazy_open();
	if (_rules_reporter.constructed())
		_publish_rules_for(QString());

	/*
	 * Bootstrap the initial window state. The ROM signal handlers are
	 * edge-triggered and miss the initial data when the ROMs are
	 * already at their current version. The periodic poll below is
	 * the reliable fallback.
	 */
	applyUpdates();

	if (!_poll_timer) {
		_poll_timer = new QTimer(this);
		_poll_timer->setInterval(50);
		connect(_poll_timer, &QTimer::timeout, this, &TasklistController::applyUpdates);
		connect(_poll_timer, &QTimer::timeout,
		        this, &TasklistController::applySettingsFocusHint);
		_poll_timer->start();
	}
}


void TasklistController::set_static_rules(QString const &rules_xml)
{
	_static_rules_xml = rules_xml;
}


void TasklistController::_on_window_list_rom()
{
	_window_list_dirty = true;
	QMetaObject::invokeMethod(this, "applyUpdates", Qt::QueuedConnection);
}


void TasklistController::_on_window_layout_rom()
{
	_window_layout_dirty = true;
	QMetaObject::invokeMethod(this, "applyUpdates", Qt::QueuedConnection);
}


TasklistController::~TasklistController()
{
	/*
 * Phase 14 W11: stop the window_list poll timer before QObject
 * parent-child cleanup runs.
 */
	if (_poll_timer) {
		_poll_timer->stop();
		_poll_timer->deleteLater();
		_poll_timer = nullptr;
	}
}


void TasklistController::_poll()
{
	_lazy_open();
	if (!_window_list_rom.constructed() || !_window_layout_rom.constructed())
		return;

	if (!_poll_timer) {
		_poll_timer = new QTimer(this);
		_poll_timer->setInterval(250);
		connect(_poll_timer, &QTimer::timeout,
		        this, &TasklistController::_poll);
		_poll_timer->start();
	}

	_window_list_rom->update();
	_window_layout_rom->update();
	_window_list_dirty   = true;
	_window_layout_dirty = true;
	applyUpdates();

	/*
	 * On the first poll, publish the initial rules so the layouter
	 * has a complete document to read. Without this, the layouter
	 * in rules="rom" mode sees an empty rules ROM and cannot place
	 * any windows (no <border> elements, no <assign>).
	 */
	static bool first_poll = true;
	if (first_poll) {
		first_poll = false;
		_publish_rules_for(QString());
	}
}


void TasklistController::_lazy_open()
{
	if (_window_list_rom.constructed() && _window_layout_rom.constructed())
		return;

	try {
		_window_list_rom.construct(_env, "window_list");
		_window_list_sigh.construct(_env.ep(), *this, &TasklistController::_on_window_list_rom);
		_window_list_rom->sigh(*_window_list_sigh);
		_window_list_rom->update();
	} catch (...) {
		Genode::log("tasklist_controller: window_list ROM not available, "
		            "tasklist disabled");
		return;
	}

	try {
		_window_layout_rom.construct(_env, "window_layout");
		_window_layout_sigh.construct(_env.ep(), *this, &TasklistController::_on_window_layout_rom);
		_window_layout_rom->sigh(*_window_layout_sigh);
		_window_layout_rom->update();
	} catch (...) {
		Genode::log("tasklist_controller: window_layout ROM not available, "
		            "geometry tracking disabled");
	}

	try {
		_focus_request.construct(_env, "focus_request", "focus_request");
	} catch (...) {
		Genode::log("tasklist_controller: focus_request report not available");
	}

	try {
		_rules_reporter.construct(_env, "rules", "rules");
	} catch (...) {
		Genode::log("tasklist_controller: rules report not available");
	}
}


bool TasklistController::_pull_payloads()
{
	bool updated = false;
	if (_window_list_rom.constructed()) {
		_window_list_rom->update();
		if (_window_list_rom->valid())
			updated = true;
	}
	if (_window_layout_rom.constructed()) {
		_window_layout_rom->update();
		if (_window_layout_rom->valid())
			updated = true;
	}
	return updated;
}


void TasklistController::applyUpdates()
{
	_pull_payloads();
	_recompute_tracked();
	_refresh_widget();
	_maybe_republish_rules();
}

void TasklistController::_maybe_republish_rules()
{
	if (!_rules_reporter.constructed()) return;

	/*
	 * The layouter re-runs its full evaluation (dissolve + re-assign +
	 * reposition every window) not only on window_list and rules changes
	 * but also on HOVER state changes (free_arrange_hover_changed) —
	 * i.e. on ordinary mouse movement between windows. The rules ROM
	 * must therefore carry the CURRENT window geometry at all times, or
	 * any dragged window snaps back at the next hover transition.
	 * Republish when the tracked geometry signature changes, stable for
	 * one extra poll: a manipulation in progress changes the signature
	 * every poll (never stable), so no mid-drag republication.
	 * Maximised round-trip: window_layout does not report the maximized
	 * flag; the heuristic below infers it from the geometry filling the
	 * target area so the republished assigns do not fight the
	 * layouter's maximize state.
	 */
	QStringList sig;
	sig.reserve(_tracked.size());
	for (auto const &st : _tracked) {
		sig.append(QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
		           .arg(st.label)
		           .arg(st.x).arg(st.y).arg(st.w).arg(st.h)
		           .arg(st.minimized ? 1 : 0)
		           .arg(st.maximized ? 1 : 0));
	}

	if (sig == _candidate_rules_sig) {
		if (_rules_sig_stable && sig != _published_rules_sig) {
			_published_rules_sig = sig;
			_publish_rules_for(QString());
		}
		_rules_sig_stable = true;
	} else {
		_candidate_rules_sig = sig;
		_rules_sig_stable   = false;
	}
}


void TasklistController::_refresh_widget()
{
	if (!_widget) return;

	auto const new_entries = _build_task_infos();
	auto const sig         = _signature(new_entries);
	if (sig == _last_emitted_signed)
		return;
	_last_emitted_signed = sig;

	_widget->applyEntries(new_entries);
}


QStringList TasklistController::_signature(QList<TaskInfo> const &entries)
{
	QStringList parts;
	parts.reserve(entries.size());
	for (auto const &t : entries) {
		parts.append(QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8")
		             .arg(t.label)
		             .arg(t.x).arg(t.y).arg(t.w).arg(t.h)
		             .arg(t.focused ? 1 : 0)
		             .arg(t.minimized ? 1 : 0)
		             .arg(t.has_alpha ? 1 : 0));
	}
	return parts;
}


void TasklistController::_recompute_tracked()
{
	if (_window_list_rom.constructed())
		_window_list_rom->update();
	if (_window_layout_rom.constructed())
		_window_layout_rom->update();

	/*
	 * window_list is the identity source: its <window> entries carry
	 * the exact wm label (which itself contains spaces, e.g.
	 * "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo") and the
	 * Qt title as separate attributes. window_layout only has the
	 * concatenated "title" (= label + " " + qt title), so a window's
	 * label is resolved by matching window_layout's title against
	 * each known (label, title) pair — never by splitting on the
	 * first space (that truncates the label to "pkg_runtime" and the
	 * published layouter <assign label=...> would match nothing).
	 * Windows present in window_layout but not yet in window_list
	 * (pending geometry) are skipped for the cycle; they appear once
	 * wm reports them.
	 */
	struct Wm_entry {
		QString label;
		QString title;
		bool    has_alpha  { false };
		bool    hidden     { false };
		bool    resizeable { false };
	};

	QList<Wm_entry> wm_entries;
	if (_window_list_rom.constructed() && _window_list_rom->valid()) {
		try {
			Genode::Node const root = _window_list_rom->node();
			root.for_each_sub_node("window", [&](Genode::Node const &n) {
				Genode::String<256> const label =
					n.attribute_value("label", Genode::String<256>());
				if (label.length() == 0) return;

				Wm_entry e;
				e.label      = QString::fromUtf8(label.string());
				e.title      = QString::fromUtf8(
					n.attribute_value("title", Genode::String<256>()).string());
				e.has_alpha  = n.attribute_value("has_alpha",  false);
				e.hidden     = n.attribute_value("hidden",     false);
				e.resizeable = n.attribute_value("resizeable", false);
				wm_entries.append(e);
			});
		} catch (...) {
			Genode::warning("tasklist_controller: window_list Node invalid");
		}
	}

	QList<Window_state> new_tracked;
	if (_window_layout_rom.constructed() && _window_layout_rom->valid()) {
		try {
			Genode::Node const root = _window_layout_rom->node();
			root.for_each_sub_node("boundary", [&](Genode::Node const &boundary) {

				/*
				 * The boundary's origin: window_layout reports
				 * screen-absolute coordinates but the layouter's
				 * <assign> xpos/ypos are target-relative. Feeding
				 * absolute values back into the rules would drift
				 * the placement by the origin on every
				 * republication cycle (observed as a +28px/cycle
				 * runaway with the panel).
				 */
				_layout_origin_x = boundary.attribute_value("xpos", 0);
				_layout_origin_y = boundary.attribute_value("ypos", 0);
				_layout_area_w    = boundary.attribute_value("width",  0);
				_layout_area_h    = boundary.attribute_value("height", 0);

				boundary.for_each_sub_node("window", [&](Genode::Node const &w) {
					Genode::String<256> const layout_title =
						w.attribute_value("title", Genode::String<256>());
					if (layout_title.length() == 0) return;

					QString const qt = QString::fromUtf8(layout_title.string());

					Wm_entry const *match = nullptr;
					for (auto const &e : wm_entries) {
						QString const composed = e.label + QStringLiteral(" ") + e.title;
						if (qt == composed || qt == e.label) {
							match = &e;
							break;
						}
					}
					if (!match) return;

					Window_state st;
					st.label          = match->label;
					st.x              = w.attribute_value("xpos", 0);
					st.y              = w.attribute_value("ypos", 0);
					st.w              = w.attribute_value("width",  0u);
					st.h              = w.attribute_value("height", 0u);
					st.geometry_known = true;
					st.focused        = w.attribute_value("focused", false);
					st.has_alpha      = match->has_alpha;
					st.hidden         = match->hidden;
					st.resizeable     = match->resizeable;

					if (st.x <= -32000 && st.y <= -32000) {
						st.minimized = true;

						/* A parked window's layout position is the
						 * off-screen sentinel, not its geometry — keep
						 * the last on-screen geometry as the restore
						 * target or a restore would re-park the window. */
						for (auto const &prev : _tracked) {
							if (prev.label != st.label) continue;
							st.x = prev.x; st.y = prev.y;
							st.w = prev.w; st.h = prev.h;
							break;
						}
					}

					bool was_maximized = false;
					for (auto const &prev : _tracked) {
						if (prev.label != st.label) continue;
						st.maximized = prev.maximized;
						was_maximized = prev.maximized
						             && _layout_area_w > 0 && _layout_area_h > 0
						             && prev.w >= (_layout_area_w - 64)
						             && prev.h >= (_layout_area_h - 64);
						break;
					}

					/*
					 * Maximize inference is two-way once a maximized
					 * geometry has been observed. Keeping the inherited
					 * flag for other non-full geometry preserves a new
					 * maximize request until the layouter applies it.
					 */
					bool const fills_area = !st.minimized
					                     && _layout_area_w > 0 && _layout_area_h > 0
					                     && st.w >= (_layout_area_w - 64)
					                     && st.h >= (_layout_area_h - 64);
					if (fills_area)
						st.maximized = true;
					else if (!st.minimized && was_maximized)
						st.maximized = false;

					new_tracked.append(st);
				});
			});
		} catch (...) {
			Genode::warning("tasklist_controller: window_layout Node invalid");
		}
	}

	/*
	 * Absent->present transition -> raise + focus that window once
	 * (a launcher-started window would otherwise stay occluded and
	 * never take input). Rules first, focus_request second: the
	 * window's own <assign> must precede the raise, matching the
	 * restore path in on_task_clicked. Exactly one pick per cycle
	 * (the last/newest new label) — a request per label would be a
	 * focus-stealing sequence. Minimized windows remain tracked and
	 * so never re-trigger; an unchanged ROM refresh has no new label.
	 */
	QString new_label;
	for (auto const &st : new_tracked) {
		if (st.minimized) continue;
		bool known = false;
		for (auto const &prev : _tracked) {
			if (prev.label == st.label) { known = true; break; }
		}
		if (!known) new_label = st.label;
	}

	_tracked = std::move(new_tracked);

	if (!new_label.isEmpty()) {
		_focused_label = new_label;
		_publish_rules_for(new_label);
		_publish_focus_request(new_label);
		Genode::log("tasklist_controller: focus new window '",
		            new_label.toUtf8().constData(), "'");
	}
}


QList<TaskInfo> TasklistController::_build_task_infos() const
{
	QList<TaskInfo> out;
	out.reserve(_tracked.size());
	for (auto const &st : _tracked) {
		TaskInfo t;
		t.label      = st.label;
		t.title      = st.label;
		t.x          = st.x;
		t.y          = st.y;
		t.w          = st.w;
		t.h          = st.h;
		t.focused    = st.focused;
		t.minimized  = st.minimized;
		t.has_alpha  = st.has_alpha;
		out.append(t);
	}
	return out;
}


TasklistController::Window_state *
TasklistController::_find(QString const &label)
{
	for (auto &st : _tracked) {
		if (st.label == label) return &st;
	}
	return nullptr;
}


void TasklistController::on_task_clicked(QString label)
{
	Genode::log("tasklist_ctrl: on_task_clicked label='", label.toUtf8().constData(),
	            "' tracked=", (int)_tracked.size());
	Window_state *st = _find(label);
	if (!st) {
		Genode::warning("tasklist_controller: click on unknown label ",
		                label.toUtf8().constData());
		return;
	}

	if (st->minimized) {
		/* Restore. */
		st->minimized = false;
		st->focused   = true;
		_focused_label = label;
		_publish_rules_for(label);
		_publish_focus_request(label);
		Genode::log("tasklist_controller: restore ", label.toUtf8().constData(),
		            " -> (", st->x, ",", st->y, ") ", st->w, "x", st->h);
	} else {
		/* Minimize (off-screen). */
		st->minimized = true;
		st->focused   = false;
		_publish_rules_for(label);
		Genode::log("tasklist_controller: minimize ", label.toUtf8().constData());
	}

	applyUpdates();
}


void TasklistController::on_toggle_maximized(QString label)
{
	Window_state *st = _find(label);
	if (!st) {
		Genode::warning("tasklist_controller: toggle_maximized on unknown label ",
		                label.toUtf8().constData());
		return;
	}

	st->maximized = !st->maximized;
	_publish_rules_for(label);
	Genode::log("tasklist_controller: toggle_maximized ", label.toUtf8().constData(),
	            " -> maximized=", st->maximized ? "yes" : "no");
	applyUpdates();
}


/*
 * Phase 16 W7 (U16.4 / D16.5) — keyboard-shortcut action slot for
 * the `focus_next` event_filter shortcut (default binding Alt-Tab).
 *
 * Walks `_tracked` starting AFTER `_focused_label` and picks the
 * next non-minimized window (forward-only, wrapping). The window's
 * `focused` state is set to true + a `focus_request` Report is
 * published (the wm + layouter chain raises the window; the
 * state-machine update reflects in the next applyUpdates() tick).
 *
 * Edge cases:
 *   - Empty _tracked: no-op (warning logged). The user is unlikely
 *     to press Alt-Tab with no windows; the framework's defense
 *     against silent drops is the warning + no action.
 *   - Single non-minimized window: re-focus it (no-op cycle but
 *     the focus_request re-asserts). Skipping this would leave
 *     the focus on a different window (no Alt-Tab effect).
 *   - All minimized: no-op + warning. Pressing Alt-Tab with no
 *     visible windows is a no-op (the user has nothing to focus).
 */
void TasklistController::cycle_focus()
{
	if (_tracked.isEmpty()) {
		Genode::warning("tasklist_controller: cycle_focus: no tracked windows");
		return;
	}

	/*
	 * Find the index of the currently-focused window in _tracked.
	 * If no focused window or the focused window is not in _tracked
	 * (e.g. it was hidden by wm), start from index -1 so the next
	 * cycle picks _tracked[0] (wraps once).
	 */
	int focused_idx { -1 };
	if (!_focused_label.isEmpty()) {
		for (int i = 0; i < _tracked.size(); ++i) {
			if (_tracked[i].label == _focused_label) {
				focused_idx = i;
				break;
			}
		}
	}

	int const n = _tracked.size();
	QString next_label { };

	/*
	 * Walk forward up to n positions. Skips minimized windows
	 * (focusing a minimized window would un-minimize it — the
	 * plan is explicit that the cycle is focus-only, not a
	 * un-minimize toggle). Wraps once.
	 */
	for (int step = 1; step <= n; ++step) {
		int const idx = (focused_idx + step + n) % n;
		Window_state const &cand = _tracked[idx];
		if (!cand.minimized && cand.geometry_known) {
			next_label = cand.label;
			break;
		}
	}

	if (next_label.isEmpty()) {
		Genode::warning("tasklist_controller: cycle_focus: no non-minimized window to focus");
		return;
	}

	Genode::log("tasklist_controller: cycle_focus ", _focused_label.toUtf8().constData(),
	            " -> ", next_label.toUtf8().constData());

	/*
	 * Update the focused state and publish the focus_request
	 * Report. The wm + layouter pick up the focus_request;
	 * applyUpdates() on the next tick refreshes the widget
	 * visual state (Focused highlight on the new task).
	 */
	for (auto &st : _tracked) {
		st.focused = (st.label == next_label);
	}
	_focused_label = next_label;
	_publish_focus_request(next_label);
	_publish_rules_for(QString());

	applyUpdates();
}


void TasklistController::_publish_focus_request(QString const &label)
{
	if (!_focus_request.constructed()) return;

	_focus_request_id++;
	_focus_request->generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("id",    _focus_request_id);
		g.attribute("label", label.toUtf8().constData());
	});
}


void TasklistController::_publish_rules_for(QString const &label)
{
	if (!_rules_reporter.constructed()) return;

	_rules_reporter->generate_xml([&](Genode::Xml_generator &g) {
		_compose_rules(g, label);
	});
}


void TasklistController::_compose_rules(Genode::Xml_generator &g, QString const &target_label)
{
	/* The Expanding_reporter's generate_xml already wraps the
	 * payload in the reporter's node type (<rules>) — emit the
	 * children directly or the document comes out double-wrapped. */
	g.node("screen", [&] {
		g.attribute("name", "screen");
	});

	/* Tracked windows before static rules: the layouter retains the
	 * FIRST matching assignment per window, so the per-window exact
	 * <assign> (which carries minimize/restore geometry) must precede
	 * the broad label_prefix fallback.
	 *
	 * Stacking is defined by assign order: the EARLIEST assign is the
	 * frontmost window (window_layouter README). The target window
	 * therefore comes first — with _focused_label as fallback for the
	 * empty-target republish — or a routine republication re-stacks
	 * tracked order over the focused window. Each window is emitted
	 * exactly once. */
	QString const front = target_label.isEmpty() ? _focused_label
	                                              : target_label;
	for (auto const &w : _tracked) {
		if (w.label == front)
			_append_assign_for(g, w);
	}
	for (auto const &w : _tracked) {
		if (w.label != front)
			_append_assign_for(g, w);
	}

	_emit_static_rules(g);

	g.node("press", [&] {
		g.attribute("key",    "BTN_LEFT");
		g.attribute("action", "drag");
	});
	g.node("release", [&] {
		g.attribute("key",    "BTN_LEFT");
		g.attribute("action", "drop");
	});
}


void TasklistController::_emit_static_rules(Genode::Xml_generator &g) const
{
	if (_static_rules_xml.isEmpty())
		return;

	try {
		QByteArray const bytes = _static_rules_xml.toUtf8();
		Genode::Node const fragment(Genode::Const_byte_range_ptr(
			bytes.constData(), Genode::size_t(bytes.size())));

		auto emit_assign = [&] (Genode::Node const &asg) {
			g.node("assign", [&] {
				asg.for_each_attribute([&] (Genode::Node::Attribute const &attr) {
					char value[256];
					Genode::size_t const n =
						attr.value.num_bytes < sizeof(value) - 1
						? attr.value.num_bytes : sizeof(value) - 1;
					Genode::memcpy(value, attr.value.start, n);
					value[n] = '\0';
					g.attribute(attr.name.string(), value);
				});
			});
		};

		if (fragment.has_type("assign"))
			emit_assign(fragment);
		else
			fragment.for_each_sub_node("assign", emit_assign);
	} catch (...) {
		Genode::warning("tasklist: malformed tasklist_static_rules ignored");
	}
}


void TasklistController::_append_assign_for(Genode::Xml_generator &g, Window_state const &w) const
{
	g.node("assign", [&] {
		g.attribute("label",  w.label.toUtf8().constData());
		g.attribute("target", "screen");

		int x = w.x - _layout_origin_x;
		int y = w.y - _layout_origin_y;
		if (w.minimized) {
			x = -32000;
			y = -32000;
		}
		g.attribute("xpos",  x);
		g.attribute("ypos",  y);
		g.attribute("width", (long)w.w);
		g.attribute("height", (long)w.h);
		g.attribute("maximized", w.maximized ? "yes" : "no");
	});
}


/*
 * Phase 16 W6/W7 follow-up (U16.3) — show-desktop toggle driver.
 * Flips the `minimized` flag on every tracked window and republishes
 * the layouter-rule ROM (which causes the layouter to re-position
 * each window at the parked (-32000, -32000) sentinel — the W7
 * tasklist state machine's parking path at tasklist_controller.cc:
 * 500-516). The state machine's `_tracked` cache preserves the
 * non-minimized geometry across the parked state, so a `false`
 * argument restores each window to its last-seen geometry.
 *
 * The "Show desktop" context-menu entry on BackgroundWidget calls
 * this via ShowDesktop::toggle. The toggle is two-state (show vs
 * restore); the menu shows the current state via the
 * show_desktop_action's checkable flag (BackgroundWidget's
 * contextMenuEvent).
 *
 * The slot updates the widget's per-window TaskInfo cache via the
 * standard applyUpdates() tick so the panel tasklist reflects the
 * new state immediately.
 *
 * GUI thread ONLY (failure-point 2 enforcement: marshalled from
 * BackgroundWidget::show_context_menu via QMetaObject::invokeMethod
 * with Qt::QueuedConnection when the menu slot fires).
 */
void TasklistController::set_all_minimized(bool minimize)
{
	if (_tracked.isEmpty()) {
		Genode::log("tasklist_controller: set_all_minimized: no tracked windows; no-op");
		return;
	}

	unsigned changed { 0 };
	for (auto &st : _tracked) {
		if (st.minimized != minimize) {
			st.minimized = minimize;
			st.focused   = false;
			++changed;
		}
	}

	if (changed == 0) {
		Genode::log("tasklist_controller: set_all_minimized: already in target state");
		return;
	}

	_publish_rules_for(QString());

	Genode::log("tasklist_controller: set_all_minimized ", minimize ? "ON" : "OFF",
	            " (", changed, " window", changed == 1 ? "" : "s", " affected)");

	applyUpdates();
}


/*
 * Criterion 18 second-open focus handoff (see the _settings_focus_hint_*
 * member block in tasklist_controller.h for the wire contract).
 *
 * Entrypoint-thread ROM signal handler: queues the GUI-thread consumer
 * via the _on_window_list_rom marshalling idiom (failure-point 2 — the
 * handler must not touch controller state or the ROM from the Genode
 * entrypoint).
 */
void TasklistController::_on_settings_focus_hint_rom()
{
	QMetaObject::invokeMethod(this, "applySettingsFocusHint",
	                          Qt::QueuedConnection);
}


/*
 * One-time, config-gated ROM arming. The `settings_focus_hint` ROM is
 * opened only when THIS component's config carries
 * <settings_focus_hint/> (the NotifierController config-gate pattern);
 * without the marker no session is ever requested, so topologies that
 * do not route the label are byte-identical to before. A failed open
 * logs a bounded warning and leaves hint processing disabled.
 */
void TasklistController::_lazy_open_settings_focus_hint()
{
	if (_settings_focus_hint_checked)
		return;
	_settings_focus_hint_checked = true;

	bool enabled { false };
	try {
		Genode::Attached_rom_dataspace const config(_env, "config");
		config.node().with_optional_sub_node("settings_focus_hint",
			[&] (Genode::Node const &) { enabled = true; });
	} catch (...) {
		Genode::warning("tasklist_controller: config unreadable — "
		                "settings_focus_hint disabled");
		return;
	}

	if (!enabled)
		return;

	try {
		_settings_focus_hint_rom.construct(_env, "settings_focus_hint");
		_settings_focus_hint_sigh.construct(
			_env.ep(), *this, &TasklistController::_on_settings_focus_hint_rom);
		_settings_focus_hint_rom->sigh(*_settings_focus_hint_sigh);
		_settings_focus_hint_rom->update();
		Genode::log("tasklist_controller: settings_focus_hint enabled");
	} catch (...) {
		Genode::log("tasklist_controller: settings_focus_hint ROM not "
		            "available, hint disabled");
	}
}


/*
 * GUI-thread consumer. Pulls the hint document (report_rom keeps only
 * the latest emission), de-duplicates on the raw document — pkgd stamps
 * a fresh 'seq' per emission, so a repeated focused open is a new
 * document while a stale re-delivery is a no-op — validates the
 * pkg/id/start_name identity, and hands the start_name to the raise.
 * Every failure mode (absent channel, malformed document, missing
 * attributes) is a bounded one-shot warning per distinct document and
 * never raises anything.
 */
void TasklistController::applySettingsFocusHint()
{
	_lazy_open_settings_focus_hint();
	if (!_settings_focus_hint_rom.constructed())
		return;

	_settings_focus_hint_rom->update();
	if (!_settings_focus_hint_rom->valid())
		return;

	char const *const base = _settings_focus_hint_rom->local_addr<char>();
	Genode::size_t  const sz  = _settings_focus_hint_rom->size();

	QString const payload = QString::fromUtf8(base, (int)sz);
	if (payload == _last_settings_focus_hint)
		return;

	try {
		Genode::Node const root = _settings_focus_hint_rom->node();

		if (!root.has_type("settings_focus_hint")) {
			Genode::warning("tasklist_controller: settings_focus_hint: "
			                "unexpected document — ignored");
			_last_settings_focus_hint = payload;
			return;
		}

		/* Contract widths of the producer (pkgd's settings result
		 * helpers / settings_start_name): a valid settings id is at
		 * most 32 chars and fits Genode::String<33> untruncated. */
		Genode::String<128> const pkg        = root.attribute_value("pkg",        Genode::String<128>());
		Genode::String<33>  const id         = root.attribute_value("id",         Genode::String<33>());
		Genode::String<64>  const start_name = root.attribute_value("start_name", Genode::String<64>());

		if (pkg.length() == 0 || id.length() == 0 || start_name.length() == 0) {
			Genode::warning("tasklist_controller: settings_focus_hint: "
			                "missing pkg/id/start_name — ignored");
			_last_settings_focus_hint = payload;
			return;
		}

		Genode::String<64> const expected_start(pkg, ".", id);
		if (start_name != expected_start) {
			Genode::warning("tasklist_controller: settings_focus_hint: "
			                "start_name does not match pkg/id — ignored");
			_last_settings_focus_hint = payload;
			return;
		}

		if (_raise_hinted_window(QString::fromUtf8(start_name.string())))
			_last_settings_focus_hint = payload;
	} catch (...) {
		Genode::warning("tasklist_controller: settings_focus_hint: "
		                "malformed document — ignored");
		_last_settings_focus_hint = payload;
	}
}


/*
 * Raise + focus the provider window for a hint start_name. Mirrors the
 * RESTORE half of on_task_clicked (including un-parking a minimized
 * window) but never its minimize toggle, and reuses the same private
 * publication paths: rules first so the window's own <assign> precedes
 * the raise (the _recompute_tracked new-window ordering), then
 * focus_request — whose sole writer stays this controller. Returns
 * false only when the window is not tracked yet, so the next poll can
 * retry after window_list catches up with the provider's first paint.
 */
bool TasklistController::_raise_hinted_window(QString const &start_name)
{
	QString matched;
	int     num_matched { 0 };
	for (auto const &st : _tracked) {
		if (!_label_matches_start_name(st.label, start_name))
			continue;
		if (matched.isEmpty())
			matched = st.label;
		++num_matched;
	}

	if (matched.isEmpty())
		return false;

	if (num_matched != 1) {
		Genode::warning("tasklist_controller: settings_focus_hint: ",
		                num_matched, " tracked windows match '",
		                start_name.toUtf8().constData(),
		                "' — ambiguous target ignored");
		return true;
	}

	for (auto &st : _tracked) {
		if (st.label != matched) {
			st.focused = false;
			continue;
		}
		if (st.minimized) {
			st.minimized = false;
			Genode::log("tasklist_controller: settings_focus_hint restores '",
			            matched.toUtf8().constData(), "'");
		}
		st.focused = true;
	}
	_focused_label = matched;

	_publish_rules_for(matched);
	_publish_focus_request(matched);

	Genode::log("tasklist_controller: settings_focus_hint '",
	            start_name.toUtf8().constData(), "' -> raise '",
	            matched.toUtf8().constData(), "'");

	applyUpdates();
	return true;
}


/*
 * Full-label / segment / prefix match of a settings <start> name
 * ("<pkg>.<id>") against a tracked wm label. The tracked label is the
 * " -> "-joined session-label chain (e.g. "pkg_runtime ->
 * sponge-de.panel -> Sponge Settings"), so the start name is matched as
 * a whole chain segment, or — with the native window_list_find_prefix
 * strncmp semantics — as the label's leading segment. The boundary
 * checks keep 'sponge-de.panel' from matching a 'sponge-de.panel2'
 * label (the probe-side helper tolerates that only because its prefixes
 * are multi-segment chains). No fuzzy matching: an unrelated window
 * must never be raised by a lookalike name.
 */
bool TasklistController::_label_matches_start_name(QString const &label,
                                                   QString const &start_name)
{
	if (label.isEmpty() || start_name.isEmpty())
		return false;

	if (label == start_name)
		return true;

	qsizetype const n = start_name.size();

	if (label.startsWith(start_name)) {
		if (label.size() == n)
			return true;
		if (label.mid(n, 4) == QStringLiteral(" -> "))
			return true;
	}

	for (QString const &seg : label.split(QStringLiteral(" -> ")))
		if (seg == start_name)
			return true;

	return false;
}
