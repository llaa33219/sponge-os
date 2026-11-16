# Runtime component topology: design gap register (part 1)

> **Breadcrumb:** parent [17 - Runtime Component Topology](../17-runtime-component-topology.md); this file holds its §5, part 1 of 2 (the freeze and liveness rows), moved verbatim. The register continues in [design-gap-register-2.md](design-gap-register-2.md) (the containment, supervision and policy rows).
> Siblings in `docs/topology/`: [tree-and-chains.md](tree-and-chains.md) (§1, §2, §2a), [incident-analyses.md](incident-analyses.md) (§3, §4, §4a), [quota-ledger.md](quota-ledger.md) (§6).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

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
- [ ] **The permanent freeze: a never-answered quota upgrade parks the
  container's entrypoint - REPRODUCED, fix NOT shipped (2026-10-21)**:
  a sandbox init forwards a child's quota upgrade to its own parent and
  waits for the answer on its SINGLE main entrypoint - the thread that
  serves every child's session plumbing and the containment alarm.
  `Parent::Upgrade_result::PENDING` is not a promise (it means "the
  request has been taken, the session's provider has not acted on it
  yet"), and a provider that never answers - the field's file-system
  server on a stalled USB stick - parks the entrypoint forever, so the
  container dies silently and the alarm that would have noticed is
  parked with it. Reproduced and measured in QEMU
  (`run/sponge-upgrade-wedge.run`, verdict WEDGED, 0 witness rounds);
  the deadlock cycle, the two fix variants and the reason the first
  one was REVERTED (it broke the e1000 driver of `sponge-pkg-driver`)
  are in `docs/evidence/sponge-upgrade-wedge.md`. The fix this defect
  needs is an ASYNCHRONOUS completion of the nested upgrade, off the
  container's entrypoint, that keeps the quota transfer and notifies
  the client - not yet implemented. Until then the field's permanence
  is NOT attributed to this defect: in the product the provider lives
  in the top-level sandbox, whose containment is alive, so the chain is
  expected to unwind there.
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
  bisection: [`docs/evidence/sponge-debug-panelfreeze.md`](../evidence/sponge-debug-panelfreeze.md).
- [x] **The quota-request wedge — the permanent-freeze mechanism, found
  and fixed (ledger row #25, 2026-10-19); the field's *total* freeze is
  still NOT reproduced in QEMU.** The 2026-10-19 field report adds the
  fact that rules out saturation: the freeze **never heals**. `Parent::
  upgrade` carries no "denied" answer, and `Sandbox::Child::
  resource_request` answered a request it could not satisfy by
  transferring the child's **own** (exhausted) leftover quota, warned
  about the failure and then logged `granted immediately` anyway — so
  the client looped forever, one RPC per iteration, against
  `Sandbox::Child`'s `_env.ep().rpc_ep()`: the single entrypoint that
  carries **every** child's `Parent::session`/`upgrade`/`close`/`exit`
  **and** the heartbeat alarm. Measured RED → GREEN on the new
  `run/sponge-quota-deadlock.run`: **3701 upgrade RPCs and 3701 false
  grants before, 4 and 0 after**, containment settling on its
  `restart_budget`. The four unbounded upgrade loops (signal-context
  caps, RPC-cap allocation, heap growth, file open) are bounded by a
  shared `Parent::UPGRADE_ATTEMPTS`, so a structurally unsatisfiable
  request is now a loud, terminating failure the heartbeat can act on
  instead of a spin.

  **`sponge_pkgd | caps: 200` is the smallest budget in the render
  chain** (`run/sponge-desktop-disk-uefi-usb.run:861`) and
  `Signal_receiver::manage` is the allocator the input path uses for
  every signal context, which is why the health label reads pkgd first.
  **No product quota was changed** — exhausting one is now visible and
  contained, and the log names the requested/assigned/configured/our-slack
  values, so the quota policy decision is the operator's, made from a
  diagnosis instead of a freeze. Still open: the *deadlocked* variant
  (`Child::upgrade` → `PENDING` → the client blocked in
  `Component_env::_block_for_session()` holding its mutex, plus the
  nested `Try_parent_service::initiate_request` → `_env.upgrade()` on
  that same entrypoint) is proved from source and not reproduced; QEMU's
  single vCPU also flatters the RED arm, because a spinner alone on the
  only core degrades differently than a spinner competing with a
  compositor and a USB HID stack on four cores. Evidence, the field
  greps, and the honest scope:
  [`docs/evidence/sponge-quota-deadlock.md`](../evidence/sponge-quota-deadlock.md).
- [x] **Respawn budget — FIXED (ledger row #24, 2026-10-18); the clamp
  leak is SEPARATED and still open (found 2026-10-16)**.
  `<heartbeat restart_after_skipped="3"/>` had no attempt budget and no
  backoff, so a child that cannot start was respawned forever. The row's
  own evidence was a flake ("reproduced only under a `usb_block` storage
  stall"), which also left two candidate mechanisms — an unbounded loop
  and a per-cycle RAM leak — conflated. `run/sponge-containment-churn.run`
  separates them, deterministically:

  * **The loop reproduced, and it is the cascade.** A permanently wedged
    critical child respawned every **2500 ms median** (heartbeat 500 ms,
    `restart_after_skipped` 3) — 48 respawns inside a 20 s window and
    still climbing. Each cycle destroys and rebuilds a PD, its ROM
    sessions and its sliced-heap slabs, so it costs CPU in init *and* in
    the kernel while the child stays permanently unusable, and it never
    converges — which is what made the supervisor's escalation phase
    structurally unreachable on the field machine.
  * **The `~35 KB/cycle` leak did NOT reproduce** on a healthy storage
    chain: the container's own `avail` oscillates by exactly ±10 240 B
    (one init allocation quantum) per cycle and returns to baseline.
    So the field's decreasing `pkg_runtime: … proceed with N` needs
    something this topology lacks — an abandoned child whose env
    sessions can never complete their close handshake because
    `rom_sys`/`cached_fs_rom` over a stalled `usb_block` never answers
    (`Library::_destroy_abandoned_children()` waits forever on
    `env_sessions_closed()`). **Still open, and named.**

  Fixes, both red→green on the new scenario:

  1. **`restart_budget` in `init`** (fork patch `aa1aac7305`, ledger row
     #24). The count lives on the config model's `<start>` node, because
     the `Child` is destroyed and rebuilt on every restart; a child that
     answers a heartbeat again resets it, so the budget bounds a *run of
     failures*, not a lifetime. On exhaustion the child is torn down
     through the existing restart path but **not** recreated, and init
     logs `Warning: <name>: heartbeat restart budget (N) exhausted after
     M restarts — child left stopped, escalation is up to whoever owns
     the config`. Only a new config generation — which needs the config
     owner to act — revives it, which is exactly the hand-off
     `sponge_supervisor` requires. `0` means unlimited, so it is opt-in
     per `<start>` node and upstream behaviour is unchanged where
     absent; the five product scenarios and `run/driver_runtime.inc` set
     it to **3**. RED 48 respawns / no marker → GREEN 4 restarts /
     marker present / container `avail` tail spread **0**.
  2. **`sponge_pkgd` no longer waits on storage from its entrypoint** —
     the `docs/evidence/logdisk-field-diagnosis.md` §3(a) rule applied
     to pkgd. `Component::construct()` runs on `env.ep()`, and `_init_store`
     / `_load_store` / `_seed_roots_from_bake` all block it on
     `File_system` + `commit_and_wait()`. The constructor now does only
     work that provably cannot touch storage and hands the rest to a
     dedicated `Bootstrap` thread, so a child *waiting on its store* is
     no longer a child with a wedged entrypoint. This is the field's
     trigger (`unresponsive(1)` immediately after first paint).

  **What the supervisor can survive, measured** (§7 of the evidence
  file): with a sibling churning every 2.5 s, the Tier-0 `churn_probe`
  sampled continuously, the Tier-0 pixel oracle kept repainting and kept
  re-announcing the desktop, and the in-sandbox bystander kept emitting.
  A containment loop inside `system` does **not** starve Tier-0 — the
  field machine did not lack a supervisor that could act, it lacked an
  end to the loop for the supervisor to observe. A *kernel-level*
  freeze still starves every user thread including the supervisor and is
  out of reach of any userspace answer; this work bounds userspace
  churn.

  Receipts: `docs/evidence/containment-churn.md`.
- [ ] **Real-hardware confirmation of row #23 — open (the only open
  half)**: the coalescing mechanism is built and measured on QEMU
  (§ above), but the magnitude at 2560x1600 on the LG gram 17ZD90N —
  where the field report originated — is not. Measure the achieved
  repaint rate and the event-loop latency of a fast scrollbar drag with
  and without the patch on that machine, so the row's cost/benefit side
  is closed on the hardware the symptom came from.
