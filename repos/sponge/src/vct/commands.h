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

/*
 * config — get/set/list the flat dotted key-value store owned by
 * sponge_configd, over the Report/ROM channel (distinct labels so it
 * never collides with sponge_pkgd's request/result slots).
 *
 *   vct config <key>            get a value
 *   vct config <key> <value>    set a value
 *   vct config list             list every known key/value
 *
 * The verb is implied by the positionals (positional=key,
 * positional2=value), matching the locked arg model (args.h). A
 * successful set prints "Set <key> = <value>", which the run scenario
 * matches on. --json is honored per existing vct conventions.
 */
class ConfigCommand : public Command
{
	public:
		explicit ConfigCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "config"; }
		char const *summary() const override { return "Get or set a configuration value."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		int _render_get_human(Genode::Xml_node const &result);
		int _render_get_json(Genode::Xml_node const &result);
		int _render_set_human(Genode::Xml_node const &result);
		int _render_set_json(Genode::Xml_node const &result);
		int _render_list_human(Genode::Xml_node const &result);
		int _render_list_json(Genode::Xml_node const &result);
};

/*
 * theme — apply the active desktop theme.
 *
 *   vct theme apply <name>
 *
 * This is the convenient, intent-level entry point for the one-way theme
 * pipeline (vct -> sponge_configd -> sponge_themed -> sponge-de). It writes
 * theme.active=<name> into the configuration store through the SAME
 * Report/ROM channel as `vct config theme.active <name>` (it is the
 * automation-default path over the identical backend); `vct config` is the
 * always-open control door that reaches the same key directly.
 *
 * Arg mapping (locked args.h model): subcommand="theme",
 * positional="apply", positional2=<name>. So `vct theme apply light`.
 *
 * The theme is resolved by sponge_themed from a staged <name>.theme ROM
 * module; an unknown name keeps the previous theme (never fatal).
 */
class ThemeCommand : public Command
{
	public:
		explicit ThemeCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "theme"; }
		char const *summary() const override { return "Apply a desktop theme (vct theme apply <name>)."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		void _print_help(Args const &args);
};

/*
 * leitzentrale — toggle the Leitzentrale expert window visibility.
 *
 *   vct leitzentrale           enable (raise the window)
 *   vct leitzentrale off       disable (hide the window)
 *   vct leitzentrale --help
 *
 * The subsystem hosting sculpt_manager is always booted (Phase 6a); this
 * command only flips the visibility flag. It writes leitzentrale.enabled
 * into sponge_configd over the same Report/ROM channel as `vct config`
 * (the automation-default path; `vct config leitzentrale.enabled <v>` is
 * the always-open control door to the same key). A bridge component
 * regenerates the leitzentrale_enabled ROM from the configd broadcast so
 * the change persists after vct exits and gui_fader fades the window
 * in/out.
 *
 * The enable is logged (docs/07 §4.3 audit trail).
 */
class LeitzentraleCommand : public Command
{
	public:
		explicit LeitzentraleCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "leitzentrale"; }
		char const *summary() const override { return "Toggle the Leitzentrale expert window."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		void _print_help(Args const &args);
		int  _run_sync_op(char const *verb, bool diff, bool keep,
		                  bool revert, Args const &args);
	};

/*
 * launch — transitions an installed-but-stopped package to running via
 * sponge_pkgd (Phase 7, docs/06-vct.md §4.2 + docs/12 §9.2.1). Shares
 * the SAME Report/ROM channel and the SAME pkgd backend as the Sponge
 * DE launcher menu click (AGENTS.md §3.3 rule 5). No --manual mode
 * (launch is a single-step op; no stop exists in Alpha). The result
 * status is one of "ok" / "not-installed" / "already-running"; the two
 * non-ok outcomes exit non-zero with a clear message. An audit line is
 * printed before the request (control philosophy, docs/06 §4.2).
 */
class LaunchCommand : public Command
{
	public:
		explicit LaunchCommand(Genode::Env &env) : _env(env) {}
		char const *name()    const override { return "launch"; }
		char const *summary() const override { return "Start an installed package."; }
		int execute(Args const &args) override;
	private:
		Genode::Env &_env;

		int _render_human(Genode::Xml_node const &result);
		int _render_json(Genode::Xml_node const &result);
};

}  /* namespace Sponge::Vct */
