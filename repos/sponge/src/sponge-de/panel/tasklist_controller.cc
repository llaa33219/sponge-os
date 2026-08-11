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
}


void TasklistController::set_static_rules(QStringList static_rules)
{
	_static_rules = std::move(static_rules);
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
			Genode::warning("tasklist_controller: window_list XML invalid");
		}
	}

	if (_window_layout_rom.constructed() && _window_layout_rom->valid()) {
		try {
			Genode::Xml_node const root = _window_layout_rom->xml();
			root.for_each_sub_node("boundary", [&](Genode::Xml_node const &boundary) {
				boundary.for_each_sub_node("window", [&](Genode::Xml_node const &w) {
					Genode::String<256> const title =
						w.attribute_value("title", Genode::String<256>());
					if (title.length() == 0) return;

					QString const qtitle = QString::fromUtf8(title.string());
					int const sp = qtitle.indexOf(' ');
					QString const label_str =
						(sp < 0) ? qtitle : qtitle.left(sp);

					for (auto &st : new_tracked) {
						if (st.label != label_str) continue;
						st.x = w.attribute_value("xpos", 0);
						st.y = w.attribute_value("ypos", 0);
						st.w = w.attribute_value("width",  0u);
						st.h = w.attribute_value("height", 0u);
						st.geometry_known = true;
						st.focused = w.attribute_value("focused", false);

						if (st.x <= -32000 && st.y <= -32000) {
							st.minimized = true;
						} else {
							st.minimized = false;
						}
						break;
					}
				});
			});
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("tasklist_controller: window_layout XML invalid");
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
	Window_state *st = _find(label);
	if (!st) {
		Genode::warning("tasklist_controller: click on unknown label ",
		                label.toUtf8().constData());
		return;
	}

	if (st->minimized) {
		/* Restore. */
		_publish_rules_for(label);
		_publish_focus_request(label);
		st->minimized = false;
		st->focused   = true;
		Genode::log("tasklist_controller: restore ", label.toUtf8().constData(),
		            " -> (", st->x, ",", st->y, ") ", st->w, "x", st->h);
	} else {
		/* Minimize (off-screen). */
		_publish_rules_for(label);
		st->minimized = true;
		st->focused   = false;
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
	g.node("rules", [&] {
		g.node("screen", [&] {
			g.attribute("name", "screen");
		});

		for (auto const &w : _tracked) {
			_append_assign_for(g, w);
		}

		g.node("press", [&] {
			g.attribute("key",    "BTN_LEFT");
			g.attribute("action", "drag");
		});
		g.node("release", [&] {
			g.attribute("key",    "BTN_LEFT");
			g.attribute("action", "drop");
		});
	});

	(void)target_label;
}


void TasklistController::_append_assign_for(Genode::Xml_generator &g, Window_state const &w) const
{
	g.node("assign", [&] {
		g.attribute("label",  w.label.toUtf8().constData());
		g.attribute("target", "screen");

		int x = w.x, y = w.y;
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
