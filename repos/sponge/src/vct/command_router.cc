/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of CommandRouter.
 *
 * The dispatch table is intentionally a flat if/else chain on the parsed
 * subcommand string. With <10 commands expected at v1.0, a map or function
 * pointer table would add indirection without benefit. Reconsider when
 * the command count exceeds ~20.
 *
 * Two-word subcommands (e.g. "component list") are handled by first
 * matching args.subcommand and then inspecting args.positional. This keeps
 * the parser simple (no grammar or token rewrites) while letting each
 * command family grow cleanly.
 */

#include "command_router.h"
#include "commands.h"
#include "notifier_reporter.h"

#include <base/log.h>

#include <util/string.h>

using namespace Sponge;
using namespace Sponge::Vct;


int CommandRouter::dispatch(Args const &args)
{
	char const *const cmd = args.subcommand.string();

	/*
	 * The notifier is only attached when enable_notifications is on
	 * (default ON, opt-out via <config enable_notifications="no">).
	 * Disabling both silences the "notifier unavailable" warning path
	 * and the actual post (the latter is a no-op when the daemon is
	 * absent anyway, but the optional gate keeps the contract crisp).
	 */
	NotifierReporter *nf = args.enable_notifications ? _notifier : nullptr;

	if (Genode::strcmp(cmd, "status") == 0) {
		StatusCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "help") == 0) {
		HelpCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "version") == 0) {
		VersionCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "component") == 0) {
		char const *const pos = args.positional.string();
		if (Genode::strcmp(pos, "list") == 0) {
			ComponentListCommand c { _env };
			return c.execute(args);
		}
		Genode::warning("vct: unknown component subcommand '", pos,
		                "' — expected 'list'");
		HelpCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "install") == 0) {
		InstallCommand c { _env, nf };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "remove") == 0) {
		RemoveCommand c { _env, nf };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "launch") == 0) {
		LaunchCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "shutdown") == 0 || Genode::strcmp(cmd, "reboot") == 0) {
		PowerCommand c { _env, nf };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "search") == 0) {
		SearchCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "update") == 0) {
		UpdateCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "list") == 0) {
		ListCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "config") == 0) {
		ConfigCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "theme") == 0) {
		ThemeCommand c { _env };
		return c.execute(args);
	}
	if (Genode::strcmp(cmd, "leitzentrale") == 0) {
		LeitzentraleCommand c { _env };
		return c.execute(args);
	}

	Genode::warning("vct: unknown subcommand '", cmd,
	                "' — falling back to help");
	HelpCommand c { _env };
	return c.execute(args);
}
