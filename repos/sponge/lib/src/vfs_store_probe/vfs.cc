/* SPDX-License-Identifier: Apache-2.0
 *
 * vfs_store_probe — instrumented single-file VFS plugin for the
 * sponge_pkgd store-flush path (Phase 16 P3.3,
 * run/sponge-pkg-dispatch-liveness.run).
 *
 * Mounted as
 *     <vfs> <dir name="store"> <store_probe name="installed.xml"
 *                              delay_ms="1500"/> </dir> </vfs>
 *
 * it backs /store/installed.xml (the pkgd store path, store.cc
 * STORE_PATH) with one in-memory file and gives the scenario two
 * observability properties the plain backends cannot:
 *
 *   (1) ATTRIBUTION: every write/sync logs the CALLING THREAD name
 *
 *           store-probe: write n=<bytes> thread=<name>
 *
 *       so the scenario can assert the store flush does NOT run on the
 *       entrypoint ("thread=ep") but on the pkgd Bootstrap worker
 *       ("thread=pkgd-store"). P3.3 hands storage I/O to that worker;
 *       a flush on the dispatch path parks the entrypoint for the
 *       duration of the write.
 *
 *   (2) LATENCY: every write sleeps `delay_ms` milliseconds before
 *       completing, widening the flush window so the scenario can park
 *       a second request inside it and observe FIFO ordering
 *       deterministically instead of racing microseconds.
 *
 * Test-only: this plugin is never mounted by product media (it is
 * loaded on demand from the vfs_store_probe.lib.so boot module via the
 * VFS external-factory probe, root.cc `_probe_external_factory`).
 *
 * Semantics note: `open` with OPEN_MODE_CREATE is OPEN-OR-CREATE (a
 * repeated _save_store rewrite succeeds), matching the store's
 * rewrite-every-mutation pattern rather than the O_EXCL reading of the
 * flag. stat reports NO_ENTRY until the first write, so a fresh store
 * logs the same "no installed-set store — starting empty" line as an
 * absent backend.
 */

#include <vfs/env.h>
#include <vfs/file_system.h>
#include <vfs/single_file_system.h>
#include <vfs/vfs_handle.h>

#include <base/heap.h>
#include <base/log.h>
#include <base/thread.h>
#include <timer_session/connection.h>
#include <util/string.h>

namespace Vfs_store_probe {

	using namespace Genode;
	using namespace Genode::Vfs;

	struct File_system;
}

struct Vfs_store_probe::File_system : Single_file_system
{
	enum { BUF_SIZE = 4096 };

	Vfs::Env &_env;

	unsigned const _delay_ms;

	char          _buf[BUF_SIZE] { };
	Genode::size_t _len { 0 };
	bool           _exists { false };

	Timer::Connection _timer { _env.env() };

	struct Store_vfs_handle : Single_vfs_handle
	{
		File_system &_fs;

		Store_vfs_handle(Directory_service &ds, Allocator &alloc,
		                 File_system &fs)
		:
			Single_vfs_handle(ds, alloc, 0), _fs(fs)
		{ }

		Read_result read(At const at, Byte_range_ptr const &dst) override
		{
			if (at.pos >= _fs._len)
				return Read_eof();

			Genode::size_t const n = Genode::min(
				_fs._len - at.pos, dst.num_bytes);

			Genode::memcpy(dst.start, _fs._buf + at.pos, n);
			return n;
		}

		Write_result write(At const at,
		                   Const_byte_range_ptr const &src) override
		{
			Genode::log("store-probe: write n=", src.num_bytes,
			            " thread=", Thread::myself()->name);

			if (_fs._delay_ms)
				_fs._timer.msleep(_fs._delay_ms);

			if (at.pos >= BUF_SIZE)
				return Write_error::DENIED;

			Genode::size_t const n = Genode::min(
				Genode::size_t(BUF_SIZE - at.pos), src.num_bytes);

			Genode::memcpy(_fs._buf + at.pos, src.start, n);

			_fs._exists = true;
			if (at.pos + n > _fs._len)
				_fs._len = at.pos + n;

			return n;
		}

		Ftruncate_result ftruncate(file_size len) override
		{
			_fs._exists = true;
			_fs._len = Genode::min(Genode::size_t(len),
			                       Genode::size_t(BUF_SIZE));
			return FTRUNCATE_OK;
		}

		Sync_result sync() override
		{
			Genode::log("store-probe: sync thread=",
			            Thread::myself()->name);
			return Sync_result::OK;
		}

		bool read_ready()  const override { return true; }
		bool write_ready() const override { return true; }
	};

	File_system(Vfs::Env &env, Parent_fs &parent_fs, Node const &config)
	:
		Single_file_system(parent_fs, Node_type::TRANSACTIONAL_FILE,
		                   name(), Node_rwx::rw(), config),
		_env(env),
		_delay_ms(config.attribute_value("delay_ms", 0U))
	{ }

	static char const *name()   { return "store_probe"; }
	char const *type() override { return "store_probe"; }

	void destruct() override { destroy(_env.alloc(), this); }


	/*********************************
	 ** Directory-service interface **
	 *********************************/

	Open_result open(char const *path, unsigned,
	                 Vfs_handle **out_handle, Allocator &alloc) override
	{
		if (!_single_file(path))
			return OPEN_ERR_UNACCESSIBLE;

		try {
			*out_handle = new (alloc) Store_vfs_handle(*this, alloc, *this);

			_exists = true;
			return OPEN_OK;
		}
		catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
		catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
	}

	Stat_result stat(char const *path, Stat &out) override
	{
		Stat_result const result = Single_file_system::stat(path, out);

		if (result == STAT_OK && _single_file(path) && !_exists)
			return STAT_ERR_NO_ENTRY;

		if (result == STAT_OK)
			out.size = _len;

		return result;
	}

	using Single_file_system::close;
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_store_probe::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &parent_fs,
		                         Node const &config) override
		{
			return { *this, { *new (env.alloc()) Fs(env, parent_fs, config) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); }
	};

	static Factory f;
	return &f;
}
