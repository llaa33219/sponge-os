#
# \brief  runtime_quota — the pkg_runtime grant vocabulary (Phase 16
#         P2.2 single grant + P2.4 grouped per-class grants).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# The grant derivation and the drift check for the package-runtime
# <start> nodes (docs/17-runtime-component-topology.md §6). It emits no
# config, injects no start node, and touches no route: the scenario
# keeps its grant literals in the config text and calls verify() so a
# class row that moved cannot silently outrun the literal.
#
# Two call shapes, dispatched on the argument count:
#
#   runtime_quota::verify 230000 512G
#       Legacy single-grant check (Phase 16 P2.2, unchanged): the one
#       `+ start pkg_runtime | caps: 230000 | ram: 512G` node of the
#       unported scenarios. The four sponge-desktop-disk* scenarios
#       still use this form until their P2.4 port.
#
#   runtime_quota::verify 6000 1488M 2000 247M 220000 512G
#       P2.4 grouped check: the three class-runtime grants in
#       simple/terminal/browser order — literal pairs straight out of
#       the scenario's `+ start pkg_runtime_simple|_terminal|_browser`
#       nodes.
#
# === THE P2.4 PER-CLASS DERIVATION =======================================
#
# The class rows (each owned by its per-class include):
#
#   runtime_simple    4512 caps / 1288M   hello 256 · textedit 1000 ·
#                                         files 1000 · calculator 1000 ·
#                                         pdf_view 256 · debug 1000
#   runtime_terminal  1100 caps /  224M   terminal
#   runtime_browser 200000 caps / 1024M   falkon
#
# Editor closure (the nano 600 + ncurses 300 / 48M + 16M pair
# installable into the SIMPLE class — nano has no <runtime_class>, so
# it defaults to simple): counted in the simple class only.
#
# Grant = (class row + closure) x 1.10, rounded up — caps to the next
# 1,000, RAM to the next MiB. That yields:
#
#   simple    (4512 + 900) x 1.1 = 5953.2  ->  6000 caps
#             (1288M + 64M) x 1.1 = 1487.2 ->  1488M
#   terminal   1100       x 1.1 = 1210     ->  2000 caps
#              224M       x 1.1 = 246.4    ->   247M
#   browser   200000      x 1.1 = 220000   -> 220000 caps
#             RAM = the remainder clamp (512G), never a sum.
#
# The system-level closure confirms the family: the app-side children
# sum becomes 24300 (fixed) + 6000 + 2000 + 220000 = 254500 caps
# against the documented 280000 base grant — +10.0% headroom, exactly
# the docs/17 §5 practice the P2.2 single grant followed.
#
# === WHY NOT THE RAW CLASS ROWS AS GRANTS ================================
#
# The raw rows (4512/1288M, 1100/224M, 200000) look like the obvious
# per-class grants and were the first proposal. They are wrong, and the
# arithmetic is worth keeping here because the next port will trip over
# it too:
#
#   1. sponge_pkgd's admission gate admits a package only when the
#      class sandbox's reported budget covers need + margin
#      (repos/sponge/lib/src/sponge_pkg_admission/pkg_admission.cc:88-95;
#      ADMISSION_MARGIN_RAM 8 MiB, ADMISSION_MARGIN_CAPS 64). The
#      reported `avail` is the runtime PD's quota MINUS its own live
#      usage (sandbox/types.h Ram_info::from_pd_stats -> stats.avail()),
#      and a measured idle nested init already spends ~70 caps / ~1.1M
#      (docs/evidence/pkg-admission-control.md: a 3200-cap grant
#      reports avail caps=3130 at idle).
#   2. So a single-package class granted exactly its row can NEVER
#      admit its own package: terminal needs 1100 + 64 caps out of an
#      avail of 1100 - 70. Even the +10% headroom alone (1210) leaves
#      only 1140 available, 24 short of need+margin — the round-up to the next
#      1,000 is what makes the policy fit a one-package row.
#   3. The same holds for RAM (need + 8 MiB against a 224M row), and
#      at spawn time an exact-sum grant clamps the last child
#      (sandbox/child.cc:481-486) — the cliff edge docs/17 §5's
#      headroom practice exists to avoid.
#
# The P2.4 UEFI-USB scenario enforces the "terminal request" and
# "terminal package boots" launch gates; with row-exact grants the
# launch is refused by admission before the package ever starts. The
# derived grants above are what the launch gates were verified against.
#
# === CAPS STAY EXPLICIT; ONLY BROWSER RAM IS A CLAMP =====================
#
# All three caps grants are explicit numbers: caps exhaustion on seL4
# hangs (docs/09-roadmap.md §11.1), and a caps clamp would starve any
# future sibling. RAM is explicit for simple/terminal (a shortfall is a
# visible init warning and a refused launch, never a hang) and the
# deliberate remainder clamp (docs/17 §6.1,
# genode/repos/os/src/lib/sandbox/child.cc:481-486) for browser alone.
# Binding config-shape conditions (not enforced here):
#
#   1. pkg_runtime_browser is the LAST `+ start` node of its config —
#      nothing may be created after it, or the clamp silently starves;
#   2. driver_runtime (explicit caps + RAM) precedes every clamped
#      node (run/driver_runtime.inc's ordering invariant).
#
# runtime_grouped::assert_wiring (run/runtime-grouped.inc) checks both
# against the staged config.
#
# === USAGE ===============================================================
#
#   source .../runtime-simple.inc
#   source .../runtime-terminal.inc
#   source .../runtime-browser.inc
#   source .../runtime-quota.inc
#
#   # grouped (P2.4):
#   runtime_quota::verify 6000 1488M 2000 247M 220000 512G
#
#   # legacy single grant (the four unported scenarios):
#   runtime_quota::verify 230000 512G
#
# The class includes must be sourced first: every derivation reads
# their QUOTA_CAPS/QUOTA_RAM rows live, so a row edit moves the
# derivation and verify() fails against a stale literal. That is the
# whole point.
#

namespace eval runtime_quota {

    #
    # The remainder-clamp RAM quantum (docs/17 §6.1). A string, not a
    # number: `512G` is an init quota token, exactly as the config
    # spells it. Used by BOTH forms: legacy pkg_runtime and the P2.4
    # pkg_runtime_browser are clamp holders.
    #
    variable CLAMP_RAM 512G

    #
    # Editor closure reachable from the editor packages (docs/17 §6):
    # pkg/nano <quota ram="48M" caps="600"/> + pkg/ncurses
    # <quota ram="16M" caps="300"/>. nano has no <runtime_class> and
    # therefore lands in the SIMPLE class (docs/12 §4.5 default), so
    # the closure is charged to that class's grant in the grouped form
    # and to the merged sum in the legacy form. RAM is part of the same
    # closure — the two packages are the same two packages.
    #
    variable EDITOR_CLOSURE_CAPS 900
    variable EDITOR_CLOSURE_RAM  64M

    #
    # Documented headroom/round-up policy (docs/17 §5 practice):
    # +10% on the base sum. The round-up unit is per magnitude — the
    # legacy single grant (~230k caps) rounds up to the next 10,000,
    # a per-class grant (1k..220k caps) to the next 1,000, RAM to the
    # next MiB. The per-class round-up is ALSO what carries the
    # admission margin on a one-package class row (see the header).
    #
    variable HEADROOM_NUM 11
    variable HEADROOM_DEN 10
    variable ROUND_UP_TO  10000
    variable CLASS_CAPS_ROUND_UP 1000
    variable CLASS_RAM_ROUND_UP  1

    #
    # Parse an init RAM quota token ("1288M", "64M", "512G") into MiB.
    # Only whole-MiB rows are shipped; a fractional row is an error
    # rather than a silent truncation.
    #
    proc _ram_mib { tok } {
        if {![regexp {^([0-9]+)([KMGkmg])$} $tok -> v unit]} {
            error "runtime_quota::_ram_mib: '${tok}' is not a RAM quota token"
        }
        switch -- [string toupper $unit] {
            K { if {$v % 1024} {
                    error "runtime_quota::_ram_mib: '${tok}' is not whole MiB"
                }
                return [expr {$v / 1024}] }
            M { return $v }
            G { return [expr {$v * 1024}] }
        }
    }

    #
    # One class row read live from its owning include. Fails loudly
    # when a class include is not loaded — a derivation that silently
    # fell back to a constant would defeat the drift check below.
    #
    proc _class_row { ns field } {
        if {![info exists ${ns}::QUOTA_CAPS]} {
            error "runtime_quota: ${ns}::QUOTA_CAPS not loaded — source \
                   ${ns}.inc (and its siblings) before runtime-quota.inc"
        }
        return [set ${ns}::${field}]
    }

    #
    # The three class rows, summed live from their owning includes.
    #
    proc class_caps_sum { } {
        set sum 0
        foreach ns {runtime_simple runtime_terminal runtime_browser} {
            incr sum [_class_row $ns QUOTA_CAPS]
        }
        return $sum
    }

    #
    # runtime_quota::class_caps <simple|terminal|browser> -> integer
    #
    # The P2.4 per-class caps grant: (class row + closure) x 1.10,
    # rounded up to the next 1,000. The closure is charged to simple
    # only (the editor packages default to the simple class).
    #
    proc class_caps { class } {
        variable EDITOR_CLOSURE_CAPS
        variable HEADROOM_NUM
        variable HEADROOM_DEN
        variable CLASS_CAPS_ROUND_UP

        array set ns { simple runtime_simple terminal runtime_terminal
                       browser runtime_browser }
        if {![info exists ns($class)]} {
            error "runtime_quota::class_caps: unknown class '${class}'"
        }

        set base [_class_row $ns($class) QUOTA_CAPS]
        if {$class eq "simple"} { incr base $EDITOR_CLOSURE_CAPS }

        # ceil at the growth step: integer division would truncate the
        # .x of x1.1 and undershoot the policy on a tight row.
        set grown [expr {($base * $HEADROOM_NUM + $HEADROOM_DEN - 1)
                         / $HEADROOM_DEN}]
        return [expr {($grown + $CLASS_CAPS_ROUND_UP - 1)
                      / $CLASS_CAPS_ROUND_UP * $CLASS_CAPS_ROUND_UP}]
    }

    #
    # runtime_quota::class_ram <simple|terminal|browser> -> quota token
    #
    # The P2.4 per-class RAM grant: `512G` (the remainder clamp) for
    # browser, (class row + closure) x 1.10 rounded up to the next MiB
    # for the explicit classes. Returns exactly what the config
    # literal must spell.
    #
    proc class_ram { class } {
        variable EDITOR_CLOSURE_RAM
        variable CLAMP_RAM
        variable HEADROOM_NUM
        variable HEADROOM_DEN
        variable CLASS_RAM_ROUND_UP

        array set ns { simple runtime_simple terminal runtime_terminal
                       browser runtime_browser }
        if {![info exists ns($class)]} {
            error "runtime_quota::class_ram: unknown class '${class}'"
        }

        if {$class eq "browser"} { return $CLAMP_RAM }

        set base [_ram_mib [_class_row $ns($class) QUOTA_RAM]]
        if {$class eq "simple"} {
            incr base [_ram_mib $EDITOR_CLOSURE_RAM]
        }

        # ceil at the growth step (same rationale as class_caps).
        set grown [expr {($base * $HEADROOM_NUM + $HEADROOM_DEN - 1)
                         / $HEADROOM_DEN}]
        set mib [expr {($grown + $CLASS_RAM_ROUND_UP - 1)
                       / $CLASS_RAM_ROUND_UP * $CLASS_RAM_ROUND_UP}]
        return "${mib}M"
    }

    #
    # The legacy single pkg_runtime caps grant (P2.2, unchanged):
    # (class-row sum + editor closure) x 1.10, rounded up to 10,000.
    # Kept byte-identical so the four unported scenarios' literal
    # `caps: 230000` keeps verifying:
    #
    #   (205612 + 900) x 1.10 = 227163.2 -> 230000
    #
    proc pkg_runtime_caps { } {
        variable EDITOR_CLOSURE_CAPS
        variable HEADROOM_NUM
        variable HEADROOM_DEN
        variable ROUND_UP_TO

        set base [expr {[class_caps_sum] + $EDITOR_CLOSURE_CAPS}]
        set grown [expr {$base * $HEADROOM_NUM / $HEADROOM_DEN}]
        return [expr {($grown + $ROUND_UP_TO - 1)
                      / $ROUND_UP_TO * $ROUND_UP_TO}]
    }

    #
    # Legacy drift check (P2.2, unchanged): the scenario writes
    # `+ start pkg_runtime | caps: 230000 | ram: 512G` as TEXT and
    # calls this with those same literals right after sourcing the
    # class includes. Any drift between the class rows and the
    # scenario literal aborts the run before QEMU starts:
    #
    #   - a class QUOTA_CAPS row that moved,
    #   - a stale caps literal,
    #   - a ram literal that is not the remainder clamp.
    #
    proc _verify_legacy { caps ram } {
        variable CLAMP_RAM

        set want_caps [pkg_runtime_caps]

        if {$caps != $want_caps} {
            error "runtime_quota::verify: caps drift — scenario grants \
                   pkg_runtime ${caps}, class rows derive ${want_caps} \
                   (class sum [class_caps_sum] + editor closure, \
                   headroom/round-up per docs/17 §5). Update the \
                   scenario's caps literal (or the class row) so they \
                   agree."
        }
        if {$ram ne $CLAMP_RAM} {
            error "runtime_quota::verify: ram drift — scenario spells \
                   '${ram}', the remainder clamp is '${CLAMP_RAM}' \
                   (docs/17 §6.1). The RAM grant is a clamp, never a \
                   computed sum."
        }

        puts "runtime_quota: pkg_runtime grant verified \
               (caps: ${caps}, ram: ${ram}; class-row sum [class_caps_sum])"
        return
    }

    #
    # P2.4 grouped drift check: six literals straight out of the three
    # `+ start pkg_runtime_simple|_terminal|_browser` lines, in
    # simple/terminal/browser order. Each pair is checked against its
    # class derivation (class_caps/class_ram above), so a moved class
    # row aborts the run before QEMU starts exactly like the legacy
    # form. The browser RAM literal must be the remainder clamp.
    #
    proc _verify_grouped { simple_caps simple_ram
                           terminal_caps terminal_ram
                           browser_caps browser_ram } {
        foreach {class caps ram} [list \
                simple   $simple_caps   $simple_ram \
                terminal $terminal_caps $terminal_ram \
                browser  $browser_caps  $browser_ram] {
            set want_caps [class_caps $class]
            set want_ram  [class_ram  $class]
            if {$caps != $want_caps} {
                error "runtime_quota::verify: pkg_runtime_${class} caps \
                       drift — scenario grants ${caps}, the ${class} \
                       class rows derive ${want_caps}. Update the \
                       scenario's caps literal (or the class row) so \
                       they agree."
            }
            if {$ram ne $want_ram} {
                error "runtime_quota::verify: pkg_runtime_${class} ram \
                       drift — scenario spells '${ram}', the ${class} \
                       derivation spells '${want_ram}' (browser is the \
                       remainder clamp, docs/17 §6.1). Update the \
                       scenario's ram literal so they agree."
            }
        }

        puts "runtime_quota: grouped runtime grants verified \
               (simple ${simple_caps}/${simple_ram}, \
                terminal ${terminal_caps}/${terminal_ram}, \
                browser ${browser_caps}/${browser_ram}; \
                class-row sum [class_caps_sum])"
        return
    }

    #
    # verify dispatches on arity so the legacy scenarios keep their
    # two-literal call while the P2.4 ports pass the three grant
    # pairs. Anything else is a caller error.
    #
    proc verify { args } {
        switch -- [llength $args] {
            2 { return [_verify_legacy {*}$args] }
            6 { return [_verify_grouped {*}$args] }
        }
        error "runtime_quota::verify: expected '<caps> <ram>' (legacy \
               single pkg_runtime grant) or '<simple_caps> <simple_ram> \
               <terminal_caps> <terminal_ram> <browser_caps> \
               <browser_ram>' (P2.4 grouped grants), got [llength $args] \
               arguments"
    }
}
