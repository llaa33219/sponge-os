#
# \brief  runtime_quota — the single pkg_runtime grant vocabulary
#         (Phase 16 P2.2, transitional).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# The TRANSITIONAL single-grant helper for the pkg_runtime node
# (docs/17-runtime-component-topology.md §6). Until P2.4 groups the
# runtimes, all enabled packages live under ONE pkg_runtime node, and
# that node's grant is one pair of numbers:
#
#   caps: 230000   — explicit, derived from the class rows below;
#   ram:  512G     — the deliberately-huge remainder clamp.
#
# This include owns the DERIVATION and the DRIFT CHECK for that pair.
# It emits no config, injects no start node, and touches no route: the
# scenario keeps its literal `+ start pkg_runtime | caps: 230000 |
# ram: 512G` and calls `runtime_quota::verify 230000 512G` so a class
# row that moved cannot silently outrun the literal in the scenario.
#
# === CAPS DERIVATION (docs/17 §6) ========================================
#
# The class rows (each owned by its per-class include):
#
#   runtime_simple    4512 caps   hello 256 · textedit 1000 ·
#                                 files 1000 · calculator 1000 ·
#                                 pdf_view 256 · debug 1000
#   runtime_terminal  1100 caps   terminal
#   runtime_browser 200000 caps   falkon
#   ----------------------------
#   class-row sum    205612 caps
#
# Editor closure (the nano 600 + ncurses 300 pair reachable from the
# editor packages, docs/17 §6): +900 caps → 206512.
#
# +10% caps headroom (docs/17 §5 practice — adding a package must not
# be a cliff-edge operation on a grant sized exactly to fit):
# 206512 × 1.10 = 227163.2
#
# Rounded up to the next 10,000 caps: 230000.
#
# The editor closure is documented precisely because it does NOT move
# the grant: without it the same arithmetic gives 205612 × 1.10 =
# 226173.2, which rounds up to the same 230000. One number covers both
# closures.
#
# === RAM: THE CLAMP, NOT A SUM ===========================================
#
# The RAM rows (runtime_simple 1288M + runtime_terminal 224M +
# runtime_browser 1024M = 2536M) are documentation for the admission
# math only. The grant is `ram: 512G` — the deliberate remainder clamp
# (docs/17 §6.1, genode/repos/os/src/lib/sandbox/child.cc:481-486:
# init silently clamps a child's RAM quantum to what the parent still
# has at creation time, so 512G means "everything left"). Two binding
# conditions hold and are NOT enforced here (they are config-shape
# invariants the scenario keeps):
#
#   1. pkg_runtime is the LAST `+ start` node of its config — nothing
#      may be created after it, or the clamp silently starves;
#   2. caps stay EXPLICIT — caps exhaustion on seL4 hangs
#      (docs/09-roadmap.md §11.1), and a caps clamp would starve any
#      future sibling.
#
# === USAGE ===============================================================
#
#   source .../runtime-simple.inc
#   source .../runtime-terminal.inc
#   source .../runtime-browser.inc
#   source .../runtime-quota.inc
#
#   runtime_quota::verify 230000 512G     ;# fail-loud drift check
#
# The class includes must be sourced first: pkg_runtime_caps() reads
# their QUOTA_CAPS rows live, so a row edit moves the derivation and
# verify() fails against a stale literal. That is the whole point.
#

namespace eval runtime_quota {

    #
    # The remainder-clamp RAM quantum (docs/17 §6.1). A string, not a
    # number: `512G` is an init quota token, exactly as the config
    # spells it.
    #
    variable CLAMP_RAM 512G

    #
    # Editor closure reachable from the editor packages (docs/17 §6):
    # pkg/nano <quota caps="600"/> + pkg/ncurses <quota caps="300"/>.
    # Added to the class-row sum for the grant derivation; see the
    # header for why it does not move the rounded grant.
    #
    variable EDITOR_CLOSURE_CAPS 900

    #
    # Documented headroom/round-up policy (docs/17 §5 practice):
    # +10% caps headroom, then rounded up to the next 10,000 caps.
    #
    variable HEADROOM_NUM 11
    variable HEADROOM_DEN 10
    variable ROUND_UP_TO  10000

    #
    # The three class rows, summed live from their owning includes.
    # Fails loudly when a class include is not loaded — a derivation
    # that silently fell back to a constant would defeat the drift
    # check below.
    #
    proc class_caps_sum { } {
        set sum 0
        foreach ns {runtime_simple runtime_terminal runtime_browser} {
            if {![info exists ${ns}::QUOTA_CAPS]} {
                error "runtime_quota::class_caps_sum: ${ns}::QUOTA_CAPS \
                       not loaded — source ${ns}.inc (and its siblings) \
                       before runtime-quota.inc"
            }
            set sum [expr {$sum + [set ${ns}::QUOTA_CAPS]}]
        }
        return $sum
    }

    #
    # runtime_quota::pkg_runtime_caps -> 230000
    #
    # The single pkg_runtime caps grant, derived from the class rows:
    #
    #   (class-row sum + editor closure) x 1.10, rounded up to 10,000
    #
    # Computed, never a magic constant — if a class row moves, this
    # number moves with it, and verify() then fails the scenario's
    # stale literal loudly (fail-loud drift checking).
    #
    proc pkg_runtime_caps { } {
        variable EDITOR_CLOSURE_CAPS
        variable HEADROOM_NUM
        variable HEADROOM_DEN
        variable ROUND_UP_TO

        set base [expr {[class_caps_sum] + $EDITOR_CLOSURE_CAPS}]
        set grown [expr {$base * $HEADROOM_NUM / $HEADROOM_DEN}]
        return [expr {($grown + $ROUND_UP_TO - 1) / $ROUND_UP_TO * $ROUND_UP_TO}]
    }

    #
    # runtime_quota::verify <caps> <ram>
    #
    # Fail-loud drift check for the scenario's literal grant. The
    # scenario writes `+ start pkg_runtime | caps: 230000 | ram: 512G`
    # as TEXT (kept literal on purpose — the config stays readable and
    # self-contained), and calls this with those same literals right
    # after sourcing the class includes. Any drift between the class
    # rows and the scenario literal aborts the run before QEMU starts:
    #
    #   - a class QUOTA_CAPS row that moved,
    #   - a stale caps literal,
    #   - a ram literal that is not the remainder clamp.
    #
    # Returns nothing on success; errors with both sides named on
    # drift.
    #
    proc verify { caps ram } {
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
}
