# Runtime component topology: incident analyses

> **Breadcrumb:** parent [17 - Runtime Component Topology](../17-runtime-component-topology.md); this file holds its §3 (terminal freeze analysis), §4 (mouse follow-up) and §4a (SPONGE DEBUG scroll-storm report), moved verbatim.
> Siblings in `docs/topology/`: [tree-and-chains.md](tree-and-chains.md) (§1, §2, §2a), [design-gap-register.md](design-gap-register.md) (§5 part 1), [design-gap-register-2.md](design-gap-register-2.md) (§5 part 2), [quota-ledger.md](quota-ledger.md) (§6).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 3. Why a terminal component froze the desktop (the honest analysis)

The 15:55 regression: `vct_tty` shipped without a `<config>` node —
the first config-less child in the `system` sandbox — and the
sandbox's entrypoint thread aborted
(`Attached_dataspace::Region_conflict`) creating it. Result: no
panel, frozen desktop.

Component isolation did **not** fail:
- No component read or wrote another component's memory.
- No capability was forged.

What failed is **liveness of a shared broker**:
1. **The sandbox entrypoint is a single thread** that brokers every
   session request of every child (Gui, ROM, Report, PD, CPU …).
   One uncaught exception in that thread froze all session traffic
   for all children — the panel could not complete its Gui session.
   The trigger was mine; the fragility (an ep abort on a child-config
   edge case, with no fault containment) is broker-level.
2. **Resource pools are shared**: children of one init draw caps/RAM
   from a single grant (`system`: 280000 caps / the RAM remainder on
   the product media). Adding a child can starve siblings — quota
   arithmetic, not fault isolation. The same arithmetic bit the
   package runtimes: each class runtime (`pkg_runtime_simple`,
   `pkg_runtime_terminal`, `pkg_runtime_browser`) draws its own caps
   grant, and `pkg_runtime_browser` additionally the RAM remainder,
   which must cover the SUM of every simultaneously-enabled package
   of its class (§6).
3. **report_rom is a shared nervous system**: one process relays every
   Report/ROM channel in its scope; a policy or evaluation problem
   there has system-wide blast radius.

## 4. The mouse follow-up (17:35 image) — topology says: suspicious

With the `<provides>` fix everything renders, but the real-hardware
cursor does not move. Per §2, the input chain has **zero nodes inside
`system`** — the vct_tty/pkgd/sponge-de changes are structurally
unable to freeze `usb_hid → event_filter → nitpicker → pointer`
unless a *shared* substrate (nitpicker load, core caps, CPU) is
saturated. The discriminating tests, in order:

1. **Diagnostic media** (`SPONGE_FBCON=on`): kernel fb console on the
   panel shows whether `usb_hid` enumerated the mouse at all, and any
   event_filter/nitpicker/pointer errors — on the real machine.
   **Hard constraint (user directive 2026-10-16): the kernel fb
   console and the desktop NEVER share the framebuffer.** FBCON media
   is legitimate only for bring-up where the desktop never comes up;
   once nitpicker owns the fb, an always-on console produces the worst
   outcome (the screen flickers between the two, the console is
   unreadable). In-desktop error surfacing is the debug package's
   raison d'être plus the panel health label (§5); a *problem-triggered
   console switch* (take the fb over on a fatal fault, show the log,
   never draw both) is the sanctioned design for field diagnosis and
   is registered as a follow-up in §5.
2. **QEMU input test**: boot the 17:55-lineage image, drive the QEMU
   mouse via QMP, screendump — proves the chain works with the same
   binaries/configs.
3. **Replug/reboot the mouse on real hw**: the known USB-enumeration
   flake family (documented in Phase 15).

## 4a. The SPONGE DEBUG scroll-storm report (2026-10-15) — measured: saturation, not a leak

§4 above predicted that a mouse that "does nothing" points at a
**saturated shared substrate** rather than at component isolation. A
later field report — the SPONGE DEBUG window freezing when the scrollbar
is dragged fast, with the freeze point moving when the package quota was
raised — is the same shape, and it has now been **measured**:

| arm | repaints | `sponge_debug` ΔRAM | nitpicker / drivers / report_rom ΔRAM |
|---|---|---|---|
| `baseline` (no storm) | — | 0 | 0 |
| `scroll` (30 s @ 30 Hz flings) | 335 | **0** | 0 |
| `grow` (+ one log line per tick) | 335 | 139 264 B (the text itself) | 0 |

Scenario `run/sponge-debug-scrollstorm.run` (base-sel4, QEMU,
`-nographic`); instrument `repos/sponge/src/test/repostorm_probe`, which
reads the sandbox state report's per-child `ram`/`caps` and names the
largest grower; full write-up and receipts in
[`docs/evidence/sponge-debug-scrollstorm.md`](../evidence/sponge-debug-scrollstorm.md).

Two facts settle it:

1. **Nothing grows.** A GUI session in Genode holds *one* long-lived
   framebuffer dataspace that the client maps once and writes into
   forever — there is no buffer rotation for a repaint to leak — and the
   Qt6 QPA plugin allocates nothing per frame. The 139 KB in the `grow`
   arm is `QTextDocument`'s per-block cost — the text itself, bounded by
   the number of lines rather than by wall time.
2. **The rate does.** The storm asked for 30 repaints/s and the
   CPU-rasterized stack delivered **11 Hz** (335 ticks in 30 064 ms).
   Input therefore arrives ~3x faster than frames can be produced; a
   hand-driven drag on real hardware builds the same backlog until the
   event loop stops draining.

So the "quota raise helped" observation is explained without a leak: more
RAM/caps headroom means a cheaper *per-frame* cost (softpipe and Qt's
backing store are sized out of that quota), so the achievable rate rises
and the freeze arrives later. The asymptote is CPU cycles. **Raising a
quota is not a fix for this class of report and none was applied.**

The unbounded-backlog half of the mechanism — the QPA plugin never
registering the framebuffer's sync signal, so a client never learns how
far behind the compositor it is — is now patched by **frame coalescing**:
**ledger row #23**, `docs/patches/qt6-qpa-frame-sync.patch`. See §5 and
[`docs/evidence/sponge-debug-scrollstorm.md`](../evidence/sponge-debug-scrollstorm.md)
§8 for the before/after numbers. The benefit is now **measured, not just
correct-by-construction**: because a QEMU/softpipe client is normally
*slower* than the compositor and would never form a backlog on its own,
the harness forces the condition with a dedicated arm, and coalescing is
observed engaging (800 submit attempts collapse to 82 frame submissions
in 30 s) with storm throughput up 11 → 19 Hz and no liveness regression.
What remains open is the real-hardware magnitude at 2560x1600, not the
mechanism — see §5.
