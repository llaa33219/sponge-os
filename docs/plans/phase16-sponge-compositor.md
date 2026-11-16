# Phase 16, Sponge Compositor and Repository Split (Work Plan)

> Status: G0 repository split implemented 2026-11-16; close-out
> verification is in progress. G1-G6 compositor implementation has not
> started, and no seL4 compositor success is claimed. This remains the
> decision-complete spec for the Mojo-first Sponge compositor and the
> repository split.
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
gated, and the shipped default stays on upstream nitpicker until the
user separately approves a flip.

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
  is proven, and this plan claims no compositor success on seL4 or
  anywhere else.
- **Repository state.** At plan registration there was one Sponge repo
  at `repos/sponge/` plus the pinned `genode/` submodule. G0 has now
  added the pinned `sponge-wm`, `sponge-de`, and `sponge-compositor`
  submodules and moved their designated sources without changing the
  desktop topology; `sponge-compositor` remains a warning-only
  scaffold. The patch ledger (`docs/11-environment.md` §4) still has
  38 rows and must not grow. The Mojo SDK is pinned by `uv.lock`
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
| **D16C.7** | **Public Genode APIs only.** Public Genode interfaces and public IPC surfaces only. No private gems headers, no new vendored patches to `genode/`, zero new `docs/11-environment.md` §4 ledger rows. If patching the fork tree becomes tempting, work stops and comes back to the user (D16.8 discipline). | AGENTS.md §5.2 and D16.8. Replacing nitpicker must not recreate the private-dependence problem the native WM exists to escape. |
| **D16C.8** | **Fallback and opt-in.** Upstream nitpicker stays the shipped compositor for every existing scenario and every shipped medium. The native compositor is selected only through the opt-in `SPONGE_COMPOSITOR=native`, which swaps the Tier-0 node in the run topology. While the native path exists, the nitpicker path stays reachable as an explicit override. | Recorded approval 6. The fallback is simultaneously the regression net and the always-open control door (AGENTS.md §1.1). |
| **D16C.9** | **First vertical slice.** The first delivered compositor slice is two opaque views + clipping + Capture on seL4 QEMU (G1). Nothing beyond that slice is claimed before its gate receipts exist. | Recorded approval 7. Small honest slices beat a broad unproven claim (AGENTS.md §1.4). |
| **D16C.10** | **Validation oracle.** Correctness is validated by golden images and by differential comparison against upstream nitpicker under identical scripted geometry, content, and input. A second scene model in C++ (or any other language) as a reference implementation is explicitly rejected. | Recorded approval 8. A second model drifts from the first and doubles the maintenance, while nitpicker is the behavior clients already expect. |
| **D16C.11** | **Least privilege and single entrypoint.** Every session request is minimum-privilege (AGENTS.md §1.2): the compositor consumes only what it needs (Framebuffer, Input, Report, ROM, Timer as each gate requires) and provides only the Gui/Event/Capture session surface. The initial design is one component with one `Component::construct` entrypoint; internal modules (scene, input, painter) keep split boundaries loose the way AGENTS.md §3.4 requires for the DE, and a later component split is an open question, not a promise. | Recorded approval 12. A compositor core is one responsibility area, and premature component splits would complicate the first slices without a security gain. |
| **D16C.12** | **Staged approval gates.** G0 through G5 proceed one gate at a time: a gate's work starts only after the previous gate's receipts are approved by the user. G6 (default flip) requires a separate, explicit user approval on top of a green G5. This plan cannot flip the default on its own. | Recorded approval 9. The flip changes what every user boots, so it gets its own decision. |
| **D16C.13** | **Incremental C++ migration later.** The existing C++ WM trio and the Qt6 DE migrate to Mojo later and incrementally, per component and per slice. This plan does not rewrite them, and no big-bang rewrite is scheduled. | Recorded approval 11. The delivered C++ stack is the proven baseline and the fallback; it is replaced piecewise only. |
| **D16C.14** | **Scenario naming.** New scenarios use `run/sponge-compositor*.run` names. Pinned names: `run/sponge-compositor-opaque.run` (G1), `run/sponge-compositor-input.run` (G2), `run/sponge-compositor-c16.run` (G3 battery on native), `run/sponge-compositor-diff.run` (differential/golden), `run/sponge-compositor-fuzz.run` (G5). No existing `run/` scenario is renamed or repurposed; the upstream anchors keep their names. | Same name-separation discipline as D16W.8: the native gates and the fallback anchors must stay independently runnable. |

## 5. Non-Goals (explicitly out of this plan)

- **Claiming delivered compositor functionality.** Nothing is marked
  complete at registration, and no seL4 compositor success is claimed
  anywhere in this document.
- **A big-bang C++ to Mojo rewrite** of the WM or the DE (D16C.13).
- **A second scene model** in C++ or any other language as a
  validation oracle (D16C.10).
- **Python interop, async, POSIX I/O** in the in-OS Mojo subset
  (D16C.6).
- **A libc dependency** in emitted Mojo objects (D16C.4).
- **New vendored patches** or new `docs/11-environment.md` §4 ledger
  rows (D16C.7).
- **Flipping the default topology** to the native compositor. That is
  G6 and needs separate user approval (D16C.12).
- **Translucent blending and visual effects.** Alpha and input masks
  are consumed for hit testing from G2 on; blending translucent views
  is not designed here (§11).
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
existing regression sweep green, and every gate keeps the
no-new-ledger-row check true.

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
- Register the pins in `docs/11-environment.md`; the patch ledger row
  count stays at 38.
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
- Golden image of the scene recorded; differential against upstream
  nitpicker for the same scene per §8; `nm -u` audit green.
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
  behavior matches upstream nitpicker in the differential. User
  approval, then G3.

### G3, Full Criterion-16 battery

- The complete Criterion-16 acceptance battery
  (`docs/plans/phase16-sponge-wm.md` §7: move, 8-zone resize, focus,
  close, minimize/restore with focus, maximize/restore, tiling
  open/close/mode/reflow, and the >=20-cycle rules-drift soak) runs
  green end to end with `SPONGE_COMPOSITOR=native`, the delivered
  Sponge WM trio sitting above the native compositor.
- The upstream anchors `run/sponge-wm{,-qmp,-tasks}.run` stay green on
  upstream nitpicker. Golden-image and differential coverage grows to
  the decorated scenes.
- Gate: the full battery green (any gap recorded as an honest partial,
  never relabeled) and the anchors green. User approval, then G4.

### G4, Product integration

- Opt-in `SPONGE_COMPOSITOR=native` on the product scenarios (the
  `sponge-alpha` and desktop-disk family) boots the shipped DE and
  apps on the native compositor. Product-topology differential against
  upstream nitpicker, plus churn/soak in the product topology.
- Shipped media and default runs keep nitpicker (D16C.8).
- Gate: product scenarios green under the opt-in with receipts. User
  approval, then G5.

### G5, Security, fuzz, session isolation, quota, churn

- Capability least-privilege audit (D16C.11) against the real init
  configs of every gate topology.
- Deterministic fuzz of the command reducer and the session-argument
  surface (malformed geometry, buffer sizes, view ids) in
  `run/sponge-compositor-fuzz.run`.
- Session isolation probes: one client cannot address another client's
  views, buffers, or input, and input never leaks across sessions.
- Quota enforcement: per-client dataspace/buffer caps hold under
  churn, and the existing churn-regression family stays green with the
  native compositor in the opt-in.
- Gate: no isolation breach, no unbounded quota growth, fuzz corpus
  clean, churn regressions green. User approval, then G6
  consideration.

### G6, Default flip (separate user approval)

- Even with G0-G5 green, the shipped default stays upstream nitpicker
  until the user gives a separate, explicit approval to make the
  native compositor the default.
- If approved, the flip lands as its own commit with full receipts,
  and the nitpicker path remains reachable through an explicit
  override (AGENTS.md §1.1: automation is the default, control stays a
  door that is always open).
- This plan does not flip the default.

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
6. **No-new-ledger-row check.** `docs/11-environment.md` §4 row count
   unchanged (38).
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
- **Ledger check** recorded per gate: zero new rows in
  `docs/11-environment.md` §4 (D16C.7).

## 10. Commit and Execution Policy

- One logical change equals one commit (AGENTS.md §4.2), conventional
  commit messages (AGENTS.md §4.3) with scope by repo (`compositor`,
  `wm`, `de`, `tool`, `docs`).
- This registration lands as one docs commit carrying only this plan.
  No code, no source moves, and no submodule pins ride along with it.
- G0's mechanical moves are their own commits. Each states "no
  behavior change" and is verified by the sweep before the next lands.
- Work order is G0 to G6, strictly staged (D16C.12). No gate starts
  on unapproved receipts, and no gate's result is rounded up.
- Build and scenario verification after every gate (AGENTS.md §4.2).

## 11. Open Questions (recorded, not blocking)

- The eventual home of vct, `sponge_configd`, `sponge_pkgd`,
  `sponge_themed`, and the shared libs. They stay in the umbrella's
  `repos/sponge/` for now (D16C.2).
- Translucent blending. Hit testing consumes alpha and input masks
  from G2, but blending translucent views is not designed here.
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
