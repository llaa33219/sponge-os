/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Command router for vct.
 *
 * Maps the parsed Args.subcommand string to a concrete Command instance,
 * constructs the command, and runs it. The router is stateless across
 * invocations: it exists only to keep the dispatch table in one place.
 *
 * Unknown subcommands are reported via Genode::warning and fall through
 * to HelpCommand so the user gets a hint, not a silent failure.
 */

#pragma once

#include <base/component.h>

#include "args.h"
#include "command.h"

namespace Sponge::Vct {

class NotifierReporter;

class CommandRouter
{
	public:

		explicit CommandRouter(Genode::Env &env) : _env(env) {}

		/* Wire the notifier reporter for completion notifications. */
		void attach_notifier(NotifierReporter *reporter) { _notifier = reporter; }

		/* Look up args.subcommand, construct the matching Command, and
		 * execute it with `args`. Returns the command's exit code. */
		int dispatch(Args const &args);

	private:

		Genode::Env &_env;
		NotifierReporter *_notifier { nullptr };
};

}  /* namespace Sponge::Vct */
