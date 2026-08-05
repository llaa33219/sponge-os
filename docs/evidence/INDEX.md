# Phase 7 Alpha — Evidence Index

> Single source of truth for Phase 7 (`phase7-alpha`) todo artifacts. One
> artifact path + verdict per todo 1-19, plus the full task-20 regression
> suite results. Every path is relative to the repository root.

## Phase 10 (post-Alpha interactive desktop)

See [`docs/evidence/phase10-index.md`](phase10-index.md) for the W0–W6
artifact table and the scenario → criterion → PASS-marker traceability
that closes `docs/09-roadmap.md` §10.

## Todos 1-19 (build-up evidence)

| Todo | Title | Primary artifact | Verdict |
| --- | --- | --- | --- |
| 1 | Doc-first: docs/12 schema + lifecycle + update semantics | `docs/evidence/task-1-phase7-alpha.md` | ✅ docs land before any W2/W3 code (per acceptance grep checks) |
| 2 | Doc-first: docs/06 vct subcommand specs + drift fix | `docs/evidence/task-2-phase7-alpha.md` | ✅ all five new subcommands specified, §8 status updated |
| 3 | seL4 media smoke (minimal ISO + disk boot) | `docs/evidence/task-3-phase7-alpha.md` (+ `task-3-phase7-alpha-{iso,disk,gate}.log`) | ✅ grub2→bender→seL4 chain boots vct from both media |
| 4 | Unified desktop `run/sponge-alpha.run` on base-sel4 | `docs/evidence/task-4-phase7-alpha.log` | ✅ `alpha-probe: PASS` (themed panel, launcher `hello`, configd live, lz_viewer marker) |
| 5 | Disk-image media + QEMU boot QA | `docs/evidence/task-5-phase7-alpha.log` | ✅ `.img` boots to `alpha-probe: PASS`; sha256 sidecar present |
| 6 | ISO media + QEMU boot QA | `docs/evidence/task-6-phase7-alpha.log` | ✅ `.iso` boots (via `-boot d`) to `alpha-probe: PASS`; sha256 sidecar present |
| 7 | `tool/dist` (Mojo) + docs/08 manual + docs/11 host-tool rows | `docs/evidence/task-7-phase7-alpha.log` (+ `task-7-phase7-alpha-failure-channel.log`) | ✅ `./tool/dist` builds + stages both media; failure path prints missing-tool apt line |
| 8 | `sponge_pkgd` runtime-config generator fixes (`<binary>`, inline `<config>`, `<parent/>` routes, caps floor) | `docs/evidence/task-8-phase7-alpha.log` | ✅ `pkg-gui-probe: PASS` (green #00ff00 window pixel-verified through Capture) |
| 9 | Installed-vs-running lifecycle in `sponge_pkgd` (launch op, `running` attribute) | `docs/evidence/task-9-phase7-alpha.log` | ✅ `lifecycle-probe: PASS` over six steps incl. `not-installed`/`already-running` |
| 10 | Click-to-launch in Sponge DE launcher + `vct launch` (shared pkgd backend) | `docs/evidence/task-10-phase7-alpha.log` | ✅ `launch-probe: PASS` — both VCT and CLICK paths verified |
| 11 | Depot-import spike + `tool/pkg_import` | `docs/evidence/task-11-phase7-alpha.log` | ✅ qt6_textedit + falkon depot archives imported + repackaged |
| 12 | Networking spike `run/sponge-net-probe.run` on base-sel4 | `docs/evidence/task-12-phase7-alpha.log` (+ run1/run2/failure-channel) | ✅ fetchurl round-trips fixture bytes via dde_ipxe + lwip; failure-channel timeout proven |
| 13 | Terminal package (`pkg/terminal`) | `docs/evidence/task-13-phase7-alpha.log` | ✅ 22 → 329 glyph round-trip on bash keystroke echo |
| 14 | Text editor package (`pkg/textedit`) | `docs/evidence/task-14-phase7-alpha.log` | ✅ qt6_textedit renders; missing-binary failure channel verified |
| 15 | `sponge_files` Qt6 file manager component | `docs/evidence/task-15-phase7-alpha.log` (+ run1/run2) | ✅ window + navigate + copy + delete + read-only refusal verified three ways |
| 16 | Falkon browser package | `docs/evidence/task-16-phase7-alpha.log` | ⚠️ Packaged (64 ROMs, 509 MB payload) but **boot-blocked by seL4 ~256 MB boot-module ceiling** — documented limitation (D5); fix path = disk-based payload staging |
| 17 | `vct shutdown` / `vct reboot` via ACPI system report | `docs/evidence/task-17-phase7-alpha.log` | ✅ QEMU exits 0 on `vct shutdown` via acpica S5; failure-channel prints audit + escape hint |
| 18 | `vct update` + `vct search` | `docs/evidence/task-18-phase7-alpha.log` | ✅ `pkg-meta-probe: PASS` six-step matrix + real-vct `sponge-vct-search{,-json}.run` |
| 19 | Installation docs + limitations register + roadmap/README flips | `docs/evidence/task-19-phase7-alpha.md` | ✅ docs/13-installation.md written; §9 checkboxes flipped with scenario traceability; version 0.1.0-alpha |

## Todo 20 — Full regression + release verification (this todo)

| Artifact | Description |
| --- | --- |
| `docs/evidence/task-20-phase7-alpha.md` | Suite results table (34 scenarios PASS, falkon excluded), 5-log spot-check grep output, dist re-verification summary, PR-body numbers per AGENTS.md §5.1, regression diagnosis |
| `docs/evidence/task-20-logs/SUMMARY.tsv` | Raw scenario-runner transcript (every run, including pre-fix FAILs) |
| `docs/evidence/task-20-logs/SUITE_TABLE.tsv` | Deduplicated suite results table (latest verdict per scenario) |
| `docs/evidence/task-20-logs/runner.sh` | Sequential scenario runner (no concurrent makes; clean var/run between runs) |
| `docs/evidence/task-20-logs/<scenario>.log` | Per-scenario log (34 files, one per in-scope scenario) |
| `docs/evidence/task-20-logs/dist.log` | `./tool/dist` re-verification from clean var/dist (both media reach `alpha-probe: PASS`) |

### Headline numbers

- **Scenario suite: 34/34 PASS** on their designated acceptance kernels
  (linux for kernel-agnostic non-GUI + linux-only; sel4 for sel4-only and
  GUI-acceptance scenarios per `run/README.md`). Sequential, no concurrent
  makes; clean var/run between runs.
- **Documented exclusion: `run/sponge-falkon.run`** — boot-blocked by
  the seL4 ~256 MB boot-module ceiling vs Falkon's 509 MB WebEngine
  payload (todo 16, decision D5). Recorded in `docs/13-installation.md`
  §6 and `docs/evidence/task-16-phase7-alpha.log`. Not a regression.
- **Media re-verification: PASS.** `rm -rf var/dist && ./tool/dist`
  produced both artifacts fresh; `alpha-probe: PASS` appears twice (once
  per `image/{disk,iso}` mode); both `.sha256` sidecars verify.
- **Regressions found and fixed by this todo (3):**
  1. `run/sponge-vct-version.run` regex was stale (`0.0.1-pre-alpha`)
     after the todo-19 version bump to `0.1.0-alpha`. Fixed: regex
     updated to `0.1.0-alpha`. **Phase-7 regression introduced by the
     version bump commit `732fd12083`.**
  2. `repos/sponge/src/sponge-de/launcher/launcher_controller.cc`
     eagerly constructed the `launcher_request`/`launcher_result`
     sessions in its constructor (todo 10). In scenarios that wire the
     launcher for display but NOT click-to-launch
     (`run/sponge-launcher.run`, `run/sponge-alpha.run`), `init` denied
     the ROM session and sponge-de died at construction — defeating the
     controller's documented "harmless timeout" intent. Fixed: the
     sessions are now lazy-constructed on the first `request_launch()`
     call (AGENTS.md §1.2 minimum-privilege). **Phase-7 regression
     introduced by the click-to-launch commit `753a520f56`.**
  3. `pkg/terminal/metadata.xml`: the gems `terminal` binary (vendored
     Genode 26.05, unpatched) uses plain `Component::construct` without
     `env.exec_static_constructors()`. When linked with `vfs.lib.so`
     (which contributes pending static constructors via its plugin
     registry), the dynamic linker's safety check
     (`repos/base/src/lib/ldso/main.cc:440`) aborts the component at
     boot. Fixed by setting `ld_check_ctors="false"` on the terminal
     `<config>` in the package metadata (a Sponge-side config, no
     vendored-tree patch) — verified safe because the vfs plugins
     registered via those static constructors are not used by this
     start node (it routes File_system to a sibling `vfs` child
     instead). The 22-glyph first paint + 22 → 282 keystroke echo
     confirm the runtime is fully functional. **Latent upstream Genode
     26.05 issue surfaced when task-20 ran sponge-terminal on its
     README-designated acceptance kernel (sel4) for the first time;
     task-13 had verified on linux.**

### Adversarial-class verdicts

| Class | Verdict |
| --- | --- |
| `misleading_success_output` | REJECTED — every PASS claim cites a per-scenario log path; 5 spot-checks (sponge-alpha, sponge-launch, sponge-net-probe, sponge-power, sponge-terminal) grepped and pasted in `task-20-phase7-alpha.md` §3, including the net-probe byte round-trip into the GUEST log (`[init -> fetchurl] SPONGE-NET-PROBE-MARKER-7c9f2a3b`). |
| `hung_or_long_commands` | REJECTED — every gate is a bounded `run_genode_until` (scenario-defined timeouts, max 600 s for sponge-launch); sequential bounded runs; no concurrent makes (shared build dir). |
| `stale_state` | REJECTED — `rm -rf var/dist` before `./tool/dist`; runner cleans `var/run/<scenario>*` between every scenario; falkon exclusion is documented, not a silent skip. |
| `flaky_tests` | REJECTED — every in-scope scenario exits 0 on its single latest run; the three pre-fix FAILs were each reproduced, root-caused, and fixed (their post-fix runs are the verdicts in `SUITE_TABLE.tsv`). |
| `dirty_worktree` | NOTED — the three regression fixes above are uncommitted working-tree changes per the task instruction "do not commit". They are the deliverable of this todo. `git status` shows: `M run/sponge-vct-version.run`, `M repos/sponge/src/sponge-de/launcher/launcher_controller.cc`, `M pkg/terminal/metadata.xml`, plus the new `docs/evidence/task-20-*` and `var/dist/*` artifacts (the latter are git-ignored). |
