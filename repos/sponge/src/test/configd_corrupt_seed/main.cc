/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * configd_corrupt_seed — pre-stages a torn store.xml before
 * sponge_configd starts (Phase 14 W6 corrupt-store variant).
 *
 * A plain Genode component (no libc, no Qt). On construct it:
 *   (1) Mounts the same writable RAM vfs the daemon will write to
 *       (its <vfs> config mirrors the daemon's mount).
 *   (2) Writes a deliberately torn <sponge-config> payload to
 *       /store.xml — truncated mid-attribute (the failure mode docs/12
 *       §13.2 documents as the "torn write" scenario the daemon's
 *       loader must recover from).
 *   (3) Enters sleep_forever() so init's child list stays stable
 *       while sponge_configd starts and reads the torn store.
 *
 * Capability surface: File_system only (plus the implicit env
 * services). No Timer, no Report, no Capture, no GUI.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <util/string.h>
#include <vfs/simple_env.h>

namespace {

struct Seed
{
	Genode::Env &_env;

	Genode::Heap                                _heap     { _env.ram(), _env.rm() };
	Genode::Constructible<Genode::Vfs::Simple_env> _vfs_env { };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	bool _ok { true };

	Seed(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("configd-corrupt-seed: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/*
	 * Write the torn payload to /store.xml on the vfs mount. The
	 * payload is deliberately truncated mid-attribute so that any
	 * reader sees incomplete XML — the load path must recover via
	 * the malformed-XML warning branch (docs/12 §13.2).
	 */
	bool _write_torn_store_xml()
	{
		if (!_vfs_env.constructed()) return false;

		/*
		 * Truncated mid-attribute: the version attribute is opened
		 * but never closed, and the document has no closing tag.
		 * Xml_node::Invalid_syntax is the expected parse failure.
		 */
		char const torn[] =
			"<sponge-config version=\"";  /* intentional truncation */

		Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

		Genode::Vfs::Vfs_handle *handle { nullptr };
		Genode::Vfs::Directory_service::Open_result open_result =
			vfs.open("/store.xml",
			         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
			         &handle, _heap);
		if (open_result == Genode::Vfs::Directory_service::OPEN_ERR_UNACCESSIBLE) {
			open_result = vfs.open("/store.xml",
			         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
			         | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
			         &handle, _heap);
		}
		if (open_result != Genode::Vfs::Directory_service::OPEN_OK)
			return false;
		Genode::Vfs::Vfs_handle::Guard guard(handle);

		Genode::size_t const len = sizeof(torn) - 1;

		handle->fs().ftruncate(handle, len);

		Genode::size_t off { 0 };
		while (off < len) {
			handle->seek(off);
			Genode::size_t n { 0 };
			Genode::Vfs::File_io_service::Write_result const w =
				handle->fs().write(handle,
				    Genode::Const_byte_range_ptr(torn + off, len - off), n);
			if (w == Genode::Vfs::File_io_service::WRITE_OK) {
				if (n == 0) return false;
				off += n;
			} else if (w == Genode::Vfs::File_io_service::WRITE_ERR_WOULD_BLOCK) {
				_vfs_env->io().commit_and_wait();
			} else {
				return false;
			}
		}

		handle->fs().queue_sync(handle);
		while (handle->fs().complete_sync(handle) ==
		       Genode::Vfs::File_io_service::SYNC_QUEUED)
			_vfs_env->io().commit_and_wait();

		return true;
	}

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid()) {
			_fail("no probe config ROM");
			return;
		}

		try {
			_config_rom.node().with_optional_sub_node("vfs",
				[&](Genode::Node const &vfs_node) {
					_vfs_env.construct(_env, _heap, vfs_node);
				});
		} catch (Genode::Xml_node::Invalid_syntax) {
			_fail("malformed probe config");
			return;
		}

		if (!_vfs_env.constructed()) {
			_fail("read-vfs not enabled in probe config");
			return;
		}

		if (!_write_torn_store_xml()) {
			_fail("could not write torn store.xml");
			return;
		}

		Genode::log("configd-corrupt-seed: torn store.xml pre-staged");
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Seed seed { env };
	seed.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
