/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd store module - persistent installed-set store + bake seeding.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::STORE_PATH/STORE_VERSION/STORE_BUF and
 * Main::_init_store/_load_store/_save_store, plus the bake pair
 * (_init_bake/_seed_roots_from_bake), from main.cc. This module owns
 * the whole persistence/bake concern; main.cc keeps the request/result
 * channels, emission wiring and bootstrap, and _init_launcher_channel
 * stays there because it is channel wiring.
 *
 * Preserved semantics (the function comments below are moved verbatim):
 *   - Persistence is opt-in: the store activates only when this
 *     component's <config> carries a <vfs> node. Without one no
 *     File_system session is opened and the daemon behaves
 *     byte-identically to the in-memory build.
 *   - Restore: _load_store tolerates every failure mode (missing,
 *     empty/oversized, unreadable, wrong root element, unsupported
 *     version, malformed XML) by starting empty with a warning, never a
 *     crash. Only _roots[] is persisted; _installed is re-derived by
 *     the caller via _sync_installed_from_roots (registry.cc).
 *   - Bake seeding runs only when no roots are known yet, so persisted
 *     removals stay authoritative across reboots. The seed ordering is
 *     owned by main.cc's _bootstrap: _init_store/_load_store, then
 *     _init_bake/_seed_roots_from_bake, all before _init_admission -
 *     cross-TU calls into this module, same Main class.
 *
 * Stack discipline is preserved exactly as it was: the STORE_BUF (4 KiB)
 * read/write scratch buffers in _load_store/_save_store stay stack
 * locals of the calling thread (the Bootstrap thread's 64 KiB stack at
 * boot, env.ep() for ops.cc's flush-before-broadcast _save_store) -
 * kept as stack locals, never made static.
 *
 * Product-media observation (docs/evidence/phase16-runtime-
 * config-coupling.md §5.1): the sponge-desktop-disk* start nodes carry
 * no <vfs> node today, so the store path is inactive there (whether
 * product-media persistence should be armed is not decided); it is
 * exercised by run/sponge-pkg-persist.run (lx_fs) and
 * run/sponge-persist-disk.run.
 */

#include "pkgd.h"

#include <base/log.h>
#include <vfs/root.h>

/* ===================== persistent store (optional) ===================== */

char const         Sponge::Pkgd::Main::STORE_PATH[]   = "/store/installed.xml";
unsigned const     Sponge::Pkgd::Main::STORE_VERSION  = 1;

/*
 * Wait primitive for one RETRY step of the store I/O loops.
 *
 * On the entrypoint (the no-thread fallback, `_single_threaded`):
 * Vfs::Env::Io::commit_and_wait is the sound primitive — its
 * Entrypoint::wait_and_dispatch_one_io_signal call dispatches the VFS
 * plugin's I/O-level ack signals inline, which is exactly what the
 * fs-plugin read/sync state machines need to make progress there (they
 * do not drain acks themselves on re-entry).
 *
 * Off the entrypoint (the Bootstrap worker and the bootstrap drains):
 * commit_and_wait parks in the same function, which defers pending
 * APPLICATION-level signals to the entrypoint and then blocks forever
 * — observed as "_wait_and_dispatch_one_io_signal called from
 * non-entrypoint thread" followed by a dead flush
 * (docs/evidence/phase16-runtime-p33.md §6). There the poll path runs
 * instead: commit() (the non-blocking half — it notifies the remote
 * peer, which try_submit_packet only marks as needed) plus a
 * timer-paced sleep (Timer::Connection blocks on its private signal
 * receiver — safe from any thread).
 *
 * Both paths share RETRY_BOUND: the bound keeps the store-failure
 * contract (a stalled provider degrades to the loud "store write
 * incomplete", never a silently lost write). On the entrypoint an
 * individual commit_and_wait can still block on the provider exactly
 * as the pre-P3.3 code did.
 */
void Sponge::Pkgd::Main::_store_io_wait()
{
	if (_single_threaded) {
		_vfs_env->io().commit_and_wait();
		return;
	}
	_vfs_env->io().commit();
	_store_timer->msleep(1);
}


unsigned const Sponge::Pkgd::Main::RETRY_BOUND = 60000;


/*
 * Build the Vfs environment only when the component <config> carries a
 * <vfs> node. Constructing Vfs::Root mounts the <fs/> plugin,
 * which opens a File_system session routed by the scenario (e.g. to an
 * lx_fs server backed by a host directory). With no <vfs> node the
 * store stays disabled and the daemon behaves exactly as the in-memory
 * Phase 4 build — this is what keeps the non-persistent scenarios and
 * any deployment that declines persistence working unchanged.
 */
void Sponge::Pkgd::Main::_init_store()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	try {
		_config_rom.node().with_optional_sub_node("vfs",
			[&](Genode::Node const &vfs_node) {
				_vfs_env.construct(_env, _heap, vfs_node);
				if (!_single_threaded)
					_store_timer.construct(_env);
				Genode::log("sponge_pkgd: persistent store enabled at ", STORE_PATH);
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — persistence disabled");
	}
}


/*
 * Restore _roots[] from the store. Every failure mode — missing file,
 * empty/oversized, unreadable, wrong root element, unsupported version,
 * malformed XML — resolves to the same safe state: an empty root set
 * plus a warning, never a crash (docs/12-package-format.md §13.2). The
 * full installed set is rebuilt afterwards by the caller via
 * _sync_installed_from_roots(), so loading and installing share one path.
 */
void Sponge::Pkgd::Main::_load_store()
{
	if (!_store_enabled()) return;

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

	Genode::Vfs::Directory_service::Stat stat { };
	if (vfs.stat(STORE_PATH, stat) != Genode::Vfs::Directory_service::STAT_OK) {
		Genode::log("sponge_pkgd: no installed-set store — starting empty");
		return;
	}
	if (stat.size == 0 || stat.size > STORE_BUF) {
		Genode::warning("sponge_pkgd: store size ", stat.size,
		                " out of range — starting empty");
		return;
	}

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH, Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_pkgd: store open failed — starting empty");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	char buf[STORE_BUF] { };
	Genode::size_t total { 0 };
	bool ok { true };
	while (total < stat.size) {
		Genode::Vfs::Vfs_handle::Read_result r
			{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
		for (unsigned retry = 0; retry < RETRY_BOUND; ++retry) {
			r = handle->read(Genode::Vfs::At { .pos = total },
			                 Genode::Byte_range_ptr(buf + total,
			                                        stat.size - total));
			if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
				break;
			_store_io_wait();
		}
		Genode::size_t const n = r.convert<Genode::size_t>(
			[](Genode::size_t bytes) { return bytes; },
			[](Genode::Vfs::Vfs_handle::Read_error) {
				return Genode::size_t(0); });
		if (n == 0) {
			ok = false; break;
		}
		total += n;
	}

	if (!ok || total == 0) {
		Genode::warning("sponge_pkgd: store unreadable — starting empty");
		return;
	}

	unsigned restored { 0 };
	try {
		Genode::Xml_node const root(buf, total);
		if (!root.has_type("sponge-installed")) {
			Genode::warning("sponge_pkgd: store root is not <sponge-installed> "
			                "— starting empty");
			return;
		}
		unsigned const version = root.attribute_value("version", 0U);
		if (version != STORE_VERSION) {
			Genode::warning("sponge_pkgd: store version ", version,
			                " unsupported (expected ", STORE_VERSION,
			                ") — starting empty");
			return;
		}
		root.for_each_sub_node("root", [&](Genode::Xml_node const &n) {
			Genode::String<64> const name =
				n.attribute_value("name", Genode::String<64>());
			if (Genode::strcmp(name.string(), "") != 0 &&
			    _num_roots < MAX_PACKAGES) {
				_roots[_num_roots++] = name;
				++restored;
			}
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: store is not valid XML — starting empty");
		_num_roots = 0;
		return;
	}

	Genode::log("sponge_pkgd: restored ", restored, " root(s) from store");
}


/*
 * Persist _roots[] to the store. Output is name-sorted with a fixed
 * attribute order so the file is byte-stable for a given root set
 * (matching the determinism contract of the config generator). A failed
 * write is logged but never blocks the install/remove: the in-memory
 * state and the broadcast still reflect the requested change, only the
 * across-reboot durability is lost for that one mutation.
 *
 * The version attribute is emitted as the literal "1" so the on-disk
 * format is grep-stable; if STORE_VERSION is bumped, update it here too.
 */
/*
 * Serialize the root set into `buf` (P3.3 split: the PURE half of the
 * flush). Output is name-sorted with a fixed attribute order so the
 * file is byte-stable for a given root set (matching the determinism
 * contract of the config generator). Runs on the entrypoint at job
 * build time (post-mutation roots) or on the Bootstrap thread at boot.
 *
 * The version attribute is emitted as the literal "1" so the on-disk
 * format is grep-stable; if STORE_VERSION is bumped, update it here too.
 */
void Sponge::Pkgd::Main::_build_store_payload(char *buf, Genode::size_t &len)
{
	unsigned order[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_roots; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_roots; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_roots; ++j)
			if (Genode::strcmp(_roots[order[j]].string(),
			                   _roots[order[best]].string()) < 0)
				best = j;
		if (best != i) {
			unsigned t = order[i]; order[i] = order[best]; order[best] = t;
		}
	}

	Genode::size_t pos { 0 };
	auto append = [&buf, &pos](char const *s) {
		while (*s && pos + 1 < STORE_BUF) buf[pos++] = *s++;
	};
	append("<sponge-installed version=\"1\">");
	for (unsigned i = 0; i < _num_roots; ++i) {
		append("<root name=\"");
		append(_roots[order[i]].string());
		append("\"/>");
	}
	append("</sponge-installed>");
	len = pos;
}


/*
 * Write a payload previously built by _build_store_payload (P3.3
 * split: the I/O half of the flush). Executes on the Bootstrap worker
 * for queued jobs (touching only the job payload) or synchronously on
 * the calling thread in the no-thread fallback and at bootstrap. A
 * failed write is logged but never blocks the install/remove: the
 * in-memory state and the broadcast still reflect the requested
 * change, only the across-reboot durability is lost for that one
 * mutation.
 */
void Sponge::Pkgd::Main::_write_store_payload(char const *buf,
                                              Genode::size_t len)
{
	if (!_store_enabled()) return;

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH,
	             Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
	             | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_pkgd: cannot open store for write");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	handle->ftruncate(len);

	Genode::size_t off { 0 };
	bool ok { true };
	while (off < len) {
		Genode::Vfs::Vfs_handle::Write_result w
			{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
		for (unsigned retry = 0; retry < RETRY_BOUND; ++retry) {
			w = handle->write(Genode::Vfs::At { .pos = off },
			                  Genode::Const_byte_range_ptr(buf + off,
			                                               len - off));
			if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
				break;
			_store_io_wait();
		}
		Genode::size_t const n = w.convert<Genode::size_t>(
			[](Genode::size_t bytes) { return bytes; },
			[](Genode::Vfs::Vfs_handle::Write_error) {
				return Genode::size_t(0); });
		if (n == 0) { ok = false; break; }
		off += n;
	}

	for (unsigned retry = 0; retry < RETRY_BOUND; ++retry) {
		if (handle->sync() != Genode::Vfs::Sync_result::RETRY)
			break;
		_store_io_wait();
		if (retry + 1 == RETRY_BOUND)
			ok = false;
	}

	if (!ok)
		Genode::warning("sponge_pkgd: store write incomplete");
}


/*
 * Synchronous flush: build + write in one call. Used at bootstrap
 * (_seed_roots_from_bake, on the Bootstrap thread) and by the
 * no-thread fallback; the install/remove request path hands
 * _build_store_payload output to the worker instead (P3.3).
 */
void Sponge::Pkgd::Main::_save_store()
{
	if (!_store_enabled()) return;

	char buf[STORE_BUF] { };
	Genode::size_t len { 0 };
	_build_store_payload(buf, len);
	_write_store_payload(buf, len);
}


/* ===================== bake manifest seeding (optional) ===================== */

/*
 * Open the bake-manifest ROM session only when this component's
 * <config> declares <bake/>. The ROM route must then be wired by the
 * scenario (mirroring sponge_configd's bake_manifest route); Genode's
 * denial path sleeps forever rather than throwing, so scenarios
 * without the route must not carry the node.
 */
void Sponge::Pkgd::Main::_init_bake()
{
	try {
		_config_rom.node().with_optional_sub_node("bake",
			[&] (Genode::Node const &) {
				_bake_manifest_rom.construct(_env, "bake_manifest");
			});
	} catch (Genode::Xml_node::Invalid_syntax) { }
}

/*
 * First-boot package seeding — the pkgd counterpart of configd's
 * boot-time _apply_bake_defaults(). The manifest (generated by
 * tool/bake from pkg/bake/<profile>.profile) lists the profile's
 * enabled packages as a "staged_packages" JSON array; each entry is
 * seeded as an install root so the launcher and pkg_runtime present
 * the pre-staged desktop on first boot. Seeds only when no roots are
 * known yet, so with a persistent store the user's removals stay
 * authoritative across reboots instead of being reverted.
 */
bool Sponge::Pkgd::Main::_seed_roots_from_bake()
{
	if (!_bake_manifest_rom.constructed() || !_bake_manifest_rom->valid())
		return false;
	if (_num_roots > 0)
		return false;

	char const *manifest = _bake_manifest_rom->local_addr<char const>();
	Genode::size_t const size = _bake_manifest_rom->size();

	auto skip_ws = [&] (Genode::size_t &p) {
		while (p < size && (manifest[p] == ' ' || manifest[p] == '\t' ||
		                    manifest[p] == '\r' || manifest[p] == '\n')) ++p;
	};

	char const *const key = "\"staged_packages\"";
	Genode::size_t const key_len = Genode::strlen(key);
	Genode::size_t i = 0;
	for (; i + key_len < size; ++i)
		if (Genode::strcmp(manifest + i, key, key_len) == 0)
			break;
	if (i + key_len >= size)
		return false;

	Genode::size_t p = i + key_len;
	skip_ws(p);
	if (p >= size || manifest[p++] != ':') return false;
	skip_ws(p);
	if (p >= size || manifest[p++] != '[') return false;

	unsigned seeded { 0 };
	for (;;) {
		skip_ws(p);
		if (p >= size || manifest[p] == ']') break;
		if (manifest[p++] != '"') break;

		char name[64] { };
		unsigned n { 0 };
		while (p < size && manifest[p] != '"') {
			if (n + 1 < sizeof(name))
				name[n++] = manifest[p];
			++p;
		}
		if (p >= size) break;
		++p;

		if (n > 0 && _num_roots < MAX_PACKAGES) {
			_roots[_num_roots++] = Genode::String<64>(name);
			++seeded;
		}

		skip_ws(p);
		if (p < size && manifest[p] == ',') { ++p; continue; }
		break;
	}

	if (seeded > 0) {
		Genode::log("sponge_pkgd: seeded ", seeded,
		            " baked package root(s) from the bake manifest");
		if (_store_enabled())
			_save_store();
	}
	return seeded > 0;
}
