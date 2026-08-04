# phase9-sel4-cspace - Work Plan

> Goal: make base-sel4's capability spaces lazily growable so Falkon (and
> other large dynamic workloads) reach first paint on seL4 — WITHOUT
> kernel pivot and WITHOUT forking to alex-ab's tree. This is the
> `platform.cc:108` XXX ("allocate intermediate CNodes ... here") resolved
> properly, Sponge-owned, ledgered, and upstreamable.
> Design context: docs/14 §12.4; evidence: docs/evidence/p4-cspace-falkon.log,
> docs/evidence/p4-cspace-fix.log (terrain fully mapped — trust it).

## Proven terrain (from Phase 8 P4 — do not re-derive)
- Child-PD CNode backing = exactly one 4KiB untyped page (`cnode.h:159`,
  `Untyped_memory::alloc_page`) → 32-byte CTE → max 128 slots/CNode →
  per-PD CSpace hard-capped at 16384 (1ST=7/2ND=7).
- Static enlargement (size-aware 16K backing, 262144 slots) boots and
  lets falkon launch+DHCP, BUT: (a) carving a fixed 16K untyped pool
  starves init RAM (512MiB) or breaks ahci disk detection (DMA/device-
  untyped class interaction); (b) the NEXT limit, vm_space
  (`NUM_VM_SEL_LOG2=15` → 32768 mappings/PD), is then hit by falkon
  (~100k frames) and rom_pkg (~130k).
- vm_space eager enlargement attempts broke boot: Platform_pd `_cnodes`
  array starves core heap; silent Platform crash; seL4 "device untyped"
  retype. Lesson: growth must be LAZY, not eager.

## C1 — DMA-safe on-demand large CNode backing
- [ ] 1. Diagnose precisely why the 16K-pool carve broke ahci (seL4 cap
  tracing / device-untyped class inspection): RAM-untyped vs
  device-memory untyped separation for ahci DMA. Document the mechanism.
- [ ] 2. Implement on-demand 16K (and larger) CNode backing WITHOUT a
  fixed pre-carved pool that starves/breaks anything: allocate the big
  backing from the RAM untyped space at CNode-construction time, with
  correct selector addressing. Must not touch the DMA/device path.
- [ ] 3. Canary: run/sponge-boot.run (ahci canary) + run/sponge-alpha.run
  PASS at the enlarged backing. No ahci regression.

## C2 — lazy vm_space growth
- [ ] 4. Lazy 3rd/4th-level vm_space CNode creation (construct lower-level
  CNodes on first use of their selector range, not at PD creation) +
  enlarged tree params (target ≥ 131072 mappings/PD). Core heap must NOT
  be starved (the eager failure mode).
- [ ] 5. Canary: all desktop scenarios PASS; falkon gets past the
  "out of selector / mapping cache full" point.

## C3 — lazy main-CSpace growth
- [ ] 6. Lazy 2nd-level main-CSpace CNode creation (grow on selector
  consumption, base-hw `upgrade_cap_slab` philosophy on seL4).

## C4 — payoff + hardening
- [ ] 7. run/sponge-falkon-disk.run: falkon_probe PASS (window pixel) +
  fixture GET in host access log. Run twice.
- [ ] 8. Full regression suite green (all run/*.run on their kernels).
- [ ] 9. docs/11 patch ledger: every vendored change recorded
  (what/where/why/how-to-drop). docs/14 §12.4 updated: falkon first
  paint achieved; blocker chain closed. docs/13 falkon entry updated.

## Commit strategy
One workstream = one commit (vendored changes may be one focused commit
each with ledger rows); scenario-gated; fail-loud always.
