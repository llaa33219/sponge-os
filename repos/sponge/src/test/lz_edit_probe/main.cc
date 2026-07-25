/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * lz_edit_probe — Phase 6c end-to-end driver.
 *
 * Edits the subsystem's model fs (appends a bogus node to /deploy — a real
 * fs change), polls configd's broadcast until lz_watch's divergence is
 * reflected as leitzentrale.diverged=true, sends a revert request, and
 * confirms the divergence clears. Exercises the genuine detection +
 * configd-sync + revert path; nothing is faked.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <os/vfs.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace {

struct Lz_edit_probe
{
	Genode::Env   &_env;
	Genode::Heap   _heap { _env.ram(), _env.rm() };
	Timer::Connection _timer { _env };

	Lz_edit_probe(Lz_edit_probe const &) = delete;
	Lz_edit_probe &operator=(Lz_edit_probe const &) = delete;

	char const *const _vfs_xml =
		"vfs\n"
		"+ dir model | + fs\n"
		"-";
	Genode::Vfs::Simple_env _vfs_env { _env, _heap,
		Genode::Node(Genode::Span(_vfs_xml, Genode::strlen(_vfs_xml))) };
	Genode::Directory _root { _vfs_env };

	/* configd broadcast (mirrors leitzentrale.diverged) — flows down. */
	Genode::Attached_rom_dataspace _lz_config { _env, "lz_config" };

	/* revert request — flows up to lz_watch via the top-level report_rom. */
	Genode::Expanding_reporter _request { _env, "lz_watch_request",
	                                      "lz_watch_request" };

	Lz_edit_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("lz-edit-probe: starting 6c cycle");

		/* 1. Wait for configd + lz_watch to be ready (diverged initially false). */
		if (!_wait_diverged(false, 100)) {
			_fail("baseline never settled (diverged did not start false)");
			return;
		}
		Genode::log("lz-edit-probe: baseline settled, editing /deploy");

		/* 2. Make a REAL fs change: append a bogus deploy node. */
		if (!_edit_deploy()) {
			_fail("could not edit /deploy");
			return;
		}
		Genode::log("lz-edit-probe: /deploy edited, waiting for detection");

		/* 3. configd must reflect the divergence. */
		if (!_wait_diverged(true, 100)) {
			_fail("divergence never detected/reflected by configd");
			return;
		}
		Genode::log("lz-edit-probe: divergence detected + configd synced");

		/* 4. Send the revert request. */
		_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op", "revert");
		});
		Genode::log("lz-edit-probe: revert requested, waiting for restore");

		/* 5. Divergence must clear after revert. */
		if (!_wait_diverged(false, 100)) {
			_fail("divergence did not clear after revert");
			return;
		}

		Genode::log("lz-edit-probe: model restored, divergence cleared");
		Genode::log("lz-edit-probe: PASS");
		_env.parent().exit(0);
	}

	bool _diverged()
	{
		_lz_config.update();
		if (!_lz_config.valid()) return false;
		bool d = false;
		_lz_config.xml().for_each_sub_node("key", [&] (Genode::Xml_node const &k) {
			if (k.attribute_value("name", Genode::String<64>()) ==
			    Genode::String<64>("leitzentrale.diverged"))
				d = (k.attribute_value("value", Genode::String<16>()) ==
				     Genode::String<16>("true"));
		});
		return d;
	}

	bool _wait_diverged(bool want, unsigned tries)
	{
		for (unsigned i = 0; i < tries; ++i) {
			_timer.msleep(200);
			if (_diverged() == want) return true;
			if (i % 10 == 0)
				Genode::log("lz-edit-probe: wait diverged=", want,
				            " poll ", i, " actual=", _diverged());
		}
		return false;
	}

	bool _edit_deploy()
	{
		try {
			/* Append a clearly-bogus marker node to /deploy so the file
			 * content changes (lz_watch's checksum will differ). */
			Genode::Readonly_file f { _root, "/model/deploy" };
			char buf[4096];
			Genode::size_t n = f.read(Genode::Readonly_file::At{0},
			                          Genode::Byte_range_ptr(buf, sizeof(buf)));
			Genode::New_file out { _root, "/model/deploy" };
			out.append(buf, n);
			out.append("\n+ option lz_edit_probe_marker\n", 30);
			return true;
		} catch (...) {
			return false;
		}
	}

	void _fail(char const *reason)
	{
		Genode::error("lz-edit-probe: FAIL ", reason);
		_env.parent().exit(1);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Lz_edit_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
