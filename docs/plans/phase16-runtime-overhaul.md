# Phase 16 Runtime Overhaul (Work Plan)

> Status: in progress. P0 baseline/audits, P1, P2, P3.1-P3.3, and P3.4
> are complete; P3.4's victim-revival gate is blocked by a pre-existing
> base-layer child-recreation failure (see
> `docs/evidence/phase16-runtime-p34.md` §5); P4-P5 remain open. Created
> 2026-10-28 on branch `refactor/phase16-runtime-overhaul` (pre-work HEAD
> `3afb40c922`).
> Relationship: this is the runtime-overhaul track of the reopened Phase 16
> (`docs/09-roadmap.md` §11 "Phase 16 Extension", 2026-10-04), alongside the
> Phase 16 daily-desktop-defaults plan (`docs/plans/phase16-daily-desktop-defaults.md`,
> closed 2026-09-20). Topology authority: `docs/17-runtime-component-topology.md`.
> Evidence conventions: `docs/evidence/` writeups, `docs/11-environment.md` §4
> patch ledger.

**Naming note (binding).** The work-breakdown groups in this plan are named
**P0 through P5**. The GPT partitions are named **P3 GENODE / P4 SPONGE-VAR /
P5 SPONGE-HOME**. The two name spaces are unrelated. Where both appear, this
plan writes "task group P3" versus "partition P3 (GENODE)".

---

## 1. Goal

Restructure the runtime layer of the product media so that failure is
contained per runtime and per provider, without changing the media layout,
without new Genode base semantics, and without breaking the verified
desktop. Concretely, after this phase:

1. Package and driver configuration is emitted per runtime with a single
   authority per runtime, and only the dirty runtime's config is
   regenerated when a package set changes.
2. Packages are grouped into three runtime classes (`simple`, `terminal`,
   `browser`) instead of one universal `pkg_runtime` with per-package
   init ambitions.
3. Session-create paths perform no storage I/O, and the package-store and
   log-sink File_system provider paths are separated (`vfs_pkg` /
   `vfs_log`), all on the existing partitions.
4. sponge-de survives package-control failures in a labeled, degraded,
   interactive state.
5. Every built-in desktop package launches from the real launcher, reaches
   first paint, accepts basic input, and survives sequential, concurrent,
   and repeated execution without exporting its failure to the DE or its
   siblings.
6. The open pkg-driver NIC uplink regression (ledger row #31 create chain,
   `docs/evidence/forwarded-create-starvation.md` §7) is captured,
   classified, and dispositioned before any related patch row or code path
   is pruned.

Every claim of success is scenario-verified on IMG (disk) product media
with QEMU-labeled evidence and real launcher first paint + input, never
boot markers alone.

## 2. Non-Goals

Explicitly out of scope. No code for these lands in any task of this plan;
see §11 for the full follow-ups register.

- **vct-install-in-terminal** (roadmap Phase 16 Extension criterion 14).
  This phase fixes the nested `binary_prefix` root cause (task P2.3) but
  does NOT claim the criterion; the resolve/download/install/run
  acceptance stays a follow-up.
- **Falkon page load** (criterion 15). This phase delivers the browser's
  real launcher open, first paint, and basic input (criterion 13, task
  P2.6); NIC-dependent web-page acceptance stays a follow-up.
- **Additional shells (zsh, fish)** and **same-program multi-instance**.
  The runtime restructure must not block them, but delivers neither.
- **ISO payload staging / lifting the boot-module ceiling.** The ~256 MiB
  Bender-relocation and seL4 untyped-cnode ceilings
  (`docs/14-boot-storage-architecture.md` §2.1, `docs/11-environment.md`
  §7.2) are unchanged. ISO media stays metadata-only / live-eval (D16.7
  stands).
- **Phase 17 UI work** (Sponge IME / CJK input, and the Phase 16 honest
  limitations register rows: panel.position left/right, real-hardware
  multi-panel, user background upload, and similar).
- **Additional partitions, in-place media migration, or installed-media resizing** (see D16R.2). New-media P3 sizing for the measured browser closure is allowed.
- **New Genode base async semantics** (see D16R.9).
- **New vendored-tree patches outside the classification budget**
  (see D16R.10).

## 3. Binding Decisions (do not re-litigate)

| # | Decision | Rationale |
|---|---|---|
| **D16R.1** | **IMG/disk acceptance.** All acceptance gates run on IMG (disk) product media (`run/sponge-desktop-disk*.run` family). ISO runs are live/eval smoke only and can never satisfy an acceptance gate. | D16.7 media split is binding; the disk image is the product. |
| **D16R.2** | **Three-partition layout and no in-place media migration.** The partition set, GPT names, and roles stay fixed: partition P3 `GENODE`, partition P4 `SPONGE-VAR`, partition P5 `SPONGE-HOME` (plus the firmware partitions), **no P6+**, no renames, no re-purposing. New media MAY grow P3 to stage the measured WebEngine closure; existing deployed media is never migrated or resized in place. | Three logical storage roles are sufficient; provider isolation does not require more partitions. New-media sizing is allowed so browser staging is not designed into a known capacity failure. |
| **D16R.3** | **Three-area layout is the naming authority.** The canonical naming is P3 `GENODE`, P4 `SPONGE-VAR`, P5 `SPONGE-HOME` (the three-area layout of `docs/17` §6.1; since P3.2 the P4 provider is the `vfs_pkg` / `vfs_log` pair, P5 is `vfs_home`). `docs/14` §4.3's older `SPONGE-DATA` wording is stale; P5.2 syncs it. No other doc may reintroduce `SPONGE-DATA` as current. | The shipped media and evidence logs show P4 `SPONGE-VAR` (64M) and P5 `SPONGE-HOME` (1024M); one naming authority prevents doc drift. |
| **D16R.4** | **Per-runtime config authority.** Exactly one producer per runtime config: `sponge_pkgd` emits the `pkg_runtime`-class configs it owns, scoped to the changed runtime only; `sponge_supervisor` owns the `system` sandbox config; `sponge_configd` owns the user-config store. No component emits another authority's config. Config regeneration is dirty-scoped: changing one runtime's package set never regenerates or restarts another runtime. | Single-writer discipline (`AGENTS.md` §1.2) applied to config emission; containment of config churn is a target failure-domain property (FD3, see `docs/evidence/config-regen-survival.md`). |
| **D16R.5** | **Pure library extraction strictly before topology changes.** The pkgd logic to be rewired (`binary_prefix` rewriting, launch model, admission math, config emission) is first extracted into pure libraries with no I/O and no `env()` access, with tests (P1), and only then is the topology changed (P2.2, P2.4). No topology commit may land before its input libraries exist. | Rewiring tested pure code is reviewable and revertable; rewiring live I/O code is how the create-chain regressions happened. |
| **D16R.6** | **Runtime classes, not universal per-package init.** Packages declare a runtime class in package metadata (`docs/12-package-format.md`, new attribute): `simple` (plain ROM start node), `terminal` (noux/sub-init packages needing `binary_prefix` + tar payloads), `browser` (heavy WebEngine closures). pkgd places each package into the matching runtime's config. There is no per-package init and no universal per-package-init framework. | The three classes match the three real shapes in the tree (docs/17 §1); a universal init would rebuild `pkg_runtime` as a framework and re-encode every special case. |
| **D16R.7** | **DE survives package-control failures.** When `sponge_pkgd` is unresponsive or a package operation fails, sponge-de stays interactive and labeled (`⚠ sponge_pkgd unresponsive(N)` health label, structured error on the operation), and never freezes. Package-control calls from the DE are bounded and never block the DE event loop. | The field freeze signature made package control a desktop-wide failure domain; the isolation model held but liveness did not (docs/17 header). |
| **D16R.8** | **Provider execution paths separated without new partitions.** The package-store File_system path and the log-sink File_system path are separated (`vfs_pkg` / `vfs_log` as distinct labels/paths/providers) at the session and config level only. Both are served from the existing partition P4/P5; no new partition, no new filesystem, no new driver. | A slow or denied log session must not starve package-store sessions (FD4). Separation is a wiring property, not a storage-layout change (D16R.2). |
| **D16R.9** | **No new Genode base async semantics.** All session-path fixes use the existing non-blocking machinery (`Env::try_*`, `try_session`, `Service::complete_create`, `async_client_notify`, rows #28-#31). No new Parent RPC semantics, no new `Genode::Env` virtuals (the row #29 vtable-ABI lesson), no new async framework in base. If work appears to need new base semantics: stop, write the evidence, record a §4.2 ledger candidate, and re-plan. | The non-blocking chain (#25-#31) is young and has one open regression on it (D16R.17). Widening base semantics while refactoring providers multiplies the blast radius. |
| **D16R.10** | **Patch classification before pruning; solo fork+pin commits.** Every patch-ledger row touched by this phase is classified **keep / remove / replace** with evidence recorded first (P4.1). Pruning (retiring a row, dropping a patch, deleting a code path) may only happen after its classification exists. Each patch change is a solo commit on the `sponge-os-genode` fork with a submodule pin bump here, ledger row committed before the patch commit, and paired rows (like #19/#20) retired together. | `AGENTS.md` §5.2 and the ledger retirement rule ("never silently absorbed"); classification-first is what "classify before pruning" means. |
| **D16R.11** | **Real launcher first paint + input, not boot markers.** Any launcher/DE acceptance gate requires observed first paint (pixel or report-ROM evidence) plus a real input event (QMP click or keystroke) that produces a measurable state change. `Run script execution successful.` or a boot marker alone never satisfies an acceptance gate. | Misleading-success-output defense (`docs/08-development.md` §4.1); "the launcher is up" is a liveness claim and must be proven by using it. |
| **D16R.12** | **QEMU evidence is never labeled real-hardware PASS.** Every receipt says which machine produced it. Real-hardware status only comes from a physical boot protocol (Phase 15-3 style, user-executed). | Honest disclosure (`AGENTS.md` §1.4); the Phase 15 matrix policy. |
| **D16R.13** | **Single serial build/scenario runner.** All builds and scenario runs go through one runner: `./tool/build run <scenario>` (that is `make -j1 -C genode/build/x86_64 run/<scenario> KERNEL=sel4 BOARD=pc`), one at a time, no concurrent make in `genode/build/x86_64`. Parallel tasks may edit files in parallel; their verification runs serialize through this runner. | Phase 12 row-28 discipline (`docs/08` §4.0.1); concurrent makes corrupt the shared build dir. |
| **D16R.14** | **Disjoint file ownership.** Each task lists the files it exclusively owns for its wave. Two tasks active in the same wave never edit the same file. Shared files (`docs/11-environment.md`, shared `run/*.inc`, `docs/17`) are touched only at their declared serial points. | Makes parallel waves safe and commits independently revertable. |
| **D16R.15** | **Independently revertable atomic commits.** Every commit builds and boots on its own, reverts cleanly on its own, and carries its own gate (§8). Conventional commits per `AGENTS.md` §4.3. | Bisectability; the row #29 disaster (a stale binary against a changed vtable) is the failure mode this prevents. |
| **D16R.16** | **Baseline and red tests before product edits.** P0 records the full green/red/partial baseline before any product code changes. Every behavior-changing task lands its failing (red) scenario and RED receipt as a commit before the first product-edit commit flips it green. | TDD orientation of the phase-14/16 plans; without a red first, a "fix" cannot be shown to fix anything. |
| **D16R.17** | **The open NIC uplink regression is captured in P0 and classified in P4 before any pruning.** `run/sponge-pkg-driver` fails at its DHCP gate today (driver comes up, `driver-probe: PASS`, but the uplink session across driver runtime -> system sandbox -> top-level init -> `nic_router` never completes; the pre-change run reached `dhcp offer from 10.0.2.2`). It is a regression of the create chain around ledger row #31. It is captured (P0.2) before refactors churn the create path, root-caused and classified (P4.2) with evidence, and only then dispositioned. Trap: the rejected upgrade-wedge fix broke `sponge-pkg-driver`'s e1000 (`docs/evidence/sponge-upgrade-wedge.md`, fork `3df26cf22b` reverted). | A known red on the exact machinery this phase restructures must be pinned before the restructure and classified before anything is removed. |
| **D16R.18** | **Follow-ups are registered, never silently delivered.** The items in §11 remain follow-ups even when this phase fixes their root causes. A criterion closes only via its own named acceptance scenario in its own phase. | `AGENTS.md` §1.4 honest disclosure; no silent re-scope in either direction. |

## 4. Target Failure Domains

The overhaul is designed against these five failure domains. Each row is a
required property, verified by the named evidence.

| FD | Domain | Current failure mode (evidence) | Target property | Verified by |
|---|---|---|---|---|
| **FD1** | Session-create / provider chain | A forwarded create can park the sandbox entrypoint and take a subtree down; the create chain crossing driver runtime -> system sandbox -> top-level init -> `nic_router` currently never completes in `run/sponge-pkg-driver` (uplink regression, `docs/evidence/forwarded-create-starvation.md` §7) | Session-create never parks an entrypoint, never blocks on I/O (P3.3), and the uplink path is dispositioned (P4.2) | P3.1 red scenario; `run/sponge-pkg-driver` DHCP gate; uplink disposition receipt |
| **FD2** | Package control vs the desktop | `sponge_pkgd` unresponsiveness or a failed install freezes or degrades the whole DE (field freeze signature) | DE stays interactive and labeled; package-control failures are contained to the operation (D16R.7) | P2.1 red scenario (`de-survival-probe`) |
| **FD3** | Runtime config regeneration | A package-set change regenerates runtime config broadly; config churn can disturb unrelated children (`docs/evidence/config-regen-survival.md`, `containment-churn.md`) | Dirty-scoped emission under per-runtime authority (D16R.4): one runtime's change never touches another runtime | P1.1 red scenario (`config-authority-probe`) |
| **FD4** | Storage/log I/O paths | Log-sink (`sponge_logdisk`) and package-store File_system sessions share provider paths; a slow or denied log session can starve package-store creates (the `vfs_log` label denial in `docs/evidence/logdisk-field-diagnosis.md`) | `vfs_pkg` and `vfs_log` are separate provider paths (D16R.8); a stalled provider does not block unrelated sessions | P3.1 red scenario (`provider-isolation-probe`) |
| **FD5** | Session-machinery patch set | Ledger rows #25-#31 are young, one open regression sits on them, and an unshipped fix attempt broke the e1000 driver | Every row is classified keep/remove/replace with evidence; every change is a solo, independently revertable fork+pin commit; nothing is pruned before classification (D16R.10, D16R.17) | P4.1 classification table; P4.2 disposition; ledger diffs in solo commits |

## 5. Phased Tasks (P0-P5, 26 tasks)

Gate convention: each task names its scenario and marker. Scenario rules
per `docs/08-development.md` §4: build list includes `lib/ld`,
`build_boot_image [build_artifacts]`, bounded `run_genode_until`, committed
symlink at `repos/sponge/run/`. "RED receipt" = the committed log showing
the scenario failing at its gate before the product edit. "PASS" always
means QEMU-labeled (D16R.12).

### P0. Baseline, capture, audit (no product edits)

**P0.1 Baseline matrix.**
Measure the current UEFI USB product topology before product edits and
record every built-in desktop package (`hello`, `terminal`, `textedit`,
`files`, `calculator`, `pdf_view`, `falkon`, `debug`) as GREEN / RED /
NOT-MEASURED for launcher request, process boot, first paint, and input.
Record the existing early-PASS holes explicitly: opt-in launch gate,
request acknowledgement versus paint, warning-only stages, and the weak
compositor OR marker. The full 36-scenario regression envelope runs only at
P5.3, not in this baseline task.
Gate: `docs/evidence/phase16-runtime-baseline.md` committed with exact
commands and curated receipts; missing coverage is NOT-MEASURED, never PASS.
Commits: `docs(evidence): record the phase-16 runtime baseline matrix` (1).
Owns: `docs/evidence/phase16-runtime-baseline.md`, `docs/evidence/phase16-runtime-baseline-*.log`.

**P0.2 Uplink capture.**
Capture the NIC uplink regression in a bounded, reproducible form before
any create-path refactoring: full serial logs of `run/sponge-pkg-driver`
at the DHCP gate, the passing pre-change reference logs, and the exact
fork-commit chain (`74c84f9f0a` -> `e0c2706225` -> `205b84fcad` ->
`82f0143d1f`). No fix, no code change.
Gate: `docs/evidence/forwarded-create-starvation.md` §7 gains a dated
capture subsection (or a sibling `uplink-regression-capture.md`) that an
implementer can re-run from.
Commits: `docs(evidence): capture the pkg-driver uplink regression before the runtime restructure` (1).
Owns: the capture evidence file. Serial point with P4.2 (same file family).

**P0.3 Config coupling audit.**
Map every config producer and consumer and every `binary_prefix` rewrite
point: which component emits which runtime config (`sponge_pkgd` ->
`pkg_runtime` / `driver_runtime`, `sponge_supervisor` -> `system`, run-script
`install_config`), which threads can touch storage at session-create time
(pkgd `Bootstrap` thread pattern, `main.cc` init order `_init_bake` before
`_init_admission`), and where `rom_pkg` / `rom_sys` / `rom_lib` are rooted
physically (needed by P2.6).
Gate: `docs/evidence/phase16-runtime-config-coupling.md` with the
producer/consumer table and the I/O-at-create inventory.
Commits: `docs(evidence): record the runtime config coupling and create-path I/O audit` (1).
Owns: that evidence file.

**P0.4 DE failure-mode audit.**
Enumerate sponge-de failure modes under package-control failure (health
watcher exclusions for `alpha_probe`/`cfg_probe`, the
`⚠ sponge_pkgd unresponsive(N)` label path, the panel health surface), and
record the target degraded-state contract P2.5 must implement.
Gate: `docs/evidence/phase16-runtime-de-failure-modes.md` with the
mode/expected-behavior table.
Commits: `docs(evidence): record sponge-de package-control failure modes` (1).
Owns: that evidence file.

**P0.5 Patch and create-path I/O audit.**
Inventory ledger rows #25-#31 (and any row whose files this phase touches)
with their fork SHAs, plus every storage I/O reachable from a session
create. This is the raw input for P4.1 classification; no classification
decisions are made here.
Gate: `docs/evidence/phase16-runtime-patch-io-audit.md` committed.
Commits: `docs(evidence): inventory session-machinery ledger rows and create-path I/O` (1).
Owns: that evidence file.

### P1. Config authority and pure-library extraction

**P1.1 Config-authority red test.**
New scenario `run/sponge-config-authority.run`: install/remove a
package in one runtime and assert (report-ROM byte match) that only that
runtime's config changed and no other runtime restarted. Currently red
(whole-regen behavior).
Gate: RED receipt committed; marker `config-authority-probe: PASS` absent.
Commits: `test(pkgd): add the per-runtime config-authority red scenario` (1).
Owns: `run/sponge-config-authority.run`, `repos/sponge/run/` symlink,
its probe source under `repos/sponge/src/test/`.

**P1.2 Pure library extraction.**
Extract from `sponge_pkgd` into pure libraries (no I/O, no `env()`):
`binary_prefix` rewriting, the launch model (`Start_model::matches()`
semantics), admission math, and config-emission templating. Each library
lands with unit-level tests (test component or host-side check per
`docs/08` conventions).
Gate: `tool/build run` on the affected product scenarios unchanged
(no behavior delta); new lib tests green.
Commits: `refactor(pkgd): extract the binary_prefix rewriter into a pure library` +
one commit per extracted library (3-4).
Owns: `repos/sponge/lib/<new pure libs>/`, `repos/sponge/include/sponge/<lib headers>`,
and the call-site rewiring inside `repos/sponge/src/sponge_pkgd/` files it
touches per commit.
Landed: `sponge_binary_prefix`, `sponge_pkg_launch`, `sponge_pkg_config`,
and `sponge_pkg_admission` now cover all four pure extraction targets. The
admission library's 153-check equivalence/boundary suite passes, and the
seL4/QEMU admission and lifecycle scenarios retain their previous verdicts
and refusal figures. Receipt: `docs/evidence/phase16-runtime-config-coupling.md`
§14.

**P1.3 pkgd module split.**
Split `sponge_pkgd` into modules along the existing thread boundary:
constructor + `Bootstrap` (all storage I/O) versus the pure core (decides,
never blocks). No new threads; the split is file and linkage structure only.
Gate: product scenarios unchanged; `docs/evidence/phase16-runtime-config-coupling.md`
updated with the module map.
Commits: `refactor(pkgd): split the IO bootstrap from the pure core` (1-2).
Owns: `repos/sponge/src/sponge_pkgd/` (exclusive for this wave).

**P1.4 Dirty-scoped emission.**
Implement per-runtime dirty tracking and scoped emission (D16R.4): a
package-set change regenerates only the affected runtime's config. This is
the commit that flips P1.1 green.
Gate: `run/sponge-config-authority.run` -> `config-authority-probe: PASS`.
Commits: `fix(pkgd): regenerate only the dirty runtime config` (1) + green receipt commit.
Owns: the pkgd emission module from P1.3.
Landed: `e5ccb99a9d` (`fix(pkgd): stop cross-runtime config republication`).
QEMU/seL4 receipt: `docs/evidence/phase16-runtime-config-coupling.md` §12.

**P1.5 Emission validation.**
Every emitted runtime config is validated before install (init.xsd /
`install_config`-equivalent validation; validator parity for any configd
key touched). Malformed emission fails loudly with a structured error,
never a silent drop.
Gate: a negative case in `run/sponge-config-authority.run` (or a
sibling) proving a malformed emission is rejected loudly; all product
scenarios green.
Commits: `fix(pkgd): validate emitted runtime configs before install` (1).
Owns: the pkgd emission module + its validation helper.
Landed: all dirty targets are rendered and validated before any target is
published; malformed emission returns a structured operation error and
restores the dirty marks. The 80-case host self-test, malformed-emission
scenario, config-authority scenario, regeneration-survival scenario, and
its empty-config positive control are green. Receipt:
`docs/evidence/phase16-runtime-config-coupling.md` §13.

### P2. DE survival and runtime grouping

**P2.1 DE failure red test.**
New scenario `run/sponge-de-pkgd-failure.run`: wedge or kill `sponge_pkgd`
during a package-control operation; assert sponge-de stays interactive
(input still reaches it), shows the health label, and surfaces a structured
error. The 2026-10-28 QEMU RED receipt is recorded in
`docs/evidence/phase16-runtime-de-failure-modes.md` §7: C1-C3 and P1-P5 are
green, only the documented C4 machine-witness and C5 persistence gaps are
RED, and `de-survival-probe: PASS` is absent.
Gate: RED receipt committed; marker `de-survival-probe: PASS` absent.
Commits: `test(sponge-de): add the package-control failure survival red scenario` (1).
Owns: `run/sponge-de-pkgd-failure.run`, its probe, symlink.

**P2.2 Per-runtime targets.**
Introduce per-runtime build/config targets: run-script includes per
runtime class (`simple` / `terminal` / `browser`) with the quota rows from
`docs/17` §6, and the `<parent-provides>` split that keeps the driver list
(`Platform`/`Dma`/`Irq`/`Io_mem`/`Io_port`) driver-runtime-only. Consumes
P1's pure emission libraries; changes no grouping yet.
Gate: the product disk scenarios build and boot unchanged topology
(groups still as today) with the new include structure in place.
Landed: `1e4e42030c` (includes + uefi-usb), `35d2cb1ec1` (remaining four),
with hello-only acceptance support in `6dea5ad432`. The receipt is
`docs/evidence/phase16-runtime-targets.md`: all static equivalence checks
pass, BIOS/AHCI and NVMe reach `alpha-probe: PASS`, UEFI variants retain
their documented structural-only QEMU limits, and the pure emission
self-test passes.
Owns: `run/runtime-*.inc` (new), the product `run/sponge-desktop-disk*.run`
edits, `repos/sponge/run/` symlinks.

**P2.3 Terminal nested binary_prefix.**
Extend `binary_prefix` rewriting (the P1.2 pure library) to nested start
nodes, so a terminal package's sub-init children (`terminal`, `vfs`,
`vfs_rom` -> `cached_fs_rom`) resolve binaries with the `bin/` prefix
like the top-level start does. Root-cause fix for the criterion-14 blocker;
criterion 14 itself stays a follow-up (§11).
Gate: a probe in `run/sponge-de-pkgd-failure.run`'s sibling or the terminal
product scenario proving the nested children resolve; no scenario regression.
Commits: `fix(pkgd): rewrite binary_prefix for nested start nodes` (1).
Owns: the `binary_prefix` pure library (transferred from P1.2 ownership)
and its tests.
Landed by verification; no additional rewrite was needed. The stated defect
was stale: `09da75ea43` already added nested-start rewriting and P1.2
preserved it in `sponge_binary_prefix`. The unchanged host golden suite
passes, and `SPONGE_LAUNCHTEST=on run/sponge-desktop-disk-uefi-usb` passes
`alpha-probe`, the terminal request, nested VFS boot, and the final launch
gate. The historical open question and receipt are in
`docs/evidence/phase16-runtime-rom-failures.md` §9. Other media still need
the terminal binary/payload staging before making the same runtime claim.

**P2.4 Grouped runtimes.**
Place packages into the three runtime classes (D16R.6) per the new package
metadata attribute: `pkg_runtime` splits into the `simple`, `terminal`,
and `browser` runtime configs emitted per-class by pkgd from P2.2's
targets. No per-package init is introduced; `docs/12-package-format.md`
gains the class attribute.
Gate: product disk scenario boots with grouped runtimes; per-scenario
quota table in `docs/17` §6 updated and consistent; launcher reaches first
paint with input (D16R.11).
Commits: `feat(pkgd): group packages into simple/terminal/browser runtimes` (2-3)
+ `docs(12): add the runtime class package attribute` (1).
Owns: pkgd grouping module, `docs/12-package-format.md` section, the
runtime configs under P2.2's includes.

**P2.5 DE hardening.**
Implement the P0.4 target contract: bounded, non-blocking package-control
calls on the DE side; health label with the responsive/unresponsive state;
structured errors on failed install/remove; no DE freeze on pkgd death.
Gate: `run/sponge-de-pkgd-failure.run` -> `de-survival-probe: PASS`.
Commits: `fix(sponge-de): survive package-control failures with a labeled degraded state` (1-2)
+ green receipt.
Owns: `repos/sponge/src/sponge-de/` (exclusive for this wave).
Landed: `6d5378684a` adds the persistent `de_op_error` launch-error witness
without changing the scenario or probe. The existing bounded launch path,
health label, and transient panel messages are unchanged. The scenario now
reports `de-survival-probe: PASS` and the design receipt is GREEN; targeted
`sponge-de-test` and `sponge-launch` regressions also pass.

**P2.6 Falkon / browser staging.**
Stage the falkon + WebEngine closure into the browser runtime's payload
area (the `rom_pkg` root as confirmed by P0.3), inside the existing
three-partition layout (D16R.2). Measure the complete closure first and grow
P3 on newly generated media when required; never migrate or resize an
installed medium. Deliver real launcher open, first paint, and basic input
(criterion 13). Page load stays a follow-up.
Gate: the browser runtime launches from the real launcher and reaches first
paint plus basic input on the product scenario; the receipt records the
staged size and new-media P3 size.
Commits: `build(run): stage the browser runtime payload set` (1) +
`docs(13): record the browser media sizing` (1 when sizing changes).
Owns: the browser runtime include, staging steps in the product run
scripts, new-media sizing in `tool/dist.mojo` / `tool/mkdata.mojo`, and
`docs/13-installation.md`.

### P3. Provider separation and create-path I/O removal

**P3.1 Provider failure red test.**
New scenario `run/sponge-provider-stall.run`: stall one File_system
provider (log sink) during session traffic and assert unrelated sessions
(package store creates, GUI, ROM) continue and no entrypoint parks.
Currently red or partially red.
Gate: RED receipt committed; marker `provider-isolation-probe: PASS` absent.
Commits: `test(providers): add the provider-stall isolation red scenario` (1).
Owns: `run/sponge-provider-stall.run`, its probe, symlink.
Landed: `66509efd9c` (`test(run): add the provider-stall isolation red scenario`).
QEMU/seL4 RED receipt: `docs/evidence/phase16-runtime-provider-stall.md`.

**P3.2 vfs_pkg / vfs_log split.**
Separate the package-store and log-sink File_system provider paths
(D16R.8): distinct labels/paths (`vfs_pkg`, `vfs_log`) wired in the runtime
configs and the `sponge_logdisk` policy, both served from partition P4/P5.
No new partitions (D16R.2); no new base semantics (D16R.9).
Gate: `run/sponge-provider-stall.run` -> `provider-isolation-probe: PASS`;
`sponge_logdisk` still writes `/log/boot.log` on SPONGE-VAR when enabled.
Commits: `fix(vfs): separate the package-store and log-sink session paths` (1-2).
Owns: the runtime config includes' vfs blocks, `sponge_logdisk` wiring,
the policy docs lines in `docs/17`.

**P3.3 create_session I/O removal.**
Remove all storage I/O from session-create paths (FD1): creates decide via
the pure core and hand actual I/O to the existing bootstrap/worker thread
pattern. Uses only the row #28-#31 non-blocking machinery (D16R.9).
Gate: P3.1 green for the create half; `run/sponge-pkg-driver` uplink gate
re-measured (captured either way for P4.2; no requirement that it passes
here); product scenarios green.
Commits: `fix(pkgd): remove storage I/O from session create paths` (1-2).
Owns: the pkgd create-path modules (P1.3 module map).
Landed: `2c12922a7e` (bootstrap metadata cache — dispatch parses from
bounded owned memory, no ROM session at dispatch), `013a4a262f` (store
flush on the Bootstrap worker — Semaphore/Signal_transmitter handoff,
strict global FIFO with dequeue de-dup, recipe-free busy error, inline
idle drain, synchronous fallback), `513d64e658` (store wait discipline —
the entrypoint-affine `commit_and_wait` replaced by timer-paced polls
plus the non-blocking `commit()` wakeup), plus the exclusive-group
placement fix `d80ff8d4c3` and the two red-first regressions
(`0509bba660`/`947f288c47` exclusivity, `1af54a29ff` dispatch-liveness).
The dispatch-liveness scenario flipped red -> green at the worker
handoff; `run/sponge-persist-disk` (real disk storage, two boots) is
green end to end; `run/sponge-pkg-driver` reached its DHCP gate
(leased 10.0.2.15) in the re-measurement — captured for P4.2, not
dispositioned here. Receipt: `docs/evidence/phase16-runtime-p33.md`.

**P3.4 Supervisor gates.**
Scope the existing supervisor gate vocabulary (`startup_grace_ms`,
`heartbeat`/`restart_after_skipped`, `restart_budget`, `grace_ms`,
`settle_ms`) per runtime subtree so one runtime's failure is contained to
it. No new gate semantics; only per-subtree values and wiring in
`sponge_supervisor`.
Gate: a failure injected into one runtime leaves the other runtimes and the
DE alive in the same scenario family; product scenarios green.
Commits: `feat(supervisor): scope lifecycle gates per runtime subtree` (1).
Owns: `repos/sponge/src/sponge_supervisor/`, the gate values in the runtime
includes.

Landed: `6d41b05937` (per-name `@KILL:<name>@` markers + the fixed
name->version table + the `target=/bumped=` publish receipt + bounded
construction-version-change logging + attribution + per-subtree
`grace_ms.<name>`/`settle_ms.<name>` lookups), `70e57cd384` (run-side
stamping/policy/arm machinery in the shared includes), `105f5fed9e`
(per-child heartbeat exposure in the five product scenarios +
`driver_runtime.inc`, byte-identical defaults, and the red-first
`SPONGE_SUPERVISOR_TEST=contain` arm), `c280ed1aad` (the flip: CONTAIN
bumps only the attributed runtime child). The contain arm fails red on
the old global behaviour (target=global, 13 sibling construction
versions rebuilt) and proves containment-by-name green
(`target=pkg_runtime_fault bumped=1`, siblings and DE stable, panel
shown once, post-wedge `alpha-probe: PASS`, no console). The victim's
process revival is blocked by a pre-existing base-layer
create-after-teardown failure that also breaks the pre-existing product
takeover arm's kill receipt; exact blocker and follow-up in
`docs/evidence/phase16-runtime-p34.md` §5. Receipt:
`docs/evidence/phase16-runtime-p34.md`.

### P4. Classification and disposition (prune only after classify)

**P4.1 Patch classification.**
Classify every ledger row from P0.5 as **keep / remove / replace**, each
with its evidence reference and its `Drop When` condition updated where
needed. Rule (D16R.10): the classification table lands as evidence and a
`docs/11-environment.md` ledger edit BEFORE any row is pruned; each patch
change is a solo fork commit + submodule pin bump here, ledger row first,
patch second; paired rows retire together.
Gate: `docs/evidence/patch-classification.md` committed;
every touched row has a class and evidence pointer.
Commits: `docs(evidence): classify session-machinery ledger rows keep/remove/replace` (1),
then per executed removal/replacement: `docs(11): retire ledger row #N` +
`vendor(genode): ...` + `chore(genode): bump the submodule pin` (solo each).
Owns: `docs/11-environment.md` (serial point), the classification evidence
file, solo `genode/` pin commits.

**P4.2 Uplink disposition.**
Root-cause the NIC uplink regression against the refactored create path
and classify it: fixed in place (keep), reverted (remove), or replaced by
a different fix within existing async semantics (replace). The disposition
updates `docs/evidence/forwarded-create-starvation.md` §7 and the P4.1
table. No pruning of create-chain rows or paths happens before this
disposition exists (D16R.17). Trap: never ship a variant that breaks the
e1000 path (`docs/evidence/sponge-upgrade-wedge.md`).
Gate: disposition written with evidence; `run/sponge-pkg-driver` reaches
its DHCP gate again OR the gap is recorded with reproduction notes and a
target phase (honest disclosure, no silent drop).
Commits: `docs(evidence): dispose of the pkg-driver uplink regression` (1)
+ any follow-up `fix(genode): ...` solo fork commit + pin bump (0-2).
Owns: the uplink evidence file (serial point with P0.2), solo fork commits.

**P4.3 Conditional removals.**
Execute the removals/replacements that P4.1/P4.2 made conditional: delete
superseded code paths, retire rows, drop patches, each in its own
independently revertable commit with its ledger edit. Anything not
classified stays untouched.
Gate: `./tool/build verify` green (`tool/patches verify && tool/hw_compat assert`);
each removal has a before/after scenario receipt.
Commits: one solo commit per removal (`revert`/`refactor`/`vendor` as fits).
Owns: per-removal files as listed in the P4.1 table.

### P5. Acceptance, docs, full sweep

**P5.1 Final acceptance.**
Acceptance on IMG (disk) product media (D16R.1): every built-in desktop
package launches from the real launcher, reaches first paint, accepts basic
input, and passes sequential/concurrent/repeated execution; killing or
wedging one app leaves the DE and siblings alive; package-control failure
leaves the DE interactive (P2.5); provider stall is contained (P3.2/P3.3).
Every receipt is QEMU-labeled (D16R.12).
Gate: an acceptance checklist committed with one receipt per app and per
failure-isolation property, green three consecutive times.
Commits: `docs(evidence): record the phase-16 runtime acceptance receipts` (1).
Owns: `docs/evidence/phase16-runtime-acceptance.md`.

**P5.2 Docs sync.**
Sync docs with the delivered behavior (one docs commit per file):
`docs/17-runtime-component-topology.md` (grouped runtimes, provider split,
supervisor gates, quota table), `docs/14-boot-storage-architecture.md` §4.3
(naming authority per D16R.3: P3 GENODE / P4 SPONGE-VAR / P5 SPONGE-HOME),
`docs/12-package-format.md` (runtime class attribute), `docs/09-roadmap.md`
(follow-ups register pointer), `docs/13-installation.md` (any staging
disclosure). No behavior claim without its scenario receipt.
Commits: `docs(17): record the grouped runtime topology` etc. (4-5).
Owns: the listed doc files (serial).

**P5.3 Full sweep + review-work.**
Final regression sweep: the full 36-scenario envelope of
`docs/evidence/phase16-index.md` §4 plus every scenario added by this
phase, run serially through the single runner (D16R.13, `make -j1`, no
concurrent make), receipts committed. Then the `review-work`
post-implementation review must pass before handoff.
Gate: sweep matrix committed (all green or each partial documented with a
follow-up pointer); review-work pass recorded.
Commits: `docs(evidence): record the phase-16 runtime full sweep` (1) +
close-out docs (1).
Owns: `docs/evidence/phase16-runtime-sweep.md`, sweep logs.

## 6. Dependency Graph

| Task | Depends on | Reason |
|---|---|---|
| P0.1 | none | Baseline before anything (D16R.16) |
| P0.2 | none | Capture before the create path is touched (D16R.17) |
| P0.3, P0.4, P0.5 | none | Audits; parallel with disjoint outputs |
| P1.1 | P0.1 | Red test asserts against the recorded baseline |
| P1.2 | P0.3 | Extraction targets come from the coupling audit |
| P1.3 | P1.2 | Split re-links the extracted libraries |
| P1.4 | P1.1, P1.3 | Flips the red test via the split modules |
| P1.5 | P1.4 | Validates what emission now produces |
| P2.1 | P0.1, P0.4 | Red test encodes the audited failure contract |
| P2.2 | P1.2, P1.5 | Targets emit through validated pure libraries |
| P2.3 | P1.2 | Extends the extracted `binary_prefix` library |
| P2.4 | P2.2, P2.3, D16R.5 | Grouping rewires the per-runtime targets (topology change after extraction) |
| P2.5 | P2.1 | Hardening flips the DE red test |
| P2.6 | P2.4, P0.3 | Browser staging needs the browser runtime and the audited ROM roots |
| P3.1 | P0.1, P0.5 | Red test encodes the audited create-path I/O |
| P3.2 | P3.1 | Split flips the isolation half |
| P3.3 | P1.3, P3.2 | I/O removal reuses the split core and the separated paths |
| P3.4 | P3.3 | Gates contain what the create path now guarantees |
| P4.1 | P0.5, P2.x, P3.x | Classification uses the refactor's evidence |
| P4.2 | P0.2, P3.3 | Disposition measured against the refactored create path |
| P4.3 | P4.1, P4.2 | Pruning only after classification (D16R.10) |
| P5.1 | P2.5, P2.6, P3.4 | Acceptance composes the delivered properties |
| P5.2 | P5.1 | Docs describe what acceptance proved |
| P5.3 | all | Sweep + review-work close the phase |

**Waves:** P0 (audits parallel) -> P1 (serial) -> P2 ∥ P3 (file-disjoint,
see §7) -> P4 (serial) -> P5 (serial).
**Critical path:** P0.1 -> P1.2 -> P1.3 -> P1.4 -> P2.2 -> P2.4 -> P5.1 -> P5.3.

## 7. File Ownership and Serialized Verification

**Ownership (exclusive per wave; transfer notes explicit):**

| Wave | Task | Exclusive files |
|---|---|---|
| P0 | P0.1-P0.5 | each its own `docs/evidence/phase16-runtime-*.md` + logs |
| P1 | P1.1 | `run/sponge-config-authority.run` + probe + symlink |
| P1 | P1.2-P1.5 | `repos/sponge/lib/<pure libs>/`, `repos/sponge/include/sponge/`, `repos/sponge/src/sponge_pkgd/` |
| P2 | P2.1, P2.5 | `run/sponge-de-pkgd-failure.run` + `repos/sponge/src/sponge-de/` |
| P2 | P2.2, P2.4, P2.6 | `run/runtime-*.inc`, product `run/sponge-desktop-disk*.run`, pkgd grouping module |
| P2 | P2.3 | the `binary_prefix` pure library (transferred from P1.2) |
| P3 | P3.1 | `run/sponge-provider-stall.run` + probe + symlink |
| P3 | P3.2-P3.4 | vfs wiring in the runtime includes, `sponge_logdisk` wiring, `repos/sponge/src/sponge_pkgd/` create modules, `repos/sponge/src/sponge_supervisor/` |
| P4 | P4.1-P4.3 | `docs/11-environment.md` (serial point), solo `genode/` commits + submodule pin |
| P5 | P5.1-P5.3 | acceptance/sweep evidence, the five docs files |

**Serialized points (never parallel):** `docs/11-environment.md`; shared
`run/*.inc` edits; `docs/17-runtime-component-topology.md`; the `genode/`
submodule pin; and **all verification runs** (D16R.13). P2 and P3 run in
parallel only while P2 stays out of `repos/sponge/src/sponge_pkgd/`
grouping-free files and P3 stays out of `repos/sponge/src/sponge-de/`;
where they must both touch pkgd (P2.4 grouping vs P3.3 create modules),
the tasks run serially in the listed order.

**Serialized verification rule:** one runner at a time. A task's gate run
takes the runner slot (`./tool/build run <scenario>`), records its receipt,
then releases it. A gate may never run concurrently with another gate or
with `./tool/build prepare`/`ports`.

## 8. Atomic Commit and Gate Policy

- Conventional commits (`AGENTS.md` §4.3): `test` / `fix` / `feat` /
  `refactor` / `docs` / `build` / `chore` / `vendor`. One logical change
  per commit.
- **Red before green (D16R.16):** the first commit of each behavior task is
  its failing scenario + RED receipt; the product-edit commit flips it.
  Example sequence:

  ```
  test(pkgd): add the per-runtime config-authority red scenario
  gate: run/sponge-config-authority.run runs and FAILS
        (config-authority-probe: PASS absent); RED receipt committed

  fix(pkgd): regenerate only the dirty runtime config
  gate: same scenario reaches config-authority-probe: PASS; build green
  ```

- **Vendor sequence (D16R.10), ledger row first, patch second:**

  ```
  docs(11): record ledger row #N classification/disposition
  gate: ledger row committed before any tree change

  vendor(genode): <the single-purpose patch change> (ledger row #N)
  gate: solo commit touching only the fork tree; tool/patches verify green

  chore(genode): bump the submodule pin to <fork SHA>
  gate: solo commit touching only the pin; full build green
  ```

- **Every commit:** builds independently, boots its affected scenario
  independently, reverts independently (D16R.15). No commit may depend on a
  later commit to compile or boot. A red-scenario commit must still leave
  the tree building (the scenario fails at its probe, not at build).
- **Gate labels:** every commit message body names its gate (scenario +
  marker + QEMU label). `docs(...)` commits carry no code; `vendor(...)`
  commits carry no Sponge-side code.
- **Verification serialization:** per §7. Receipts live in
  `docs/evidence/`; nothing is marked done without its receipt.

## 9. Verification Contract

1. **Baseline first:** no product commit before P0.1's baseline is
   committed (D16R.16).
2. **Per-task gates:** exactly as each task states; a task is not complete
   until its gate receipt exists.
3. **Acceptance shape (D16R.11):** launcher/DE acceptance requires first
   paint + a real input event with a measurable effect; boot markers never
   suffice. Acceptance runs on IMG/disk media (D16R.1).
4. **Labeling (D16R.12):** every receipt names the machine (QEMU
   `KERNEL=sel4 BOARD=pc` or physical). Real-hardware PASS is out of scope
   for this phase and never claimed.
5. **Final sweep (P5.3):** the full 36-scenario envelope plus this phase's
   new scenarios, serial `make -j1`, receipts committed; each partial
   carries a follow-up pointer.
6. **review-work:** the post-implementation review must pass before
   handoff; its verdict is recorded in the close-out evidence.
7. **Docs sync:** no behavior claim in docs without a scenario receipt
   (P5.2).

## 10. Commit Budget (estimate)

P0: 5 docs commits. P1: ~6 (1 test + 3-4 refactor + 1-2 fix).
P2: ~9 (2 test + 3-4 feat/fix + 1-2 build + docs). P3: ~5.
P4: ~5 + per-removal solo commits (0-4). P5: ~4.
Total: ~35-40 commits, each independently revertable.

## 11. Follow-ups Register (NOT delivered by this phase)

Per D16R.18, these remain open follow-ups with their roadmap identity.
Fixing a root cause here never closes the item.

| Follow-up | Roadmap identity | What this phase does instead |
|---|---|---|
| vct-install-in-terminal | Phase 16 Extension criterion 14 (terminal + `vct install` flow) | P2.3 fixes the nested `binary_prefix` root cause only; resolve/download/install/run acceptance stays follow-up |
| Falkon page load | Phase 16 Extension criterion 15 | P2.6 delivers criterion 13 (launcher open + first paint + basic input); NIC wiring + page acceptance stay follow-up |
| Additional shells (zsh, fish) | Phase 16 Extension feature | Terminal runtime accommodates future noux tars; no shell ports here |
| Same-program multi-instance | Phase 16 Extension feature | pkgd keeps the `already-running` idempotent semantics; per-instance launch model is follow-up |
| ISO ceiling / ISO payloads | D16.7 honest disclosure; `docs/14` §2.1 ceilings | ISO stays metadata-only; no ceiling work |
| Phase 17 UI (Sponge IME, CJK) and the Phase 16 §6 register rows unrelated to browser launch (panel.position left/right, real-hardware multi-panel, user background upload) | `docs/09-roadmap.md` Phase 17; `docs/evidence/phase16-index.md` §6 | Untouched; not silently absorbed |

## 12. Open Questions (recorded, not blocking)

1. **rom_pkg physical root.** P0.3 confirms where the package-payload ROM
   is rooted today. The staging rule is already fixed (P2.6 stages the
   measured closure and grows P3 only on newly generated media when
   required), so the answer changes only where P2.6 points, not what it may do.
2. **Uplink root cause location.** P4.2 must decide whether the regression
   lives in the row #28-#31 machinery or in scenario-side wiring. Until
   then, no create-chain row is pruned (D16R.17).
3. **Validation mechanism (P1.5).** Whether emitted-config validation
   reuses the run-tool's `install_config`/init.xsd path or needs a small
   test component is a P1.5 implementation choice; the gate (loud failure
   on malformed emission) is fixed.
