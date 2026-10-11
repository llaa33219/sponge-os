/* SPDX-License-Identifier: Apache-2.0
 *
 * Optional_rom_dataspace — a ROM session whose ABSENCE is survivable.
 *
 * Genode's Attached_rom_dataspace cannot be used for an optional ROM:
 * Env::session() answers a denied session with
 *
 *   Error: stop because parent denied <SERVICE>-session: label="..."
 *
 * followed by sleep_forever() (base/src/lib/base/component.cc). The
 * component is stopped — the failure is NOT an exception, so a
 * try/catch around the constructor cannot make it optional. Optional
 * services must be acquired through Env::try_session(), which returns
 * a Session_result instead of stopping the component; that is exactly
 * what upstream's event_filter does for its optional filter ROMs
 * (repos/os/src/server/event_filter/main.cc).
 *
 * This class is the read-only counterpart of Attached_rom_dataspace
 * (same update() / valid() / local_addr() / size() / xml() surface, so
 * callers stay unchanged) on top of a non-fatal session request. Used
 * for ROMs a scenario MAY route — the component must work in the
 * topologies that do not (AGENTS.md §1.1: the deployment must never be
 * the only thing that keeps the component alive).
 */

#pragma once

#include <base/attached_dataspace.h>
#include <base/env.h>
#include <base/local.h>
#include <rom_session/client.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

namespace Sponge::Sponge_DE {

class Optional_rom_dataspace
{
	private:

		using Local_rm = Genode::Env::Local_rm;

		Genode::Env &_env;
		Local_rm     &_rm;

		/*
		 * The session request needs an id from the component-local
		 * parent session id space; the element frees the id in the
		 * destructor, and its id is what Env::close() takes (same
		 * bookkeeping as base's Connection_base).
		 */
		Genode::Parent::Client _parent_client { };
		Genode::Id_space<Genode::Parent::Client>::Element const _id_space_element;

		Genode::Constructible<Genode::Rom_session_client> _rom { };
		Genode::Constructible<Genode::Attached_dataspace> _ds  { };

		void _try_attach()
		{
			/*
			 * Same order as base's Attached_rom_dataspace: drop the
			 * old mapping before asking the server for a new
			 * dataspace, or the detach of a server-destroyed
			 * dataspace produces error messages at core.
			 */
			_ds.destruct();
			if (!_rom.constructed())
				return;

			try { _ds.construct(_rm, _rom->dataspace()); }
			catch (Genode::Attached_dataspace::Invalid_dataspace) { }
		}

	public:

		explicit Optional_rom_dataspace(Genode::Env &env)
		:
			_env(env),
			_rm(env.rm()),
			_id_space_element(_parent_client, _env.id_space())
		{ }

		~Optional_rom_dataspace()
		{
			if (_rom.constructed())
				_env.close(_id_space_element.id());
		}

		/*
		 * Request the ROM session for 'label'. Returns true when the
		 * session opened AND its content is attached — i.e. when the
		 * caller may treat this object like an Attached_rom_dataspace.
		 *
		 * False means the parent denied the session (no route in the
		 * scenario) or the dataspace could not be attached; the object
		 * stays valid() == false and the caller runs without the ROM.
		 * Calling open() again retries (the id is reusable after a
		 * denied request — no session was created).
		 *
		 * GUI thread ONLY.
		 */
		bool open(char const *label);

		/* Content present (session opened and dataspace attached). */
		bool valid() const { return _ds.constructed(); }

		/* Re-read the ROM content; re-attaches when the module changed. */
		void update();

		Genode::size_t size() const { return _ds.constructed() ? _ds->size() : 0; }

		template <typename T> T *local_addr() {
			return _ds->local_addr<T>(); }

		template <typename T> T const *local_addr() const {
			return _ds->local_addr<T const>(); }

		/* Always returns a valid node; "<empty/>" when unusable. */
		Genode::Xml_node xml() const
		{
			try {
				if (valid() && local_addr<void const>())
					return Genode::Xml_node(local_addr<char>(), size());
			} catch (Genode::Xml_node::Invalid_syntax) { }

			return Genode::Xml_node("<empty/>");
		}
};

}  /* namespace Sponge::Sponge_DE */