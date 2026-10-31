/*
 * \brief  Process-local shared memory file system for qtwebengine:
 *         qtwebengine creates files in a single directory (the root
 *         directory of this plugin), allocates space once with
 *         'ftruncate()' and then maps the file contents with 'mmap()'
 *         as shared memory from different threads.
 * \author Christian Prochaska
 * \date   2019-03-29
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * Source port provenance (Sponge OS, 2026-10-31, uncommitted)
 * ----------------------------------------------------------
 * Base: cproc/src/vfs_qtwebengine_shm recipe version 2026-10-23
 *   URL: https://depot.genode.org/cproc/src/vfs_qtwebengine_shm/2026-10-23.tar.xz
 *   SHA-256: 795045d08967dd26cfca313c2869fc531f391d5cc68182d07c33b05e299e04dd
 * Adapted to the Genode 26.08 VFS API as vendored in genode/
 * ('git describe' 26.08-36-g82f0143d1f). The 2026-10-23 recipe
 * targets the post-2026-10-16 master API ('Vfs::File_channel',
 * 'Open_attr', 'Follow_error'), none of which exist in 26.08. The
 * 26.08 model is 'Vfs::Vfs_handle' +
 * 'open(path, unsigned mode, Vfs_handle **, Allocator &)'.
 *
 * Interface adaptations (no facade, no ABI shim):
 *
 * 1. 'struct File_channel : Vfs::File_channel' -> ': Vfs::Vfs_handle'
 *    with the 26.08 ctor '(Directory_service &, Allocator &, int)'.
 *    The master 'Attr { .writeable }' propagation becomes the 26.08
 *    'status_flags' pass-through of the open 'mode' (the idiom used by
 *    'os/src/lib/vfs/ram_file_system.h').
 * 2. 'File_channel::Resize_result resize()' -> 'Ftruncate_result
 *    ftruncate()'; 'Dataspace_vfs_file::resize' returns
 *    'Vfs_handle::Ftruncate_result' ('FTRUNCATE_OK' /
 *    'FTRUNCATE_ERR_NO_PERM'), matching the pre-refactor vintage of
 *    this plugin (cproc recipe 2026-05-05, SHA-256
 *    be14be41965e09861dd2fed463a2bc442db6cb2574c91a7a0367b7a04c05488e,
 *    whose 'Directory_service::open' signature is identical to 26.08).
 * 3. 'open(path, Open_attr, Allocator &)' -> 'open(path, unsigned
 *    mode, Vfs_handle **, Allocator &)'.  'attr.create' is
 *    'mode & OPEN_MODE_CREATE'.  The master's 'Open_error::DENIED'
 *    sites map to the 26.08 result codes this caller contract
 *    requires: create-on-existing returns 'OPEN_ERR_EXISTS' so that
 *    'Vfs::File_handle' (os/include/vfs/file_handle.h) performs its
 *    documented retry without the CREATE flag; allocation failure of
 *    the file object returns 'OPEN_ERR_NO_SPACE'; missing entry on a
 *    non-create open returns 'OPEN_ERR_UNACCESSIBLE' (all three as in
 *    the 2026-05-05 vintage).
 * 4. 'Follow_result follow()' (2026-10-23) is dropped: 26.08 has no
 *    'follow' hook.  Symlink resolution in 26.08 is driven by
 *    'stat'/'openlink', and this file system never reports
 *    'Node_type::SYMLINK', so the 2026-10-23 'NO_SYMLINK' semantics
 *    hold by construction.
 * 5. Handle teardown: the master channel's 'destruct()' becomes the
 *    26.08 'Directory_service::close(Vfs_handle *)' override
 *    destroying the handle (running the unchanged '~File_channel'
 *    bookkeeping), following 'ram_file_system.h'.
 * 6. 'Stat::type' values: master's 'Dirent_type::DIRECTORY/FILE' ->
 *    26.08 'Node_type::DIRECTORY/CONTINUOUS_FILE'; 'Stat_result::
 *    OK/DENIED' -> 'STAT_OK/STAT_ERR_NO_ENTRY'.  'Stat::device' is
 *    filled as in the 2026-05-05 vintage.  26.08 'Stat' has no
 *    'inode' member.
 * 7. 'opendir' and the directory handle from the 2026-05-05 vintage
 *    are retained: 26.08 exposes 'Directory_service::opendir' as a
 *    first-class entry point and the proven binary implemented it
 *    (empty listing).  The master recipe dropped it with the
 *    'File_channel' unification.  'Dataspace_vfs_file::resize' sets
 *    '_length' only after a successful RAM allocation so a failed
 *    'ftruncate' never leaves a stale length that would make the
 *    destructor free an invalid capability.
 * 8. 'stat' leaves 'size' at zero exactly as both upstream vintages
 *    do; 'Dataspace_vfs_file::length()' is likewise unused upstream.
 *
 * The 'vfs_file_system_factory' export below is byte-identical to the
 * 2026-10-23 recipe; its 'Vfs::File_system::Factory' shape
 * ('Instance::Attempt create(Vfs::Env &, Parent_fs &, Node const &)',
 * '_free') already matches the 26.08 interface.
 */

#include <vfs/env.h>
#include <vfs/file_system.h>
#include <vfs/vfs_handle.h>
#include <base/ram_allocator.h>
#include <dataspace/client.h>
#include <util/list.h>
#include <util/string.h>

namespace Vfs_shm {

	using namespace Genode;
	using namespace Genode::Vfs;

	class File_system;
}


class Vfs_shm::File_system : public Vfs::File_system
{
	public:

		enum { MAX_NAME_LEN = 128 };

	private:

		class Dataspace_vfs_file : public List<Dataspace_vfs_file>::Element
		{
			private:

				using Filename = String<MAX_NAME_LEN>;
				Filename _filename { };

				Allocator     &_alloc;
				Ram_allocator &_ram;

				file_size _length { 0 };

			public:

				unsigned int open_count   { 0 };
				bool unlink_on_last_close { false };

				bool matches(const char *path)
				{
					return (strlen(path) == (strlen(_filename.string()))) &&
					       (strcmp(path, _filename.string()) == 0);
				}

				Ram_dataspace_capability ds_cap { };

				Dataspace_vfs_file(char const *name, Allocator &alloc, Ram_allocator &ram)
				: _filename(name), _alloc(alloc), _ram(ram) { }

				~Dataspace_vfs_file()
				{
					if (_length > 0)
						_ram.free(ds_cap);
				}

				Allocator &alloc() { return _alloc; }

				file_size length() { return _length; }

				Vfs_handle::Ftruncate_result resize(file_size size)
				{
					if (_length > 0) {
						error(__PRETTY_FUNCTION__, ": resizing not supported yet");
						return Vfs_handle::FTRUNCATE_ERR_NO_PERM;
					}

					ds_cap = _ram.alloc((size_t)size);

					/* track the length only after the allocation succeeded */
					_length = size;

					return Vfs_handle::FTRUNCATE_OK;
				}
		};


		struct File_channel : Vfs_handle
		{
			Allocator          &_alloc;
			File_system        &_fs;
			Dataspace_vfs_file &_file;

			File_channel(Allocator &alloc, File_system &fs, Dataspace_vfs_file &file,
			             Directory_service &ds, int status_flags)
			:
				Vfs_handle(ds, alloc, status_flags),
				_alloc(alloc), _fs(fs), _file(file)
			{ }

			~File_channel()
			{
				_file.open_count--;
				if ((_file.open_count == 0) && (_file.unlink_on_last_close)) {
					_fs._files.remove(&_file);
					destroy(_file.alloc(), &_file);
				}
			}

			bool read_ready () const override { return false; }
			bool write_ready() const override { return false; }

			Read_result read(At, Byte_range_ptr const &) override
			{
				return Read_error::DENIED;
			}

			Ftruncate_result ftruncate(file_size len) override
			{
				try { return _file.resize(len); }
				catch (Out_of_ram)  { return FTRUNCATE_ERR_NO_SPACE; }
				catch (Out_of_caps) { return FTRUNCATE_ERR_NO_SPACE; }
				catch (Denied)      { return FTRUNCATE_ERR_NO_PERM; }
			}

			Dataspace_vfs_file &file() { return _file; }
		};


		/*
		 * Directory handle as implemented by the pre-refactor vintage
		 * of this plugin (cproc recipe 2026-05-05): the listing is
		 * empty.  Sub directories are not supported.
		 */
		struct Dataspace_vfs_dir_handle : Vfs_handle
		{
			/*
			 * Noncopyable
			 */
			Dataspace_vfs_dir_handle(Dataspace_vfs_dir_handle const &);
			Dataspace_vfs_dir_handle &operator = (Dataspace_vfs_dir_handle const &);

			public:

				Dataspace_vfs_dir_handle(Directory_service &ds, Allocator &alloc)
				: Vfs_handle(ds, alloc, 0) { }

				Read_result read(At, Byte_range_ptr const &dst) override
				{
					error("Dataspace_vfs_dir_handle::read() called, not implemented");

					if (dst.num_bytes < sizeof(Dirent))
						return Read_error::DENIED;

					Dirent &out = *(Dirent*)dst.start;

					out = {
						.type = Dirent_type::END,
						.rwx  = { },
						.name = { }
					};

					return sizeof(Dirent);
				}

				bool read_ready () const override { return true; }
				bool write_ready() const override { return false; }

				Ftruncate_result ftruncate(file_size) override
				{
					return FTRUNCATE_ERR_NO_PERM;
				}
		};

		Vfs::Env &_env;

		List<Dataspace_vfs_file> _files { };

		unsigned _num_dirent = 0;

		bool _root(const char *path)
		{
			return (strcmp(path, "") == 0) || (strcmp(path, "/") == 0);
		}

		Dataspace_vfs_file *_lookup(char const *path)
		{
			for (Dataspace_vfs_file *file = _files.first(); file; file = file->next()) {
				if (file->matches(path))
					return file;
			}
			return nullptr;
		}

	public:

		File_system(Vfs::Env &env, Node const &node)
		:
			Vfs::File_system(Ident::from_node(node)), _env(env)
		{ }

		~File_system() { }

		/*********************************
		 ** Directory service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *path) override
		{
			Ram_dataspace_capability ds_cap;

			Dataspace_vfs_file *file = _lookup(path);

			if (!file)
				return ds_cap;

			return file->ds_cap;
		}

		void release(char const *, Dataspace_capability) override
		{
			/* the dataspace gets freed when the file is deleted */
		}

		Stat_result stat(char const *path, Stat &out) override
		{
			out = Stat { };
			out.device = (addr_t)this;

			if (_root(path)) {
				out.type = Node_type::DIRECTORY;

			} else if (_lookup(path)) {
				out.type = Node_type::CONTINUOUS_FILE;
				out.rwx  = Node_rwx::rw();
			} else {
				return STAT_ERR_NO_ENTRY;
			}
			return STAT_OK;
		}

		unsigned num_dirent(char const *path) override
		{
			if (_root(path))
				return 1;
			else
				return _num_dirent;
		}

		bool directory(char const *path) override
		{
			if (_root(path))
				return true;

			return false;
		}

		bool dir_entry_exists(char const *path) override {
			return _lookup(path) != nullptr; }

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle,
		                       Allocator   &alloc) override
		{
			if (!_root(path))
				return OPENDIR_ERR_LOOKUP_FAILED;

			if (create)
				return OPENDIR_ERR_PERMISSION_DENIED;

			try {
				*out_handle = new (alloc)
					Dataspace_vfs_dir_handle(*this, alloc);
				return OPENDIR_OK;
			}
			catch (Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }
		}

		/*
		 * Sub directories are not supported.
		 */
		Open_result open(char const  *path, unsigned mode,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			Dataspace_vfs_file *file { };

			bool const create = mode & OPEN_MODE_CREATE;

			if (create) {

				if (_lookup(path))
					return OPEN_ERR_EXISTS;

				if (strlen(path) >= MAX_NAME_LEN)
					return OPEN_ERR_NAME_TOO_LONG;

				try { file = new (alloc) Dataspace_vfs_file(path, alloc, _env.env().ram()); }
				catch (Allocator::Out_of_memory) { return OPEN_ERR_NO_SPACE; }

				_files.insert(file);
			} else {
				file = _lookup(path);
				if (!file) return OPEN_ERR_UNACCESSIBLE;
			}

			try {
				File_channel &c = *new (alloc)
					File_channel(alloc, *this, *file, *this, (int)mode);
				file->open_count++;
				*out_handle = &c;
				return OPEN_OK;
			}
			catch (Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}

		void close(Vfs_handle *vfs_handle) override
		{
			if (vfs_handle && (&vfs_handle->ds() == this))
				destroy(vfs_handle->alloc(), vfs_handle);
		}

		Rename_result rename(char const *, char const *) override
		{
			return RENAME_ERR_NO_PERM;
		}

		Unlink_result unlink(char const *path) override
		{
			Dataspace_vfs_file *file = _lookup(path);

			if (!file)
				return UNLINK_ERR_NO_ENTRY;

			if (file->open_count == 0) {
				_files.remove(file);
				destroy(file->alloc(), file);
			} else {
				file->unlink_on_last_close = true;
			}

			return UNLINK_OK;
		}

		/***************************
		 ** File_system interface **
		 ***************************/

		static char const *name()   { return "qtwebengine_shm"; }
		char const *type() override { return "qtwebengine_shm"; }

		void destruct() override { destroy(_env.alloc(), this); }
};


extern "C" Genode::Vfs::File_system::Factory *vfs_file_system_factory(void)
{
	using namespace Genode;

	struct Factory : Vfs::File_system::Factory
	{
		using Fs = Vfs_shm::File_system;

		Instance::Attempt create(Vfs::Env &env, Vfs::Parent_fs &, Node const &config) override
		{
			return { *this, { *new (env.alloc()) Fs(env, config) } };
		}

		void _free(Instance &instance) override { instance.fs.destruct(); };
	};

	static Factory f;
	return &f;
}
