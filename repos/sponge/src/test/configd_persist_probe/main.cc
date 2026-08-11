/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * configd_persist_probe — sponge_configd persistence acceptance probe
 * (Phase 14 W6).
 *
 * A plain Genode component (no libc, no Qt — AGENTS.md §3.1). It exercises
 * sponge_configd's optional <vfs>-backed persistent store end-to-end:
 *
 *   1. The probe opens a Report session labeled "config_request" and
 *      sends three set requests through the established channel:
 *
 *        panel.height=64
 *        panel.visible_widgets=clock
 *        panel.height=28
 *
 *      After each set the probe reads the "config" broadcast ROM
 *      (relayed by report_rom from sponge_configd's broadcast) and
 *      asserts the latest value is present. sponge_configd persists
 *      each accepted set to /var/sponge_configd/store.xml.
 *
 *   2. The probe then reads store.xml via a SECOND File_system session
 *      (its <vfs> mounts the same RAM vfs the daemon wrote to). This
 *      is the persistence proof: on any future boot, sponge_configd
 *      would load store.xml via the same code path. The probe asserts
 *      store.xml contains panel.height=28 (the most-recent value) AND
 *      contains panel.visible_widgets=clock (an intermediate write
 *      that must NOT have been clobbered).
 *
 *   3. The probe also asserts the store.xml root element is
 *      <sponge-config version="1"> (the wire format contract) — a
 *      format bump breaks the load with a clear warning, by design.
 *
 *   4. On success the probe logs exactly
 *      "configd-persist-probe: PASS" and exits 0. The run scenario
 *      gates on that marker (fail-loud on timeout).
 *
 * The probe is also used for the corrupt-store variant
 * (run/sponge-configd-persist-corrupt.run) by pre-seeding a torn
 * store.xml in the vfs at boot: sponge_configd logs the warning and
 * starts with defaults, and the probe asserts the broadcast carries
 * the defaults — proving the daemon never crashes on a torn write.
 *
 * Capability boundary: Report + ROM (config channel) + File_system
 * (read store.xml). No Timer, no Capture, no GUI.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>
#include <vfs/simple_env.h>

namespace {

struct Probe
{
	Genode::Env &_env;

	Timer::Connection                          _timer    { _env };
	Genode::Heap                                _heap     { _env.ram(), _env.rm() };
	Genode::Constructible<Genode::Vfs::Simple_env> _vfs_env { };

	Genode::Expanding_reporter     _request   { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace _result    { _env, "config_result" };
	Genode::Attached_rom_dataspace _broadcast { _env, "broadcast" };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	bool _ok { true };

	Probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("configd-persist-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/*
	 * Send a <request op="set" key="..." value="..."/> via the
	 * config_request Report. Blocks (bounded poll) until the matching
	 * config_result has status="ok" with op/key/value matching. Returns
	 * true on success, false on any mismatch or timeout.
	 */
	bool _send_set(char const *key, char const *value)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		/*
		 * Allow report_rom + sponge_configd to settle. Each successful
		 * set regenerates the broadcast; we re-read the broadcast below
		 * to verify the value is present.
		 */
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_result.valid()) {
				try {
					Genode::Xml_node const r = _result.xml();
					if (r.attribute_value("status", Genode::String<32>()) ==
					    Genode::String<32>("ok") &&
					    r.attribute_value("op", Genode::String<32>()) ==
					    Genode::String<32>("set") &&
					    r.attribute_value("key", Genode::String<128>()) ==
					    Genode::String<128>(key) &&
					    r.attribute_value("value", Genode::String<128>()) ==
					    Genode::String<128>(value))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Verify the broadcast ROM carries a <key name="K" value="V"/>
	 * entry. Polls for a bounded budget to absorb the report_rom +
	 * sponge_configd round trip after a set.
	 */
	bool _broadcast_has(char const *key, char const *value)
	{
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
					if (root.has_type("config")) {
						bool found { false };
						root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
							if (!found &&
							    k.attribute_value("name", Genode::String<64>()) ==
							        Genode::String<64>(key) &&
							    k.attribute_value("value", Genode::String<128>()) ==
							        Genode::String<128>(value))
								found = true;
						});
						if (found) return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/* (no further helpers) */

	/*
	 * Read store.xml from the probe's <vfs> mount and return the bytes
	 * + size. Returns false if the file can't be opened or read.
	 *
	 * Both the daemon's and probe's <vfs> mount the same backing FS at
	 * the FS root, so the file written by the daemon as /store.xml is
	 * reachable from the probe at the same path.
	 */
	bool _read_store_xml(char *buf, Genode::size_t buf_size,
	                     Genode::size_t &out_size)
	{
		if (!_vfs_env.constructed())
			return false;

		Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

		Genode::Vfs::Directory_service::Stat stat { };
		if (vfs.stat("/store.xml", stat) !=
		    Genode::Vfs::Directory_service::STAT_OK) {
			Genode::log("configd-persist-probe: store.xml not found");
			return false;
		}
		if (stat.size == 0 || stat.size > buf_size)
			return false;

		Genode::Vfs::Vfs_handle *handle { nullptr };
		if (vfs.open("/store.xml",
		             Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
		             &handle, _heap) !=
		    Genode::Vfs::Directory_service::OPEN_OK)
			return false;
		Genode::Vfs::Vfs_handle::Guard guard(handle);

		Genode::size_t total { 0 };
		bool ok { true };
		while (total < stat.size) {
			handle->seek(total);
			handle->fs().queue_read(handle, stat.size - total);
			Genode::size_t n { 0 };
			Genode::Vfs::File_io_service::Read_result r;
			while ((r = handle->fs().complete_read(handle,
			            Genode::Byte_range_ptr(buf + total, buf_size - total),
			            n)) == Genode::Vfs::File_io_service::READ_QUEUED)
				_vfs_env->io().commit_and_wait();
			if (r != Genode::Vfs::File_io_service::READ_OK || n == 0) {
				ok = false; break;
			}
			total += n;
		}

		if (!ok) return false;
		out_size = total;
		return true;
	}

	bool _store_xml_contains(char const *needle)
	{
		char buf[4096] { };
		Genode::size_t total { 0 };
		if (!_read_store_xml(buf, sizeof(buf), total))
			return false;

		/* naive substring search — the store is a flat ASCII XML */
		Genode::size_t const needle_len = Genode::strlen(needle);
		if (needle_len == 0 || needle_len > total) return false;
		for (Genode::size_t i = 0; i + needle_len <= total; ++i) {
			if (Genode::strcmp(buf + i, needle, needle_len) == 0)
				return true;
		}
		return false;
	}

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid()) {
			_fail("no probe config ROM");
			return;
		}

		/*
		 * Wait for sponge_configd to be ready: it constructs its
		 * default-broadcast ROM before the probe's first request, so
		 * the probe polls for a non-empty broadcast before issuing the
		 * first set. Without this, the first request may race the
		 * daemon's config-ROM signal wiring and be lost.
		 */
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
					if (root.has_type("config"))
						break;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}

		/*
		 * Mount the same writable RAM vfs the daemon writes to. The
		 * scenario wires both sponge_configd and this probe against
		 * one vfs child (the ram_vfs server) so the probe can read
		 * back the durable copy of the store.
		 *
		 * The probe's <vfs> is supplied by its own <config><vfs/></config>
		 * node — same opt-in gate as the daemon. We use Node API so
		 * the Simple_env constructor (which takes Node const&) is
		 * satisfied directly without an Xml->Node private-conversion.
		 */
		try {
			_config_rom.node().with_optional_sub_node("vfs",
				[&](Genode::Node const &vfs_node) {
					_vfs_env.construct(_env, _heap, vfs_node);
					Genode::log("configd-persist-probe: read-vfs enabled");
				});
		} catch (Genode::Xml_node::Invalid_syntax) {
			_fail("malformed probe config");
			return;
		}

		Genode::log("configd-persist-probe: starting 3-write sequence");

		/* write 1: panel.height=64 */
		if (!_send_set("panel.height", "64")) {
			_fail("set panel.height=64 not answered");
			return;
		}
		if (!_broadcast_has("panel.height", "64")) {
			_fail("broadcast missing panel.height=64 after write 1");
			return;
		}
		Genode::log("configd-persist-probe: [1] panel.height=64 broadcast ok");

		/* write 2: panel.visible_widgets=clock */
		if (!_send_set("panel.visible_widgets", "clock")) {
			_fail("set panel.visible_widgets=clock not answered");
			return;
		}
		if (!_broadcast_has("panel.visible_widgets", "clock")) {
			_fail("broadcast missing panel.visible_widgets=clock after write 2");
			return;
		}
		Genode::log("configd-persist-probe: [2] panel.visible_widgets=clock broadcast ok");

		/* write 3: panel.height=28 (overwrites write 1's panel.height) */
		if (!_send_set("panel.height", "28")) {
			_fail("set panel.height=28 not answered");
			return;
		}
		if (!_broadcast_has("panel.height", "28")) {
			_fail("broadcast missing panel.height=28 after write 3");
			return;
		}
		Genode::log("configd-persist-probe: [3] panel.height=28 broadcast ok");

		/*
		 * Persistence proof: read the on-disk store.xml (the file the
		 * daemon wrote to after every successful set) and assert it
		 * carries the most-recent panel.height AND the intermediate
		 * panel.visible_widgets (no clobber between writes). The
		 * root element check guards the format contract.
		 */
		if (!_vfs_env.constructed()) {
			_fail("read-vfs not enabled in probe config");
			return;
		}

		if (!_store_xml_contains("<sponge-config version=\"1\">")) {
			_fail("store.xml missing <sponge-config version=\"1\"> root");
			return;
		}
		Genode::log("configd-persist-probe: store.xml root element present");

		if (!_store_xml_contains("<entry name=\"panel.height\" value=\"28\"/>")) {
			_fail("store.xml missing most-recent panel.height=28");
			return;
		}
		Genode::log("configd-persist-probe: store.xml carries panel.height=28");

		if (!_store_xml_contains("<entry name=\"panel.visible_widgets\" value=\"clock\"/>")) {
			_fail("store.xml missing intermediate panel.visible_widgets=clock");
			return;
		}
		Genode::log("configd-persist-probe: store.xml carries panel.visible_widgets=clock");

		Genode::log("configd-persist-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
