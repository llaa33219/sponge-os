/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Concrete vct subcommands. Phase 0 ships three placeholders that all
 * do the minimum useful thing: emit their banner via Genode::log, and
 * warn that full functionality is pending.
 *
 * Each command holds only a reference to Genode::Env; real backend
 * connections (sponge_pkgd, sponge_configd) land in Phase 4+.
 *
 * Real backends are added in Phase 4+ (see docs/09-roadmap.md).
 */

#pragma once

#include <base/component.h>

#include "command.h"

namespace Sponge::Vct {

class HelpCommand : public Command
{
	public:
		explicit HelpCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "help"; }
		char const *summary() const override { return "Show vct command help."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;
};

class VersionCommand : public Command
{
	public:
		explicit VersionCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "version"; }
		char const *summary() const override { return "Print vct and Sponge OS version."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;
};

class StatusCommand : public Command
{
	public:
		explicit StatusCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "status"; }
		char const *summary() const override { return "Show a summary of system status."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;
};

class ComponentListCommand : public Command
{
	public:
		explicit ComponentListCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "component list"; }
		char const *summary() const override { return "List the running components."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;
};

}  /* namespace Sponge::Vct */
