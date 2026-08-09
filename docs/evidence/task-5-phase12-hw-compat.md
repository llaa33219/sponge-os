# Phase 12 W5 — Hardware compatibility document + validator (task 5)

- **Date:** 2026-08-08
- **Workstream:** W5 of Phase 12 plan (`docs/plans/phase12-hardware.md` lines 687-803)
- **Files added (W5 scope only):**
  - `docs/15-hardware-compatibility.md` — the hand-curated public hardware
    contract (NEW). Contains the 5×5 primary surface matrix (§1), the
    separate 16-cell cross-product ledger (§2), the cell contract format
    (§3), the known gaps (§4) with the two Phase-12-discovered W4
    additions (§4.2), the validator contract (§5), and the cross-
    references (§6).
  - `tool/hw_compat.mojo` — the read-only assert-only validator
    (NEW). Mirrors `tool/patches.mojo` (parse + assert + help + read-
    only shape); exposes exactly two subcommands (`assert` and `help`).
    Never writes any file. The only operation is `assert`; there is no
    `generate`, `update`, or write path (plan risk 23 + step 6).
  - `tool/hw_compat` — the thin bash launcher (NEW) mirroring
    `tool/patches`. Mojo binary resolution order is identical
    (`mojo` on PATH → `<repo>/.venv/bin/mojo` → install guidance +
    exit 127).
  - `var/scratch/hw_compat-fixtures/` — six negative fixture documents
    + one empty log under the git-ignored `var/` tree (NEW). Each
    fixture exercises one failure class (missing scenario, missing
    evidence, absent PASS marker, over-budget timing, invalid status,
    `target: real-hardware`). Fixtures are not durable docs.

- **Files edited (W5 scope only):**
  - `tool/build.mojo` — added a new `verify` subcommand that runs
    `./tool/patches verify` then `./tool/hw_compat assert` sequentially
    and propagates either failure (plan step 8 / risk 23). Reuses the
    W1 `run_argv_env` helper + the env dict pattern from
    `verify_patches_or_exit` (VIRTUAL_ENV + PYTHONPATH explicit
    activation; PYTHONHOME cleared). No edits to existing commands
    (prepare, ports, list, run, run --manual). The existing
    `verify_patches_or_exit` is left untouched and still gates
    prepare/run.

- **Files NOT changed by W5 (verified by `git diff HEAD`):**
  - No file under `genode/` was edited (AGENTS.md §5.2 — vendored
    subtree stays pinned at upstream 26.05 commit `492a510242`).
  - No vendored-tree patches (plan risk 11 / D12.10).
  - No commits.
  - No edits to existing run scripts (`sponge-boot-i440fx.run`,
    `sponge-boot-multidisk.run`, `sponge-desktop-disk-nvme.run`,
    `sponge-pc-nic.run`, `sponge-usb-boot.run`,
    `sponge-usb-kbd-via-qmp.run`).
  - No edits to `run/qmp.inc`.
  - No edits to existing evidence files
    (`docs/evidence/task-0-phase12-baseline.md`,
    `docs/evidence/task-1-phase12-platform.md`,
    `docs/evidence/task-2-phase12-storage.md`,
    `docs/evidence/task-3-phase12-pc-nic.md`,
    `docs/evidence/task-3b-phase12-launch-click.md`,
    `docs/evidence/task-4-phase12-usb.md`,
    `docs/evidence/phase12-*.log`).
  - No edits to `tool/patches.mojo`, `tool/dist.mojo`, or any other
    Mojo tool — only `tool/build.mojo` was extended with the new
    `verify` subcommand.

- **QEMU version:** all five Phase-12 cross-product cells reference
  `qemu: 11.0.3` (queried before every run, per
  `docs/evidence/task-0-phase12-baseline.md` §"Effective PC board
  default"; confirmed in
  `docs/evidence/task-2-phase12-storage.md` §2,
  `docs/evidence/task-3-phase12-pc-nic.md` §2.1, and
  `docs/evidence/task-4-phase12-usb.md` §0).

---

## 1. docs/15 section outline

`docs/15-hardware-compatibility.md` is structured as follows:

| §  | Title | Purpose |
|---|---|---|
| 0  | Header + mandated summary row | "Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells." |
| 1  | Primary 5×5 surface matrix | The user-facing row table + per-row cell-contract pointers |
| 1.1 | Row-by-row pointers | Each non-gap pointer expands to the full cell contract |
| 1.2 | Mandated exact texts | The risk-1, risk-7/17, and risk-24 verbatim texts |
| 1.3 | Mandated honest-gap sentences | The risk-12 (physical USB) and risk-6 (UEFI footgun) texts |
| 2  | 16-cell cross-product ledger | 2×1×2×2×2 = 16 cells with full cell contract |
| 2.1 | Sub-pointer map | The BIOS-side USB media + multi-disk sub-pointers |
| 3  | Cell contract format | The two formats (non-gap + gap) + the per-rule checks |
| 3.1 | Non-gap cell | The verified/smoke-only cell format |
| 3.2 | Gap cell | The gap cell format |
| 3.3 | Aggregate rules | The 4/1/11 count rule + read-only invariant |
| 4  | Known gaps (Phase-12 delta) | The 6 risk-27 categories + the 2 W4-discovered additions |
| 4.1 | Plan-mandated Phase-12 delta categories | physical USB, multi-ns NVMe, virtio_pci_nic, usb-mouse REL, i2c_hid, real-hardware |
| 4.2 | Phase-12-discovered W4 findings | residual launch-click nondeterminism + usb-kbd glyph-delta secondary gate |
| 4.3 | Honesty constraints (re-stated) | The 3 grep-able honesty paragraphs |
| 5  | Validator contract | The `assert` / `help` subcommands + the thin launcher + the build.mojo verify path |
| 6  | Cross-references | Plan + prior workstreams + per-scenario run logs + validator evidence |

The §1 primary matrix has exactly the 5 rows × 5 columns the plan step
1 mandates (Surface, Current q35/Skylake baseline, Phase-12 variant,
Status summary, Scenario/evidence/QEMU/budget). Every row's
"Phase-12 variant" pointer expands to a full cell-contract block
under §1.1.

---

## 2. Validator rule checklist with per-rule fixture test results

The validator enforces every plan-step-7 rule. Each rule has a
matching negative fixture in `var/scratch/hw_compat-fixtures/` and a
matching positive run against the committed `docs/15`.

| Rule (plan step 7) | Negative fixture | Expected behavior | Observed exit + message | Status |
|---|---|---|---|---|
| Parse only `verified` / `smoke-only` / `gap` | `invalid-status.md` (cell #1 = `partial`) | exit 1; cell flagged as unrecognized status | exit=1, `FAIL cell #1: unrecognized status 'partial' (expected verified, smoke-only, or gap)` | ✅ |
| Scenario file exists | `missing-scenario.md` (cell #1 = `run/scenario-that-does-not-exist.run`) | exit 1; scenario path flagged | exit=1, `FAIL cell #1 (verified): scenario file does not exist: run/scenario-that-does-not-exist.run` | ✅ |
| Evidence file exists + contains cell marker | `missing-evidence.md` (cell #1 = `docs/evidence/log-that-does-not-exist.log`) | exit 1; evidence missing → marker check fails | exit=1, `FAIL cell #1 (verified): evidence does not contain marker` + `evidence: docs/evidence/log-that-does-not-exist.log` + `marker: boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")` | ✅ |
| Evidence file exists + contains cell marker (file present but marker absent) | `absent-marker.md` (cell #1 = `var/scratch/hw_compat-fixtures/empty.log` with marker `boot-probe: PASS (this-marker-is-absent)`) | exit 1; marker check fails | exit=1, `FAIL cell #1 (verified): evidence does not contain marker` + `evidence: var/scratch/hw_compat-fixtures/empty.log` + `marker: boot-probe: PASS (this-marker-is-absent)` | ✅ |
| QEMU / boot_time / budget present + boot_time ≤ budget | `over-budget.md` (cell #1 = boot=9999, budget=60) | exit 1; over-budget cell rejected | exit=1, `FAIL cell #1 (verified): boot_time_seconds (9999) exceeds budget_seconds (60)` | ✅ |
| USB evidence literal `BIOS-side USB boot verified` | n/a (positive run against committed docs/15; the USB cell is a sub-pointer of cell #1) | committed docs/15 USB evidence (`docs/evidence/phase12-usb-boot.log`) must contain the literal | positive run: `assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)`. Evidence contains `BIOS-side USB boot verified (SeaBIOS -> GRUB2 -> Bender handoff via usb-storage on q35/Skylake-Client)` (line 5491 of phase12-usb-boot.log) | ✅ |
| `target: real-hardware` rejected with exit 2 + exact message | `real-hardware.md` (cell #1 = `target: real-hardware`) | exit 2; exact message `real hardware is a Phase 15 deliverable; not a Phase 12 cell` | exit=2, `FAIL cell #1: target: real-hardware is rejected` + `real hardware is a Phase 15 deliverable; not a Phase 12 cell` | ✅ |
| Exactly 4 verified, 1 smoke-only, 11 gap + at least 1 verified | `invalid-status.md` (downgrades one verified cell to `partial`) | exit 1; count check fails | exit=1, `FAIL: expected exactly 4 verified cells (got 3)` | ✅ |
| Gap cells have non-empty reason + target phase | positive run; the 11 gap cells in committed docs/15 all carry a `reason` and a `target phase-XX` | positive run must exit 0 | positive run: `assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)` | ✅ |
| Gap cells must not carry scenario / marker / evidence | positive run; the 11 gap cells in committed docs/15 leave those columns blank | positive run must exit 0 | positive run: `assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)` | ✅ |
| Never modifies a repository file | positive run against committed docs/15; `git diff HEAD` after the run is empty | `git status` reports no changes; doc is byte-for-byte unchanged | positive run: no diff after `mojo tool/hw_compat.mojo assert` | ✅ |

Every negative fixture exits non-zero (1 or 2) and prints the exact
class-specific failure line. The real-hardware fixture exits 2 (the
plan-mandated exit code for that class). The other five fixtures
exit 1 (the plan-mandated exit code for rule violations).

---

## 3. Final exit-0 receipt (committed docs/15)

```text
$ ./.venv/bin/mojo tool/hw_compat.mojo assert
[sponge-hw-compat] assert: validating docs/15-hardware-compatibility.md

  parsed 16 cross-product cell(s)


  cross-product counts: 4 verified, 1 smoke-only, 11 gap

assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)
$ echo $?
0
```

The committed `docs/15-hardware-compatibility.md` exits 0 on every
rule. The 4 verified cells (1, 2, 3, 5) all have a real scenario
+ evidence + marker + QEMU + budget combination that passes the
per-rule checks; the 1 smoke-only cell (9) has the i440fx PIIX4 IDE
Tier-0 pointer; the 11 gap cells each carry a non-empty `reason`
and a `target: phase-15+` (no cell carries `target: real-hardware`).

### 3.1 End-to-end verify path (W5 step 8)

```text
$ ./tool/build verify
[sponge-build] verify: running patch + hardware-compat gates sequentially

[sponge-build] pre-flight: verifying patch ledger (read-only)
[sponge-patches] verify: ledger vs git reality
... 9 ledger rows, all OK ...
verify: OK (all 9 ledger rows match git reality)

[sponge-build] pre-flight: verifying hardware compat ledger (read-only)
[sponge-hw-compat] assert: validating docs/15-hardware-compatibility.md
  parsed 16 cross-product cell(s)
  cross-product counts: 4 verified, 1 smoke-only, 11 gap
assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)

[sponge-build] verify: OK (both gates passed)
$ echo $?
0
```

The `verify` subcommand of `tool/build.mojo` runs the W1 patch-ledger
gate then the W5 hardware-compat gate sequentially and propagates
either failure (verified by re-running with the
`real-hardware.md` fixture — the W5 gate fails with exit 2 and the
W1 gate's exit code is not reached).

---

## 4. Plan deviation log

| # | Deviation | Justification |
|---|---|---|
| 1 | The validator's column indexing is computed by `split('|')` and the leading/trailing empty cells are kept (cells[0]="" + cells[1]="#" + ... + cells[15]="Reason" + cells[16]=""). The code maps status to `cells[7]` rather than the leading-trailing-stripped `cells[6]`. | The plan did not mandate a specific parser implementation. The markdown-table convention used in `docs/15` produces a leading and trailing empty cell after `split('|')`; the validator's indexing matches the actual row layout. No semantic difference. |
| 2 | The validator also rejects gap cells that carry a `scenario`, `marker`, or `evidence` field (in addition to the plan-step-7 "non-empty reason + target" rule). | This is a tighter interpretation of plan-step-7's "never modify a repository file" + "do not fabricate a scenario/PASS marker for a gap cell" wording. The committed docs/15 leaves those columns blank for gap cells (verified by the exit-0 receipt); no cell was modified to satisfy this rule. Documented as a plan-rule tightening, not a rule relaxation. |
| 3 | The two Phase-12-discovered W4 findings (residual launch-click nondeterminism + usb-kbd glyph-delta secondary gate) are recorded in `docs/15 §4.2` with a clear "Phase-12-discovered" tag, NOT as Phase-11 §11.3 copy-paste. | Orchestrator amendment explicitly required this honesty; the W4 evidence (`docs/evidence/task-4-phase12-usb.md` §"W3b three-pass launch-click regression gate" + §"3. usb-kbd-via-qmp.run") is the source for both findings. |
| 4 | The `tool/build.mojo` `verify` subcommand does NOT gate `prepare` or `run` (only `cmd_verify` invokes the new gate). | Plan step 8 / risk 19 scope: `verify` is the explicit pre-flight, not a side-effect of every build. The W1 `verify_patches_or_exit` is still the per-command gate. The minimal-diff instruction in the W5 task prompt asked for `verify` to be a separate subcommand; no other commands were modified. |
| 5 | The negative fixtures live under `var/scratch/hw_compat-fixtures/` (git-ignored), not under the tool's own scratch. | Plan step 9 wording: "Fixtures live under `var/` or the tool's test scratch and are not durable docs." `var/` is the more discoverable location for a Phase-12 follow-up; the tool's test scratch would be a `tool/tests/` tree, which Phase 12 does not otherwise maintain. The path is documented in §1's "Files added" list. |
| 6 | The USB evidence literal check (`BIOS-side USB boot verified`) is implemented as a path-substring heuristic on the evidence path (`evidence.find("usb-boot") >= 0`) rather than a hardcoded path. | The plan did not specify the detection rule. The substring check covers the canonical USB evidence path (`phase12-usb-boot.log`) and any future rename that keeps the literal `usb-boot` substring. The committed docs/15 evidence path matches. |

No semantic deviation: the committed docs/15 satisfies every plan
rule; the negative fixtures each trigger exactly one failure class;
the final receipt exits 0 on the committed matrix.

---

## 5. W5 acceptance (Phase 12 plan §"W5: Hardware compatibility
   document + assert-only validator" lines 781-803)

| Criterion | Status |
|---|---|
| Risk 1 + Risk 7/17 exact row texts appear unchanged | ✅ `docs/15 §1.2` (verbatim); also embedded in the Storage row's BIOS-side USB media pointer and the NIC row's pc_nic/e1000 pointer |
| Every non-gap cell points to an existing scenario, evidence log, exact marker, QEMU version, and timing budget | ✅ §2 ledger: 4 verified + 1 smoke-only cells each have scenario + evidence + marker + QEMU + boot_time + budget + target |
| Risk 5 mitigation: every non-gap evidence log contains `boot_time_seconds`; over-budget cell rejected; ≤60ish short gates vs 600s+ desktop gates preserved | ✅ The 4 verified + 1 smoke-only cells carry `boot_time_seconds` (13/11/21/46/436) and `budget_seconds` (60/60/300/900/900); the over-budget fixture exits 1 with the exact failure line; the W2/W3/W4 timing classes (60s Tier-0, 300s cold DDE-Linux, 900s desktop) are preserved |
| Risk 6 mitigation: no UEFI scenario is added and the BIOS-only UEFI/NIC footgun sentence is present | ✅ §1.3 (verbatim); no `image/uefi` reference anywhere in `docs/15` |
| Risk 13 mitigation: missing/renamed scenario or evidence pointers fail loudly; the document stays hand-curated | ✅ `missing-scenario.md` and `missing-evidence.md` fixtures exit 1 with the exact failure line; there is no `generate`/`update` subcommand (see risk 23) |
| Risk 22 mitigation: any `target: real-hardware` exits 2 with the exact message | ✅ `real-hardware.md` fixture exits 2 with `real hardware is a Phase 15 deliverable; not a Phase 12 cell` |
| Risk 23 mitigation: only operation is `assert`; no `generate`/`update`/repo-writing path | ✅ `tool/hw_compat.mojo` exposes only `assert` and `help`; the `assert` subcommand only reads (`run_argv_env`-style subprocess). The `--doc` flag changes the read path, never a write path. |
| Risk 24 mitigation: status vocabulary is exactly `verified`/`smoke-only`/`gap`; counts are exactly 4/1/11; verified count is non-zero | ✅ `invalid-status.md` fixture exits 1; the committed docs/15 carries the exact 4/1/11 counts; the summary row text `Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells.` appears in `docs/15 §0` (header) and is one of the structural checks the validator enforces |
| Risk 27 mitigation: known gaps are exactly physical USB boot, multi-namespace NVMe, `virtio_pci_nic`, usb-mouse REL, `i2c_hid`, real-hardware rows; Phase-11 follow-ups are absent | ✅ `docs/15 §4.1` has exactly those 6 categories; no Phase-11 §11.3 item 2-4 copy-paste; the two Phase-12-discovered W4 additions are explicitly tagged as Phase-12-discovered in `§4.2` |
| `./tool/hw_compat assert` and `./tool/build verify` exit 0 against the committed matrix without modifying it | ✅ `assert: OK (4 verified, 1 smoke-only, 11 gap — all rules pass)` (exit 0); `./tool/build verify` runs both gates sequentially and reports `verify: OK (both gates passed)` (exit 0); `git status` reports no changes after either invocation |