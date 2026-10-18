# Phase 7 Alpha — Evidence Index

> Single source of truth for Phase 7 (`phase7-alpha`) todo artifacts. One
> artifact path + verdict per todo 1-19, plus the full task-20 regression
> suite results. Every path is relative to the repository root.

## Design verdicts (runtime topology)

Settled by measurement rather than by source reading; each carries a
reproduction scenario.

| Question | Verdict | Artifact |
|---|---|---|
| Does a `pkgd` config regeneration that crosses the reporter-buffer boundary kill running `pkg_runtime` children? | **Falsified** — the children survive | [`config-regen-survival.md`](config-regen-survival.md) (`run/sponge-config-regen-survival.run`) |
| Does `sponge_pkgd` refuse a launch whose quota does not fit, or silently let the sandbox clamp it? | **Refused** — `insufficient-resources` with the figures | [`pkg-admission-control.md`](pkg-admission-control.md) (`run/sponge-pkg-admission.run`) |
| Does Genode 26.08's `init` grant or forward a child's `resource_request()` upward? | **No upward forwarding**; grant-from-slack works, revival only via a config change to the hosting sandbox | [`dynamic-resource-topup.md`](dynamic-resource-topup.md) (`run/sponge-resource-request.run`) |
| Can the 2026-10-16 field report ("SPONGE DEBUG appears, never finishes rendering, then dies" on the LG gram at 2560x1600, no error text) be the heartbeat, the QPA frame-pacing patch, or the remainder clamp? | **No, to all three, each by measurement.** Time to first paint is **1.3 s against a 6 s containment budget**, 1 spawn, **0** skipped heartbeats, and identical with the heartbeat removed. QPA patch A/B at the same resolution: 1300 ms patched vs 990 ms reverted — and the then-shipped patch was provably inert (inverted guard in `wait_for_frame()`); correcting it was measured to be a **24x throughput collapse** and was reverted. That blocking revision has since been replaced by **frame coalescing**, which contains no wait at all and keeps first paint un-deferred by construction. The clamp leaves the client with tens of MiB of headroom. The `uefi-usb` storage stall is a flake; the crash-respawn cascade and per-cycle RAM leak in that log are real but are its *consequence*, not the field report. **The field regression itself is not reproduced in QEMU** — `sponge_debug` now prints `construct`/`paint-begin`/`FIRSTPAINT` ms on the real console, which is what the next field boot needs | [`sponge-debug-firstpaint.md`](sponge-debug-firstpaint.md) (`run/sponge-debug-firstpaint.run`) |
| Can the top-level supervisor, proven in a lean topology, actually supervise the **product** desktop — and can it do so without fighting the sandbox's own heartbeat containment? | **Yes, and the escalation rule is what makes it safe.** The supervisor owns the `system` config on real media (templates *derived* from the staged `/system/init/system.config`, so staging stays the single source of truth), takes the display through its own nitpicker domain, and acts only when the sandbox's own containment has failed: a fault is held for `grace_ms` and re-read every tick, so a wedge the sandbox fixes by itself costs one log line. Three product-only defects were measured on the way — a config forwarded **truncated** (13662 → 3325 bytes, three children out of eleven), the boot probes' clean exit read as a fault, and `settle_ms` shorter than the respawn-to-refault time, which made the console phase structurally unreachable. `sponge-desktop-disk-nvme` stays red for a **pre-existing** reason (missing `bake::stage`, reproduced at HEAD) | [`product-supervision.md`](product-supervision.md) (`run/sponge-desktop-disk.run`, `SPONGE_SUPERVISOR_TEST=takeover`/`control`) |
| Can the display driver leave the Tier-0 static topology and become a replaceable package — and can it be switched at RUNTIME without a nitpicker restart? | **Yes, both media, and no restart needed.** `fb_vesa` (BIOS/VESA) and `fb_boot` (UEFI/GOP) are ordinary `<kind>driver</kind>` packages seeded from the media's bake manifest; the BIOS media logs `using 1024x768` and the UEFI media `using boot framebuffer: 2560x1600x32 @ 0x80000000`, both with `alpha-probe: PASS` on top. The runtime switch `vct remove fb_vesa` + `vct install fb_boot` was measured in the product topology: 1024x768 VESA out, 1280x800 GOP in, and the composited screen **came back and settled** on a rendered desktop (`247/640` sampled pixels) — so the handover is the package stop/start itself, and the supervisor-mediated `nitpicker` restart fallback turned out to be unnecessary. A second fb driver running at the same time is REFUSED by `sponge_pkgd` with `exclusive-conflict` naming the incumbent. Three latent defects had to be fixed to get there (a dropped `<config>` attribute in the config generator, case-mismatched hardware service names in `system`'s `parent-provides`, and a Genode routing trap that makes a packaged driver unable to fetch a CORE rom module — silently) | [`fb-driver-packages.md`](fb-driver-packages.md) (`run/sponge-desktop-disk.run` with `SPONGE_FB_SWITCH=on`, `run/sponge-desktop-disk-uefi.run`, `run/sponge-pkg-driver.run`) |
| Can a component log stream be made durable on the writable partition, so a freeze plus a hard reset leaves evidence? | **Partly — the mechanism works, the coverage does not, so it ships OFF by default.** The sink (`repos/sponge/src/sponge_logdisk`) writes `/log/boot.log` on SPONGE-VAR, size-capped, per-boot, with the previous boot preserved, and the product media boots and passes with it wired. Two findings are reusable: a LOG provider must not issue its `File_system` request on the thread that serves LOG writes (that deadlocked the boot right after `acpi`), and the root must be managed on a secondary entrypoint. Measured blocker for coverage: **a component inside a nested sandbox cannot obtain a `File_system` from a service provided by a sibling of the outer sandbox**, so a Tier-0 sink cannot see the nested `system` subtree — which is what a freeze investigation actually needs. Core's log is not capturable at all | [`logdisk-field-diagnosis.md`](logdisk-field-diagnosis.md) (`run/sponge-desktop-disk-uefi-usb`, `SPONGE_LOGDISK=on`) |
| Does the SPONGE DEBUG "freeze when the scrollbar is dragged fast" field report come from a resource leak? | **No leak.** 335 repaints over 30 s move **0 bytes** in every component; the log-growth variant costs 139 264 B (the text itself). The stack sustains **9-11 Hz against a requested 30 Hz** — shared-substrate CPU saturation (`docs/17` §4), not per-component exhaustion. The unbounded-backlog half (QPA plugin never registering the framebuffer sync) is ledger row **#23**, and it is now **fixed and measured**: the inert blocking wait was replaced by **frame coalescing**, which engages on this very harness — 800 submit attempts in 30 s collapse to 82 actual frame submissions (81 coalesced), storm throughput rises 11 → 19 Hz, max tick gap stays 139-172 ms against the rejected wait's 2657 ms, and per-child ΔRAM is unchanged. The `baseline` arm logs exactly one coalesced frame per run, which is the control separating "worked" from "never needed". Real-hardware magnitude at 2560x1600 remains the one open half, see §8 | [`sponge-debug-scrollstorm.md`](sponge-debug-scrollstorm.md) (`run/sponge-debug-scrollstorm.run`) |

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
