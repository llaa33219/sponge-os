/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkg_config — pure runtime-config generation for Sponge OS
 * package sandboxes.
 *
 * This library renders the sandbox <config> that sponge_pkgd publishes
 * for each named runtime target. It was extracted verbatim from
 * sponge_pkgd's _generate_runtime_config/_emit_runtime_config
 * (Phase 16 P1.2, docs/plans/phase16-runtime-overhaul.md) so the
 * emission templating becomes testable and reusable without touching
 * pkgd's I/O. The pre-extraction behavior is preserved byte for byte
 * for identical package state.
 *
 * PURITY CONTRACT (hard):
 *
 *   No Genode::Env, no Report/ROM sessions, no File_system, no
 *   hardware access, no exceptions, no mutable global state. The only
 *   Genode headers used are the pure <util/string.h> and
 *   <util/xml_generator.h>. The caller owns every output buffer.
 *
 * NAMED RUNTIME TARGETS:
 *
 *   One call of 'emit_target' renders exactly one target's config.
 *   There is no two-report assumption anywhere in the library: the
 *   app and driver sandboxes of today are just two Target values
 *   (the 'app_target'/'driver_target' presets), and the app runtime
 *   classes add three more (the 'simple_target'/'terminal_target'/
 *   'browser_target' presets) as further Target values and additional
 *   emission calls without any change to the emission algorithm.
 *   Membership is a pure string match between
 *   'Package_view::runtime_class' and 'Target::runtime_class'.
 *
 * DETERMINISM CONTRACT (unchanged from the pre-extraction code):
 *
 *   The output for a given package state is byte-identical every run:
 *   <start> nodes are emitted in name-sorted order (selection sort on
 *   strcmp, so install order cannot perturb an already-running child's
 *   config), every attribute is emitted in a fixed order, and there
 *   are no volatile fields. init diffs the new config against the live
 *   tree, so unchanged children keep running across an unrelated
 *   install.
 *
 * BUFFER BOUNDS (unchanged):
 *
 *   - the sort index holds at most MAX_PACKAGES entries;
 *   - the <parent-provides> union holds at most MAX_PROVIDES names,
 *     built in package-array order (fixed target services first, then
 *     the declared session names of this target's packages,
 *     de-duplicated);
 *   - per-package sessions are read up to MAX_SESSIONS.
 *   Callers exceeding these bounds with their counts get the extra
 *   entries ignored (the pre-extraction code could not exceed them).
 *
 * VIEW CONTRACT:
 *
 *   All strings are non-null, null-terminated, and already at the
 *   width sponge_pkgd's metadata parser produces (session names at
 *   most 31 characters — Genode::String<32> — everything else at most
 *   63). The pre-extraction code stored session names through
 *   Genode::String<32> at emission time; staying within the parse
 *   widths keeps the emitted bytes identical.
 *
 * SESSION ROUTING RULES (docs/12-package-format.md §7.2, kept with the
 * templating that implements them):
 *
 *   - 'is_parent_route': a declared default-route routes via <parent/>
 *     when it is the literal "parent", empty, OR names an outer-system
 *     service (nitpicker/vfs/event_filter/...) that lives outside the
 *     package sandbox — never as a <child name="nitpicker"/> (rule 1).
 *   - 'session_label': materialize the label attribute per rule 2
 *     (explicit label wins; read-only File_system gets "<pkg>-ro").
 */

#pragma once

#include <util/string.h>
#include <util/xml_generator.h>

namespace Sponge::Pkg_config {

/*
 * Bounds mirrored from sponge_pkgd's parse-time capacities. See the
 * "BUFFER BOUNDS" section above.
 */
enum {
	MAX_PACKAGES = 32,   /* installed-set / sort-index bound   */
	MAX_SESSIONS = 16,   /* sessions per package (parse bound) */
	MAX_PROVIDES = 64    /* <parent-provides> union bound      */
};

/*
 * One session route as emission sees it. Field widths follow the
 * metadata parse types (sponge_pkgd's Package::Session).
 */
struct Session_view
{
	char const *name     { "" };   /* service name, e.g. "Gui"        */
	char const *route    { "" };   /* default-route target, see above */
	bool        readonly { false }; /* File_system only               */
	char const *label    { "" };   /* explicit label hint, or ""      */
};

/*
 * One package as emission sees it: a pure value description with no
 * behavior and no I/O. The caller (sponge_pkgd) fills these from its
 * parsed Package state.
 */
struct Package_view
{
	char const *name          { "" };
	char const *binary        { "" };  /* defaults to name at parse   */
	char const *ram           { "" };  /* quantum, e.g. "48M"         */
	unsigned    caps          { 0 };
	char const *config_xml    { "" };  /* complete <config>...</config>
	                                    * block, emitted verbatim, or
	                                    * "" when the metadata carries
	                                    * none                       */

	/*
	 * Emission-class token, matched against 'Target::runtime_class'.
	 * Today sponge_pkgd derives it from the metadata <kind>
	 * ("driver" for <kind>driver</kind>, "app" for everything else —
	 * exactly the pre-extraction is_driver() split). The runtime-class
	 * targets consume the committed metadata spelling
	 * <runtime_class>simple|terminal|browser</runtime_class>
	 * (docs/12-package-format.md §4.5, default simple); the class
	 * grouping in sponge_pkgd is a later unit.
	 */
	char const *runtime_class { "app" };

	/*
	 * Phase 7 lifecycle gate (docs/12-package-format.md §9.2.1):
	 * <start> is emitted only for running packages. Installed-but-
	 * stopped packages are deliberately absent from the sandbox
	 * config, so init never constructs them until a launch adds them.
	 * Their session names still count for <parent-provides>.
	 */
	bool        running       { false };

	Session_view sessions[MAX_SESSIONS] { };
	unsigned     num_sessions { 0 };
};

/* Fixed <parent-provides> set of one target, in emission order. */
struct Service_list
{
	char const *const *names { nullptr };
	unsigned           count { 0 };
};

/*
 * One named runtime target: one generated sandbox config.
 */
struct Target
{
	/* Diagnostic name ("app", "driver", ...). Never emitted. */
	char const  *name          { "" };

	/* Membership token matched against Package_view::runtime_class. */
	char const  *runtime_class { "" };

	/* Fixed services always present in <parent-provides>. */
	Service_list base_services { };
};

/*
 * Per-emission options. Both fields map onto what the pre-extraction
 * code read from pkgd members; see 'emit_target'.
 */
struct Emit_options
{
	/*
	 * Disk-served binary path prefix (docs/14 §4.4/§4.6). Empty in
	 * the boot-module model, where the binary name is the bare module
	 * name. See 'emit_target' for the <binary> rule.
	 */
	char const *binary_prefix { "" };

	/*
	 * Test-only (repro harness) padding budget: the emission gains
	 * pad_bytes/64 <p v="...."/> nodes so a write overflows the
	 * caller's report buffer on purpose. 0 in production. The
	 * pre-extraction code padded the app config only; sponge_pkgd
	 * preserves that by passing 0 for the driver emission.
	 */
	unsigned    pad_bytes     { 0 };
};

/*
 * Render one target's sandbox <config> content into 'g', which is the
 * bounded sink the caller owns (typically the Xml_generator that
 * Expanding_reporter::generate_xml hands out over its report buffer).
 *
 * 'packages' is the full installed set in the caller's array order —
 * the <parent-provides> union is built in that order. The <start>
 * nodes are emitted in name-sorted order (determinism contract).
 *
 * Emitted structure, in this exact order:
 *
 *   <report delay_ms="2000" init_ram="yes" init_caps="yes"
 *           child_ram="yes"/>          — sandbox state reporting
 *   <repro_padding>...</repro_padding> — only when pad_bytes != 0
 *   <parent-provides>...</parent-provides>
 *   <default-route><any-service><parent/><any-child/></any-service>
 *   </default-route>
 *   <default caps="1000"/>
 *   <start ...>...</start>             — running, selected packages
 *
 * <binary> is emitted only when the effective binary path
 * ('options.binary_prefix' + package binary) differs from the package
 * name. The inline 'config_xml' block is appended verbatim (raw,
 * non-sanitized) between <resource> and <route>.
 */
void emit_target(Genode::Xml_generator &g,
                 Target const &target,
                 Package_view const *packages,
                 unsigned num_packages,
                 Emit_options const &options);

/*
 * Convenience wrapper for tests and host tooling: render a complete
 * "<config>...</config>" document (the report node type sponge_pkgd
 * uses) into a caller-owned buffer. Returns the number of bytes used
 * (including the terminating null), or 0 when the buffer is too small.
 */
Genode::size_t emit_config(char *dst, Genode::size_t dst_len,
                           Target const &target,
                           Package_view const *packages,
                           unsigned num_packages,
                           Emit_options const &options);

/* Session-routing rules; see the header comment. */
bool is_parent_route(char const *route);
Genode::String<96> session_label(char const *pkg_name,
                                 Session_view const &session);

/*
 * Preset targets for the two sandboxes sponge_pkgd emits today. Their
 * base-services lists ARE the capability boundary (AGENTS.md §1.2):
 * 'app_target' carries no hardware service at all, so an application
 * package cannot obtain Platform/Irq/Io_mem/Io_port even by naming
 * them; 'driver_target' grants only the bare core services plus what
 * the installed driver packages declared.
 */
Target app_target();
Target driver_target();

/*
 * Preset targets for the three app runtime classes
 * (docs/12-package-format.md §4.5, <runtime_class>simple|terminal|
 * browser</runtime_class>, default simple). Each carries the SAME
 * APP_BASE_SERVICES capability set as 'app_target' — no hardware
 * service in any of them, so the driver-only capabilities
 * (Platform/Dma/Irq/Io_mem/Io_port) stay out of every app target. A
 * package is emitted into exactly one of them via the runtime_class
 * string match; 'emit_target' itself is unchanged.
 */
Target simple_target();
Target terminal_target();
Target browser_target();

}  /* namespace Sponge::Pkg_config */
