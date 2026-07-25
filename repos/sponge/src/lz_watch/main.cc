/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * lz_watch — Leitzentrale model-fs change detector + snapshot/revert (6c).
 *
 * Watches the subsystem's model fs (the deploy config that sculpt_manager
 * rewrites on user edits), keeps a baseline snapshot in RAM, and reports
 * divergence. Handles snapshot (re-baseline) and revert (restore baseline)
 * requests. See target.mk for the full design.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <os/vfs.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

using Path = Genode::Directory::Path;

/* Only /deploy is watched in Phase 6c — it is the file sculpt_manager
 * rewrites when components are added/removed. More files can be added
 * behind the same scheme without touching the report shape. */
char const *const WATCHED_FILES[] = { "deploy" };
unsigned const NUM_WATCHED = sizeof(WATCHED_FILES) / sizeof(WATCHED_FILES[0]);

unsigned const MAX_FILE = 8192;
unsigned const POLL_MS  = 200;


struct Lz_watch
{
	Genode::Env   &_env;
	Genode::Heap   _heap { _env.ram(), _env.rm() };

	Lz_watch(Lz_watch const &) = delete;
	Lz_watch &operator=(Lz_watch const &) = delete;

	Timer::Connection _timer { _env };

	/* Vfs over the model fs (mounted at /model via the <fs> plugin). */
	char const *const _vfs_xml =
		"vfs\n"
		"+ dir model | + fs\n"
		"-";

	Genode::Vfs::Simple_env _vfs_env { _env, _heap,
		Genode::Node(Genode::Span(_vfs_xml, Genode::strlen(_vfs_xml))) };
	Genode::Directory _root { _vfs_env };

	/* lz_model divergence report (flows up to configd + vct + probe). */
	Genode::Expanding_reporter _model_report { _env, "lz_model", "lz_model" };

	/* lz_watch_result answer report. */
	Genode::Expanding_reporter _result_report { _env, "result", "lz_watch_result" };

	/* Request ROM (snapshot/revert), flows down from the top-level. */
	Genode::Attached_rom_dataspace _request { _env, "lz_watch_request" };
	Genode::Signal_handler<Lz_watch> _request_sigh {
		_env.ep(), *this, &Lz_watch::_handle_request };

	/* Baseline snapshot of each watched file (RAM). */
	struct Snapshot {
		char        data[MAX_FILE];
		Genode::size_t len { 0 };
	};
	Snapshot _baseline[NUM_WATCHED] { };

	bool _have_baseline { false };

	Lz_watch(Genode::Env &env) : _env(env)
	{
		Genode::log("lz-watch: starting");

		_take_snapshot();   /* initial baseline */
		_have_baseline = true;
		_emit_model();

		_request.sigh(_request_sigh);

		_poll_loop();
	}

	/* Read a watched file into buf; return bytes read (0 if missing). */
	Genode::size_t _read_file(char const *rel, char *buf, Genode::size_t cap)
	{
		Path const p { "/model/", rel };
		Genode::size_t const sz = _root.file_size(p);
		if (sz == 0 || sz > cap) return 0;
		try {
			Genode::Readonly_file f { _root, p };
			return f.read(Genode::Readonly_file::At{0},
			              Genode::Byte_range_ptr(buf, sz));
		} catch (...) { return 0; }
	}

	void _take_snapshot()
	{
		for (unsigned i = 0; i < NUM_WATCHED; ++i) {
			_baseline[i].len = _read_file(WATCHED_FILES[i],
			                              _baseline[i].data, MAX_FILE);
		}
	}

	bool _file_changed(unsigned i)
	{
		char cur[MAX_FILE];
		Genode::size_t const cur_len = _read_file(WATCHED_FILES[i], cur, MAX_FILE);
		if (cur_len != _baseline[i].len) return true;
		return Genode::memcmp(cur, _baseline[i].data, cur_len) != 0;
	}

	void _revert()
	{
		for (unsigned i = 0; i < NUM_WATCHED; ++i) {
			Path const p { "/model/", WATCHED_FILES[i] };
			try {
				Genode::New_file f { _root, p };
				f.append(_baseline[i].data, _baseline[i].len);
			} catch (...) {
				Genode::warning("lz-watch: revert failed for ", WATCHED_FILES[i]);
			}
		}
	}

	void _emit_model()
	{
		bool any_changed = false;
		_model_report.generate_xml([&] (Genode::Xml_generator &g) {
			for (unsigned i = 0; i < NUM_WATCHED; ++i) {
				bool const ch = _have_baseline && _file_changed(i);
				any_changed = any_changed || ch;
				g.node("file", [&] {
					g.attribute("name", WATCHED_FILES[i]);
					g.attribute("changed", ch ? "true" : "false");
				});
			}
			g.attribute("diverged", any_changed ? "true" : "false");
		});
	}

	void _emit_result(char const *op, char const *status)
	{
		_result_report.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op", op);
			g.attribute("status", status);
		});
	}

	void _handle_request()
	{
		_request.update();
		if (!_request.valid()) return;
		Genode::Xml_node const req = _request.xml();
		Genode::String<16> const op =
			req.attribute_value("op", Genode::String<16>());

		if (op == Genode::String<16>("snapshot")) {
			_take_snapshot();
			_emit_model();
			_emit_result("snapshot", "ok");
			Genode::log("lz-watch: snapshot taken (new baseline)");
		} else if (op == Genode::String<16>("revert")) {
			_revert();
			_emit_model();
			_emit_result("revert", "ok");
			Genode::log("lz-watch: reverted model to baseline");
		} else {
			_emit_result(op.string(), "error: unknown op");
		}
	}

	void _poll_loop()
	{
		for (unsigned i = 0; ; ++i) {
			_timer.msleep(POLL_MS);
			_handle_request();
			if (i % 3 == 0)   /* re-emit ~every 600ms; cheap and bounded */
				_emit_model();
		}
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Lz_watch watch { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
