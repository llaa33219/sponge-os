/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of LauncherController. See launcher_controller.h.
 */

#include "launcher_controller.h"

#include "launcher_menu_view.h"

#include <base/log.h>
#include <util/hid.h>
#include <util/string.h>

#include <QTimer>

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Read <launcher source="pkgd"/> from the component config. Returns true
 * only when explicitly opted in; absent (or any other value) leaves the
 * launcher disabled, which keeps sponge-de-test.run (no pkgd route)
 * booting clean. Same pattern ThemeController uses for <theme source=...>.
 */
bool config_asks_for_pkgd(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	config.update();
	if (!config.valid())
		return false;

	char const *const base = config.local_addr<char>();
	Genode::size_t  const sz  = config.size();

	bool enabled = false;

	bool const is_xml = (sz > 0 && base[0] == '<');
	if (is_xml) {
		try {
			Genode::Xml_node const root(base, sz);
			root.for_each_sub_node("launcher", [&](Genode::Xml_node const &l) {
				if (!enabled)
					enabled = l.attribute_value("source",
					           Genode::String<32>()) ==
					          Genode::String<32>("pkgd");
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!enabled && n.has_type("launcher"))
				enabled = n.attribute_value("source",
				          Genode::String<32>()) ==
				          Genode::String<32>("pkgd");
		});
	}

	return enabled;
}

}  /* namespace */


LauncherController::LauncherController(Genode::Env &env, QObject *parent)
:
	QObject(parent),
	_env(env)
{
	_launcher_report.enabled(true);

	/* Always publish an initial empty launcher report so the probe
	 * (and any watcher) reads a well-formed <launcher count="0"/>
	 * before any pkgd update arrives. */
	_publish_report();

	bool const wired = config_asks_for_pkgd(env);
	if (!wired) {
		Genode::log("sponge-de: launcher source=none (no pkgd wiring; "
		            "panel button will report 'not attached')");
		return;
	}

	Genode::log("sponge-de: launcher source=pkgd (live)");
	_installed_rom.construct(_env, "installed");

	_poll_timer = new QTimer(this);
	_poll_timer->start(1500);
	QObject::connect(_poll_timer, &QTimer::timeout, this, &LauncherController::poll);

	/* Kick off the first poll immediately so the launcher is not
	 * empty for the first 1.5 s after the panel appears. */
	QMetaObject::invokeMethod(this, "poll", Qt::QueuedConnection);
}


void LauncherController::poll()
{
	if (!_installed_rom.constructed())
		return;

	if (_read_and_parse())
		_publish_report();
}


bool LauncherController::_read_and_parse()
{
	_installed_rom->update();
	if (!_installed_rom->valid())
		return false;

	try {
		return _try_parse(_installed_rom->xml());
	} catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


bool LauncherController::_try_parse(Genode::Xml_node const &root)
{
	if (!root.has_type("installed"))
		return false;

	QVector<App> parsed;
	parsed.reserve(MAX_APPS);

	root.with_optional_sub_node("packages",
		[&](Genode::Xml_node const &pkgs) {
			pkgs.for_each_sub_node("package",
			[&](Genode::Xml_node const &p) {
				if (parsed.size() >= (int)MAX_APPS)
					return;

				Genode::String<64> const category =
					p.attribute_value("category", Genode::String<64>());

				/* Launcher entries are exactly the packages that
				 * declare a launcher category (docs/12 §4.6). */
				if (Genode::strcmp(category.string(), "") == 0)
					return;

				App a;
				a.name = QString::fromUtf8(
					p.attribute_value("name", Genode::String<64>()).string());
				a.category = QString::fromUtf8(category.string());
				a.binary = QString::fromUtf8(
					p.attribute_value("binary", Genode::String<64>()).string());
				a.description = QString::fromUtf8(
					p.attribute_value("description", Genode::String<256>()).string());
				parsed.append(a);
			});
		});

	QString const sig = _signature_of(parsed);
	if (sig == _last_result_signature)
		return false;

	_apps = parsed;
	_last_result_signature = sig;
	_live_list_seen = true;

	Genode::log("sponge-de: launcher list updated (",
	            (unsigned)_apps.size(), " app", _apps.size() == 1 ? "" : "s", ")");

	emit appsChanged();
	return true;
}


QString LauncherController::_signature_of(QVector<App> const &apps) const
{
	/* Stable identity string for change detection: concatenation of
	 * name|category pairs. Order is whatever QVector gives us, but
	 * pkgd emits the list name-sorted so the signature is stable
	 * across runs for the same installed set. */
	QString s;
	for (App const &a : apps)
		s += a.name + QStringLiteral("|") + a.category + QStringLiteral(";");
	return s;
}


void LauncherController::_publish_report()
{
	(void)_launcher_report.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("count", (unsigned)_apps.size());

		for (App const &a : _apps) {
			g.node("app", [&] {
				g.attribute("name",     a.name.toUtf8().constData());
				g.attribute("category", a.category.toUtf8().constData());
				g.attribute("binary",   a.binary.toUtf8().constData());
			});
		}
	});
}
