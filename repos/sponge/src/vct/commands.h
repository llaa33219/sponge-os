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

/*
 * install — resolves a package via sponge_pkgd (Report/ROM channel).
 * --explain previews the plan; a plain `vct install <pkg>` executes it
 * (sponge_pkgd regenerates the pkg_runtime config). --manual executes
 * the same install but renders each step individually (no interactive
 * [Y/n] is possible yet — Genode has no terminal input channel — so
 * each step is printed and the install proceeds).
 */
class InstallCommand : public Command
{
	public:
		explicit InstallCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "install"; }
		char const *summary() const override { return "Install a Sponge OS package."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		int _render_explain_human(Genode::Xml_node const &result);
		int _render_explain_json(Genode::Xml_node const &result);
		int _render_install_human(Genode::Xml_node const &result);
		int _render_install_json(Genode::Xml_node const &result);
		void _render_manual_plan(Genode::Xml_node const &explain_result);
		void _render_manual_done(Genode::Xml_node const &install_result);
};

/*
 * remove — drops an installed package (and its now-unused dependencies)
 * via sponge_pkgd, which regenerates the pkg_runtime config so the
 * nested init abandons the child.
 */
class RemoveCommand : public Command
{
	public:
		explicit RemoveCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "remove"; }
		char const *summary() const override { return "Remove an installed Sponge OS package."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		int _render_human(Genode::Xml_node const &result);
		int _render_json(Genode::Xml_node const &result);
};

/*
 * list — prints the installed package set (the exact view of what
 * sponge_pkgd will regenerate into pkg_runtime). Empty set prints a
 * clean "no packages installed" message.
 */
class ListCommand : public Command
{
	public:
		explicit ListCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "list"; }
		char const *summary() const override { return "List installed Sponge OS packages."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		int _render_human(Genode::Xml_node const &result);
		int _render_json(Genode::Xml_node const &result);
};

}  /* namespace Sponge::Vct */
