/* SPDX-License-Identifier: Apache-2.0
 *
 * provider_isolation_probe — provider-stall isolation probe (P3.1).
 *
 * Per-path checks for run/sponge-provider-stall.run
 * (docs/plans/phase16-runtime-overhaul.md P3.1, FD4/D16R.8). Both
 * checks are BOUNDED and run on worker threads — the sponge_pkgd
 * Bootstrap pattern (docs/evidence/phase16-runtime-patch-io-audit.md
 * §5.1) — so a session create that never returns can never park the
 * probe's entrypoint: the probe always prints its verdict and exits.
 *
 *   path=sys-rom    read ROM "sys_marker" through the rom_sys chain
 *                   (cached_fs_rom -> vfs_sys -> part_block P3 ->
 *                   disk) and byte-compare it. Must pass while the P4
 *                   provider path is stalled: it is the unrelated-ROM
 *                   assertion AND the software-stall-vs-media-death
 *                   discriminator (the same physical disk keeps
 *                   serving P3 reads during the stall).
 *
 *   path=pkgd-store write + read back a marker through a File_system
 *                   session on the P4 package-store region (the
 *                   pkgd-store consumer shape: <vfs><dir store><fs/>
 *                   mounts the session at /store — run/sponge-pkg-
 *                   persist.run's sponge_pkgd mount). Must pass while
 *                   the log path is stalled. On today's shared vfs_var
 *                   this session rides the same provider instance as
 *                   the log sink and parks with it (RED,
 *                   reason=create_parked_30000ms); after the P3.2
 *                   vfs_pkg/vfs_log split it rides vfs_pkg alone
 *                   (GREEN).
 *
 * The log-sink path is asserted host-side (sponge_logdisk's own
 * markers): no probe can reach the 'sponge_logdisk -> ' label_prefix
 * policy — only a child literally named 'sponge_logdisk' matches it
 * (init prefixes the requesting child's name to every routed label).
 *
 * Markers (the run script gates on these and prints per-path RED):
 *
 *   provider-isolation-probe: PASS path=sys-rom
 *   provider-isolation-probe: PASS path=pkgd-store
 *   provider-isolation-probe: PASS
 *   provider-isolation-probe: RED path=<p> reason=<r>
 *   provider-isolation-probe: FAIL (isolation broken under provider stall)
 *   provider-isolation-probe: DONE
 *
 * Every wait is bounded: the workers get BOUND_MS, then the verdict is
 * printed regardless. A parked worker reports
 * 'reason=read_parked_<BOUND_MS>ms' / 'reason=create_parked_<BOUND_MS>ms'.
 * The DONE marker always prints, so the scenario's final gate cannot
 * hang.
 *
 * Why this probe exists (the plan names 'provider-isolation-probe' as
 * P3.1's gate): no existing probe performs a bounded cross-path
 * File_system round-trip with per-path markers — configd_persist_probe
 * is coupled to the configd report channel, configd_corrupt_seed only
 * writes and sleeps. The fault injection itself needs NO new code:
 * run/sponge-provider-stall.run reuses test/ep_wedge as the
 * never-answering stall body and liveness oracle, and
 * test/partition_check as the media-alive witness.
 *
 * AGENTS.md §3.1: qualified Genode types (Genode::size_t), snake_case
 * methods, PascalCase classes, #pragma once not needed (single TU).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/sleep.h>
#include <base/thread.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>
#include <vfs/root.h>

namespace {

/*
 * One bounded wait for both workers (milliseconds). The healthy path
 * finishes in well under a second; a parked provider path reports at
 * the bound. The scenario's gate timeouts exceed this, so the probe's
 * DONE marker always arrives first.
 */
constexpr Genode::uint64_t BOUND_MS = 30000;

/* Byte-exact sys-rom payload; run/sponge-provider-stall.run stages the
 * identical bytes at /system/sys_marker on P3. */
char const SYS_MARKER[]      = "sponge-provider-stall sys-rom marker\n";
char const SYS_MARKER_NAME[] = "sys_marker";

/* Store round-trip payload and path. */
char const STORE_PATH[]    = "/store/pstall.txt";
char const STORE_PAYLOAD[] = "provider-isolation-probe store roundtrip\n";

/* 'reason' strings for the parked case — kept literal and greppable. */
char const REASON_ROM_PARKED[]   = "read_parked_30000ms";
char const REASON_STORE_PARKED[] = "create_parked_30000ms";


/*
 * One bounded per-path check on its own thread.
 *
 * 'done' is written last (after 'ok'/'reason') and is volatile: single
 * writer (this thread), single reader (the entrypoint polling it).
 */
struct Worker : Genode::Thread
{
	enum Kind { ROM, STORE };

	Kind const kind;

	Genode::Env  &_env;
	Genode::Heap &_heap;

	volatile bool   done { false };
	bool            ok   { false };
	char const     *reason { "not-run" };

	Worker(Genode::Env &env, Genode::Heap &heap, Kind kind)
	:
		Thread(env, kind == ROM ? "rom-check" : "store-check",
		       Stack_size { 64 * 1024 }),
		kind(kind), _env(env), _heap(heap)
	{ }

	Worker(Worker const &) = delete;
	Worker &operator=(Worker const &) = delete;

	void entry() override
	{
		if (kind == ROM) _check_rom();
		else             _check_store();

		done = true;
		Genode::sleep_forever();
	}

	void _fail(char const *why)
	{
		ok = false;
		reason = why;
	}

	/*
	 * path=sys-rom: the ROM session create parks on this thread if the
	 * rom_sys chain is broken — that is exactly why the check is not on
	 * the entrypoint. A media failure reports 'read_parked_30000ms';
	 * the scenario then fails its media-alive control leg instead of
	 * silently claiming isolation.
	 */
	void _check_rom()
	{
		try {
			Genode::Attached_rom_dataspace rom { _env, SYS_MARKER_NAME };
			rom.update();
			if (!rom.valid()) {
				_fail("rom_invalid");
				return;
			}

			Genode::size_t const expect = sizeof(SYS_MARKER) - 1;
			Genode::size_t const sz     = rom.size();
			char const * const  p       = rom.local_addr<char const>();

			if (sz < expect ||
			    Genode::strcmp(p, SYS_MARKER, expect) != 0) {
				_fail("payload_mismatch");
				return;
			}
			ok = true;

		} catch (...) {
			_fail("rom_exception");
		}
	}

	/*
	 * path=pkgd-store: mount the <vfs> node from the probe config
	 * (which opens the File_system session — the create parks here on
	 * a stalled provider), write STORE_PATH, sync, read back and
	 * byte-compare. Mirrors configd_corrupt_seed's write dance and
	 * configd_persist_probe's read dance.
	 */
	void _check_store()
	{
		Genode::Constructible<Genode::Vfs::Root> vfs_env { };

		try {
			Genode::Attached_rom_dataspace cfg { _env, "config" };
			cfg.update();
			if (!cfg.valid()) {
				_fail("no_config");
				return;
			}
			cfg.node().with_optional_sub_node("vfs",
				[&] (Genode::Node const &vfs_node) {
					vfs_env.construct(_env, _heap, vfs_node); });

		} catch (...) {
			_fail("vfs_mount_exception");
			return;
		}

		if (!vfs_env.constructed()) {
			_fail("no_vfs_node");
			return;
		}

		Genode::Vfs::File_system &vfs = vfs_env->fs();

		/* --- write --- */
		Genode::Vfs::Vfs_handle *handle { nullptr };
		Genode::Vfs::Directory_service::Open_result open_result =
			vfs.open(STORE_PATH,
			         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
			         &handle, _heap);
		if (open_result == Genode::Vfs::Directory_service::OPEN_ERR_UNACCESSIBLE) {
			open_result =
				vfs.open(STORE_PATH,
				         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
				         | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
				         &handle, _heap);
		}
		if (open_result != Genode::Vfs::Directory_service::OPEN_OK) {
			_fail("store_open_failed");
			return;
		}
		Genode::Vfs::Vfs_handle::Guard guard(handle);

		Genode::size_t const len = sizeof(STORE_PAYLOAD) - 1;
		handle->ftruncate(len);

		Genode::size_t off { 0 };
		while (off < len) {
			Genode::Vfs::Vfs_handle::Write_result w
				{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
			for (;;) {
				w = handle->write(
					Genode::Vfs::At { .pos = off },
					Genode::Const_byte_range_ptr(STORE_PAYLOAD + off,
					                             len - off));
				if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
					break;
				vfs_env->io().commit_and_wait();
			}
			Genode::size_t const n = w.convert<Genode::size_t>(
				[] (Genode::size_t bytes) { return bytes; },
				[] (Genode::Vfs::Vfs_handle::Write_error) {
					return Genode::size_t(0); });
			if (n == 0) {
				_fail("store_write_failed");
				return;
			}
			off += n;
		}

		while (handle->sync() == Genode::Vfs::Sync_result::RETRY)
			vfs_env->io().commit_and_wait();

		/* --- read back --- */
		Genode::Vfs::Vfs_handle *rhandle { nullptr };
		if (vfs.open(STORE_PATH,
		             Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
		             &rhandle, _heap)
		    != Genode::Vfs::Directory_service::OPEN_OK) {
			_fail("store_reopen_failed");
			return;
		}
		Genode::Vfs::Vfs_handle::Guard rguard(rhandle);

		char buf[sizeof(STORE_PAYLOAD) - 1] { };
		Genode::size_t total { 0 };
		while (total < sizeof(buf)) {
			Genode::Vfs::Vfs_handle::Read_result r
				{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
			for (;;) {
				r = rhandle->read(
					Genode::Vfs::At { .pos = total },
					Genode::Byte_range_ptr(buf + total,
					                       sizeof(buf) - total));
				if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
					break;
				vfs_env->io().commit_and_wait();
			}
			Genode::size_t const n = r.convert<Genode::size_t>(
				[] (Genode::size_t bytes) { return bytes; },
				[] (Genode::Vfs::Vfs_handle::Read_error) {
					return Genode::size_t(0); });
			if (n == 0) break;
			total += n;
		}

		if (total != sizeof(buf) ||
		    Genode::strcmp(buf, STORE_PAYLOAD, sizeof(buf)) != 0) {
			_fail("roundtrip_mismatch");
			return;
		}

		ok = true;
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Genode::Heap heap { env.ram(), env.rm() };

	static Worker rom_w   { env, heap, Worker::ROM   };
	static Worker store_w { env, heap, Worker::STORE };

	Timer::Connection timer { env };

	auto wait_done = [&] (volatile bool &done) {
		Genode::uint64_t waited { 0 };
		while (!done && waited < BOUND_MS) {
			timer.msleep(250);
			waited += 250;
		}
	};

	/*
	 * Finish the independent ROM check before starting the intentionally
	 * parked store check. A pending session create holds Component_env's
	 * component-wide session mutex, so starting the store worker first
	 * would prevent the ROM worker from even issuing its ROM request.
	 */
	if (rom_w.start() != Genode::Thread::Start_result::OK) {
		Genode::error("provider-isolation-probe: FAIL "
		              "(ROM worker thread could not be created)");
		Genode::log("provider-isolation-probe: DONE");
		env.parent().exit(1);
		Genode::sleep_forever();
	}
	wait_done(rom_w.done);

	if (store_w.start() != Genode::Thread::Start_result::OK) {
		Genode::error("provider-isolation-probe: FAIL "
		              "(store worker thread could not be created)");
		Genode::log("provider-isolation-probe: DONE");
		env.parent().exit(1);
		Genode::sleep_forever();
	}
	wait_done(store_w.done);

	bool all_ok = true;

	if (rom_w.done && rom_w.ok) {
		Genode::log("provider-isolation-probe: PASS path=sys-rom");
	} else {
		all_ok = false;
		Genode::error("provider-isolation-probe: RED path=sys-rom reason=",
		              rom_w.done ? rom_w.reason : REASON_ROM_PARKED);
	}

	if (store_w.done && store_w.ok) {
		Genode::log("provider-isolation-probe: PASS path=pkgd-store");
	} else {
		all_ok = false;
		Genode::error("provider-isolation-probe: RED path=pkgd-store reason=",
		              store_w.done ? store_w.reason : REASON_STORE_PARKED);
	}

	if (all_ok)
		Genode::log("provider-isolation-probe: PASS");
	else
		Genode::error("provider-isolation-probe: FAIL "
		              "(isolation broken under provider stall)");

	/* Always printed last — the scenario's final gate. */
	Genode::log("provider-isolation-probe: DONE");

	env.parent().exit(all_ok ? 0 : 1);
	Genode::sleep_forever();
}


Genode::size_t Component::stack_size()
{
	return 64 * 1024;
}
