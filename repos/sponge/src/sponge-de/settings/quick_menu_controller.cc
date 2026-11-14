/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of QuickMenuController. See quick_menu_controller.h.
 */

#include "quick_menu_controller.h"

#include "../launcher/launcher_controller.h"

#include <base/log.h>
#include <util/xml_node.h>

#include <QDateTime>
#include <QTimer>

using namespace Sponge::Sponge_DE;


namespace {

constexpr qint64  CHANNEL_RETRY_MS = 5000;
constexpr unsigned QUICK_POLL_MAX  = 60;
constexpr int     QUICK_POLL_MS    = 100;

}  /* namespace */


QuickMenuController::QuickMenuController(Genode::Env &env, QObject *parent)
:
	QObject(parent),
	_env(env)
{
	_panel_ch.request_label = "panel_settings_quick_request";
	_panel_ch.result_label  = "panel_settings_quick_result";
	_bg_ch.request_label    = "background_settings_quick_request";
	_bg_ch.result_label     = "background_settings_quick_result";
}


void QuickMenuController::attach_launcher_controller(LauncherController *launcher)
{
	if (_launcher)
		disconnect(_launcher, nullptr, this, nullptr);

	_launcher = launcher;
	if (!_launcher)
		return;

	connect(_launcher, &LauncherController::settings_open_ok,
	        this, &QuickMenuController::_on_settings_open_ok);
	connect(_launcher, &LauncherController::settings_open_failed,
	        this, &QuickMenuController::_on_settings_open_failed);
}


QuickMenuController::Channel &
QuickMenuController::_channel(Target target)
{
	return target == Target::PANEL ? _panel_ch : _bg_ch;
}


char const *QuickMenuController::_settings_id(Target target) const
{
	return target == Target::PANEL ? "panel" : "background";
}


bool QuickMenuController::_open_channel(Channel &ch)
{
	if (ch.request.constructed() && ch.result.constructed())
		return true;

	qint64 const now_ms = QDateTime::currentMSecsSinceEpoch();

	if (ch.failed) {
		qint64 const remaining = CHANNEL_RETRY_MS - (now_ms - ch.attempt_ms);
		if (remaining > 0) {
			Genode::log("sponge-de: quick channel ", ch.request_label,
			            " still unavailable; next retry in ", remaining, " ms");
			return false;
		}
	}

	ch.attempt_ms = now_ms;

	try {
		if (!ch.request.constructed())
			ch.request.construct(_env, "quick_request", ch.request_label);
		if (!ch.result.constructed())
			ch.result.construct(_env, ch.result_label);
	} catch (Genode::Exception &) {
		ch.failed = true;
		Genode::warning("sponge-de: opening quick channel ", ch.request_label,
		                " failed; retrying at most every ",
		                (unsigned)CHANNEL_RETRY_MS, " ms");
		return false;
	}

	if (!ch.request.constructed() || !ch.result.constructed()) {
		ch.failed = true;
		Genode::warning("sponge-de: quick channel ", ch.request_label,
		                " still unopened after a construct attempt");
		return false;
	}

	if (ch.failed)
		Genode::log("sponge-de: quick channel ", ch.request_label,
		            " opened (retry succeeded)");
	ch.failed = false;
	return true;
}


void QuickMenuController::change_panel_height(QString const &panel_id, int px)
{
	if (px < 16 || px > 128) {
		emit quick_change_failed(QStringLiteral("panel height: must be between 16 and 128"));
		return;
	}

	Change c;
	c.target = Target::PANEL;
	c.op     = "height";
	c.id     = panel_id.toUtf8();
	c.value  = QByteArray::number(px);
	c.what   = QStringLiteral("panel height");
	if (!panel_id.isEmpty())
		c.what += QStringLiteral(" (") + panel_id + QStringLiteral(")");
	_submit(c);
}


void QuickMenuController::change_panel_position(QString const &panel_id,
                                                QString const &position)
{
	if (position != QLatin1String("top") && position != QLatin1String("bottom")) {
		emit quick_change_failed(QStringLiteral("panel position: only top and bottom are available"));
		return;
	}

	Change c;
	c.target = Target::PANEL;
	c.op     = "position";
	c.id     = panel_id.toUtf8();
	c.value  = position.toUtf8();
	c.what   = QStringLiteral("panel position");
	if (!panel_id.isEmpty())
		c.what += QStringLiteral(" (") + panel_id + QStringLiteral(")");
	_submit(c);
}


void QuickMenuController::change_panel_widgets(QString const &panel_id,
                                               QString const &widgets)
{
	if (widgets.isEmpty()) {
		emit quick_change_failed(QStringLiteral("panel widgets: empty list refused"));
		return;
	}

	Change c;
	c.target = Target::PANEL;
	c.op     = "widgets";
	c.id     = panel_id.toUtf8();
	c.value  = widgets.toUtf8();
	c.what   = QStringLiteral("panel widgets");
	if (!panel_id.isEmpty())
		c.what += QStringLiteral(" (") + panel_id + QStringLiteral(")");
	_submit(c);
}


void QuickMenuController::change_background_color(QString const &hex)
{
	if (!hex.startsWith(QLatin1Char('#')) || hex.size() != 7) {
		emit quick_change_failed(QStringLiteral("background color: expected #RRGGBB"));
		return;
	}

	Change c;
	c.target = Target::BACKGROUND;
	c.op     = "color";
	c.value  = hex.toUtf8();
	c.what   = QStringLiteral("background color");
	_submit(c);
}


void QuickMenuController::change_background_image(QString const &path)
{
	if (path.isEmpty()) {
		emit quick_change_failed(QStringLiteral("background image: empty path refused"));
		return;
	}

	Change c;
	c.target = Target::BACKGROUND;
	c.op     = "image";
	c.value  = path.toUtf8();
	c.what   = QStringLiteral("background image");
	_submit(c);
}


void QuickMenuController::open_panel_settings()
{
	_submit_open(Target::PANEL);
}


void QuickMenuController::open_background_settings()
{
	_submit_open(Target::BACKGROUND);
}


void QuickMenuController::_emit_superseded()
{
	if (_phase == Phase::IDLE)
		return;

	QString const message = _pending.what +
	                        QStringLiteral(": superseded by a newer quick action");
	Genode::warning("sponge-de: quick change failed: ",
	                message.toUtf8().constData());
	emit quick_change_failed(message);
}


void QuickMenuController::_submit_open(Target target)
{
	/*
	 * Serve-both rule (header): any operation already in flight
	 * for this target already runs the idempotent start-or-focus
	 * ensure-start this open needs, and its completion reports the
	 * shared outcome. Never queue a second launcher request — the
	 * launcher pair carries exactly one pending request and a
	 * second would supersede and fail the first.
	 */
	if (_phase != Phase::IDLE && target == _pending.target)
		return;

	_emit_superseded();
	++_gen;
	_pending        = Change { };
	_pending.target = target;
	_pending.what   = (target == Target::PANEL)
	                ? QStringLiteral("panel settings")
	                : QStringLiteral("background settings");
	_open_only      = true;
	_phase          = Phase::ENSURING;
	_request_ensure();
}


void QuickMenuController::_submit(Change const &change)
{
	if (_phase == Phase::ENSURING && change.target == _pending.target) {
		/*
		 * The idempotent start-or-focus in flight serves this
		 * target already; only the queued operation changes.
		 */
		_emit_superseded();
		_pending   = change;
		_open_only = false;
		return;
	}

	/*
	 * Supersede: the generation bump kills any in-flight poll chain
	 * and (pkg, id) matching drops the superseded ensure-start's
	 * late reply. Every change re-runs ensure-start (binding
	 * lifecycle — the provider may have exited since).
	 */
	_emit_superseded();
	++_gen;
	_pending   = change;
	_open_only = false;
	_phase = Phase::ENSURING;
	_request_ensure();
}


void QuickMenuController::_request_ensure()
{
	if (!_launcher) {
		_fail(QStringLiteral("package manager not attached"));
		return;
	}

	_launcher->request_settings_open(QStringLiteral("sponge-de"),
	                                 QString::fromUtf8(_settings_id(_pending.target)));
}


void QuickMenuController::_on_settings_open_ok(QString const &pkg,
                                               QString const &id,
                                               QString const &action)
{
	if (_phase != Phase::ENSURING)
		return;
	if (pkg != QLatin1String("sponge-de") ||
	    id != QString::fromUtf8(_settings_id(_pending.target)))
		return;

	if (_open_only) {
		QString const message = _pending.what + QStringLiteral(" opened");
		Genode::log("sponge-de: ", message.toUtf8().constData(),
		            " (provider ", action.toUtf8().constData(), ")");
		_phase     = Phase::IDLE;
		_open_only = false;
		emit quick_change_ok(message);
		return;
	}

	Genode::log("sponge-de: quick change '", _pending.what.toUtf8().constData(),
	            "': provider ", action.toUtf8().constData());
	_send_request();
}


void QuickMenuController::_on_settings_open_failed(QString const &pkg,
                                                   QString const &id,
                                                   QString const &reason)
{
	if (_phase != Phase::ENSURING)
		return;
	if (pkg != QLatin1String("sponge-de") ||
	    id != QString::fromUtf8(_settings_id(_pending.target)))
		return;

	_fail(reason);
}


void QuickMenuController::_send_request()
{
	Channel &ch = _channel(_pending.target);

	if (!_open_channel(ch)) {
		_fail(QStringLiteral("quick settings channel unavailable"));
		return;
	}

	/*
	 * Snapshot the result ROM before writing the request: content
	 * byte-identical to this snapshot is a stale answer of an
	 * earlier request and must not complete this change (see the
	 * header's acceptance rules).
	 */
	_snapshot.clear();
	ch.result->update();
	if (ch.result->valid())
		_snapshot = QByteArray(ch.result->local_addr<char const>(),
		                       (int)ch.result->size());

	ch.request->generate_xml([&] (Genode::Xml_generator &g) {
		g.attribute("op", _pending.op.constData());
		if (!_pending.id.isEmpty())
			g.attribute("id", _pending.id.constData());
		g.attribute("value", _pending.value.constData());
	});

	Genode::log("sponge-de: quick request '", _pending.what.toUtf8().constData(),
	            "' -> ", _pending.value.constData());

	_phase = Phase::POLLING;
	_poll_count = 0;
	_schedule_poll();
}


void QuickMenuController::_schedule_poll()
{
	unsigned const gen = _gen;
	QTimer::singleShot(QUICK_POLL_MS, this, [this, gen]() {
		if (gen != _gen)
			return;
		_poll_quick_result();
	});
}


void QuickMenuController::_poll_quick_result()
{
	if (_phase != Phase::POLLING)
		return;

	Channel &ch = _channel(_pending.target);

	if (ch.result.constructed())
		ch.result->update();

	if (!ch.result.constructed() || !ch.result->valid()) {
		if (++_poll_count >= QUICK_POLL_MAX) {
			_fail(QStringLiteral("no answer from the settings provider"));
			return;
		}
		_schedule_poll();
		return;
	}

	QByteArray const content(ch.result->local_addr<char const>(),
	                         (int)ch.result->size());
	bool const fresh = (content != _snapshot);

	bool parsed = false, quick_root = false;
	QByteArray status, rop, rvalue, rerror;

	try {
		Genode::Xml_node const r(content.constData(),
		                         (Genode::size_t)content.size());
		quick_root = r.has_type("quick_result");
		if (quick_root) {
			parsed = true;
			status = QByteArray(r.attribute_value("status",
			                    Genode::String<32>()).string());
			rop    = QByteArray(r.attribute_value("op",
			                    Genode::String<32>()).string());
			rvalue = QByteArray(r.attribute_value("value",
			                    Genode::String<128>()).string());
			rerror = QByteArray(r.attribute_value("error",
			                    Genode::String<192>()).string());
		}
	} catch (Genode::Xml_node::Invalid_syntax) {
		parsed = false;
	}

	if (!parsed) {
		if (fresh) {
			_fail(QStringLiteral("malformed quick result from the settings provider"));
			return;
		}
	} else if (rop == _pending.op && status == "ok" &&
	           rvalue == _pending.value) {
		_finish_ok();
		return;
	} else if (rop == _pending.op && status == "error" && fresh) {
		_fail(rerror.isEmpty() ? QStringLiteral("change refused")
		                      : QString::fromUtf8(rerror));
		return;
	}

	if (++_poll_count >= QUICK_POLL_MAX) {
		_fail(QStringLiteral("no matching quick result from the settings provider"));
		return;
	}
	_schedule_poll();
}


void QuickMenuController::_finish_ok()
{
	QString const message = _pending.what + QStringLiteral(" set to ") +
	                        QString::fromUtf8(_pending.value);
	Genode::log("sponge-de: ", message.toUtf8().constData());
	_phase     = Phase::IDLE;
	_open_only = false;
	emit quick_change_ok(message);
}


void QuickMenuController::_fail(QString const &reason)
{
	QString const message = _pending.what + QStringLiteral(": ") + reason;
	Genode::warning("sponge-de: quick change failed: ",
	                message.toUtf8().constData());
	_phase     = Phase::IDLE;
	_open_only = false;
	emit quick_change_failed(message);
}
