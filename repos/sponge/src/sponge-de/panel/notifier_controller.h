/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * NotifierController — sponge_notifier ROM bridge for Sponge DE.
 *
 * Watches sponge_notifier's `notifications` broadcast ROM (relayed by
 * report_rom from sponge_notifier's `notifications` report) and
 * republishes the active list as a Qt signal that the NotifierWidget
 * consumes on the GUI thread. The controller is the in-sponge-de
 * counterpart of the daemon's `<notifications>` channel, mirroring the
 * ConfigController / ThemeController pattern (one in-sponge-de bridge
 * per external Report/ROM).
 *
 * ROM LABEL:
 *
 *   The watched ROM session is labeled "notifications" (distinct from
 *   the daemon's Report label "notifications" — the producer is
 *   sponge_notifier, the consumer is sponge-de, the relay is
 *   report_rom). The run script's policy is:
 *
 *     policy | label: sponge-de -> notifications | report: sponge_notifier -> notifications
 *
 *   And sponge-de's route declares:
 *
 *     service ROM | label: notifications | + child report_rom
 *
 * ACTIVATION:
 *
 *   The controller is constructed unconditionally in sponge_de_main.cc;
 *   if the `notifications` ROM cannot be opened (no sponge_notifier in
 *   the topology, or no route for the `notifications` label), the
 *   controller degrades to no-op: no ROM is opened, no signals are
 *   emitted, no errors raised. The widget still constructs (it's a
 *   passive paint target) and the popover stays hidden because the
 *   active list is empty.
 *
 * THREAD MODEL (the critical invariant — failure-point 2):
 *
 *   The ROM signal handler runs on the Genode entrypoint dispatcher
 *   thread, NOT the Qt event-loop thread blocked in QApplication::exec.
 *   Touching any QWidget or calling QApplication APIs from the signal
 *   handler is undefined behavior.
 *
 *   The handler reads the ROM (a plain shared dataspace), parses the
 *   <notifications> XML, and marshals the entry list to the GUI thread
 *   via QMetaObject::invokeMethod(..., Qt::QueuedConnection). The
 *   actual signal emission happens in applyEntries() on the GUI
 *   thread. A 250 ms QTimer poll acts as a safety net for the same
 *   reason ConfigController uses one (ROM signals are not guaranteed
 *   to be dispatched while QApplication::exec is blocked).
 *
 * WIRE FORMAT:
 *
 *   The ROM is <notifications count="N" max_live="M"> with N children:
 *     <notification id="..." ts="..." source="..." kind="..." ttl_ms="...">
 *       <title>...</title>
 *       <body>...</body> (optional)
 *     </notification>
 *   The controller flattens entries into single-line strings for the
 *   widget (kind + title + optional body), preserving the daemon's
 *   FIFO order. The widget itself never touches the XML.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Sponge::Sponge_DE {

class NotifierWidget;

class NotifierController : public QObject
{
	Q_OBJECT

	public:

		explicit NotifierController(Genode::Env &env, QObject *parent = nullptr);

		/* Attach the widget AFTER it is constructed but BEFORE the
		 * first broadcast arrives. The controller's first marshalled
		 * signal fans out to the widget's applyEntries(). */
		void attach_widget(NotifierWidget *widget);

		~NotifierController() override;

	private slots:

		/* GUI thread. Parses the marshalled payload, emits
		 * entries_changed with the flattened list. The widget
		 * handles empty-list auto-hide. */
		void applyEntries(QString payload);

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Attached_rom_dataspace>             _notif_rom { };
		Genode::Constructible<Genode::Signal_handler<NotifierController>> _sigh      { };

		QTimer *_poll_timer { nullptr };

		NotifierWidget *_widget { nullptr };

		/*
		 * De-dup: the flattened list is compared to the last-applied
		 * string, so consecutive identical broadcasts do not
		 * re-paint the widget.
		 */
		QString _last_payload;

		bool _read_payload(QString &payload);
		void _on_rom();   /* entrypoint thread: read ROM, marshal */
		void _poll();     /* GUI thread: pull + apply */
		void _lazy_open(); /* GUI thread: lazy session opening */

	signals:

		/* The flattened entry list. Each entry is a single line
		 * suitable for the widget's text painter. Empty list means
		 * "no active notifications" — the widget hides itself. */
		void entries_changed(QStringList entries);
};

}  /* namespace Sponge::Sponge_DE */
