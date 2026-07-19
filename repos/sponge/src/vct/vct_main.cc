/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of Vct::Main.
 */

#include "vct_main.h"
#include "args.h"

#include <base/log.h>

#include <sponge/version.h>

using namespace Sponge;
using namespace Sponge::Vct;


Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("vct (", Sponge::VERSION_STRING, " / ", Sponge::CODENAME, ") starting");
}


void Main::run()
{
	_config_rom.update();

	Args const args = parse_args(_config_rom.local_addr<char>(), _config_rom.size());

	if (args.verbose) {
		Genode::log("vct: subcommand='", args.subcommand,
		            "' positional='",   args.positional,
		            "' explain=",       args.explain,
		            " manual=",         args.manual,
		            " json=",           args.json);
	}

	_router.dispatch(args);
}
