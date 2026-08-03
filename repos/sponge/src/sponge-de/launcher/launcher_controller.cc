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

	/*
	 * Launch channel (Phase 7 todo 10): constructed LAZILY on the
	 * first request_launch() call, NOT here. The earlier eager
	 * construction opened Report/ROM sessions for "launcher_request"
	 * and "launcher_result" even in scenarios that wire the launcher
	 * for display but not for click-to-launch (run/sponge-launcher.run,
	 * run/sponge-alpha.run). When the surrounding scenario has no
	 * report_rom policy for those labels, init denied the session and
	 * sponge-de stopped at construction time — defeating the
	 * "harmless timeout" intent of the polling guards below. Lazy
	 * construction also matches AGENTS.md §1.2 (minimum privilege: a
	 * component requests only the sessions it actually needs).
	 *
	 * request_launch() constructs both _launch_request and _launch_result
	 * on first invocation; subsequent calls reuse them. Scenarios that
	 * never trigger a click pay zero session cost.
	 */

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
			a.running = (p.attribute_value("running", Genode::String<8>("no"))
			             == Genode::String<8>("yes"));
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
		 * name|category|running pairs. Running state is included so a
		 * launch (installed→running) triggers a repopulate and the menu
		 * suffix updates. */
		QString s;
		for (App const &a : apps)
			s += a.name + QStringLiteral("|") + a.category
			     + QStringLiteral("|") + (a.running ? QStringLiteral("1")
			                                        : QStringLiteral("0"))
			     + QStringLiteral(";");
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
				g.attribute("running",  a.running ? "yes" : "no");
			});
		}
	});
}


/* ===================== click-to-launch (Phase 7 todo 10) ===================== */

void LauncherController::request_launch(QString const &name)
{
	/*
	 * Lazy-construct the launch channel on first use (see constructor
	 * comment). If the surrounding scenario does not wire the
	 * launcher_request/launcher_result labels, the construction itself
	 * would fail — so we catch that by checking validity after the
	 * attempt and falling back to the not-wired warning. The
	 * Expanding_reporter and Attached_rom_dataspace open their sessions
	 * synchronously in their constructor; a denied session is reported
	 * by Genode as a component-fatal error, so we only attempt the
	 * construction when a launch is actually requested (a scenario that
	 * does not wire the labels will never call request_launch, so the
	 * denied-session path is never triggered there).
	 */
	if (!_launch_request.constructed()) {
		_launch_request.construct(_env, "request", "launcher_request");
	}
	if (!_launch_result.constructed()) {
		_launch_result.construct(_env, "launcher_result");
	}

	if (!_launch_request.constructed() || !_launch_result.constructed()) {
		Genode::warning("sponge-de: launch request for '",
		                name.toUtf8().constData(),
		                "' but launcher_request/launcher_result channels "
		                "could not be opened");
		return;
	}

	/*
	 * Stash the package name so the non-blocking result poll can match
	 * the answer, and reset the poll counter. If a previous launch is
	 * still pending, the new request supersedes it (the user clicked
	 * again — acceptable for the Alpha single-writer model).
	 */
	_pending_launch_name = name;
	_launch_poll_count = 0;

	QByteArray const utf8 = name.toUtf8();
	char const *const pkg = utf8.constData();

	Genode::log("sponge-de: launcher click-to-launch '", pkg, "'");

	_launch_request->generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op",  "launch");
		g.attribute("pkg", pkg);
	});

	/*
	 * Drive the result poll via a single-shot QTimer defer (not a
	 * tight loop) so the Qt event loop stays responsive. Each tick
	 * re-checks the launcher_result ROM for a matching <result/>.
	 */
	QMetaObject::invokeMethod(this, [this]() { _poll_launch_result(); },
	                          Qt::QueuedConnection);
}


void LauncherController::_poll_launch_result()
{
	if (_pending_launch_name.isEmpty())
		return;

	if (!_launch_result.constructed() || !_launch_result->valid()) {
		if (++_launch_poll_count >= LAUNCH_POLL_MAX) {
			Genode::warning("sponge-de: launch result for '",
			                _pending_launch_name.toUtf8().constData(),
			                "' timed out (launcher_result unavailable)");
			_pending_launch_name.clear();
			return;
		}
		QTimer::singleShot(100, this, [this]() { _poll_launch_result(); });
		return;
	}

	_launch_result->update();

	QByteArray const utf8 = _pending_launch_name.toUtf8();
	char const *const pkg = utf8.constData();

	bool matched = false;
	Genode::String<32> status { };

	try {
		Genode::Xml_node const r = _launch_result->xml();
		if (r.has_type("result") &&
		    r.attribute_value("op", Genode::String<32>()) == Genode::String<32>("launch") &&
		    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg)) {
			matched = true;
			status = r.attribute_value("status", Genode::String<32>());
		}
	} catch (Genode::Xml_node::Invalid_syntax) { }

	if (!matched) {
		if (++_launch_poll_count >= LAUNCH_POLL_MAX) {
			Genode::warning("sponge-de: launch result for '",
			                _pending_launch_name.toUtf8().constData(),
			                "' timed out");
			_pending_launch_name.clear();
			return;
		}
		QTimer::singleShot(100, this, [this]() { _poll_launch_result(); });
		return;
	}

	/* Got a matching result: log the outcome for observability. */
	char const *const st = status.string();
	if (Genode::strcmp(st, "ok") == 0) {
		Genode::log("sponge-de: launched '", pkg, "'");
	} else if (Genode::strcmp(st, "not-installed") == 0) {
		Genode::warning("sponge-de: launch '", pkg,
		                "' failed: not installed");
	} else if (Genode::strcmp(st, "already-running") == 0) {
		Genode::log("sponge-de: launch '", pkg,
		            "': already running");
	} else {
		Genode::warning("sponge-de: launch '", pkg,
		                "' returned status='", st, "'");
	}

	_pending_launch_name.clear();
	_launch_poll_count = 0;
}
