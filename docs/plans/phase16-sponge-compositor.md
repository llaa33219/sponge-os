# Phase 16, Sponge Compositor and Repository Split (Work Plan)

> Status: G0 repository split and close-out completed 2026-11-18 with
> receipts in [`../evidence/phase16-g0-closeout.md`](../evidence/phase16-g0-closeout.md).
> G1 completed on 2026-11-18 with receipts in
> [`../evidence/phase16-compositor-index.md`](../evidence/phase16-compositor-index.md).
> G2 input battery verified 2026-11-18 on seL4 QEMU: both Tier-0s pass all
> five phases and the overall marker, the Event-injection differential
> matches exactly, and the full-trace digest diverges only by the documented
> attach-time hover-establishment records enumerated under section 8.2. On
> 2026-11-19 the user approved the native default, superseding the original
> flip timing: `run/compositor.inc` now selects `sponge_compositor` for
> unset or `SPONGE_COMPOSITOR=native` and the nitpicker fallback for
> `SPONGE_COMPOSITOR=upstream`, with default and fallback runs recorded in
> the evidence index. G3 is verified 2026-11-20 with an explicitly
> documented intermittent close-time input failure (section 7 G3): the
> status is **G1-G3 verified with documented intermittent close-time input
> failure**, not a claim of perfect stability. Alpha blending and WM/DE
> integration are proven on seL4 QEMU; G4-G5, product integration, and
> physical hardware remain uncompleted and unclaimed.
> Relationship: companion to the delivered Sponge WM plan
> (`docs/plans/phase16-sponge-wm.md`, D16W.x), the Mojo language policy
> (`docs/plans/mojo-language-policy.md`, DLP.x), and the runtime topology
> record (`docs/17-runtime-component-topology.md`). Decision IDs here
> are **D16C.x**. Verified FFI/build basis is cited as
> [`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md)
> (created in parallel with this registration). The roadmap
> (`docs/09-roadmap.md`) is not touched by this registration.

---

## 1. Goal

Deliver a Sponge-native compositor that can take over nitpicker's
Tier-0 job (the composited screen the whole desktop shares), written
Mojo-first in the shape the user approved: a **Mojo native object plus
a thin C++ Genode adapter plus a `Genode::Heap` KGEN shim**. Alongside
it, deliver the repository split: an umbrella repository plus three
pinned repositories (`sponge-wm`, `sponge-de`, `sponge-compositor`).
Mojo is used as broadly as practical and native Mojo is the
recommended default, but correctness and security win every conflict
and the upstream nitpicker fallback remains mandatory. The first
vertical slice is deliberately small: two opaque views, clipping, and
a Capture consumer on seL4 QEMU. Everything beyond that slice is
gated. The 2026-11-16 staging kept the shipped default on upstream
nitpicker until a separate user-approved flip; that approval came on
2026-11-19 after the green G2 receipts, so the default is now native
(section 2).

## 2. Recorded User Approvals (2026-11-16, binding)

The user approved this plan's design on 2026-11-16. Recorded choices,
do not re-litigate:

1. **Repository names.** Umbrella plus three pinned repositories
   consumed as git submodules: `sponge-wm`, `sponge-de`,
   `sponge-compositor`.
2. **Architecture.** Mojo native object + thin C++ Genode adapter +
   `Genode::Heap` KGEN shim.
3. **Mojo breadth.** Mojo used as broadly as practical, native Mojo
   recommended; correctness/security and the upstream fallback stay
   mandatory and override breadth.
4. **C++ scope limit.** C++ is limited to `Component::construct`, the
   Gui/Capture/Event RPC adapters, dataspace/signal/framebuffer glue,
   and the KGEN runtime shim.
5. **Mojo-owned core.** Scene/session/view stack, command reducer,
   clipping/damage, painter, alpha/input-mask hit testing, and input
   state are Mojo-owned.
6. **Fallback shape.** Upstream nitpicker remains the fallback and the
   shipped compositor; the native path is opt-in via
   `SPONGE_COMPOSITOR=native`.
7. **First vertical slice.** Two opaque views + clipping + Capture on
   seL4 QEMU.
8. **Validation.** Golden images and upstream nitpicker differential
   validation, rather than a second C++ scene model.
9. **Gate structure.** Phases G0-G6 with staged approval gates; the G6
   default flip happens only after separate user approval.
10. **Mojo constraints.** Load `mojo-syntax` and consult the official
    current docs before writing Mojo; no Python interop, no async, no
    POSIX I/O in the initial in-OS subset; check `nm -u` after every
    Mojo SDK bump.
11. **Incremental migration.** The existing C++ WM/DE code migrates
    incrementally later, not as a big-bang rewrite.
12. **Least privilege, single entrypoint.** Least-privilege
    capabilities and a single-entrypoint initial design.

**Superseding approval (2026-11-19).** The user approved making the
native compositor the default after the green G2 receipts, earlier than
the original G6-after-G5 sequence. Unset or `SPONGE_COMPOSITOR=native`
now selects `sponge_compositor`, and `SPONGE_COMPOSITOR=upstream`
selects the nitpicker fallback, which stays reachable as an explicit
override. Approvals 6 and 9 are superseded on that point only: the
fallback shape survives, the staged flip timing does not. The gate
structure is otherwise unchanged: G3 closes with its intermittent
close-time failure documented (section 7 G3), G4-G5 remain uncompleted,
and no gate result is rounded up.

## 3. Baseline (2026-11-16, plan-time truth)

- **Topology.** Upstream nitpicker is the Tier-0 compositor server the
  whole desktop shares. The chain is
  `sponge-de / apps -> wm -> nitpicker -> fb_<pkg> -> framebuffer`
  (`docs/17-runtime-component-topology.md`). Clients reach the
  compositor through the public Gui/Capture/Event session surface, and
  that public surface is what a native replacement must speak.
- **Delivered above the compositor.** The Sponge WM trio
  (`sponge_wm`, `sponge_layouter`, `sponge_decorator`, protocols v1
  and v1.1 per `docs/plans/phase16-sponge-wm.md`) and Sponge DE are
  C++/Qt6 today and run against that public surface unchanged.
- **Verified Mojo basis**
  ([`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md)):
  seL4 pure arithmetic PASS (`sum=42`); seL4 `List[Int64]` with the
  `Genode::Heap` KGEN shim PASS (`add=42 list-sum=45`).
  `mojo build --emit object` is an experimental emit path and runtime
  coverage is subset-specific. Nothing beyond those two probe shapes
  was proven at registration; the gate receipts recorded since are
  G1-G3 (section 7) and claim nothing beyond themselves.
- **Repository state.** At plan registration there was one Sponge repo
  at `repos/sponge/` plus the pinned `genode/` submodule. G0 has now
  added the pinned `sponge-wm`, `sponge-de`, and `sponge-compositor`
  submodules and moved their designated sources without changing the
  desktop topology; `sponge-compositor` remains a warning-only
  scaffold. The G0 terminal F7 repair added patch-ledger rows #39–#41
  (`docs/11-environment.md` §4); the compositor itself adds no further
  rows. The Mojo SDK is pinned by `uv.lock`
  (currently 1.0.0b2); its use inside OS components is governed by
  this plan and the probe subset.
- **Build surface.** Genode's build consumes pre-compiled objects
  through `SRC_O` (`genode/repos/base/mk/generic.mk`), which is the
  link seam for emitted Mojo objects. No build-system patch is needed.

## 4. Binding Decisions (do not re-litigate)

| ID | Decision | Rationale |
|---|---|---|
| **D16C.1** | **Umbrella plus three pinned repositories.** The existing `sponge-os` repository is the umbrella. It pins three new repositories as git submodules at `repos/sponge-wm/`, `repos/sponge-de/`, `repos/sponge-compositor/`, named exactly `sponge-wm`, `sponge-de`, `sponge-compositor`, hosted alongside `sponge-os-genode` (AGENTS.md §5.2). Each is a Genode repo following the AGENTS.md §3.2 component layout. Wiring uses direct `REPOSITORIES += $(SPONGE_ROOT)/repos/<name>` entries in `tool/build.mojo`'s managed `build.conf` block, with zero changes or new symlinks under `genode/`. Pin discipline is the §5.2 discipline: pin, don't float; every bump is deliberate, recorded, and verified. | Reproducibility (AGENTS.md §5.2). One `git clone --recursive` keeps bringing the whole build, each component family gets its own history, and the vendored Genode fork stays the only pinned framework tree. |
| **D16C.2** | **Split contents.** `sponge-wm` takes the WM trio (`sponge_wm`, `sponge_layouter`, `sponge_decorator`), the `sponge_wm_protocol` library, and its contract tests. `sponge-de` takes the DE (panel, launcher, tasklist, settings, background). `sponge-compositor` is the new home of the compositor (Mojo core, C++ adapter, KGEN shim). The G0 moves are mechanical and change no behavior. Everything not named here (vct, `sponge_configd`, `sponge_pkgd`, `sponge_themed`, shared libs, `tool/`, `run/`, `docs/`) stays in the umbrella's `repos/sponge/` until a later split decision (§11). Shared headers live in exactly one owning repo; cross-repo consumption goes through Genode repo include mechanisms, never relative `../..` paths. | AGENTS.md §1.4 one-responsibility boundaries, applied at repository granularity. Keeping the unsplit services out of G0 prevents the split from becoming a big-bang rearrangement of everything. |
| **D16C.3** | **Language split.** Mojo owns the scene/session/view stack, the command reducer, clipping and damage computation, the painter, alpha/input-mask hit testing, and input state. C++ is limited to four duties: `Component::construct` entry, the Gui/Capture/Event RPC adapters, dataspace/signal/framebuffer glue, and the KGEN runtime shim. Any new compositor responsibility goes to Mojo first; a C++ exception needs a recorded reason. | Recorded approvals 3 and 4. The four C++ duties are exactly where Genode's C++ component ABI and IPC surface must be spoken; everything else is ours to write in the recommended language. |
| **D16C.4** | **Build seam.** Mojo units compile with the pinned toolchain as `mojo build --emit object --relocation-model=pic` and enter the Genode target through `SRC_O` (`genode/repos/base/mk/generic.mk`). Emitted objects are self-contained PIC objects with **no libc dependency**. A Sponge build helper pins the exact flags and fails loudly on drift. | The emit path is experimental and subset-specific (probe), so the seam is pinned instead of improvised. `SRC_O` is public Genode build surface, which keeps D16C.7 true. |
| **D16C.5** | **KGEN runtime shim on `Genode::Heap`.** The KGEN shim is the sanctioned runtime bridge for Mojo objects on Genode. It is C++, implemented on `Genode::Heap`, and supplies the allocator/runtime entry points the emitted objects reference (the `List[Int64]` probe shape). Nothing else crosses into Mojo as a runtime service. | Proven in `../evidence/mojo-ffi-probe.md`. One declared bridge keeps `nm -u` auditable (§6) and keeps allocation inside Genode's quota model. |
| **D16C.6** | **In-OS Mojo subset.** The initial in-OS subset excludes Python interop, async, and POSIX I/O (full contract in §6). All I/O and OS services cross the C++ adapter seam. Widening the subset is a later, separate decision with an `nm -u` re-baseline. | Runtime coverage is subset-specific (probe). A small subset keeps the no-libc claim true and the seam reviewable. |
| **D16C.7** | **Public Genode APIs only.** Public Genode interfaces and public IPC surfaces only. No private gems headers and no compositor-specific vendored patches or new ledger rows. The pre-compositor G0 terminal F7 repair is recorded separately in rows #39–#41; those commits are not a compositor dependency or a precedent for adding compositor patches. If patching the fork tree for the compositor becomes tempting, work stops and comes back to the user (D16.8 discipline). | AGENTS.md §5.2 and D16.8. Replacing nitpicker must not recreate the private-dependence problem the native WM exists to escape. |
| **D16C.8** | **Fallback and selection.** Original 2026-11-16 shape: upstream nitpicker stays the shipped compositor for every existing scenario and every shipped medium, and the native compositor is selected only through the opt-in `SPONGE_COMPOSITOR=native`. Superseded 2026-11-19 (user approval): unset or `SPONGE_COMPOSITOR=native` selects the native compositor as the default, and `SPONGE_COMPOSITOR=upstream` selects the nitpicker fallback, which stays reachable as an explicit override (verified default/fallback receipts in the evidence index). Invalid values fail loudly. | Recorded approval 6, superseded on selection by the 2026-11-19 approval. The fallback is simultaneously the regression net and the always-open control door (AGENTS.md §1.1). |
| **D16C.9** | **First vertical slice.** The first delivered compositor slice is two opaque views + clipping + Capture on seL4 QEMU (G1). Nothing beyond that slice is claimed before its gate receipts exist. | Recorded approval 7. Small honest slices beat a broad unproven claim (AGENTS.md §1.4). |
| **D16C.10** | **Validation oracle.** Correctness is validated by golden images and by differential comparison against upstream nitpicker under identical scripted geometry, content, and input. A second scene model in C++ (or any other language) as a reference implementation is explicitly rejected. | Recorded approval 8. A second model drifts from the first and doubles the maintenance, while nitpicker is the behavior clients already expect. |
| **D16C.11** | **Least privilege and single entrypoint.** Every session request is minimum-privilege (AGENTS.md §1.2): the compositor consumes only what it needs (Framebuffer, Input, Report, ROM, Timer as each gate requires) and provides only the Gui/Event/Capture session surface. The initial design is one component with one `Component::construct` entrypoint; internal modules (scene, input, painter) keep split boundaries loose the way AGENTS.md §3.4 requires for the DE, and a later component split is an open question, not a promise. | Recorded approval 12. A compositor core is one responsibility area, and premature component splits would complicate the first slices without a security gain. |
| **D16C.12** | **Staged approval gates.** G0 through G5 proceed one gate at a time: a gate's work starts only after the previous gate's receipts are approved by the user. The original G6 sequencing put the default flip after G5 under a separate, explicit user approval; the 2026-11-19 approval superseded that timing, and the flip landed after the green G2 receipts instead. G3-G5 staging is unchanged, and this plan cannot flip a default on its own. | Recorded approval 9, flip timing superseded 2026-11-19. The flip changes what every user boots, so it gets its own decision; that decision arrived earlier than the G6 slot. |
| **D16C.13** | **Incremental C++ migration later.** The existing C++ WM trio and the Qt6 DE migrate to Mojo later and incrementally, per component and per slice. This plan does not rewrite them, and no big-bang rewrite is scheduled. | Recorded approval 11. The delivered C++ stack is the proven baseline and the fallback; it is replaced piecewise only. |
| **D16C.14** | **Scenario naming.** New scenarios use `run/sponge-compositor*.run` names. Pinned names: `run/sponge-compositor-opaque.run` (G1), `run/sponge-compositor-input.run` (G2), `run/sponge-compositor-c16.run` (G3 battery on native), `run/sponge-compositor-diff.run` (differential/golden), `run/sponge-compositor-fuzz.run` (G5). No existing `run/` scenario is renamed or repurposed; the upstream anchors keep their names. | Same name-separation discipline as D16W.8: the native gates and the fallback anchors must stay independently runnable. |

## 5. Non-Goals (explicitly out of this plan)

- **Claiming gate results without receipts.** Registration marked
  nothing complete. G1-G3 are claimed only with their receipts and
  their honest gaps (the G3 close-time failure stays documented), and
  nothing beyond them is claimed.
- **A big-bang C++ to Mojo rewrite** of the WM or the DE (D16C.13).
- **A second scene model** in C++ or any other language as a
  validation oracle (D16C.10).
- **Python interop, async, POSIX I/O** in the in-OS Mojo subset
  (D16C.6).
- **A libc dependency** in emitted Mojo objects (D16C.4).
- **New compositor-specific vendored patches** or ledger rows beyond the
  recorded G0 F7 repair (#39–#41, D16C.7).
- **A default flip without separate user approval** (D16C.12). The user
  gave that approval on 2026-11-19, earlier than the G6 slot; the flip
  landed with the `SPONGE_COMPOSITOR=upstream` fallback intact and
  carries no G3-G5 completion claim.
- **Visual effects beyond alpha blending.** Alpha blending
  (straight-alpha `blit_alpha`) and alpha/input-mask hit testing are
  delivered and proven on seL4 QEMU; fancier visual effects are not
  designed here.
- **GPU acceleration.** CPU painting only across these gates.
- **A physical-hardware gate for the native compositor.** G1-G5 prove
  on seL4 QEMU; hardware bring-up is a later, separately registered
  effort.
- **Wayland/X11 compatibility layers** of any kind.
- **Splitting the compositor into multiple components**
  (single-entrypoint initial design, D16C.11).
- **Finding a new home for vct, `sponge_configd`, `sponge_pkgd`,
  `sponge_themed`** (they stay put; §11).
- **Roadmap changes.** `docs/09-roadmap.md` wording stands; it is
  touched only with real receipts in the same commit (AGENTS.md
  §5.4).

## 6. Mojo In-OS Contract (binding)

This contract applies to every Mojo unit compiled into a Genode
component under this plan. Before writing any Mojo code here, load the
`mojo-syntax` skill and consult the official current Mojo
documentation. The skill's corrections override pretrained patterns,
and every generated file is build-verified with the pinned toolchain
before it counts as delivered.

1. **Subset.** No Python interop (`std.python`), no async
   (`__async`/`Coroutine`), no POSIX I/O (no files, sockets, or
   process calls) inside Genode. Everything OS-shaped crosses the C++
   adapter seam of D16C.3.
2. **Build.** `mojo build --emit object --relocation-model=pic` per
   unit; objects enter the Genode target through `SRC_O`. The emit
   path is experimental (probe), so the Sponge build helper pins the
   exact flags and rejects drift.
3. **No libc.** Emitted objects reference nothing from libc. The KGEN
   shim (D16C.5) plus the C++ adapter are the only allowed providers
   of runtime symbols.
4. **`nm -u` after every Mojo SDK bump.** The undefined-symbol set of
   every emitted object is diffed against the declared shim/adapter
   ABI list. A new undefined symbol stops the bump unless the user
   approves adding it to the declared surface. Receipts are recorded
   per gate in the evidence index.
5. **ABI seam.** Fixed-width integers and plain pointers only across
   the C++/Mojo boundary. No C++ or Mojo aggregate types cross the
   seam.
6. **Toolchain pin.** In-OS builds use the Mojo SDK pinned by
   `uv.lock` (`docs/11-environment.md`). Bumps are deliberate,
   recorded, and re-verified per rule 4.

Illustrative shape of the Mojo-owned core (not implementation; this
exact snippet compiles with the pinned toolchain, Mojo 1.0.0b2, via
`--emit object --relocation-model=pic` with an empty `nm -u`):

```mojo
# Illustrative shape only, not implementation. Written per mojo-syntax.
from std.math import min, max

@fieldwise_init
struct Rect(Copyable, Movable, Writable):
    var x: Int32
    var y: Int32
    var w: Int32
    var h: Int32

def intersect(clip: Rect, r: Rect) -> Rect:
    var x1 = max(clip.x, r.x)
    var y1 = max(clip.y, r.y)
    var x2 = min(clip.x + clip.w, r.x + r.w)
    var y2 = min(clip.y + clip.h, r.y + r.h)
    var w = x2 - x1
    var h = y2 - y1
    if w < 0:
        w = 0
    if h < 0:
        h = 0
    return Rect(x1, y1, w, h)
```

The Genode side stays flat. Shape of the target (illustrative):

```makefile
TARGET  := sponge_compositor
SRC_CC  := main.cc gui_adapter.cc kgen_shim.cc
SRC_O   := build/scene.o build/painter.o
LIBS    := base
```

## 7. Implementation Gates (G0-G6)

Staged per D16C.12: every gate closes with curated receipts and user
approval before the next gate begins. Nothing here starts before this
plan is checked in. Every gate keeps the upstream anchors and the
existing regression sweep green. Every gate also keeps the
no-new-compositor-specific-ledger-row check true; the G0 F7 rows #39–#41
remain recorded separately. G0-G3 are verified (G3 with a documented
intermittent close-time input failure, section 7 G3); G4-G5 remain
uncompleted, and the default flip recorded under G6 landed early per the
2026-11-19 approval (section 2).

### G0, Repository split

- Create the umbrella pinning `sponge-wm`, `sponge-de`,
  `sponge-compositor` as submodules under `repos/` (D16C.1). Register
  their absolute repository paths in `tool/build.mojo`'s managed
  `REPOSITORIES` block; zero changes or new symlinks under `genode/`.
- Mechanical source moves per D16C.2: WM trio plus `sponge_wm_protocol`
  into `sponge-wm`; DE into `sponge-de`; a compositor scaffold into
  `sponge-compositor` (`target.mk`, `main.cc`, `README.md` per
  AGENTS.md §3.2). Each move commit changes no behavior and says so in
  its message.
- Register the pins in `docs/11-environment.md`; the F7 terminal
  lifecycle repair adds rows #39–#41, with no compositor-specific patch
  or ledger row.
- Close-out receipts: [`../evidence/phase16-g0-closeout.md`](../evidence/phase16-g0-closeout.md)
  records the split pins, F7 commits, regression matrix, and remaining
  non-compositor limitations.
- Gate: the full existing regression sweep green after the moves;
  submodules pinned; `nm -u` baseline recorded for any Mojo objects
  that exist at that point (none are required yet). User approval,
  then G1.

### G1, Opaque smoke (the first vertical slice)

- `sponge-compositor` serves the public Gui/Capture/Event surface with
  the Mojo scene/session/view stack, command reducer,
  clipping/damage, and opaque painter (D16C.3), plus the thin C++
  adapter and the `Genode::Heap` KGEN shim (D16C.4/D16C.5).
- `run/sponge-compositor-opaque.run`: seL4 QEMU boots the native
  compositor under a minimal topology with two opaque view clients.
  Clipping is verified at every overlap region (the correct view shows
  through), and a Capture consumer sees the composed screen.
- Golden image of the scene recorded; `g1-diff: MATCH` with checksum
  `0x670d6b5d9fc0ed7b`; differential against upstream nitpicker for the
  same scene per §8; `nm -u` audit green.
- Gate: the scenario green on seL4 QEMU with receipts. User approval,
  then G2.

### G2, Input

- Alpha/input-mask hit testing and input state land in the Mojo core
  (D16C.3): pointer position, button state, focus, and Event delivery
  to the hit view. View-stack ordering is enforced by the hit test,
  and input masks pass clicks through to lower views where the mask
  says so.
- `run/sponge-compositor-input.run`: QMP-driven pointer input lands on
  the correct of two overlapping views, mask holes fall through to the
  view below, and the pointer report stays sane.
- Gate: the input battery green on seL4 QEMU, and scripted input
  behavior matches upstream nitpicker in the differential (exact
  `trace-inject` match `0xc50006d9e1676755`). The section
  8.2 clause allows an enumerated divergence; G2's is the attach-time
  hover-establishment gap (exact on the Event-injection stream). Met
  2026-11-18; user approval, then G3.

### G3, Full Criterion-16 battery (verified 2026-11-20; intermittent close-time failure documented)

Status wording is **G1-G3 verified with documented intermittent
close-time input failure**, not "G3 complete" and not a claim of
perfect stability.

- The complete Criterion-16 acceptance battery
  (`docs/plans/phase16-sponge-wm.md` §7: move, 8-zone resize, focus,
  close, minimize/restore with focus, maximize/restore, tiling
  open/close/mode/reflow, and the >=20-cycle rules-drift soak) runs
  green end to end under default-native selection (unset, or
  `SPONGE_COMPOSITOR=native` as the explicit equivalent), the delivered
  Sponge WM trio and DE tasklist sitting above the native compositor.
- Receipts: native QMP full 16-step PASS
  (`/tmp/opencode/g3-c16-qmp-final4.log`); native tasks PASS
  (`/tmp/opencode/g3-c16-tasks-fixed.log`); native full 20-cycle tiling
  PASS with close and reflow PASS
  (`/tmp/opencode/g3-c16-tiling-final3.log`); upstream QMP and tasks
  anchors PASS (`/tmp/opencode/g3-upstream-qmp.log`,
  `/tmp/opencode/g3-upstream-tasks.log`), upstream tiling not claimed;
  `./tool/build verify` all 41 patch-ledger rows and hardware gates
  green; all three Mojo objects keep empty `nm -u`; host receiver and
  guard-page repros pass. Alpha blending and WM/DE integration are
  proven on seL4 QEMU. Capacities are 32 sessions, 64 views, and
  3 damage rects; default native and explicit upstream fallback both
  work.
- Intermittent close-time input failure (explicit, never relabeled):
  `/tmp/opencode/g3-c16-tiling-final2.log` failed at close (the master
  close did not destroy its window), while
  `/tmp/opencode/g3-c16-tiling-final3.log` and
  `/tmp/opencode/g3-c16-tiling-restoretablet2.log` passed the same
  close and reflow gates.
- The G3 functional changes are fixes, not changes to the acceptance
  assertions: per-session `pressed_session` preserves input across view
  recreation; bind-time texture extents with an in-extent fast path and
  safe tiling; per-session domain Info_rom (panel 1024x28 versus the
  full-screen default); per-Capture-session damage distribution and
  sync only on nonempty damage; DE static rules converted to
  boundary-relative coordinates; and the tiling scenario's
  absolute-tablet close path (W4 scale transform, one-marker host
  dispatch) with conditional restore-before-close.
- The upstream anchors `run/sponge-wm{,-qmp,-tasks}.run` stay a
  standing requirement; decorated-scene golden/differential growth is
  not claimed at this gate. Gate: the full battery green (any gap
  recorded as an honest partial, never relabeled) and the anchors
  green. User approval, then G4.

### G4, Product integration (uncompleted)

- Native selection on the product scenarios (the
  `sponge-alpha` and desktop-disk family) boots the shipped DE and
  apps on the native compositor. Product-topology differential against
  upstream nitpicker, plus churn/soak in the product topology.
- The 2026-11-19 default flip is not evidence for this gate: it covers
  the run-scenario selection seam only (D16C.8), and no product-topology
  run has been executed or claimed on the native compositor.
  `SPONGE_COMPOSITOR=upstream` keeps the comparison path explicit.
- Gate: product scenarios green under default-native selection with
  receipts. User approval, then G5.

### G5, Security, fuzz, session isolation, quota, churn (uncompleted)

- Capability least-privilege audit (D16C.11) against the real init
  configs of every gate topology.
- Deterministic fuzz of the command reducer and the session-argument
  surface (malformed geometry, buffer sizes, view ids) in
  `run/sponge-compositor-fuzz.run`.
- Session isolation probes: one client cannot address another client's
  views, buffers, or input, and input never leaks across sessions.
- Quota enforcement: per-client dataspace/buffer caps hold under
  churn, and the existing churn-regression family stays green with the
  native compositor (default selection since 2026-11-19).
- Gate: no isolation breach, no unbounded quota growth, fuzz corpus
  clean, churn regressions green. User approval, then the G6 record
  below (already superseded as a flip gate).

### G6, Default flip (sequencing superseded; flip landed 2026-11-19)

- The original sequencing kept the default on upstream nitpicker until
  a separate, explicit user approval on top of green G5. The user gave
  that approval on 2026-11-19 after the green G2 receipts, so the flip
  landed earlier than the G6 slot (section 2).
- The flip is its own change with default/fallback receipts in the
  evidence index, and the nitpicker path remains reachable through
  `SPONGE_COMPOSITOR=upstream` (AGENTS.md §1.1: automation is the
  default, control stays a door that is always open).
- The flip is a selection change only. It proves no G3-G5 result and
  no WM/DE or product integration.

## 8. Verification Contract

All native-path runs are on seL4 QEMU unless named otherwise. A
failing or flaky gate is recorded as an honest partial and never
relabelled green (the Criterion-16 honesty rule applies unchanged).

1. **Golden images.** Fixed scenes captured through the Capture path
   and stored with the gate receipts. Opaque scenes compare
   byte-exact.
2. **Upstream nitpicker differential.** Identical scripted geometry,
   content, and input run against upstream nitpicker and the native
   compositor; captured screens compared. Opaque regions match
   exactly. Any allowed divergence is enumerated per gate and approved
   together with it. This differential is the oracle; there is no
   second scene model (D16C.10).
3. **`nm -u` audit** (§6 rule 4) at every gate and after every Mojo
   SDK bump.
4. **Build.** `tool/build` (or the standard Genode build) green after
   every gate.
5. **Anchors and sweep.** The upstream anchors and the existing
   regression sweep stay green at every gate.
6. **No-new-compositor-ledger-row check.** `docs/11-environment.md` §4
   stays at the post-F7 baseline of 41 rows; no compositor-specific row is
   added (D16C.7).
7. **seL4 QEMU is the receipt.** base-linux may serve as a fast dev
   path but never counts as proof. The probe basis and the gates are
   seL4.
8. **QMP-driven interaction** wherever input is involved, following
   the Criterion-16 battery discipline.

## 9. Doc and Evidence Obligations

- **Evidence index** `docs/evidence/phase16-compositor-index.md`,
  created at gate execution (G1 onward), not at registration
  (AGENTS.md §5.6). Curated receipts only; raw per-run logs stay
  transient and are not promoted by default.
- **Cited basis:**
  [`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md)
  carries the FFI/build probe results this plan stands on (seL4 pure
  arithmetic `sum=42`; seL4 `List[Int64]` with the `Genode::Heap` KGEN
  shim `add=42 list-sum=45`). This plan adds no new probe claims.
- **Doc sync** at gate close-outs (AGENTS.md §5.4):
  `docs/03-architecture.md` and `docs/17-runtime-component-topology.md`
  once the native path exists in any topology; `docs/11-environment.md`
  for submodule pins and SDK-bump `nm -u` receipts;
  `docs/08-development.md` for the Mojo build steps; the AGENTS.md §2
  repo map after G0; README only when behavior actually ships.
- **Roadmap honesty.** `docs/09-roadmap.md` is untouched by this
  registration and gains text only in the same commit as real
  receipts.
- **Ledger check** recorded per gate: zero new compositor-specific rows in
  `docs/11-environment.md` §4; the pre-compositor F7 rows #39–#41 remain a
  separate baseline (D16C.7).

## 10. Commit and Execution Policy

- One logical change equals one commit (AGENTS.md §4.2), conventional
  commit messages (AGENTS.md §4.3) with scope by repo (`compositor`,
  `wm`, `de`, `tool`, `docs`).
- This registration lands as one docs commit carrying only this plan.
  No code, no source moves, and no submodule pins ride along with it.
- G0's mechanical moves are their own commits. Each states "no
  behavior change" and is verified by the sweep before the next lands.
- Work order is G0 to G6, strictly staged (D16C.12); the 2026-11-19
  approval superseded only the default-flip timing (G6), and G3-G5
  remain strictly staged. No gate starts on unapproved receipts, and
  no gate's result is rounded up.
- Build and scenario verification after every gate (AGENTS.md §4.2).

## 11. Open Questions (recorded, not blocking)

- The eventual home of vct, `sponge_configd`, `sponge_pkgd`,
  `sponge_themed`, and the shared libs. They stay in the umbrella's
  `repos/sponge/` for now (D16C.2).
- When the single entrypoint should split into multiple components,
  and what the criterion for that split is (D16C.11).
- Widening the in-OS Mojo subset (async and friends) and what an
  `nm -u` re-baseline would require (D16C.6).
- Real-hardware bring-up for the native compositor, to be registered
  as its own effort.
- Mojo runtime coverage beyond the two probe shapes. Any new shape the
  compositor depends on (SIMD buffers in the painter, for example) is
  probe-verified first, in the same style as
  `../evidence/mojo-ffi-probe.md`.
