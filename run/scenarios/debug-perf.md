# Debug and performance probes

Breadcrumb: [`run/README.md`](../README.md) → Debug and performance probes

Field-report investigations: scroll-storm resource attribution,
time-to-first-paint at field resolution, and slow-storage startup-gap
measurement. Full catalog entries follow verbatim (cautions and
limitations included); the parent index carries one-line summaries.

## Scenarios
- `sponge-debug-scrollstorm.run` — **resource attribution for the
  SPONGE DEBUG "freeze when the scrollbar is dragged fast" field
  report**. Boots SPONGE DEBUG on the real render path (Qt6 QPA genode →
  nitpicker → `vesa_fb` → GOP) inside a nested `system` init whose
  state report is armed with `child_ram`/`child_caps`, drives a
  config-gated repaint storm inside the app, and has `repostorm_probe`
  (`repos/sponge/src/test/repostorm_probe/`) sample every child's
  RAM/caps and name the largest grower. Four arms via `SPONGE_ARM`:
  `baseline` (no storm), `scroll` (30 s @ 30 Hz viewport flings),
  `grow` (the same plus one appended log line per tick), and `burst`
  (the FORCED back-pressure arm: `storm_burst_hz=100` adds an 8x8
  damage update at 100 Hz on top of the unmodified storm, so the
  client provably outruns the ~25 Hz compositor and the QPA plugin's
  frame-coalescing path — ledger row #23 — actually engages instead of
  merely compiling). Gate: `repostorm-probe: VERDICT <PASS|FAIL>`.
  **Result: no leak** — 337 repaints move 0 bytes in every component.
  With coalescing the mechanism is measured engaging: on `burst`,
  800 submit attempts in 30 s collapse to 82 actual frame submissions
  (81 coalesced), storm throughput rises 11 → 19 Hz, max tick gap stays
  at 139-172 ms (the rejected blocking wait measured 2657 ms), and
  per-child ΔRAM is unchanged. The `baseline` arm logs exactly one
  coalesced frame per run — the control that shows the self-gating
  telemetry distinguishes "worked" from "never needed". Evidence
  `docs/evidence/sponge-debug-scrollstorm.md`. base-sel4 only
  (`vesa_fb`/`ps2`/`usb_hid`).

  **Extended 2026-10-17 with the field's defining parameters** (all
  opt-in; every default leaves the four arms above bit-identical, so the
  numbers in that evidence file still hold). A fifth arm `freeze` runs
  the field gesture for 60 s. Knobs: `SPONGE_FB_W/H` (panel resolution,
  default = the stock 1024x768 recipe), `SPONGE_STORM_WIN` (the
  storming client's window geometry, default = the product 700x480 —
  every render-path cost is proportional to the damaged area),
  `SPONGE_HEARTBEAT=on` (production containment on every child, which
  makes init print a per-child `skipped_heartbeats` counter — the
  **starvation ranking**; `repostorm_probe` is deliberately excluded
  because it runs its whole measurement inside `Component::construct`
  and therefore can never answer a heartbeat), `SPONGE_SMP` (guest
  vCPU count), `SPONGE_FB_PERIOD_MS` (the framebuffer driver's capture
  period), `SPONGE_FB_CAPS`/`SPONGE_NITPICKER_CAPS`/`SPONGE_DRIVERS_CAPS`
  (the control door for the mapping quotas, which are otherwise
  **derived from the requested resolution**), and
  `SPONGE_MIN_HZ`/`SPONGE_MAX_GAP_MS` (the pass/fail bounds; 0 = do not
  assert). Two fixes make the numbers trustworthy: the probe's
  measurement window is now **derived from the storm** so it strictly
  contains the load (it previously finished at 80 s while the storm ran
  to 84 s, and which end-of-run marker landed last flipped between
  runs), and the storm's gate matches the **full** telemetry shape rather
  than a prefix, because QEMU's `-nographic` serial splits a long write
  and a prefix match cut the line mid-number.

  **Result at the field resolution:** the 2026-10-17 field freeze (drag →
  `pkgd` unresponsive → the mouse dies too) is **not** reproduced on a
  correctly-sized configuration — 19 Hz, 387 ms worst gap, 0 kernel
  page-table flushes. A **capability quota too small for the
  panel-resolution buffer** does reproduce a 6x collapse (3 Hz, 1379 ms,
  532 flushes) through base-sel4's mapping cache, and the shipped media
  is already sized for it (`fb` 24000 / `nitpicker` 20000). Evidence,
  the six-run bisection and the field check:
  `docs/evidence/sponge-debug-panelfreeze.md`.

- `sponge-debug-firstpaint.run` — **time-to-first-paint of the SPONGE
  DEBUG package against the containment budget, at an arbitrary
  framebuffer resolution**. The 2026-10-16 field report ("the SPONGE
  DEBUG window appears but never finishes rendering, then dies", LG
  gram 17ZD90N, `boot_fb`/GOP 2560x1600, no error text) cannot be
  diagnosed without a number for how long the client takes to reach its
  first frame, so `sponge_debug` now reports its own progress on the
  serial console (`construct ms=`, `paint-begin ms=`,
  `FIRSTPAINT ms=`) and this scenario boots the real render chain
  (`sponge_debug` → Qt6 QPA → `nitpicker` → `vesa_fb` → GOP) with the
  production heartbeat at `SPONGE_FB_W` x `SPONGE_FB_H` (default
  **2560x1600**, the LG gram's panel resolution). The host-side gate
  asserts the framebuffer really came up at that resolution, that the
  client constructed **exactly once** (more than one = a containment
  kill), and prints the largest `skipped_heartbeats` the sandbox
  reported for it. `SPONGE_HEARTBEAT=off` is the toggle arm — the
  containment feature is never disabled in the product, only in this
  scenario. Two stated deviations: the package is a direct child of
  `system` rather than of a nested `pkg_runtime` (same generated
  signal, period and budget; the extra layer only adds a route hop that
  init's concurrent child start makes racy), and the display driver is
  `vesa_fb` at the requested mode rather than `boot_fb` (only the
  producer of the pixels differs). Result at 2560x1600: **1.3 s to
  first paint against a 6 s budget, 1 spawn, 0 skipped heartbeats**,
  unchanged with the heartbeat removed — which is what rules the
  heartbeat out. Evidence `docs/evidence/sponge-debug-firstpaint.md`.
  base-sel4 only (`vesa_fb`/`ps2`/`usb_hid`).

- **`SPONGE_SLOW_STORAGE_KBPS`** (env-gated, **off by default**;
  measurement only) — a slow-storage emulation on the boot medium of
  `run/sponge-desktop-disk-uefi-usb.run`, the field's own media. It
  limits the read/write bandwidth of the stick with QEMU's block
  throttling, so the QEMU/field gap around component startup can be
  **measured** instead of argued: at 4 MB/s five contained children —
  `sponge-de` among them — take 8–10 s to become responsive against the
  sandbox's 10 s heartbeat budget, and the sandbox says so by name
  (`startup took N ms - the heartbeat budget (M ms) would have killed
  it`). The gate timeout scales with the knob. Never shipped; receipts
  in `docs/evidence/startup-grace.md` §2.
