# Runtime Component Topology — the 0.2.0 product media

> Verified against `run/sponge-desktop-disk-uefi-usb.run` (17:35 image
> lineage). This is the inventory of every component above the seL4
> kernel, what it does, and — critically — which shared brokers the
> "isolated" components actually depend on. Written after the 2026-10-04
> real-hardware regression (vct_tty's missing `<provides>` froze the
> whole desktop): the isolation model held, but liveness did not, and
> the difference is the point of this document.

## 1. The tree

```
seL4 kernel
└─ core            (root server: caps, PDs, threads, IO port/memory)
   └─ init         (root task: quota broker + session router)
      │
      │ ── hardware / plumbing (all Tier-0 boot modules) ──
      ├─ timer          periodic timeouts
      ├─ acpi           ACPI table reports
      ├─ pci_decode     PCI device inventory report
      ├─ platform       platform-driver service
      ├─ usb            pc_usb_host — the xHCI controller driver
      │                 (DDE Linux; claims the physical controller)
      ├─ usb_block      USB mass-storage class driver → the boot stick
      ├─ usb_hid        USB HID class driver → the mouse (Input svc)
      ├─ ps2            PS/2 keyboard/mouse (Input svc)
      ├─ event_filter   consumes Input, applies rules, provides Event
      │                 → routes Event to nitpicker
      ├─ part_block     GPT partitioning of the stick
      ├─ vfs + rump     ext2 on P3 (GENODE partition) → File_system
      ├─ rom_sys        cached_fs_rom chroot /system  → ROM (binaries,
      │                 configs of every system child)
      ├─ rom_lib        cached_fs_rom chroot /system/lib → ROM (.lib.so)
      ├─ fb             boot_fb — GOP framebuffer + capture buffer
      ├─ nitpicker      the GUI server: framebuffer owner, domains,
      │                 input focus, compositing
      ├─ pointer        draws the mouse cursor (Gui client of
      │                 nitpicker's pointer domain); emits the hover
      │                 Report that the layouter's hover-state
      │                 re-evaluation consumes
      ├─ report_rom     top-level Report/ROM relay (window_list, focus,
      │                 hover, … — the whole wm-stack nervous system)
      │
      └─ system        (nested init #1 — caps 280000, RAM = the remainder)
         ├─ report_rom   #2 — internal channels (request/result,
         │               launcher_request/result, installed, rules, …)
         ├─ wm           window manager: virtualizes Gui for apps
         ├─ layouter     window_layouter: window placement (rules ROM)
         ├─ decorator    window chrome renderer
         ├─ sponge_configd   key/value config backend (persistent store)
         ├─ sponge_themed    theme server
         ├─ sponge_pkgd      package backend; regenerates pkg_runtime
         ├─ vct_tty          Terminal bridge for the in-terminal vct
         ├─ sponge-de        the desktop (panel, launcher, tasklist,
         │                   background, settings, shortcuts)
         ├─ alpha_probe / cfg_probe   boot-time verifiers
         └─ pkg_runtime  (nested init #2 — caps 230000, RAM = the remainder)
            └─ hello, calculator, pdf_view, falkon, terminal, …
```

Both RAM grants read "the remainder", not a fixed number — see §6.1.
Caps stay fixed and hand-derived. The `caps` figures above are the
**uefi-usb** values, which are the largest of the five desktop-disk
variants (they carry `vct_tty`). The two plain/ahci and nvme disk
variants that omit `vct_tty` grant `system` 263000 caps. Per-scenario
numbers are in §6.

## 2. The three data chains

**Storage** (how anything loads at all):
`xHCI → pc_usb_host → usb_block → part_block → vfs/rump →
cached_fs_rom (rom_sys, rom_lib) → every system child's binary/config`
One chain, shared by all of `system`. A stall here starves every
system child — this is the boot-critical path.

**Input** (how the mouse reaches the screen):
`USB mouse → pc_usb_host → usb_hid → event_filter → nitpicker →
(pointer domain) → pointer component → cursor redraw`
Entirely top-level. **No node of this chain lives inside `system`.**

**Graphics** (how pixels reach the panel):
`sponge-de / apps → wm → nitpicker → fb → GOP framebuffer`
Apps are three broker hops from the hardware.

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
   arithmetic, not fault isolation. The same arithmetic bit
   `pkg_runtime`: as a nested init it draws its own 230000 caps plus
   the RAM remainder, which must cover the SUM of every
   simultaneously-enabled package (§6).
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
[`docs/evidence/sponge-debug-scrollstorm.md`](evidence/sponge-debug-scrollstorm.md).

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
[`docs/evidence/sponge-debug-scrollstorm.md`](evidence/sponge-debug-scrollstorm.md)
§8 for the before/after numbers. The benefit is now **measured, not just
correct-by-construction**: because a QEMU/softpipe client is normally
*slower* than the compositor and would never form a backlog on its own,
the harness forces the condition with a dedicated arm, and coalescing is
observed engaging (800 submit attempts collapse to 82 frame submissions
in 30 s) with storm throughput up 11 → 19 Hz and no liveness regression.
What remains open is the real-hardware magnitude at 2560x1600, not the
mechanism — see §5.

## 5. Design gap register (what "microkernel-proper" would add)

The user's intuition is correct: properly deployed, a terminal
component must never be able to freeze the desktop. Status after the
2026-10-04 containment deployment and the 2026-10 quota-sizing fix
(§6):

- [x] **Repaint back-pressure — FIXED by frame coalescing (ledger
  row #23), measured 2026-10-16**: a component that repaints faster
  than the compositor can drain builds an unbounded backlog.
  `Framebuffer::Session` already offers `sync_sigh`/`sync_source`
  (`genode/repos/os/include/framebuffer_session/framebuffer_session.h:218-231`)
  and nitpicker already implements the server side
  (`genode/repos/os/src/server/nitpicker/main.cc:537`), but the Qt6 QPA
  plugin never registered them, so the client never learned how far
  behind the compositor it was. Fixed by
  `docs/patches/qt6-qpa-frame-sync.patch`, applied by
  `genode/repos/libports/src/qt6/base/target.mk` (row #23 in
  `docs/11-environment.md` §4).

  **The mechanism is coalescing, not waiting.** At most one frame is in
  flight; while a frame is unconsumed a submit does not blit and does
  **not** wait — it merges its damage into the pending region and
  returns — and the acknowledged frame performs one coalesced submit.
  There is no sleep, poll loop or `usleep` on the Qt thread anywhere in
  the patch. This matters: an earlier revision waited (bounded,
  50 x 5 ms) at the frame boundary, and making that wait real collapsed
  the repaint storm from 310 ticks / 10 Hz (max gap 140 ms) to 13 ticks
  / 0.4 Hz (max gap **2657 ms**) — a 24x regression, because `flush()` is
  called several times per painted frame and because blocking the Qt
  thread starves the input processing that must keep running
  (`docs/evidence/sponge-debug-firstpaint.md` §4). That revision was
  reverted and is not to be reintroduced.

  Measured on `run/sponge-debug-scrollstorm.run`, A/B per arm
  (`docs/evidence/sponge-debug-scrollstorm.md` §8.2): on the forced
  back-pressure arm, 800 submit attempts in 30 s collapse to **82 actual
  frame submissions** (81 coalesced), max tick gap **172 ms** (vs 2657 ms
  for the rejected wait), per-child ΔRAM **0**, and storm throughput
  rises from 347 to 582 ticks (11 → 19 Hz). The `baseline` arm logs
  exactly **one** coalesced frame in a whole run — the control proving
  the self-gating telemetry distinguishes "worked" from "never needed",
  and that first paint is never deferred. Non-regression: `sponge-de-test`,
  `sponge-launch`, `sponge-de-sel4-interactive`, `sponge-desktop-disk`
  all PASS.
- [ ] **Field freeze: NOT reproduced at the panel resolution, and the
  mechanism that would produce it is named and measured (2026-10-17)**.
  The field report is a fast scrollbar drag on a resizeable window on the
  2560x1600 panel, after which the panel reads `⚠ sponge_pkgd
  unresponsive(N)` and the whole system freezes **including the mouse**.
  `run/sponge-debug-scrollstorm.run` now boots the field's two defining
  parameters (panel resolution + window geometry) and measures:

  * **a correctly-sized panel configuration is healthy** — 19 Hz with a
    387 ms worst event-loop gap and **0** kernel page-table flushes, on a
    *single* QEMU vCPU. The freeze does not reproduce there;
  * a **capability quota too small for the panel-resolution buffer**
    saturates base-sel4's PD mapping cache, and the kernel then falls
    back to `flush page table entries` **hundreds of times per minute**
    under a repaint storm (567 vs 25 idle at the same resolution). That
    is a kernel-wide cost, which is exactly why an idle `pkgd` and the
    input path (§2: no node of the mouse chain lives inside `system`)
    fall with it. RED → GREEN on this scenario: **3 Hz / 1379 ms /
    532 flushes → 19 Hz / 387 ms / 0 flushes**;
  * ruled out **with numbers**, at the field resolution: a RAM/caps leak
    (delta_max 0), the client's own rasterisation (a full-panel client
    window on a 1024x768 framebuffer is healthy at 16 Hz), the
    compositor's poll rate (5x slower driver: no change), and blocked
    entrypoints (the heartbeat ranking does not move).

  The shipped media is already sized correctly (`fb | caps: 24000`,
  `nitpicker | caps: 20000`; `run/sponge-desktop-disk-uefi*.run`), so
  **the field cause is still open**, and the discriminating test is one
  grep on the real console — `grep -c 'mapping cache full'` during the
  drag versus idle. Zero throughout ⇒ a different cause, and the next
  candidates are the two rows below. Evidence, receipts and the full
  bisection: [`docs/evidence/sponge-debug-panelfreeze.md`](evidence/sponge-debug-panelfreeze.md).
- [ ] **Respawn budget and clamp leak — open (found 2026-10-16, NOT
  the field regression)**: `<heartbeat restart_after_skipped="3"/>` has
  no attempt budget and no backoff, so a child that cannot start is
  respawned forever, and with `ram: 512G` each cycle leaks part of the
  clamped grant (~35 KB/cycle measured). Reproduced only under a
  `usb_block` storage stall in `run/sponge-desktop-disk-uefi-usb`, where
  `report_rom` aborts with
  `Attached_dataspace::Region_conflict` and every ROM-dependent child
  loses its config source; a healthy boot of the same scenario shows
  zero of it. Two changes are wanted — an init respawn budget/backoff
  and a leak-free clamped grant — and neither is a first-paint fix. The
  `report_rom` abort itself is not root-caused; the obvious candidate
  (`rom_module.h` re-allocating a module backing store) was checked and
  is **not** a leak, because `Genode::Constructible` inherits
  `Reconstructible::construct()`, which destructs first. Evidence:
  `docs/evidence/sponge-debug-firstpaint.md` §5.
- [ ] **Real-hardware confirmation of row #23 — open (the only open
  half)**: the coalescing mechanism is built and measured on QEMU
  (§ above), but the magnitude at 2560x1600 on the LG gram 17ZD90N —
  where the field report originated — is not. Measure the achieved
  repaint rate and the event-loop latency of a fast scrollbar drag with
  and without the patch on that machine, so the row's cost/benefit side
  is closed on the hardware the symptom came from.
- [x] **Broker fault containment — system level (deployed)**: the
  five desktop-disk product scenarios' system init runs
  `<heartbeat rate_ms="2000"/>` with
  `<heartbeat restart_after_skipped="3"/>` on the critical
  children (wm, layouter, decorator, sponge_configd, sponge_themed,
  sponge_pkgd, vct_tty, sponge-de, pkg_runtime). A child whose
  entrypoint wedges is abandoned and respawned within ~6 s. Verified:
  the product scenario boots PASS with the deployment in place.
- [x] **Broker fault containment — top level (closed)**: the top-level
  init still cannot run a heartbeat — core provides no Timer on
  base-sel4, the session is denied and init stops (the constraint is
  unchanged and still true) — but nothing above the sandbox needs
  init's heartbeat any more. `sponge_supervisor` is a plain Tier-0
  component that holds a Timer of its own, OWNS the `system`
  sandbox's config through `report_rom` (the pkgd pattern applied one
  level up), and READS that sandbox's own `state` report through the
  **top-level** relay, never through a relay inside the subtree it
  supervises. On a rising `skipped_heartbeats` it rewrites the config
  with a fresh `<start>` version — the only restart lever a config
  owner has (`Start_model::matches()` compares only `name` and
  `version`) — which abandons and respawns the subtree; if the fault
  survives the grace window it arms `sponge_console`, which takes the
  display as the only Gui client, and hands it back a bounded hold
  later. QEMU red→green with a pixel oracle:
  `run/sponge-supervisor-takeover.run` →
  `supervisor-takeover: PASS - wedge detected, subtree killed, console
  owned the display, desktop restored`, plus a control run that
  asserts a healthy system is never touched. Receipts, the measured
  timings, and the fourteen defects found on the way:
  `docs/evidence/top-level-supervision.md`.
- [ ] **A wedged `system` *init* is still unobservable** (the residual
  gap of the row above): the supervisor detects wedged *children*
  (rising `skipped_heartbeats`), children that have exited, and a
  sandbox that publishes nothing at all within its 30 s boot budget. It
  cannot detect a sandbox whose own init stopped publishing, because a
  Genode state report is **event-driven** — a healthy sandbox publishes
  nothing, so report age cannot distinguish quiet from dead. Measured,
  not assumed: the control run produced exactly that false positive
  before the detector was rewritten. Closing it needs a periodic
  report mechanism upstream, or a monitor that forces a report on every
  heartbeat tick; both are upstream questions, not configuration.
- [ ] **`sponge_supervisor` is not yet wired into the six product
  scenarios.** v1 proves the mechanism in a lean topology
  (`run/sponge-supervisor-takeover.run`). The product rollout means
  giving the top-level `report_rom` the extra policies and moving the
  `system` config from a boot module to a supervisor-owned report on
  real media — a separate change with its own regression budget.
- [ ] **Broker fault containment — `run/sponge-alpha.run` (blocked by
  topology)**: alpha is a FLAT topology. `wm`, `layouter`,
  `decorator`, `sponge_configd`, `sponge_themed`, `sponge_pkgd`,
  `sponge-de` and `pkg_runtime` are direct children of the
  TOP-LEVEL init; its only nested init (`drivers`) hosts the hardware
  set, not the desktop. A heartbeat is served by the sandbox that
  hosts the children, so it can only be deployed on a non-top-level
  init — and the top-level one has no Timer (row above). Deploying
  containment here therefore requires restructuring alpha into the
  nested-`system` shape the disk scenarios already use, which is a
  topology change and would invalidate alpha's `alpha-probe: PASS`
  evidence. Not done; the rationale is recorded in the scenario file
  above its top-level init configuration.
- [ ] **Broker hardening**: the config-less-child ep abort is an
  upstream-adjacent fragility (workaround: every child ships an
  explicit — possibly empty — config node; vct_tty's fix). The
  containment above now bounds its blast radius to the child.
- [x] **Config-regeneration kill-all — tested, falsified (no patch)**:
  the hypothesis that a pkgd runtime-config regeneration crossing the
  `Expanding_reporter` buffer boundary transiently invalidates
  pkg_runtime's config ROM and makes init abandon every running child
  was tested headlessly on base-sel4/QEMU
  (`run/sponge-config-regen-survival.run`, forced growth on every
  write): children survive. `report_rom`'s `Module::_unregister`
  zeroes the module content on writer close but never notifies
  readers, so pkg_runtime keeps the last valid config. A control run
  (empty-but-valid config) confirms the detector fires when children
  really die. No init guard patch was added — the problem class does
  not exist. Evidence: `docs/evidence/config-regen-survival.md`.
- [x] **Resource headroom policy**: every `system` and `pkg_runtime`
  caps grant across the five desktop-disk product scenarios carries
  ~10% headroom over its own children sum (§6). The RAM side is no
  longer a hand-derived figure at all: both grants are **remainder
  clamps** (see §6.1), so the headroom is whatever the machine has
  left after the fixed services — 4521 MiB for `pkg_runtime` at
  `-m 6G`, 2473 MiB at `-m 4G`, same unchanged file. Headroom is
  therefore no longer constant, and on a small box it can be thin:
  the refusal path for a package that no longer fits is
  `sponge_pkgd`'s admission gate, not a silent clamp.
- [x] **Remainder-clamp RAM grants — deployed, measured**
  (§6.1, `docs/evidence/remainder-clamp-ram.md`): `system` and
  `pkg_runtime` declare `ram: 512G`, a deliberately huge quantum that
  Genode's init clamps to the parent's remaining availability at
  creation time (`genode/repos/os/src/lib/sandbox/child.cc:481-486`).
  The run scripts are consequently physical-RAM-size agnostic — one
  file boots `-m 4G`, `-m 6G` and a 16 GiB laptop. Two conditions
  make it sound, and both are asserted in every run file:
  the clamped child must be the **last** `+ start` node in its config
  (else a later child eats out of its pool), and **caps must stay
  explicit** — caps exhaustion on seL4 *hangs* rather than failing,
  and a zero-sum cap remainder would starve any future sibling.
- [x] **Admission control — an over-budget package is refused, not
  clamped** (`sponge_pkgd`): `launch` (and `install`, for an
  `<autostart/>` package) compares the declared `<quota ram= caps=>`
  against `pkg_runtime`'s own reported remaining budget and answers
  `insufficient-resources` with the four figures, BEFORE the package
  enters the running set and BEFORE the config is regenerated. The
  budget comes from the `pkg_runtime -> state` sandbox report, not from
  a configured copy of the grant, so it is always the real remaining
  pool. Wired in all five desktop-disk product scenarios; runtime-
  verified on `sponge-desktop-disk` (`resource admission armed` and
  `alpha-probe: PASS` in the same boot). On
  `sponge-desktop-disk-nvme` the gate is wired but NOT yet reachable:
  pkgd stops earlier on a pre-existing `bake_manifest` denial (§5 row
  below). Receipts: `run/sponge-pkg-admission.run`,
  `docs/evidence/pkg-admission-control.md`.
- [x] **Dynamic quota top-up — measured, not assumed**
  (`docs/evidence/dynamic-resource-topup.md`,
  `run/sponge-resource-request.run`): Genode 26.08's `init` answers a
  child's `resource_request()` immediately **out of its own sandbox's
  remaining slack**, and otherwise parks it. There is **no upward
  forwarding**: a parked request stays parked however much memory sits
  above, because the top-level init has nobody to ask (core never
  satisfies an init resource request — `repos/os/src/init/main.cc`
  registers a `resource_avail` handler precisely to stop init blocking
  on one). The only revive path is a **configuration change applied to
  the hosting sandbox by its owner** — which for `pkg_runtime` is
  `sponge_pkgd`. No `init` patch is needed or wanted. The consequence
  for the gap register: a package's declared quota is a **ceiling for
  the life of the process**, not a starting point, which is why the
  admission margin above is not zero — the runtime backstop can only
  hand out what admission has not already promised to someone else.
- [~] **Admission control not armed in `run/sponge-alpha.run`** (blocked
  by a pre-existing gap): alpha's `pkg_runtime` is still granted
  400 caps / 32M, sized for the hello-only Phase-7 topology rather than
  the eight-package `desktop` profile it stages (§6 "Residual gap").
  Arming the gate there would refuse every baked package, converting
  alpha's current silent clamp into eight visible refusals and
  regressing its own `alpha-probe: PASS` criterion. The relay lands
  together with that quota resize, which is deliberately separate work
  (it would also have to grow alpha's 2G QEMU envelope and re-earn the
  PASS evidence).
- [~] **`sponge-desktop-disk-nvme` stops before admission is reached**
  (pre-existing, found while wiring the gate in): that scenario's
  `sponge_pkgd` config carries `<bake/>` and a `bake_manifest` ROM
  route, but the scenario never stages `bake_manifest.json` — it never
  calls `bake::stage`, unlike `sponge-desktop-disk.run:359`. A denied
  ROM session is `sleep_forever()`, so pkgd stops in `_init_bake()`
  (`main.cc:3068`), which runs *before* `_init_admission()`
  (`main.cc:3084`). Verified present at `171b50700f`, i.e. before the
  admission work, so it is not a regression from it; the gate itself is
  correctly wired and does arm once that gap is closed. Removing
  `<bake/>` was tried and reverted: it was added deliberately by
  `c68f95d415` (2026-09-22), after the scenario's own "INTENTIONALLY
  OMITTED" comment (`113073260e`, 2026-09-20). A second, independent
  pre-existing gap sits behind it — the scenario stages only
  `pkg_hello` + `sponge-de` while Phase-16 W3 extended `alpha_probe` to
  install seven packages (`install terminal returned: error`). Receipts:
  `docs/evidence/pkg-admission-control.md` §8.
- [~] **Containment regression test**: `test/ep_wedge` (wedge +
  witness modes) and `run/sponge-heartbeat-restart.run` are written
  and the component builds, but the scenario is blocked by a
  PRE-EXISTING environment regression: flat ISO boots deny the HPET
  IO_MEM (`sponge-pkg-explain` fails identically), so no flat
  scenario can run a timer on this host today. The disk/UEFI product
  path is unaffected. Revisit when the flat-boot HPET issue is
  root-caused (QEMU/seaBIOS ACPI exposure).
- [~] **Persistent log on the writable partition (field diagnosis) —
  PARTIAL, opt-in**: `repos/sponge/src/sponge_logdisk` is a LOG server
  that writes the system log to `/log/boot.log` on SPONGE-VAR, one file
  per boot with the previous boot preserved at `/log/boot.log.1`, capped
  at 8 MiB, flushed per message. It is wired into
  `run/sponge-desktop-disk-uefi-usb.run` behind `SPONGE_LOGDISK=on`
  (**off by default**, see the coverage and coupling arguments in that
  file and `docs/evidence/logdisk-field-diagnosis.md`).

  What is closed: the *mechanism* — a durable, size-capped, per-boot
  record on the writable partition that survives a hard reset, readable
  with `cat` from a terminal or with `e2cp` off the media. Two design
  findings came out of it and are documented in the component header:
  a LOG provider must not issue its `File_system` request on the thread
  that serves LOG writes (that deadlocked the boot right after `acpi`),
  and the root must be managed on a secondary entrypoint, not `env.ep()`.

  What is NOT closed: coverage, and therefore default-ON. init evaluates
  a child's OWN `<route>` for that child's session requests, so a Tier-0
  sink is only reachable from the Tier-0 children that carry no `<route>`
  node (today `report_rom`, `nitpicker`, `timer`). The nested `system`
  subtree — pkgd, configd, themed, wm, layouter, decorator, sponge-de,
  the package runtimes, i.e. exactly the components a scroll-storm freeze
  implicates — is not captured: reaching the sink from there requires the
  sink to be a child of that same sandbox, and **a component inside a
  nested sandbox cannot obtain a `File_system` from a service provided
  by a SIBLING of the outer sandbox** (measured: the session is denied,
  `docs/evidence/logdisk-field-diagnosis.md` §4). Core's own log is
  not capturable at all — core writes it to the kernel's log memory and
  the serial line; `sponge_logdisk` sits above it in userspace.

  The follow-up that closes it: move the sink INSIDE the nested
  `system` sandbox (giving it the sandbox's heartbeat containment, which
  a Tier-0 component does not have) and give that sandbox its own `vfs`
  + `part_block` over the log partition, so the sink's `File_system` is a
  sibling grant like every other service inside the sandbox. Only then
  does default-ON make sense. Core's log and the serial line remain the
  fallback for the kernel phase.

- [~] **Problem-triggered console switch (mechanism delivered, log tail
  outstanding)**: the framebuffer has exactly one owner at a time, and
  that property is now *enforced* rather than *convention*: the
  supervisor destroys the wedged subtree first, arms `sponge_console`
  second, and waits `settle_ms` for the console's Gui session to
  actually close before it rebuilds the desktop — so there is no
  instant at which both are clients
  (`run/sponge-supervisor-takeover.run`, phase 5). The handover is
  over the **display**, not over the fb device: the compositor keeps
  the fb with exactly one writer (`vesa_fb`/`boot_fb`, a Capture
  client) and the console is simply the only Gui client while it runs.
  Seizing the device instead would mean two components mapping the same
  physical memory — the permanent corruption `docs/11-environment.md`
  row 12 measured on real hardware, where `boot_fb`'s Capture blit
  repaints only the compositor's dirty rectangles and stale console
  text is never erased.
  **Outstanding:** the console shows the *state* the supervisor knows
  (the fault banner), not a *log tail*. Getting the log stream to a
  component above the supervised subtree is the same relay problem in a
  different shape, and it is not done. Until then, field diagnosis of
  the log stream still relies on the panel health label and the debug
  package's report views.

## 6. Quota ledger (the five desktop-disk scenarios)

`pkg_runtime` hosts every enabled package **simultaneously**, so its
grant must cover the SUM of the package quotas declared in
`pkg/*/metadata.xml`, not one package's worth. The `desktop` bake
profile (`pkg/bake/desktop.profile`) enables eight packages:

| package | caps | RAM |
|---|---:|---:|
| hello | 256 | 8M |
| terminal | 1100 | 224M |
| textedit | 1000 | 64M |
| files | 1000 | 64M |
| calculator | 1000 | 64M |
| pdf_view | 256 | 1024M |
| falkon | 200000 | 1024M |
| debug | 1000 | 64M |
| **enabled sum** | **205612** | **2536M** |

(206512 caps counting the `nano` 600 + `ncurses` 300 pair reachable
from the editor packages.) Hence the uniform
`pkg_runtime | caps: 230000 | ram: 512G` on all five desktop-disk
scenarios: **+11.9% caps** over the enabled sum, and **RAM by the
remainder-clamp** described in §6.1 — a grant that tracks the machine
instead of a constant that has to be re-derived per `-m` value and per
package-profile change.

Before this change the four ahci/nvme desktop-disk variants granted
`pkg_runtime` 8000 caps / 1536M — falkon (200000 caps) was **25x
short** and could never launch, and the RAM sum overshot the grant by
~1 GiB. The uefi-usb variant's 210000 caps / 2048M was closer but
still ~488M under on RAM and under on caps once pdf_view and terminal
are counted. RAM then went 2816M → the clamp; caps never moved off an
explicit number.

### 6.1 The remainder clamp (RAM only)

`system` and `pkg_runtime` declare `ram: 512G`. That is **not** a claim
that the system needs 512 GiB — it is the mechanism. Genode's init
silently clamps a child's RAM quantum to the parent's remaining
availability at the moment the child is created:

```c++
/* genode/repos/os/src/lib/sandbox/child.cc:481-486 */
Ram_quota const avail_ram = Genode::Child::effective_quota(quota_limit.ram);

if (ram_quota.value > avail_ram.value) {
    warning(name(), ": configured RAM exceeds available RAM, proceed with ", avail_ram);
    ram_quota = avail_ram;
}
```

`quota_limit` is the sandbox PD's live `stats.ram.avail()` minus the
sandbox's `Preservation` reserve (`library.cc:96-108, 123`) — what is
genuinely still free at that instant. The caps path
(`child.cc:490-495`) is the identical clamp. Upstream behaviour, used
as-is; **no vendored patch**.

Two conditions make it sound:

1. **The clamped child must be the LAST `+ start` node in its config.**
   The remainder is only "everything left" if nothing is created
   afterwards. `pkg_runtime`-last came from `248afa12d6` (real-hardware
   config-delivery timing); `system`-last is already true. A per-file
   scan confirms both in all five scenarios — no reordering was
   needed.
2. **Caps stay explicit.** Caps exhaustion on seL4 *hangs* (a child
   that cannot get a capability waits forever — §11.1 of
   docs/09-roadmap.md), which is the worst failure mode a resource can
   have; and a cap grant that absorbed the whole remainder would starve
   any future sibling added to the config. RAM has no such failure
   mode, which is exactly why only RAM uses the clamp.

Measured on `run/sponge-desktop-disk`, same unchanged file, two boots:

| | `-m 6G` | `-m 4G` |
|---|---:|---:|
| init pool | 5686 MiB | 3638 MiB |
| `system` grant | 4807.1 MiB | 2759.1 MiB |
| `pkg_runtime` grant | **4521.2 MiB** | **2473.2 MiB** |
| verdict | `alpha-probe: PASS` | `alpha-probe: PASS` |

The arithmetic closes: `system − pkg_runtime` = 285.9 MiB at *both*
pool sizes = the 284 MiB of fixed system children plus ~1.9 MiB of the
nested init's own allocation. Tier-0 for this scenario totals 874 MiB,
and 5686 − 874 ≈ 4807, the observed `system` grant. Full receipts:
`docs/evidence/remainder-clamp-ram.md`.

| scenario | `system` children (caps / fixed RAM) | `system` caps grant | `pkg_runtime` caps | QEMU `-m` (ceiling) |
|---|---|---:|---:|---:|
| sponge-desktop-disk | 8650 / 284M | 263000 | 230000 | 6G |
| sponge-desktop-disk-nvme | 8650 / 284M | 263000 | 230000 | 6G |
| sponge-desktop-disk-uefi | 24100 / 284M | 280000 | 230000 | 8G |
| sponge-desktop-disk-uefi-nvme | 24100 / 284M | 280000 | 230000 | 8G |
| sponge-desktop-disk-uefi-usb | 24300 / 292M | 280000 | 230000 | 8G |

(The `system` children columns exclude `pkg_runtime` itself, whose
caps remain 230000 in every scenario; the uefi variants' larger sums
come from the 6000-cap `decorator`, 8000-cap `sponge-de` and 6000-cap
`alpha_probe`, plus uefi-usb's `vct_tty` at 200 caps / 8M.)

The `-m` column is now a **ceiling only** — it bounds the boot and
nothing has to agree with it, because the two RAM grants follow the
pool. The values are unchanged on purpose: the existing receipts were
earned at them, and changing them would invalidate that evidence for
no functional gain. The two boot paths still reserve very different
amounts of guest RAM, which is why the ceilings differ:

- **Direct-kernel (BIOS) path** — core reserves ~458 MiB for kernel +
  rootserver. Measured: `-m 6G` ⇒ "5686 MiB RAM and 523288 caps
  assigned to init"; `-m 4G` ⇒ 3638 MiB.
- **OVMF/UEFI path** — ~2.4 GiB, not ~458 MiB. Measured on
  `sponge-desktop-disk-uefi-usb`: `-m 8G` ⇒ 5826 MiB assigned to init.
  The UEFI ceiling stays at 8G: uefi-usb alone carries ~1902 MiB of
  Tier-0 (the three-area layout adds `vfs_var` 32M, `vfs_home` 32M and
  `rom_pkg` 768M for falkon's 509 MiB payload on top of the two 256M
  `rom_*` servers), so after the OVMF reservation a 6G guest would leave
  the fixed system services with almost nothing.

**Residual gap**: `run/sponge-alpha.run` still grants `pkg_runtime`
400 caps / 32M — sized for the hello-only Phase-7 topology, not for
the `desktop` profile it stages by default. Its 2G QEMU envelope
would have to grow in lockstep and its PASS evidence would have to be
re-earned, so the change is deliberately separate. The clamp is also
**not** applied there: alpha is a flat topology whose `pkg_runtime` is
a direct child of the *top-level* init, where "last created" would
mean nothing useful (the drivers sub-init and the desktop children are
siblings created in arbitrary order).
