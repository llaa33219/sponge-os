/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of NotifyPoster. See notify_poster.h for the
 * wire contract and the threading model.
 */

#include "notify_poster.h"

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

using namespace Sponge::Sponge_DE;


namespace {

bool parse_notifier_asks_for_daemon(Genode::Attached_rom_dataspace &config)
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
			root.for_each_sub_node("notifier", [&](Genode::Xml_node const &n) {
				if (!live)
					live = n.attribute_value("source", Genode::String<32>())
					     == Genode::String<32>("daemon");
			});
		} catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		if (root.has_type("config")) {
			root.for_each_sub_node([&](Genode::Hid_node const &n) {
				if (!live && n.has_type("notifier"))
					live = n.attribute_value("source", Genode::String<32>())
					     == Genode::String<32>("daemon");
			});
		}
	}

	return live;
}

}  /* namespace */


bool Sponge::Sponge_DE::notifier_asks_for_daemon(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	return parse_notifier_asks_for_daemon(config);
}


NotifyPoster::NotifyPoster(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
	_opt_in = notifier_asks_for_daemon(_env);
}


void NotifyPoster::post(QString title, QString body, QString kind, unsigned ttl_ms)
{
	if (!_opt_in)
		return;

	if (!_enabled) {
		try {
			_request.construct(_env, "notif_request", "notif_request");
			_enabled = true;
			Genode::log("sponge-de: notify poster wired to sponge_notifier");
		}
		catch (...) {
			_enabled = false;
		}
	}

	if (!_enabled) {
		if (title != _last_post_title) {
			Genode::warning("notifier unavailable, dropping: ", title.toUtf8().constData());
			_last_post_title = title;
		}
		return;
	}

	if (kind != "info" && kind != "warn" && kind != "error")
		kind = QStringLiteral("info");

	Genode::uint64_t now_ms = 0;
	{
		Timer::Connection t(_env);
		now_ms = (Genode::uint64_t)t.curr_time().trunc_to_plain_ms().value;
	}
	if (title == _last_post_title
	 && body  == _last_post_body
	 && now_ms - _last_post_ms < 1000)
		return;
	_last_post_title = title;
	_last_post_body  = body;
	_last_post_ms    = now_ms;

	Genode::String<96>  source("sponge-de");
	_request->generate_xml([&](Genode::Xml_generator &g) {
		g.node("notification", [&] {
			g.attribute("source", source);
			g.attribute("kind",   kind.toUtf8().constData());
			g.attribute("ttl_ms", ttl_ms);
			g.node("title", [&] { g.append_sanitized(title.toUtf8().constData()); });
			if (!body.isEmpty())
				g.node("body", [&] { g.append_sanitized(body.toUtf8().constData()); });
		});
	});
}
