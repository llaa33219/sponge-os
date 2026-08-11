/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * vct argument model and parser.
 *
 * DESIGN (locked, see docs/06-vct.md):
 *
 *   vct does not receive a Unix-style argv. Instead, `init` supplies the
 *   invocation arguments as a config ROM. Genode's runtime delivers this
 *   ROM in HID (human-intelligible data) format, e.g.:
 *
 *     config
 *     + arg status
 *     -
 *
 *   For test fixtures and tooling, XML is also accepted:
 *
 *     <config>
 *       <arg name="status"/>
 *     </config>
 *
 *   A future Sponge OS shell (or `vct run` wrapper) builds this config on
 *   the user's behalf when they type `vct status`. vct itself is agnostic to
 *   how the config arrived: it only knows the parsed Args.
 *
 *   This is the standard Genode component-config pattern. It keeps the
 *   capability model intact (no hidden channel), and it makes every
 *   invocation inspectable by Leitzentrale and `init`'s config dump.
 *
 * USAGE:
 *
 *   Args args = Sponge::Vct::parse_args(data, size);
 *   if (args.subcommand == "install") { ... }
 *
 * EXTENDING:
 *
 *   To add a new flag, extend the `Args` struct and add a branch to
 *   `parse_token` in args.cc. To add a new positional, extend the struct
 *   and decide on its index in the parser.
 */

#pragma once

#include <util/string.h>

namespace Sponge::Vct {

/* Maximum number of <arg> tokens accepted from the config ROM.
 * Extra tokens are reported and dropped (not silently ignored). */
constexpr unsigned MAX_ARGS = 32;

struct Args
{
	/* Recognized: status, help, version, install, remove, list, config,
	 * component, leitzentrale. Defaults to "status" when no positional
	 * <arg> is supplied. */
	Genode::String<32>  subcommand  { "status" };

	/* First positional (package name for install, config key, ...). */
	Genode::String<128> positional  { "" };

	/* Second positional (e.g. config value for "config set"). */
	Genode::String<128> positional2 { "" };

	bool explain { false };   /* --explain : dry-run, no execution   */
	bool manual  { false };   /* --manual  : step-by-step prompt     */
	bool json    { false };   /* --json    : machine-readable output */
	bool verbose { false };   /* --verbose : debug-level logging     */

	Genode::String<8> lang { "en" };

	/* Non-zero means the user's invocation was truncated. */
	unsigned dropped { 0 };

	/* Phase 14 W4: notifications on install/remove/shutdown/reboot.
	 * Default ON; opt out via <config enable_notifications="no">. */
	bool enable_notifications { true };
};

/* Parse the config ROM bytes (HID or XML) into Args. */
Args parse_args(char const *data, Genode::size_t size);

}  /* namespace Sponge::Vct */
