/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of NotifierController. See notifier_controller.h for
 * the wire contract and the threading model.
 */

#include "notifier_controller.h"

#include "panel/notifier_widget.h"

#include <base/log.h>
#include <util/string.h>

#include <QMetaObject>
#include <QTimer>

using namespace Sponge::Sponge_DE;


NotifierController::NotifierController(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
	/*
	 * Try to open the notifications ROM. If no sponge_notifier is in
	 * the topology (the run scenario did not include it, or no
	 * report_rom policy routes the label), the session is denied at
	 * the parent. The catch block keeps the controller's ROM
	 * Constructible deconstructed forever — a clean no-op fallback.
	 */
	try {
		_notif_rom.construct(_env, "notifications");
		_sigh.construct(_env.ep(), *this, &NotifierController::_on_rom);
		_notif_rom->sigh(*_sigh);
		_notif_rom->update();
		_poll_timer = new QTimer(this);
		_poll_timer->setInterval(250);
		connect(_poll_timer, &QTimer::timeout,
		        this, &NotifierController::_poll);
		_poll_timer->start();

		/* Drain any pre-sigh content. */
		_on_rom();
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::log("notifier_controller: sponge_notifier ROM not available, "
		            "popover disabled");
	}
}


void NotifierController::attach_widget(NotifierWidget *widget)
{
	_widget = widget;
	connect(this, &NotifierController::entries_changed,
	        widget, &NotifierWidget::applyEntries);
}


bool NotifierController::_read_payload(QString &payload)
{
	if (!_notif_rom.constructed())
		return false;

	_notif_rom->update();
	if (!_notif_rom->valid())
		return false;

	/* Read the raw bytes; the GUI thread parses them. */
	char const *const base = _notif_rom->local_addr<char>();
	Genode::size_t  const sz  = _notif_rom->size();
	payload = QString::fromUtf8(base, (int)sz);
	return true;
}


void NotifierController::_on_rom()
{
	/* Runs on the Genode entrypoint thread. Read the ROM, marshal
	 * the payload to the GUI thread. */
	QString payload;
	if (!_read_payload(payload))
		return;

	QMetaObject::invokeMethod(this, "applyEntries",
	                           Qt::QueuedConnection,
	                           Q_ARG(QString, payload));
}


void NotifierController::_poll()
{
	/* Same dest as the signal handler, but pulled from the GUI thread
	 * so the QTimer + sigh both funnel into applyEntries. */
	QString payload;
	if (!_read_payload(payload))
		return;
	applyEntries(payload);
}


void NotifierController::applyEntries(QString payload)
{
	/* De-dup: identical payload is a no-op. */
	if (payload == _last_payload)
		return;
	_last_payload = payload;

	QStringList entries;

	/*
	 * Parse the <notifications> document directly. An empty payload
	 * (the initial state before the daemon has published) is a valid
	 * empty list — the widget hides itself.
	 */
	try {
		Genode::Xml_node const root(payload.toUtf8().constData(),
		                            payload.toUtf8().size());
		if (!root.has_type("notifications"))
			return;

		root.for_each_sub_node("notification", [&](Genode::Xml_node const &n) {
			QString title;
			n.with_optional_sub_node("title", [&](Genode::Xml_node const &t) {
				title = QString::fromUtf8(t.decoded_content<Genode::String<128>>().string());
			});
			if (title.isEmpty())
				return;

			QString body;
			n.with_optional_sub_node("body", [&](Genode::Xml_node const &b) {
				body = QString::fromUtf8(b.decoded_content<Genode::String<256>>().string());
			});

			QString const kind = QString::fromUtf8(
				n.attribute_value("kind", Genode::String<16>("info")).string());

			/*
			 * Flatten to a single line the widget's painter can draw.
			 * Format: "[kind] title — body" (body omitted if empty).
			 */
			QString line;
			if (!kind.isEmpty())
				line = QStringLiteral("[%1] %2").arg(kind, title);
			else
				line = title;
			if (!body.isEmpty())
				line += QStringLiteral(" — ") + body;
			entries.append(line);
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		/* Malformed payload — clear entries and re-emit (widget hides). */
		entries.clear();
	}

	emit entries_changed(entries);
}
