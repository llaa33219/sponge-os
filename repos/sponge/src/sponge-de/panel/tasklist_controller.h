/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
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

		void _on_window_list_rom();
		void _on_window_layout_rom();
		void _poll();
		void _lazy_open();

		bool _pull_payloads();

		void _recompute_tracked();

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
