/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Command abstraction for vct subcommands.
 *
 * Every vct subcommand (install, status, config, leitzentrale, ...) implements
 * the Command interface. The router constructs one instance per dispatch and
 * passes the parsed Args; the command runs and returns an exit code.
 *
 * Commands are intentionally tiny: they validate input, call into backend
 * services (sponge_pkgd, sponge_configd, ...) via RPC, and format output.
 * No business logic lives here.
 */

#pragma once

#include <base/env.h>

#include "args.h"

namespace Sponge::Vct {

class Command
{
	public:

		virtual ~Command() = default;

		virtual char const *name() const = 0;

		virtual char const *summary() const = 0;

		/* Execute the command against `args`. Returns process-style exit
		 * code (0 = success, non-zero = failure). */
		virtual int execute(Args const &args) = 0;
};

}  /* namespace Sponge::Vct */
