#
# \brief  runtime_grouped — the P2.4 grouped-runtime wiring shared
#         include (simple/terminal/browser package runtimes).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# Phase 16 P2.4 splits the single `pkg_runtime` sandbox into THREE
# class runtimes — `pkg_runtime_simple`, `pkg_runtime_terminal`,
# `pkg_runtime_browser` — each a nested init whose config sponge_pkgd
# emits per <runtime_class> under its <grouped_runtimes/> opt-in
# (docs/12-package-format.md §4.5, repos/sponge/src/sponge_pkgd/README
# "Runtime config generation"). Every product-media scenario that uses
# the grouped topology sources this file for the wiring that would
# otherwise be five hand-maintained copies:
#
#   1. relay_policies        — the report_rom policies of the whole
#                              grouped machinery (class configs, class
#                              state reports, the debug feeds, and the
#                              sponge-de/cfg_probe consumers kept at
#                              their existing ROM names but pointed at
#                              the simple class);
#   2. pkgd_admission_nodes  — the sponge_pkgd <config> markers
#                              (<grouped_runtimes/> + the three
#                              explicit per-class admission gates);
#   3. pkgd_state_routes     — the sponge_pkgd ROM routes for the three
#                              per-class sandbox state reports;
#   4. falkon_rom_rules      — the Tier-0 `system`-child rom_pkg rules
#                              for the browser class (falkon's binary +
#                              WebEngine payload closure);
#   5. expand                — @PLACEHOLDER@ expansion (the
#                              driver_runtime.inc pattern, because the
#                              system.config block is a braced Tcl
#                              literal in which no substitution
#                              happens);
#   6. assert_wiring / assert_tier0 — the fail-loud structural
#                              assertions (three nodes, config/state
#                              policies, quota ordering).
#
# The three <start> node blocks themselves stay LITERAL in each
# scenario (docs/14 §4.4 "init config is data, not image"), with their
# grant literals drift-checked by runtime_quota::verify
# (run/runtime-quota.inc) and re-asserted inside assert_wiring.
#
# === THE NODE NAMES ARE A CONTRACT =======================================
#
# `pkg_runtime_simple` / `pkg_runtime_terminal` / `pkg_runtime_browser`
# are spelled with the `pkg_runtime` PREFIX on purpose:
#
#   - the layouter and the DE tasklist rules match
#     `label_prefix: pkg_runtime` and therefore keep covering every
#     package window without a per-class rule (run/sponge-desktop-
#     disk-*.run: `+ assign | label_prefix: pkg_runtime | target: ...`);
#   - the launch-marker chain for the terminal package becomes
#     `pkg_runtime_terminal -> terminal -> vfs` — the same shape as the
#     legacy `pkg_runtime -> terminal -> vfs`, one renamed element;
#   - report_rom's per-class policies are label-exact and cannot cross-
#     match (`label: pkg_runtime_simple -> config` vs the terminal and
#     browser ones).
#
# Do NOT rename these nodes without also rewriting those rules.
#
# === ROUTING MODEL (what the labels mean) ================================
#
# A request from a package child reaches the `system` sandbox's route
# with the accumulated label `pkg_runtime_<class> -> <pkg> -> <rom>`
# and the Tier-0 `system`-child route with
# `system -> pkg_runtime_<class> -> ...`. Route rules match `label:`
# / `label_prefix:` / `label_suffix:` against the label MINUS the
# requesting child's own name, and `label_last:` against the final
# element (genode/repos/os/src/lib/sandbox/utils.h
# service_node_matches). report_rom policies match the FULL label
# delivered to the server. Hence:
#
#   report_rom:  label: pkg_runtime_simple -> config
#                report: sponge_pkgd -> runtime_simple
#   system route: label_prefix: pkg_runtime_browser -> falkon
#                 + child rom_pkg
#
# === BROWSER LAST, EXPLICIT CAPS (binding) ==============================
#
# pkg_runtime_browser carries the RAM remainder clamp
# (run/runtime-quota.inc, docs/17 §6.1) and must therefore be the LAST
# `+ start` node of the system config; driver_runtime (explicit caps +
# RAM, run/driver_runtime.inc) precedes all three. Caps are explicit on
# every runtime (caps exhaustion hangs on seL4, docs/09 §11.1).
# assert_wiring checks the ordering.
#
# === PORT CHECKLIST (completed for all five product scenarios) ==========
#
# Per scenario, once run/runtime-grouped.inc and the updated
# run/runtime-quota.inc are sourced:
#
#   1. report_rom <config>: delete the `pkg_runtime -> config` +
#      `sponge_pkgd -> runtime_state` pair, the `pkg_runtime -> debug ->
#      {...}` trio and the `sponge-de -> runtime_state` /
#      `sponge-de -> runtime_mirror` / `cfg_probe -> config` consumer
#      lines; drop `    @GROUPED_RELAY_POLICIES@` in their place.
#   2. sponge_pkgd <config>: replace `    + admission` with
#      `    @GROUPED_ADMISSION_NODES@`.
#   3. sponge_pkgd <route>: replace the `label: runtime_state` line
#      with `    @GROUPED_PKGD_STATE_ROUTES@`.
#   4. Replace the single `+ start pkg_runtime` node with the three
#      literal class nodes (copy the UEFI-USB block; the grant literals
#      are class-row-derived and identical on every media) and call
#      runtime_quota::verify with the six literals.
#   5. Tier-0 `system` route: replace the two falkon rom_pkg rules with
#      `    [runtime_grouped::falkon_rom_rules]`.
#   6. Launch markers: `pkg_runtime -> terminal -> vfs` becomes
#      `pkg_runtime_terminal -> terminal -> vfs`.
#   7. Chain runtime_grouped::expand BEFORE driver_runtime::expand (the
#      latter's leftover-@ check catches anything both missed), and
#      call runtime_grouped::assert_wiring (plus assert_tier0 where the
#      falkon rules live).
#
# SPDX-License-Identifier: Apache-2.0
#

namespace eval runtime_grouped {

    #
    # The three runtime node names — the single source of truth for
    # every fragment below and for assert_wiring.
    #
    variable simple_node   pkg_runtime_simple
    variable terminal_node pkg_runtime_terminal
    variable browser_node  pkg_runtime_browser

    #
    # HID fragment A — the report_rom relay policies of the grouped
    # machinery. Replaces (in one place) the legacy
    # `pkg_runtime -> config` / `sponge_pkgd -> runtime_state` pair,
    # the `pkg_runtime -> debug -> {...}` feed trio, and the
    # sponge-de/cfg_probe consumer lines.
    #
    # Four groups, in order:
    #
    #   1. class config relays: pkgd's per-class runtime configs
    #      (`sponge_pkgd -> runtime_simple|_terminal|_browser`) become
    #      each runtime's own `config` ROM;
    #   2. class state relays: each runtime's sandbox `<state>` report
    #      becomes `sponge_pkgd -> runtime_*_state`, the budget its
    #      admission gate reads (never another class's pool);
    #   3. debug feeds: the SPONGE DEBUG package lives in the SIMPLE
    #      class (docs/17 §6) and reads `installed` / `window_list` /
    #      `launcher_result` through its own three relays;
    #   4. consumers kept at their existing ROM names (so sponge-de and
    #      cfg_probe are untouched component-side) but pointed at the
    #      simple class: `runtime_state`, `runtime_mirror`, `config`.
    #
    # NOTE: no leading indentation on the first line — the
    # @GROUPED_RELAY_POLICIES@ token sits at the indentation its
    # context needs (the driver_runtime.inc fragment convention).
    #
    proc relay_policies {} {
        variable simple_node
        variable terminal_node
        variable browser_node

        return "+ policy | label: ${simple_node} -> config       | report: sponge_pkgd -> runtime_simple
    + policy | label: ${terminal_node} -> config     | report: sponge_pkgd -> runtime_terminal
    + policy | label: ${browser_node} -> config      | report: sponge_pkgd -> runtime_browser
    + policy | label: sponge_pkgd -> runtime_simple_state   | report: ${simple_node} -> state
    + policy | label: sponge_pkgd -> runtime_terminal_state | report: ${terminal_node} -> state
    + policy | label: sponge_pkgd -> runtime_browser_state  | report: ${browser_node} -> state
    + policy | label: ${simple_node} -> debug -> installed      | report: sponge_pkgd -> installed
    + policy | label: ${simple_node} -> debug -> window_list    | report: wm -> window_list
    + policy | label: ${simple_node} -> debug -> launcher_result | report: sponge_pkgd -> launcher_result
    + policy | label: cfg_probe -> config          | report: sponge_pkgd -> runtime_simple
    + policy | label: sponge-de -> runtime_state | report: ${simple_node} -> state
    + policy | label: sponge-de -> runtime_mirror | report: sponge_pkgd -> runtime_simple"
    }

    #
    # HID fragment B — the sponge_pkgd <config> markers. Replaces the
    # legacy `    + admission` line.
    #
    # <grouped_runtimes/> switches pkgd's emission to the three class
    # targets; the three EXPLICIT admission nodes arm each class gate
    # (the bare <admission/> is only the simple-class alias — using it
    # alone would narrow product coverage to one class). The driver
    # gate (<driver_admission/>) stays a separate line in the
    # scenario's pkgd node: driver_runtime is the same sandbox in both
    # modes.
    #
    proc pkgd_admission_nodes {} {
        return "+ grouped_runtimes
    + simple_admission
    + terminal_admission
    + browser_admission"
    }

    #
    # HID fragment C — the sponge_pkgd <route> lines for the three
    # per-class sandbox state reports. Replaces the legacy
    # `+ service ROM | label: runtime_state | + child report_rom` line
    # (grouped mode never opens the legacy channel).
    #
    proc pkgd_state_routes {} {
        return "+ service ROM | label: runtime_simple_state   | + child report_rom
    + service ROM | label: runtime_terminal_state | + child report_rom
    + service ROM | label: runtime_browser_state  | + child report_rom"
    }

    #
    # HID fragment D — the Tier-0 `system`-child rom_pkg rules for the
    # BROWSER class (falkon's binary + its WebEngine payload closure).
    # Goes where the legacy `pkg_runtime -> bin/falkon` /
    # `label_prefix: pkg_runtime -> falkon` pair sat, inside the
    # subst'ed Tier-0 config (`    [runtime_grouped::falkon_rom_rules]`,
    # the driver_runtime::platform_policies insertion idiom).
    #
    # The `label: ... | + child rom_pkg | label: falkon` target rewrite
    # serves falkon's BINARY out of the payload root (rom_pkg chroots
    # /system/pkg/falkon/payload and resolves the label's last
    # element); the prefix rule steers every payload ROM (the WebEngine
    # libs exist ONLY there) BEFORE the generic .lib.so -> rom_lib rule.
    #
    proc falkon_rom_rules {} {
        variable browser_node
        return "+ service ROM | label: ${browser_node} -> bin/falkon | + child rom_pkg | label: falkon
    + service ROM | label_prefix: ${browser_node} -> falkon | + child rom_pkg"
    }

    #
    # Expand every @GROUPED_*@ placeholder and fail loudly if one of
    # the required ones survived. Callers chain this BEFORE
    # driver_runtime::expand, whose leftover-@PLACEHOLDER@ check then
    # catches anything both expands missed. Built with `list`, NOT with
    # braces (the driver_runtime.inc lesson): a braced literal would
    # leave the [procs] unevaluated.
    #
    proc expand {text {required ""}} {
        set map [list \
            @GROUPED_RELAY_POLICIES@     [relay_policies] \
            @GROUPED_ADMISSION_NODES@    [pkgd_admission_nodes] \
            @GROUPED_PKGD_STATE_ROUTES@  [pkgd_state_routes]]
        foreach {ph value} $map {
            if {[string first $ph $text] < 0} { continue }
            set text [string map [list $ph $value] $text]
        }
        foreach ph $required {
            if {[string first $ph $text] >= 0} {
                error "runtime_grouped::expand: placeholder ${ph} survived expansion"
            }
        }
        return $text
    }

    #
    # One-line-needle check used by the assertions below.
    #
    proc _require_line { text needle what } {
        if {[string first $needle $text] < 0} {
            error "runtime_grouped: structural assertion failed — ${what} \
                   is missing (expected '${needle}')"
        }
    }

    proc _forbid_line { text needle what } {
        if {[string first $needle $text] >= 0} {
            error "runtime_grouped: structural assertion failed — ${what} \
                   must be gone but '${needle}' is still present"
        }
    }

    #
    # runtime_grouped::assert_wiring <system_config_hid>
    #
    # The P2.4 structural assertions, against the staged system.config
    # HID (after runtime_grouped::expand, driver_runtime::expand and
    # supervisor::prepare have run, so the version stamps are in):
    #
    #   - the three runtime <start> nodes exist, each with EXACTLY the
    #     grant literals runtime-quota.inc derives (the literal text
    #     and the derivation cannot drift past both verify() and this);
    #   - the three class-config and three class-state report_rom
    #     policies exist (and the pkgd state routes);
    #   - the quota ordering holds: driver_runtime before the runtimes,
    #     simple < terminal < browser, and pkg_runtime_browser is the
    #     LAST top-level <start> (the RAM remainder-clamp invariant,
    #     docs/17 §6.1);
    #   - nothing of the legacy single-runtime wiring survived.
    #
    # Returns nothing on success; errors with the missing/forbidden
    # needle named otherwise.
    #
    proc assert_wiring { hid_text } {
        variable simple_node
        variable terminal_node
        variable browser_node

        if {![info exists ::runtime_quota::CLAMP_RAM]} {
            error "runtime_grouped::assert_wiring: run/runtime-quota.inc \
                   must be sourced first (the grant literals come from \
                   runtime_quota::class_caps/class_ram)"
        }

        #
        # 1. The three nodes, with the derived grant literals.
        #
        foreach {class node} [list \
                simple   $simple_node \
                terminal $terminal_node \
                browser  $browser_node] {
            set caps [runtime_quota::class_caps $class]
            set ram  [runtime_quota::class_ram  $class]
            _require_line $hid_text \
                "+ start ${node} | caps: ${caps} | ram: ${ram}" \
                "the ${class} runtime node (caps ${caps} / ram ${ram})"
        }

        #
        # 2. The config/state policies and the pkgd state routes: every
        #    line of the canonical fragments must be present.
        #
        foreach line [split [relay_policies] "\n"] {
            _require_line $hid_text [string trimleft $line] \
                "grouped relay policy '[string trimleft $line]'"
        }
        foreach line [split [pkgd_state_routes] "\n"] {
            _require_line $hid_text [string trimleft $line] \
                "pkgd state route '[string trimleft $line]'"
        }

        #
        # 3. pkgd's grouped-mode markers.
        #
        foreach marker [list "+ grouped_runtimes" "+ simple_admission" \
                             "+ terminal_admission" "+ browser_admission"] {
            _require_line $hid_text $marker "pkgd config marker '${marker}'"
        }

        #
        # 4. Quota ordering. Collect the top-level <start> nodes in
        #    order and assert the clamp invariant.
        #
        set order {}
        foreach line [split $hid_text "\n"] {
            if {[regexp {^\+ start ([A-Za-z0-9_-]+)(\s|$)} $line -> n]} {
                lappend order $n
            }
        }
        if {[lindex $order end] ne $browser_node} {
            error "runtime_grouped: structural assertion failed — the last \
                   top-level <start> is '[lindex $order end]', not \
                   '${browser_node}'; the RAM remainder clamp would be \
                   broken (docs/17 §6.1)"
        }
        foreach n [list driver_runtime $simple_node $terminal_node $browser_node] {
            if {[lsearch -exact $order $n] < 0} {
                error "runtime_grouped: structural assertion failed — \
                       top-level <start> '${n}' not found"
            }
        }
        set i_drv  [lsearch -exact $order driver_runtime]
        set i_s    [lsearch -exact $order $simple_node]
        set i_t    [lsearch -exact $order $terminal_node]
        set i_b    [lsearch -exact $order $browser_node]
        if {!($i_drv < $i_s && $i_s < $i_t && $i_t < $i_b)} {
            error "runtime_grouped: structural assertion failed — runtime \
                   node ordering must be driver_runtime < ${simple_node} < \
                   ${terminal_node} < ${browser_node} (got: ${order})"
        }

        #
        # 5. The legacy single-runtime wiring must be gone. The
        #    `label_prefix: pkg_runtime` layouter/DE rules are NOT
        #    legacy — they are the prefix contract the node names
        #    preserve — so only the exact old labels are forbidden.
        #
        foreach {needle what} [list \
                "label: pkg_runtime -> config" \
                    "the legacy pkg_runtime config relay" \
                "label: sponge_pkgd -> runtime_state" \
                    "the legacy pkgd runtime_state relay" \
                "label: pkg_runtime -> debug" \
                    "the legacy debug feed policies" \
                "+ start pkg_runtime " \
                    "the legacy single pkg_runtime node" \
                "label: pkg_runtime -> bin/falkon" \
                    "the legacy falkon binary rule" \
                "label_prefix: pkg_runtime -> falkon" \
                    "the legacy falkon payload rule"] {
            _forbid_line $hid_text $needle $what
        }

        puts "runtime_grouped: grouped runtime wiring verified \
               (3 nodes with derived grants, 3 config + 3 state relays, \
                quota ordering ${simple_node} < ${terminal_node} < \
                ${browser_node}-last)"
        return
    }

    #
    # runtime_grouped::assert_tier0 <tier0_config_text>
    #
    # The falkon rom_pkg rules live in the Tier-0 `system`-child route,
    # not in system.config — assert them where they are written. Also
    # asserts the ORDERING the payload serving depends on: the rules
    # must sit BEFORE the generic `label_suffix: .lib.so -> rom_lib`
    # rule, because the WebEngine closure exists only in the payload
    # root — a falkon .lib.so request that reached the generic rule
    # first would resolve against /system/lib and fail before rom_pkg
    # ever saw it.
    #
    proc assert_tier0 { config_text } {
        variable browser_node

        foreach line [split [falkon_rom_rules] "\n"] {
            _require_line $config_text [string trimleft $line] \
                "falkon rom_pkg rule '[string trimleft $line]'"
        }
        _forbid_line $config_text "label: pkg_runtime -> bin/falkon" \
            "the legacy falkon binary rule"
        _forbid_line $config_text "label_prefix: pkg_runtime -> falkon" \
            "the legacy falkon payload rule"

        set first_rule [string trimleft [lindex [split [falkon_rom_rules] "\n"] 0]]
        set rules_pos  [string first $first_rule $config_text]
        set libso_pos  [string first \
            "+ service ROM | label_suffix: .lib.so | + child rom_lib" $config_text]
        if {$libso_pos < 0} {
            error "runtime_grouped: structural assertion failed — the generic \
                   .lib.so -> rom_lib rule is missing from the Tier-0 config"
        }
        if {$rules_pos > $libso_pos} {
            error "runtime_grouped: structural assertion failed — the falkon \
                   rom_pkg rules must precede the .lib.so -> rom_lib rule \
                   (payload libs exist only in the payload root)"
        }

        puts "runtime_grouped: falkon rom_pkg routing verified \
               (${browser_node} scoped, before .lib.so resolution)"
        return
    }
}
