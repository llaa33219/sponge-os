/* SPDX-License-Identifier: Apache-2.0
 *
 * store_io.h — package-private store I/O for the settings_demo
 * providers: the sponge_configd _init_store/_load_store/_save_store
 * pattern (docs/12-package-format.md §13) applied to one value in one
 * file per provider.
 *
 * The store is reached through Genode::Vfs::Root over the component's
 * own <config><vfs> node (the established repo pattern — no direct
 * File_system::Connection anywhere in Sponge components). The <fs/>
 * plugin inside that node opens the File_system session the package
 * metadata declares (subpath /app/settings_demo — the store directory
 * scoped to this one package).
 *
 * OPT-IN GATE (docs/12 §13.4): no <vfs> node in the component config
 * means no File_system session is ever opened. The provider then runs
 * with in-memory state only and says so — the persistence escape
 * hatch is a config edit, nothing more.
 *
 * FAIL-SOFT (docs/12 §13.2): every load failure resolves to "start
 * from the provider default plus one bounded warning"; every save
 * failure logs one bounded warning and leaves the in-memory value in
 * place. Writes are crash-consistent: tmp file, sync, rename over the
 * store (the single-writer contract: each provider writes only its
 * own file, nothing else in the store directory).
 */

#pragma once

#include <base/heap.h>
#include <os/vfs.h>
#include <util/reconstructible.h>
#include <vfs/root.h>

#include "store.h"

namespace Sponge::Settings_demo {

struct Store_io
{
	private:

		Genode::Env &_env;
		Genode::Heap _heap;
		Genode::Constructible<Genode::Vfs::Root> _vfs { };

	public:

		explicit Store_io(Genode::Env &env)
		: _env(env), _heap(env.ram(), env.rm()) { }

		bool enabled() const { return _vfs.constructed(); }

		void init(Genode::Node const &config_node, char const *who,
		          char const *path)
		{
			try {
				config_node.with_optional_sub_node("vfs",
					[&] (Genode::Node const &vfs_node) {
						_vfs.construct(_env, _heap, vfs_node);
						Genode::log(who, ": package-private store enabled at ",
						            path);
					});
			} catch (Genode::Xml_node::Invalid_syntax) {
				Genode::warning(who, ": malformed <config> — "
				                "persistence disabled");
			}
		}

		bool load(char const *who, char const *path, char const *entry_name,
		          Value_rules rules, char *value_out, unsigned cap)
		{
			if (!enabled())
				return false;

			Genode::Vfs::File_system &vfs = _vfs->fs();

			Genode::Vfs::Directory_service::Stat stat { };
			if (vfs.stat(path, stat) !=
			    Genode::Vfs::Directory_service::STAT_OK) {
				Genode::log(who, ": no store — starting with default");
				return false;
			}
			if (stat.size == 0 || stat.size > STORE_BUF) {
				Genode::warning(who, ": store size ", stat.size,
				                " out of range — starting with default");
				return false;
			}

			Genode::Vfs::Vfs_handle *handle { nullptr };
			if (vfs.open(path,
			             Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
			             &handle, _heap) !=
			    Genode::Vfs::Directory_service::OPEN_OK) {
				Genode::warning(who, ": store open failed — "
				                "starting with default");
				return false;
			}
			Genode::Vfs::Vfs_handle::Guard guard(handle);

			char buf[STORE_BUF] { };
			Genode::size_t total { 0 };
			bool ok { true };

			while (total < stat.size) {
				Genode::Vfs::Vfs_handle::Read_result r
					{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
				for (;;) {
					r = handle->read(Genode::Vfs::At { .pos = total },
					                 Genode::Byte_range_ptr(
					                     buf + total, stat.size - total));
					if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
						break;
					_vfs->io().commit_and_wait();
				}
				Genode::size_t const n = r.convert<Genode::size_t>(
					[] (Genode::size_t bytes) { return bytes; },
					[] (Genode::Vfs::Vfs_handle::Read_error) {
						return Genode::size_t(0); });
				if (n == 0) {
					ok = false;
					break;
				}
				total += n;
			}

			if (!ok || total == 0) {
				Genode::warning(who, ": store unreadable — "
				                "starting with default");
				return false;
			}

			if (!parse_store(buf, (unsigned)total, entry_name, rules,
			                 value_out, cap)) {
				Genode::warning(who, ": store rejected — "
				                "starting with default");
				return false;
			}

			Genode::log(who, ": restored ", entry_name, " from store");
			return true;
		}

		bool save(char const *who, char const *path, char const *entry_name,
		          char const *value)
		{
			if (!enabled())
				return false;

			char buf[STORE_BUF] { };
			unsigned const len = gen_store(buf, sizeof(buf), entry_name,
			                               value);
			if (len == 0) {
				Genode::warning(who, ": store serialization failed");
				return false;
			}

			Genode::Vfs::File_system &vfs = _vfs->fs();

			char tmp_path[128] { };
			unsigned pos = 0;
			detail::append(tmp_path, sizeof(tmp_path), pos, path);
			if (!detail::append(tmp_path, sizeof(tmp_path), pos, ".tmp")) {
				Genode::warning(who, ": store tmp path too long");
				return false;
			}
			tmp_path[pos] = '\0';

			Genode::Vfs::Vfs_handle *tmp_handle { nullptr };
			Genode::Vfs::Directory_service::Open_result tmp_open =
				vfs.open(tmp_path,
				         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
				         &tmp_handle, _heap);
			if (tmp_open ==
			    Genode::Vfs::Directory_service::OPEN_ERR_UNACCESSIBLE) {
				tmp_open = vfs.open(tmp_path,
				         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
				         | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
				         &tmp_handle, _heap);
			}
			if (tmp_open != Genode::Vfs::Directory_service::OPEN_OK) {
				Genode::warning(who, ": cannot open store tmp for write");
				return false;
			}
			Genode::Vfs::Vfs_handle::Guard tmp_guard(tmp_handle);

			tmp_handle->ftruncate(len);

			{
				Genode::size_t off { 0 };
				bool ok { true };
				while (off < len) {
					Genode::Vfs::Vfs_handle::Write_result w
						{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
					for (;;) {
						w = tmp_handle->write(
							Genode::Vfs::At { .pos = off },
							Genode::Const_byte_range_ptr(
								buf + off, len - off));
						if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
							break;
						_vfs->io().commit_and_wait();
					}
					Genode::size_t const n = w.convert<Genode::size_t>(
						[] (Genode::size_t bytes) { return bytes; },
						[] (Genode::Vfs::Vfs_handle::Write_error) {
							return Genode::size_t(0); });
					if (n == 0) {
						ok = false;
						break;
					}
					off += n;
				}

				while (tmp_handle->sync() == Genode::Vfs::Sync_result::RETRY)
					_vfs->io().commit_and_wait();

				if (!ok) {
					Genode::warning(who, ": store tmp write incomplete");
					return false;
				}
			}

			if (vfs.rename(tmp_path, path) !=
			    Genode::Vfs::Directory_service::RENAME_OK) {
				Genode::warning(who, ": store rename failed");
				return false;
			}

			Genode::log(who, ": persisted ", entry_name);
			return true;
		}
};

} /* namespace Sponge::Settings_demo */
