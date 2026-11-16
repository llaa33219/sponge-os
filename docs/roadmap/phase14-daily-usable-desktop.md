> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 14.

### Phase 14: Sponge DE as a Daily-Usable Desktop

#### Goal

Sponge DE can serve as a primary working environment for everyday
tasks — not a demo, but a desktop one can actually sit down and use.

#### Completion Criteria

- [x] **Session stability** — **partial**. The W9 stability probe
  (`repos/sponge/src/sponge-de/test/leak_audit/`) and the 200-cycle
  leak-audit scenario (`run/sponge-de-leak-audit.run`) plus the
  5-cycle fast-fail scenario (`run/sponge-de-stability-fastfail.run`)
  are boot-verified end-to-end on `base-sel4` in QEMU. The full
  30-min wall-clock cycle (`run/sponge-de-stability.run`) was not
  re-boot-verified in the W12 close-out session (the W12 spec's
  20-min/scenario skip rule bounds the close-out regression sweep
  by design; the stability workload is a sustained scenario that
  exceeds that bound). Honest limitation registered; the fast-fail
  and leak-audit variants prove the probe and the gate logic.
  Evidence: `docs/evidence/phase14-index.md` §1 W9 +
  `docs/evidence/phase14-index.md` §2 criterion-1 row.
- [x] **Core desktop services complete** — **delivered**.
  Notifications (`run/sponge-notify.run` →
  `notify-probe: PASS`), clipboard (`run/sponge-clipboard-qtsettext.run`
  → `clipboard-probe: PASS`, qtsettext: clipboard Qt -> server
  write harness PASSED), window management minimize/restore/focus
  (`run/sponge-wm-tasks.run` → `wm-tasks-probe: PASS`). Honest
  caveats: textedit `Ctrl-C` keyboard and the workflow step-5
  tasklist click on the heavier workflow topology are documented
  Phase-15+ limitations (W5 qtwrite dropout + W8 layouter
  hover-state race). Evidence: `docs/evidence/phase14-index.md`
  §2 criterion-2 row + §5 honest-limitations register.
- [x] **Everyday workflow proven end-to-end** — **partial**.
  Steps 1-4 of `run/sponge-de-workflow.run` PASS (boot → terminal
  → textedit → cross-component clipboard). Step 5 (tasklist click
  on the heavier workflow topology) is blocked by a layouter
  hover-state timing race documented in
  `docs/evidence/phase14-w8-workflow-scenario.md`. The browser
  criterion is **fully proven** via `run/sponge-falkon-rescue.run`
  (D14.5 Attempt 1 PASS: Falkon booted from disk, window
  pixel-verified, fixture page loaded over the `pc_nic`/slirp
  stack). Evidence: `docs/evidence/phase14-index.md` §2
  criterion-3 row + §4 D14.5 decision record.
- [x] **Paper cuts from Phases 10–13 resolved** — **delivered**.
  Full 4-way disposition matrix in
  `docs/plans/phase14-daily-desktop.md` paper-cut appendix
  (50 rows: `Resolved in 14` / `Re-scoped` / `Blocked` /
  `Not-a-defect`). Every Phase-14-relevant item is implemented
  in W11 (theme-aliases cleanup, terminal_runtime.inc dedup,
  `vct status --resources`, QTimer destructor fixes, leak-audit
  probe). Items not in Phase 14 carry explicit target phases
  (15+ / 16 / 17) per U5's "Re-scoped items carry a target
  phase" rule. Evidence: `docs/evidence/phase14-index.md` §5 +
  §7 disposition tables.

  **Phase 16 reclassification of two `Re-scoped → Phase 15+`
  items, per U16.2** (decision recorded
  2026-09-19): rows #6 (`panel.position` is boot-time-only) and
  #14 (`panel.position` duplicate of #6) of the Phase 14 W11
  paper-cut appendix are now **`Re-scoped → Phase 16`** instead
  of `Re-scoped → Phase 15+`. Phase 16 criterion 3 explicitly
  brings `panel.position` back into scope (live, click-to-apply
  panel-menu action via dual nitpicker panel domains per D16.2 +
  U16.2, see `docs/plans/phase16-daily-desktop-defaults.md`).
  The live `panel.position` rewrite lands in Phase 16 W5, after
  which the rows are reclassified `Resolved in 16`. No other
  rows of the Phase 14 disposition matrix change.

## Status note (moved from README)

Facts relocated from the README `Current Status` section (Phase 14
close-out bullet) that had no home in the roadmap set:

- **W12 close-out regression sweep:** 6 scenarios, serial `make -j1`:
  `sponge-de-test`, `sponge-alpha`, `sponge-wm-tasks`,
  `sponge-clipboard-qtsettext`, `sponge-notify`,
  `sponge-configd-persist` — all PASS. Full receipts:
  `docs/evidence/phase14-index.md` +
  `docs/evidence/phase14-envelope-*.log`.
- **Session stability re-verification:** the 30-min wall-clock cycle is
  to be re-verified in a dedicated Phase-15 close-out pass.
- **Everyday-workflow U2 rationale:** U2 holds because the
  `clipboard_qtsettext` harness and the workflow probe are separate
  address spaces; the W7 tasklist recipe works on the standalone
  wm-tasks topology (step 5's failure is specific to the heavier
  workflow topology).
- **Tasklist implementation detail** (design home:
  `docs/05-sponge-de.md` §4.6; state machine
  `docs/plans/wm-state-table.md`): the panel tasklist lives at
  `repos/sponge/src/sponge-de/panel/tasklist/`; `TasklistController`
  subscribes to wm's `window_list` and the layouter's `window_layout`
  reports; per-window click writes the layouter's `rules` ROM (in
  `rules="rom"` mode) and emits a `focus_request` report;
  `TasklistWidget` renders as a horizontal strip in the panel
  QHBoxLayout with three visual states (Normal-Visible,
  Normal-Visible-Focused, Minimized). U3: no decorative-only
  minimize.
