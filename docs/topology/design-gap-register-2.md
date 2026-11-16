# Runtime component topology: design gap register (part 2)

> **Breadcrumb:** parent [17 - Runtime Component Topology](../17-runtime-component-topology.md); this file holds its §5, part 2 of 2 (the containment, supervision and policy rows), moved verbatim. Part 1 (the freeze and liveness rows) is [design-gap-register.md](design-gap-register.md).
> Siblings in `docs/topology/`: [tree-and-chains.md](tree-and-chains.md) (§1, §2, §2a), [incident-analyses.md](incident-analyses.md) (§3, §4, §4a), [quota-ledger.md](quota-ledger.md) (§6).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

- [x] **Broker fault containment — system level (deployed)**: the
  five desktop-disk product scenarios' system init runs
  `<heartbeat rate_ms="2000"/>` with
  `<heartbeat restart_after_skipped="3"/>` on the critical
  children (wm, layouter, decorator, sponge_configd, sponge_themed,
  sponge_pkgd, vct_tty, sponge-de, pkg_runtime). A child whose
  entrypoint wedges is abandoned and respawned within **10 s** — the
  skip count lags one heartbeat behind the unanswered count, so the
  kill lands on the fifth unanswered heartbeat at this rate, not the
  fourth (the "~6 s" this row used to state was wrong). Verified:
  the product scenario boots PASS with the deployment in place.
- [x] **A child that is slow to START is not a wedged child (closed
  2026-10-19, ledger row #26)**: in the classic (non-pthread) Genode,
  `Component::construct()` runs **on the entrypoint thread** — the
  very thread that has to process the heartbeat signal — so a child
  whose construct outlives the 10 s budget cannot answer a single
  heartbeat, and both the sandbox and `sponge_supervisor` above it read
  that as a wedge: abandon, respawn, kill the whole subtree, restart
  from zero. That is the LG gram's `published system kill=25`. The
  missing signal was the sticky one — *has this child ever answered
  anything?* — and it now exists: `base::Child::heartbeat_responded()`,
  a per-child `startup_grace_ms` (product media: 120 s, opt-in, 0 =
  upstream behaviour), and `heartbeat_state="starting"` in the state
  report, which the supervisor reads instead of guessing from the skip
  count (`ignore_starting`, default on). Measured: on the product media
  with a 4 MB/s boot medium, **five contained children — `sponge-de`
  among them — take 8–10 s to become responsive against that 10 s
  budget**; the margin is already gone at a bandwidth a USB 2.0 stick
  exceeds. The grace is bounded and reported: a child that never
  answers produces a "startup grace exhausted" warning and is killed
  when the window ends, and the restart budget still bounds the
  attempts. Red→green with a three-arm scenario
  (`run/sponge-startup-grace.run`) and the field's own signature
  (`kill=2`) reproduced by switching the supervisor's exemption off.
  Receipts: `docs/evidence/startup-grace.md`.
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
- [x] **`sponge_supervisor` wired into the product media (deployed
  2026-10-17)**: all five desktop-disk scenarios
  (`run/sponge-desktop-disk{,-nvme,-uefi,-uefi-nvme,-uefi-usb}.run`)
  now run the supervisor and the console. `run/supervisor.inc` is the
  shared, **asserted** wiring: it derives the supervisor's two config
  templates from the `/system/init/system.config` the scenario already
  stages (staging stays the single source of truth — the supervisor
  never carries a second copy of the desktop composition), moves the
  `system` sandbox's config ROM from `rom_sys` to the top-level
  `report_rom`, adds the console's own nitpicker domain (layer 8, above
  panel/launcher/default) and starts the supervisor + console **before**
  the sandbox, which must stay the last `<start>` node so its RAM
  remainder clamp is intact. A drifted anchor aborts the scenario
  loudly instead of booting an unsupervised desktop.

  Three product-only findings, each measured (receipts:
  [`docs/evidence/product-supervision.md`](../evidence/product-supervision.md)):

  1. **ESCALATION — the sandbox contains first.** The product sandbox
     runs `<heartbeat rate_ms="2000"/>` with
     `restart_after_skipped="3"`, so it abandons and respawns a wedged
     child itself at ~8 s. The supervisor therefore does not act on the
     first fault: it holds the fault for `grace_ms` (4 s) and re-reads
     the flag every tick. A fault the sandbox's own restart fixed costs
     one log line and nothing else. The rule is *act only when the
     supervised sandbox's own containment has failed or been
     exhausted, or when the sandbox has published nothing at all within
     `boot_grace_ms`*. No detection threshold changed.
  2. **Clean exits are excluded BY CONFIGURATION.** `alpha_probe` and
     `cfg_probe` exit 0 after doing their job, and Genode keeps an
     exited child in the state report until it is abandoned, so an
     unconditional "any exit is a fault" test tears down the desktop
     minutes after a successful boot. The `ignore_exited` policy carries
     the same pair sponge-de's health watcher excludes
     (`repos/sponge/src/sponge-de/main.cc`), so panel and supervisor
     cannot disagree about what a clean exit is.
  3. **`settle_ms` is a product parameter, not a constant.** The lean
     proof runs the sandbox heartbeat at 500 ms; the product runs it at
     2000 ms, and a respawned child needs 10–12 s to climb back over
     `fault_skipped`. With the lean 3 s settle window the supervisor
     declared the fault "contained" before the respawned child had
     reached the threshold and the console phase was structurally
     unreachable. `settle_ms = 20000` over the measured 12 s.

  Product-topology proof, env-gated and **off by default**
  (`SPONGE_SUPERVISOR_TEST`, shipped media never carries a fault
  child): `takeover` shows detect → kill → console (a **pixel** claim,
  `test/takeover_probe` reading the composited screen) → hand back;
  `control` puts the SAME fault inside the product's own
  `restart_after_skipped="3"` containment and asserts the negative — no
  kill, no console — while the desktop reaches `alpha-probe: PASS`
  underneath. The control arm is the half that matters: a supervisor
  that tears down a healthy desktop on a timer is worse than one that
  does nothing.

  4. **P3.4 — the gates and the containment are scoped per runtime
     subtree.** The runtime subtrees (`pkg_runtime*`,
     `driver_runtime`, all direct children of `system`) carry per-name
     restart markers (`@KILL:<name>@`) in the templates, and a fault
     attributed to one of them is contained BY NAME: only that child's
     `<start>` version is bumped, so the sibling runtimes and the DE
     are never rebuilt (their rendered bytes are unchanged and
     `Start_model::matches()` keeps them). Global bumps — boot,
     console, nominal restore, non-runtime faults — are unchanged, as
     is plain `@KILL@` for the standalone acceptance template. The
     publish receipt names the target (`target=<name|global>
     bumped=<n>`) and the state report's per-child construction
     versions are logged when they change (bounded), which is the
     rebuild witness. `grace_ms.<name>`/`settle_ms.<name>` policy rows
     override the escalation and CONTAIN settle per subtree (console
     and RELEASE stay global). The per-child heartbeat triplets
     (`restart_after_skipped` / `restart_budget` / `startup_grace_ms`,
     defaults 3 / 3 / 120000, byte-identical to the previous inline
     literals) are exposed per runtime child in the five product
     scenarios and `driver_runtime.inc`. The `contain` arm proves the property on
     the product topology: a persistent fault in `pkg_runtime_fault` is
     contained repeatedly with `target=pkg_runtime_fault bumped=1`,
     sibling and DE construction versions stable, panel shown once, a
     post-wedge `alpha-probe: PASS`, and no console takeover
     (`docs/evidence/phase16-runtime-p34.md`). P5.1's acceptance suite
     re-runs the containment leg green in three consecutive all-green
     passes, with `wedge: no churn` asserting zero `version-change`
     lines (`docs/evidence/phase16-runtime-acceptance.md`).

  One structural limitation, recorded honestly: a *persistent* fault
  plus a full-subtree restart lever are mutually exclusive with a fully
  booted desktop, because the supervisor keeps killing a subtree that
  needs minutes to boot. The recovery receipt in the takeover arm is
  therefore the display handover (console disarmed, `phase=1 desktop
  restored`, pixels back to the desktop background), not a second
  `alpha-probe: PASS`; the healthy path is what the five scenario
  regressions and the control arm prove. P3.4 lifts the limitation for
  runtime-subtree faults (the `contain` arm reaches its post-wedge
  `alpha-probe: PASS` because the DE is never rebuilt), and the
  **revived child's process now really restarts**. The earlier
  base-layer blocker — config-driven child recreation after a teardown
  failing with `create_thread: invalid PD argument`, which also broke
  the takeover arm's kill receipt (`docs/evidence/phase16-runtime-p34.md`
  §5 keeps the pre-fix comparison receipts) — is **fixed in place** by
  the proven P3.4 lifecycle pair at fork pin `48a946ba15`: ledger row
  #32 (`489f977822`, RPC object keys unique across incarnations) and
  row #33 (`48a946ba15`, pending sessions preserved across reissue and
  destruction). With them, the `contain` arm reaches real second and
  third victim incarnations and the product takeover reaches
  `SUPERVISOR TAKEOVER PASS` with real revivals and the console-to-
  desktop handback (receipts: `docs/evidence/phase16-runtime-capability-key-reuse.md`,
  `docs/evidence/phase16-runtime-pending-session-lifetime.md`;
  classification: `docs/evidence/patch-classification.md` — rows #32/#33
  are **keep**, retired only as a pair).
- [~] **Broker fault containment — `run/sponge-alpha.run`: evaluated
  2026-10-17, NOT APPLICABLE (deliberately unchanged)**: the supervisor
  was evaluated against alpha as part of the product rollout and does not
  apply. The supervisor works by OWNING the config of a *nested* sandbox
  through `report_rom`; in alpha the desktop children are direct
  children of the TOP-LEVEL init, whose config is not reachable that way
  — there is no parent to relay it, and the supervisor cannot be handed
  the config of the init that started it. Two further reasons, in order:
  (i) no Timer — the supervisor's own clock is a Tier-0 `timer` child,
  and this host's flat ISO boot denies the HPET `IO_MEM` (the row above),
  while alpha's default media mode is the ISO; (ii) no state report to
  read — with no nested sandbox there is no `system -> state` for the
  top-level relay to carry, so the whole observation channel would have
  to be built. Making the supervisor meaningful on alpha would mean
  restructuring it into the nested-`system` shape the disk scenarios use,
  which invalidates alpha's own `alpha-probe: PASS` evidence. Alpha
  remains the ISO/live-media reference scenario. Rationale in full:
  `docs/evidence/product-supervision.md` §7. Superseded detail below:

  alpha is a FLAT topology. `wm`, `layouter`,
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
- [x] **Resource headroom policy**: every `system` and class-runtime
  caps grant across the five desktop-disk product scenarios carries
  ~10% headroom over its own children sum (§6). The RAM side is no
  longer a hand-derived figure at all: `system` and
  `pkg_runtime_browser` are **remainder
  clamps** (see §6.1), so the headroom is whatever the machine has
  left after the fixed services and the two explicit class runtimes —
  the refusal path for a package that no longer fits is
  `sponge_pkgd`'s admission gate, not a silent clamp.
- [x] **Remainder-clamp RAM grants — deployed, measured**
  (§6.1, `docs/evidence/remainder-clamp-ram.md`): `system` and
  `pkg_runtime_browser` declare `ram: 512G`, a deliberately huge quantum that
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
  against its class runtime's own reported remaining budget and answers
  `insufficient-resources` with the four figures, BEFORE the package
  enters the running set and BEFORE the config is regenerated. The
  budget comes from the class runtime's `-> state` sandbox report
  (`runtime_simple_state` / `runtime_terminal_state` /
  `runtime_browser_state` in the grouped topology), not from
  a configured copy of the grant, so it is always the real remaining
  pool. Wired in all five desktop-disk product scenarios. The grouped
  topology is runtime-verified on `sponge-desktop-disk-uefi-usb` (two
  boots, `alpha-probe: PASS` and the terminal launch gate); the other
  four are structurally asserted by `runtime_grouped::assert_wiring`
  and the static harness, not yet booted. Legacy single-runtime
  admission was verified on `sponge-desktop-disk` (`resource admission
  armed` and `alpha-probe: PASS` in the same boot). On
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

- [x] **The xHCI BAR was mapped to the WRONG physical address — FOUND
  AND FIXED (ledger row 27, 2026-10-21); the storage chain of §2 is
  now whole on the gram's class of device address.** The field report
  from the LG gram 17ZD90N was `xhci_hcd 00:03.0: Host halt failed,
  -110` → the Tier-0 storage chain (§2: `xHCI → pc_usb_host →
  usb_block → part_block → vfs`) died → `/system` was never served →
  `system` never started → the supervisor kill-looped the subtree
  (`state_seen=0`, `kill=N`). `status = 0x0` in the driver's own
  message said the controller's MMIO read **zeroes**: the frame cap
  did not cover the BAR's address.
  **The mechanism was the kernel's, not core's.** seL4 creates the
  objects retyped from an untyped at that untyped's free index — one
  monotonic watermark per untyped. Core's high-phys path (row 14)
  fast-forwards the watermark with throwaway frames to reach a BAR in
  the middle of a device untyped, but seL4 resets the free index to
  the untyped's base whenever the untyped has no live child — and the
  throwaway frames are deleted immediately, so the untyped was
  childless at every step. The fast-forward consumed nothing and the
  real frame was created at the untyped's BASE: the gram's BAR
  (0x601d140000, 384.9 GiB) was mapped to 256 GiB — the GOP
  framebuffer's address, which is why the display worked while the
  storage chain did not. Every address verified so far (16 GiB,
  32 GiB, the QEMU GOP at 2 GiB) sat exactly on a untyped boundary,
  where the buggy and the correct path coincide; that is why every
  QEMU gate passed.
  **The fix (row 27, a kernel port patch):** a device untyped keeps
  its free index when its last child is deleted, and the high-MMIO
  device-untyped ladder becomes a uniform 64 GiB grid so that device
  windows far apart (the gram's framebuffer at 256 GiB and its xHCI
  BAR at 384.9 GiB) get independent untypeds and their mapping order
  stops mattering.
  **The proof is the new harness case** `tool/hwrepro --case
  midwindow`: one extra 64-bit-BAR device makes OVMF place the xHCI
  BAR at 32 GiB + 16 KiB — inside a device untyped, the gram's class.
  Before the fix it reproduced the field signature verbatim; after it
  the case is CLEAN (the usb chain enumerates the mouse and the
  stick, `sponge_configd: ready`), `inwindow` stays CLEAN and
  `boundary` still REPRODUCED the 54 TiB out-of-range stall.
  Receipts: `docs/evidence/xhci-high-bar-mapping.md`.
  **Still open:** the panel itself (one reflash-and-eyeball on the
  gram — the laptop has no serial, the on-panel boot log is the only
  witness), and the residual limitation this row does not remove:
  within one device untyped the watermark is monotonic, so the same
  device window can be materialised only once (a driver that closes
  and re-opens the same BAR gets a clean refusal, not a second frame).
