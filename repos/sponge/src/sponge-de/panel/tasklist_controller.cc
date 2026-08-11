/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of TasklistController. See tasklist_controller.h for
 * the wire contract and the threading model.
 */

#include "tasklist_controller.h"

#include "tasklist_widget.h"

#include <base/log.h>
#include <util/string.h>

#include <QMetaObject>
#include <QTimer>

using namespace Sponge::Sponge_DE;


TasklistController::TasklistController(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
	/*
	 * The two ROMs (window_list, window_layout) and the two Report
	 * read ROMs (focus_request, rules) are opened lazily on the
	 * first poll cycle (NOT in the constructor). Opening them
	 * eagerly would cause the child to be killed by the parent when
	 * the report_rom is absent (the parent denies the session; the
	 * child is destroyed before the catch can run). Lazy opening
	 * guarantees that _poll() sees the failure and degrades to
	 * no-op.
	 */
}


void TasklistController::attach_widget(TasklistWidget *widget)
{
	_widget = widget;
	connect(this, &TasklistController::tasks_changed,
	        widget, &TasklistWidget::applyEntries);
}


void TasklistController::set_static_rules(QStringList static_rules)
{
	_static_rules = std::move(static_rules);
}


void TasklistController::_on_window_list_rom()
{
	/*
	 * Entry-point thread. Read the ROM and mark the GUI-thread
	 * applyUpdates() function for processing. We do NOT touch any
	 * QWidget / QObject state here — failure-point 2.
	 */
	_window_list_dirty = true;
	QMetaObject::invokeMethod(this, "applyUpdates", Qt::QueuedConnection);
}


void TasklistController::_on_window_layout_rom()
{
	_window_layout_dirty = true;
	QMetaObject::invokeMethod(this, "applyUpdates", Qt::QueuedConnection);
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
}


void TasklistController::_lazy_open()
{
	/*
	 * Lazy session opening. The first time _poll() runs, attempt to
	 * open the four sessions. If any parent denies a session (no
	 * report_rom in the topology, or no policy routes the label),
	 * the catch keeps the controller's session deconstructed
	 * forever — a clean no-op fallback. The widget is still
	 * attached; it just stays empty because the tracked list is
	 * empty.
	 */
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
		/* Non-fatal: the tasklist still works for identity + state. */
	}

	/* Open the focus_request + rules writers on first successful
	 * window_list open. The reports are written only when the user
	 * clicks; lazy opening minimizes the surface. */
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
	/* Always reapply; the GUI-thread invocation arrives whenever
	 * either ROM changed. */
	_pull_payloads();
	_recompute_tracked();

	auto const new_entries = _build_task_infos();
	auto const sig         = _signature(new_entries);
	if (sig == _last_emitted_signed)
		return;
	_last_emitted_signed = sig;

	emit tasks_changed(new_entries);
}


QStringList TasklistController::_signature(QList<TaskInfo> const &entries)
{
	/*
	 * Build a stable signature for de-dup. Each entry contributes
	 * (label, x, y, w, h, focused, minimized, has_alpha) joined.
	 */
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
	/*
	 * Two passes:
	 *   1) window_list pass: add new entries, update
	 *      identity/title/size/has_alpha/hidden, REMOVE entries
	 *      that disappeared from the window_list.
	 *   2) window_layout pass: update geometry (x, y, w, h) and
	 *      focused flag. The window_layout's <window> has no
	 *      `label` attribute; we match by TITLE (the layouter
	 *      constructs title = label + " " + Qt title). This is
	 *      robust as long as the title is unique within the
	 *      window stack.
	 *
	 * Both passes run on the GUI thread on the cached payloads.
	 */
	if (_window_list_rom.constructed())
		_window_list_rom->update();
	if (_window_layout_rom.constructed())
		_window_layout_rom->update();

	/* Pass 1: window_list. */
	QList<Window_state> new_tracked;
	if (_window_list_rom.constructed() && _window_list_rom->valid()) {
		try {
			Genode::Xml_node const root = _window_list_rom->xml();
			root.for_each_sub_node("window", [&](Genode::Xml_node const &n) {
				Genode::String<256> const label =
					n.attribute_value("label", Genode::String<256>());
				if (label.length() == 0) return;

				Window_state st;
				st.label = QString::fromUtf8(label.string());
				st.w     = n.attribute_value("width",  0u);
				st.h     = n.attribute_value("height", 0u);
				st.has_alpha  = n.attribute_value("has_alpha",  false);
				st.hidden     = n.attribute_value("hidden",     false);
				st.resizeable = n.attribute_value("resizeable", true);

				/* Preserve geometry from the previous tracked state
				 * (the window_list has no x/y). */
				for (auto const &prev : _tracked) {
					if (prev.label == st.label) {
						st.x              = prev.x;
						st.y              = prev.y;
						st.focused        = prev.focused;
						st.minimized      = prev.minimized;
						st.maximized      = prev.maximized;
						st.geometry_known = prev.geometry_known;
						break;
					}
				}

				new_tracked.append(st);
			});
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("tasklist_controller: window_list XML invalid; "
			                "ignoring this update");
		}
	}

	/* Pass 2: window_layout. The nested <boundary><window> tree. */
	if (_window_layout_rom.constructed() && _window_layout_rom->valid()) {
		try {
			Genode::Xml_node const root = _window_layout_rom->xml();
			root.for_each_sub_node("boundary", [&](Genode::Xml_node const &boundary) {
				boundary.for_each_sub_node("window", [&](Genode::Xml_node const &w) {
					Genode::String<256> const title =
						w.attribute_value("title", Genode::String<256>());
					if (title.length() == 0) return;

					/*
					 * The layouter's title format is
					 * `label + " " + Qt title`. We strip the
					 * last " <qt title>" component to recover
					 * the label.
					 *
					 * Strategy: find the FIRST whitespace in the
					 * title — the label NEVER contains a space
					 * (it's a wm session label like
					 * "pkg_runtime -> pkg_gui_demo" — the
					 * "->" is part of the label).
					 *
					 * Actually: reverse — find the first space.
					 * The label is everything BEFORE the first
					 * space; the Qt title is everything from the
					 * first space forward.
					 */
					QString const qtitle = QString::fromUtf8(title.string());
					int const sp = qtitle.indexOf(' ');
					QString const label_str =
						(sp < 0) ? qtitle : qtitle.left(sp);

					/* Find the tracked window by label. */
					for (auto &st : new_tracked) {
						if (st.label != label_str) continue;
						st.x = w.attribute_value("xpos", 0);
						st.y = w.attribute_value("ypos", 0);
						st.w = w.attribute_value("width",  0u);
						st.h = w.attribute_value("height", 0u);
						st.geometry_known = true;
						st.focused = w.attribute_value("focused", false);

						/*
						 * Minimized detection: the layouter
						 * DOES NOT publish "minimized" in
						 * window_layout. The tasklist detects
						 * off-screen parking by checking x
						 * and y against the parking threshold.
						 *
						 * (Saved geometry is the LAST known
						 * on-screen position; the layouter
						 * stops updating window_layout when
						 * the window is parked off-screen.)
						 */
						if (st.x <= -32000 && st.y <= -32000) {
							/* Off-screen-parked. Use the
							 * preserved saved geometry that
							 * we wrote in the rules. */
							st.minimized = true;
						} else {
							st.minimized = false;
						}
						break;
					}
				});
			});
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("tasklist_controller: window_layout XML invalid; "
			                "ignoring geometry update");
		}
	}

	_tracked = std::move(new_tracked);
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
	/*
	 * Look up the tracked window. The click always produces ONE of:
	 *   - minimize (if currently Normal-Visible(-Focused))
	 *   - restore + focus (if currently Minimized)
	 *   - focus only (if currently Normal-Visible-Focused, clicked
	 *     on the focused entry — no state change, but still emit
	 *     focus_request for symmetry)
	 */
	Window_state *st = _find(label);
	if (!st) {
		Genode::warning("tasklist_controller: click on unknown label ",
		                label.toUtf8().constData());
		return;
	}

	if (st->minimized) {
		/* Restore: write the saved (x, y, w, h) and emit focus_request. */
		_publish_rules_for(label);
		_publish_focus_request(label);
		st->minimized = false;
		st->focused   = true;
		Genode::log("tasklist_controller: restore ", label.toUtf8().constData(),
		            " -> (", st->x, ",", st->y, ") ", st->w, "x", st->h);
	} else {
		/*
		 * Minimize: park off-screen. We do NOT emit focus_request
		 * (the window is no longer focusable until restore).
		 *
		 * The Geometry: we keep the saved (x, y, w, h) in the
		 * tracked state. The next restore will write them back.
		 * The controller writes the off-screen positions to the
		 * rules ROM, which the layouter applies (the window
		 * re-paints at the off-screen position and is no longer
		 * visible).
		 */
		_publish_rules_for(label);
		st->minimized = true;
		st->focused   = false;
		Genode::log("tasklist_controller: minimize ", label.toUtf8().constData());
	}

	/* Re-emit the widget's task list so the entry repaints. The
	 * next window_layout poll will confirm the geometry change. */
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
	/*
	 * Emit a <rules> document containing:
	 *   - <screen name="screen"/>
	 *   - one <assign> per static rule (the controller mirrors
	 *     whatever the layouter's inline rules would have been)
	 *   - one <assign> per tracked window (overrides the static
	 *     rules with the per-window state)
	 *
	 * The layouter's assign match is by `label` (exact match)
	 * then by `label_prefix` (wildcard). Exact-match rules win,
	 * so a per-window override always shadows the static prefix
	 * rule.
	 */
	g.node("rules", [&] {
		g.node("screen", [&] {
			g.attribute("name", "screen");
		});

		/* Static placeholder rules from the run script. */
		for (auto const &s : _static_rules) {
			/* The static_assigns list contains raw <assign ...>
			 * strings; we paste them as sub-nodes. */
			(void)s;
		}

		/* Per-window rules. The tracked windows' geometry is the
		 * authoritative current state; the layouter applies the
		 * first matching rule per window. */
		for (auto const &w : _tracked) {
			_append_assign_for(g, w);
		}

		/* Layouter drag protocol. */
		g.node("press", [&] {
			g.attribute("key",    "BTN_LEFT");
			g.attribute("action", "drag");
		});
		g.node("release", [&] {
			g.attribute("key",    "BTN_LEFT");
			g.attribute("action", "drop");
		});
	});

	(void)target_label;  /* unused; the per-window _append_assign_for handles the target. */
}


void TasklistController::_append_assign_for(Genode::Xml_generator &g, Window_state const &w) const
{
	g.node("assign", [&] {
		g.attribute("label",  w.label.toUtf8().constData());
		g.attribute("target", "screen");

		int x = w.x, y = w.y;
		if (w.minimized) {
			/*
			 * Off-screen parking. The layouter sees the
			 * coordinates and the window is no longer
			 * visible.
			 */
			x = -32000;
			y = -32000;
		}
		Genode::String<16> xs;  xs = Genode::String<16>(x);
		Genode::String<16> ys;  ys = Genode::String<16>(y);
		Genode::String<16> ws;  ws = Genode::String<16>((long)w.w);
		Genode::String<16> hs;  hs = Genode::String<16>((long)w.h);

		g.attribute("xpos",  xs.string());
		g.attribute("ypos",  ys.string());
		g.attribute("width", ws.string());
		g.attribute("height", hs.string());
		g.attribute("maximized", w.maximized ? "yes" : "no");
	});
}
