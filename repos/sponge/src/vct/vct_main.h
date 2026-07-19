/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Top-level coordinator for the vct component.
 *
 * Vct::Main is constructed once when the component boots and owns the
 * command router, the Genode environment reference, the config ROM that
 * carries this invocation's arguments, and the entry point for backend
 * RPCs (added in later phases).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>

#include "command_router.h"

namespace Sponge::Vct {

class Main
{
	public:

		explicit Main(Genode::Env &env);

		/* Parse the config ROM into Args and dispatch exactly once.
		 * vct is short-lived by design. */
		void run();

	private:

		Genode::Env                   &_env;
		Genode::Heap                   _heap       { &_env.ram(), &_env.rm() };
		/* "config" ROM supplied by init; carries <args><arg>...</arg></args>. */
		Genode::Attached_rom_dataspace _config_rom { _env, "config" };
		CommandRouter                  _router     { _env };
};

}  /* namespace Sponge::Vct */
