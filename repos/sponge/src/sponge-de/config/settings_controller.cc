/* SPDX-License-Identifier: Apache-2.0
 *
 * SettingsController — write-side bridge implementation. See
 * settings_controller.h for the channel contract and thread model.
 */

#include "settings_controller.h"

#include <base/log.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_node.h>

#include <QApplication>
#include <QTimer>

#include "../settings/settings_dialog.h"

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Parse the child config (HID or XML). Returns true only when the
 * child explicitly opts in via <de_config source="controller"/>;
 * absent (or any other value) leaves the controller in fallback
 * mode. Mirrors parse_config_asks_for_configd at
 * config_controller.cc:37 — same dual-parser pattern so scenarios
 * delivering the config in either format boot cleanly.
 */
bool parse_de_config_asks_for_controller(Genode::Attached_rom_dataspace &config)
{
	config.update();
	if (!config.valid())
		return false;

	char const *const base = config.local_addr<char>();
	Genode::size_t  const sz  = config.size();

	bool live = false;

	bool const is_xml = (sz > 0 && base[0] == '<');
	if (is_xml) {
		try {
			Genode::Xml_node const root(base, sz);
			root.for_each_sub_node("de_config", [&](Genode::Xml_node const &c) {
				if (!live)
					live = c.attribute_value("source",
					         Genode::String<32>()) ==
					       Genode::String<32>("controller");
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!live && n.has_type("de_config"))
				live = n.attribute_value("source",
				         Genode::String<32>()) ==
				       Genode::String<32>("controller");
		});
	}

	return live;
}

}  /* namespace */


bool Sponge::Sponge_DE::de_config_asks_for_controller(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	return parse_de_config_asks_for_controller(config);
}


SettingsController::SettingsController(Genode::Env &env, QObject *parent)
:
	QObject(parent),
	_env(env)
{
	bool const live = de_config_asks_for_controller(_env);

	if (!live) {
		Genode::log("sponge-de: de_config source=none (no SettingsController "
		            "wiring; settings dialog unavailable in this topology)");
		return;
	}

	Genode::log("sponge-de: de_config source=controller (live settings writes)");

	/*
	 * The QTimer placeholder is reserved for future async paths
	 * (e.g. an asynchronous_apply mode). Today's writes go
	 * synchronously through the shared backend client from the
	 * dialog's Apply click — bounded by the client's own poll
	 * loop (~6s worst case).
	 */
	_poll_timer = new QTimer(this);
	_poll_timer->setSingleShot(true);
}


SettingsController::~SettingsController()
{
	/*
	 * Phase 14 W11 #47 symmetry with ConfigController: stop the
	 * poll timer before QObject parent-child cleanup.
	 */
	if (_poll_timer) {
		_poll_timer->stop();
		_poll_timer->deleteLater();
		_poll_timer = nullptr;
	}
}


void SettingsController::request_set(QString key, QString value)
{
	/*
	 * Lazy-construct the dedicated client on first use. If the
	 * surrounding scenario does not wire the de_config_request /
	 * de_config_result routes (Phase 10's interactive scenario
	 * has no report_rom policy for the DE-side label, etc.), the
	 * constructor itself would fatal-deny. Catch that by checking
	 * validity after the construction attempt and falling back to
	 * the not-wired warning (the same defensive pattern as
	 * LauncherController::request_launch at
	 * launcher_controller.cc:276-289).
	 */
	if (!_client.constructed()) {
		_client.construct(_env, "de_config_request", "de_config_result");
	}

	if (!_client.constructed()) {
		Genode::warning("sponge-de: settings write '", key.toUtf8().constData(),
		                "' but de_config_request/de_config_result channels "
		                "could not be opened (no report_rom policy)");
		emit request_failed(key, QStringLiteral(
		    "DE-side config channel unavailable (no report_rom policy "
		    "for sponge-de -> de_config_request / de_config_result)"));
		return;
	}

	Genode::log("sponge-de: settings write '", key.toUtf8().constData(),
	            "' = '", value.toUtf8().constData(), "'");

	/*
	 * Issue the write through the shared backend client. The
	 * client's internal `_timer.msleep(200)` initial settle is a
	 * short synchronous wait on a Timer session (NOT an
	 * indefinite block); since `Reporter::generate_xml` is also
	 * short, the total budget here is bounded (~6s worst case,
	 * sub-50 ms when configd is healthy) and acceptable on the
	 * GUI thread for a single user-initiated Apply click.
	 */
	QByteArray const k = key.toUtf8();
	QByteArray const v = value.toUtf8();
	if (!_client->config_set(k.constData(), v.constData())) {
		/*
		 * Poll budget exhausted (configd never answered within
		 * ~6s — same out of 60 retries). Surface as a failure;
		 * the dialog's QMessageBox shows "config request timeout".
		 */
		Genode::warning("sponge-de: settings write '", key.toUtf8().constData(),
		                "' timed out (configd did not answer)");
		emit request_failed(key, QStringLiteral("configd did not answer (timeout)"));
		return;
	}

	/*
	 * config_set() returning true means configd emitted a
	 * matching `<result op="set" key=K value=V/>` reply. Parse
	 * the result on the GUI thread and surface the outcome.
	 */
	try {
		Genode::Xml_node const r = _client->result_xml();
		Genode::String<32> const status =
			r.attribute_value("status", Genode::String<32>());

		if (status == Genode::String<32>("ok")) {
			Genode::log("sponge-de: settings write '", key.toUtf8().constData(),
			            "' = '", value.toUtf8().constData(),
			            "' -> ok (D16.9 validator parity)");
			emit request_succeeded(key, value);
			return;
		}

		/*
		 * status="error": the closed-registry validator rejected
		 * the write (unknown key, malformed value, charset
		 * violation, ...). Surface the structured error text via
		 * request_failed; the dialog shows it in a QMessageBox.
		 * Phase 16 F2 (no silent failures).
		 */
		Genode::String<256> const err =
			r.attribute_value("error", Genode::String<256>());
		Genode::warning("sponge-de: settings write '", key.toUtf8().constData(),
		                "' rejected: ", err.string());
		emit request_failed(key, QString::fromUtf8(err.string()));
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge-de: settings write '", key.toUtf8().constData(),
		                "' returned malformed result XML");
		emit request_failed(key,
		    QStringLiteral("configd returned malformed result"));
	}
}


void SettingsController::open_settings_dialog()
{
	/*
	 * The dialog itself is implemented in
	 * settings/settings_dialog.{h,cc}. The dialog header is
	 * included at file scope (above) because a method-body
	 * `#include` would reopen the already-active `Sponge::Sponge
	 * _DE` namespace and confuse the C++ parser.
	 *
	 * Phase 16 W7 (U16.4 / D16.5) — the dialog is heap-allocated
	 * so the dismiss path can close it without invalidating the
	 * pointer. The dialog's Qt::WA_DeleteOnClose attribute makes
	 * it auto-delete after close(), and the _dialog pointer is
	 * cleared in the destroyed() lambda so a subsequent
	 * open_settings_dialog call constructs a fresh one.
	 *
	 * The lazy-load avoids the 50+ KB Qt6 dialog construction in
	 * scenarios that never open the settings panel (Phase 7 /
	 * Phase 14 / Phase 15 closeout probes, etc.).
	 */
	if (_dialog) {
		Genode::log("sponge-de: open_settings_dialog: already open; raising");
		_dialog->raise();
		_dialog->activateWindow();
		return;
	}

	Genode::log("sponge-de: open_settings_dialog requested");

	_dialog = new SettingsDialog();
	_dialog->setAttribute(Qt::WA_DeleteOnClose);
	_dialog->set_controller(this);
	QObject::connect(this, &SettingsController::request_failed,
	                 _dialog, &SettingsDialog::on_request_failed);
	QObject::connect(this, &SettingsController::request_succeeded,
	                 _dialog, &SettingsDialog::on_request_succeeded);
	/*
	 * Clear _dialog when the dialog is destroyed (Qt fires
	 * QObject::destroyed with the pointer; the dialog is
	 * already in its destructor but the QObject layer still
	 * emits the signal so external observers can clear refs).
	 */
	QObject::connect(_dialog, &QObject::destroyed, this, [this]() {
		_dialog = nullptr;
	});

	_dialog->show();
	_dialog->raise();
	_dialog->activateWindow();
}


/*
 * Phase 16 W7 — dismiss the settings dialog if one is open.
 * The Qt::WA_DeleteOnClose attribute set in open_settings_dialog
 * ensures the dialog's memory is freed after close() returns;
 * the destroyed() lambda clears _dialog. No-op when the dialog
 * is not currently open (the Dismisser always calls this in its
 * priority list, the no-op is the documented behavior).
 */
void SettingsController::close_settings_dialog()
{
	if (!_dialog) {
		return;
	}
	Genode::log("sponge-de: close_settings_dialog (dismiss path)");
	_dialog->close();
}
