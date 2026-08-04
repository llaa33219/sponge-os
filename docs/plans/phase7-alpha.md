# phase7-alpha - Work Plan

## TL;DR (For humans)
**What you'll get:** Sponge OS Alpha 0.1.0 — a bootable USB disk image and ISO that start straight into the themed Sponge desktop on seL4 (in QEMU), where you can click-to-launch a terminal, text editor, file manager, and the Falkon web browser, manage everything with `vct` (including shutdown), open Leitzentrale as a window, and follow a written install + quick-start guide.

**Why this approach:** Two load-bearing decisions. (1) Docs before code: the package-format and vct design docs are amended first because the repo's own rules make design docs the precondition for code, and the Alpha's trickiest semantics ("install = enable a pre-staged package" on a single-file seL4 boot image) must be written down before anyone implements them. (2) Risky unknowns become early spikes with pre-decided fallbacks: the seL4-from-media boot chain, the network stack, and the depot import are each proven in isolation first — if the depot import fails, the editor falls back to vim-in-terminal and the browser becomes a documented limitation, so the plan never dead-ends.

**What it will NOT do:** Run on real hardware (QEMU only), keep your installed apps across reboots on the Alpha media, or give you networking beyond the emulator's built-in user-mode network. No backups, no snapshots, no stability guarantee — this is an honest Alpha.

**Effort:** XL
**Risk:** High - seL4 capability/memory sizing and the Falkon/WebEngine dependency chain are both proven failure hotspots; mitigated by fail-loud probe gates and spike-first ordering, but they can still burn time.
**Decisions to sanity-check:** install-means-enable (no runtime binary delivery) as the Alpha semantics; no install persistence on the media; both disk image AND ISO formats (doubles media verification); Falkon included despite being the heaviest item.

Your next move: start work now, or run the dual high-accuracy review (Momus) first. Full execution detail follows below.

---

> TL;DR (machine): XL effort, high risk (seL4 quotas + WebEngine), 20 todos in 6 waves (W0 docs-first → W1 media → W2 launch → W3 apps → W4 vct → W5 docs/regression), delivering bootable Alpha media + 4 apps + docs.

## Scope
### Must have
- One unified desktop scenario `run/sponge-alpha.run` on base-sel4: PC driver set + nitpicker + wm/window_layouter/decorator + sponge-de + sponge_configd/sponge_themed/sponge_pkgd + launcher feed + Leitzentrale subsystem, gated by probe PASS markers.
- Installable media from that scenario: GPT disk image (`.img`, `image/disk`) AND El Torito ISO (`.iso`, `image/iso`), each boot-verified headlessly in QEMU to the same PASS markers; artifacts + SHA-256 in `var/dist/`.
- Launch lifecycle: sponge_pkgd runtime-config generator fixes (`<binary>`, inline `<config>`, `<parent/>` routes incl. extended `parent-provides`, label/readonly/subpath materialization); installed-vs-running model (installed = registered/stopped, launch adds the `<start>` node); launcher click-to-launch and `vct launch` sharing the same pkgd backend (AGENTS.md §3.3 rule 5).
- Default app set as installable (pre-staged) packages: terminal (gems `terminal` + noux `bash-minimal`/`vim-minimal`), text editor (`qt6_textedit` host-side depot repackage; pre-decided fallback: vim-in-terminal + known limitation), file manager (new `sponge_files` Qt6 component: navigate/open/copy/delete), browser (Falkon Qt6 pin `cproc/pkg/falkon_qt6-jemalloc/2026-04-22` host-side repackage) with QEMU slirp networking proven first by a dedicated net-probe scenario.
- vct Alpha gaps: `shutdown`, `reboot` (platform System session), `update`, `search`, `launch` — each with `--json` + `--help` per AGENTS.md §3.3.
- Doc-first amendments (W0) before any code: docs/12 schema + lifecycle + update semantics; docs/06 new subcommand specs + §8 drift fix.
- `docs/13-installation.md` (install + quick-start), known-limitations register, roadmap §9 checkbox flips with scenario traceability, version bump to `0.1.0-alpha`.
### Must NOT have (guardrails, anti-slop, scope boundaries)
- No real-hardware support matrix — QEMU only (documented limitation).
- No runtime binary delivery on the Alpha media — install = enable pre-staged packages (Metis A1).
- No install persistence on the seL4 media (no writable fs component; documented limitation; base-linux lx_fs persistence remains the dev-flow proof).
- No networking beyond QEMU slirp + dde_ipxe e1000 (no wifi, no network config UI).
- No base-hw port, no snapshot/rollback, no OTA, no login manager, no multi-monitor, no lz merge op.
- No interactive `[Y/n]` prompts for `--manual` (still no terminal input channel for vct; honest reporting per Phase 4 precedent).
- No `as any`-style type bypasses, no empty exception handlers, no code/doc drift (AGENTS.md §1.4, §5.4).
- No modifications outside the repository from tooling (AGENTS.md §3.5); no re-implementation of Genode internals (§5.2).

## Verification strategy
> Zero human intervention - all verification is agent-executed.
- Test decision: **tests-after** per feature; every wave ships or extends a `run/*.run` probe scenario (repo convention, AGENTS.md §4.2). Framework: Genode run tool + `run_genode_until` PASS markers; Capture pixel checks and synthetic Event clicks per existing probes (`sponge_de_probe`, `lz_viz_probe`).
- **Fail-loud rule (§11.1 lesson)**: every scenario gates on an explicit PASS marker with bounded iterations; a silent hang fails the run by timeout, never passes.
- Networking QA: host fixture `python3 -m http.server` on the host; guest fetches `http://10.0.2.2:<port>/` (QEMU slirp) and asserts content.
- Media QA: QEMU booted from `.img` (`-hda`) and `.iso` (`-boot d -cdrom`) must reach the identical desktop PASS markers as direct kernel boot.
- Evidence: docs/evidence/task-<N>-phase7-alpha.<ext>

## Execution strategy
### Parallel execution waves
> Target 5-8 todos per wave. Fewer than 3 (except the final) means you under-split.

- **W0 (docs-first, doc hierarchy gate)**: todos 1-2. No code until these land.
- **W1 (unified desktop + media)**: todos 3-7. 3 before 4-7; 4 blocks 5/6; 5 and 6 parallel; 7 after 5+6.
- **W2 (launch lifecycle)**: todos 8-10. Sequential (8 → 9 → 10).
- **W3 (apps)**: todos 11-16. 11 (depot spike) and 12 (net spike) parallel; 13/14/15 parallel after 11 (14 depends on 11 outcome); 16 after 11+12+13.
- **W4 (vct gaps)**: todos 17-18. Parallel with each other; after W2 (shares vct/pkgd codebase surfaces) and W1 (needs platform driver in scenario).
- **W5 (docs + release)**: todos 19-20. After all above.

### Dependency matrix
| Todo | Depends on | Blocks | Can parallelize with |
| --- | --- | --- | --- |
| 1. docs/12 amendment | — | 2, 8, 9, 11-18 | 2 |
| 2. docs/06 amendment | 1 | 17, 18 | 1 |
| 3. seL4 media smoke test | 1 | 4-7 | — |
| 4. unified desktop scenario | 1, 3 | 5, 6, 8 | — |
| 5. disk image + boot QA | 4 | 7, 19 | 6 |
| 6. ISO image + boot QA | 4 | 7, 19 | 5 |
| 7. tool/dist + docs/08+11 rows | 5, 6 | 19 | — |
| 8. pkgd generator fixes | 1, 4 | 9, 10, 13-16 | — |
| 9. installed-vs-running model | 8 | 10, 17 | — |
| 10. click-to-launch + vct launch | 9 | 13-16, 19 | 17, 18 (later) |
| 11. depot-import spike + tool/pkg_import | 1 | 14, 16 | 12 |
| 12. networking spike (net-probe) | 1 | 16 | 11, 13, 15 |
| 13. terminal package | 10, 11* | 16 | 12, 14, 15 |
| 14. editor package | 10, 11 | — | 12, 13, 15 |
| 15. sponge_files component | 10 | 19 | 12, 13, 14 |
| 16. Falkon package | 11, 12, 13 | 19 | — |
| 17. vct shutdown/reboot | 2, 4 | 19 | 18 |
| 18. vct update/search | 1, 2 | 19 | 17 |
| 19. installation docs + limitations + roadmap flips | 5-7, 10, 15-18 | 20 | — |
| 20. full regression + release | 19 | — | — |
*terminal needs pkg_import only if noux ports are staged via depot repackaging; primary path is in-tree source build (see todo 13).

## Todos
> Implementation + Test = ONE todo. Never separate.
<!-- APPEND TASK BATCHES BELOW THIS LINE WITH edit/apply_patch - never rewrite the headers above. -->
- [x] 1. Doc-first: amend docs/12-package-format.md (schema, lifecycle, update semantics)
  What to do: Extend the metadata schema and semantics BEFORE any W2/W3 code: (a) add an optional `<config>` element whose inner XML is emitted verbatim into the package's `<start>` node by pkgd; (b) define parent-route notation for `<session default-route="parent">` (route to the outer system via `<parent/>`) vs child routes; (c) specify that label/readonly/subpath attributes are materialized per existing §7.2 rules 2-3 (label-routed sessions, `<pkg>-ro` label rewriting, vfs subpath policy) — promoting them from "defined" to "implemented in Phase 7"; (d) define the installed-vs-running lifecycle: installed = registered in the pkg set (no `<start>` node), running = `<start>` node present in pkg_runtime; `launch` transitions installed→running; no stop operation in Alpha; (e) define update semantics: `vct update` re-resolves installed roots against the on-image repo metadata and reports version deltas honestly (no fetching — the repo is fixed at build time); (f) amend §10 evolution path: depot interop lands in Phase 7 as HOST-SIDE repackaging (tool/pkg_import), explicitly NOT runtime fetching; (g) state the Alpha install semantics (Metis A1): on base-sel4 all binaries are pre-staged into the boot image; install = runtime enable. Must NOT do: do not touch any .cc file; do not change §13 persistence format.
  Parallelization: Wave 0 | Blocked by: — | Blocks: 2, 8, 9, 11-18
  References (executor has NO interview context - be exhaustive): docs/12-package-format.md §4.1 (schema table), §7.2 (routing rules 1-3), §9.2 (out-of-scope update text to replace), §10 (evolution path), §13 (persistence, unchanged); repos/sponge/src/sponge_pkgd/main.cc:810-879 (the generator this schema feeds); docs/plans/phase7-alpha-decisions.md amendments A1/A2/A7/A8.
  Acceptance criteria (agent-executable): `grep -n "config" docs/12-package-format.md` shows the new `<config>` element in the §4.1 table; `grep -n "installed-vs-running\|installed = registered" docs/12-package-format.md` hits; §9.2 no longer lists `vct update` as out of scope; §10 describes host-side repackaging as delivered.
  QA scenarios (name the exact tool + invocation): happy — read the amended sections and confirm internal consistency (no contradiction with §13); failure — `grep -n "out of scope" docs/12-package-format.md` must NOT match `update` anymore; confirm docs/12 still describes `nano`/`ncurses` samples unchanged (no collateral edits). Evidence docs/evidence/task-1-phase7-alpha.md
  Commit: Y | docs(pkg): schema + lifecycle + update semantics for Phase 7
- [x] 2. Doc-first: amend docs/06-vct.md (new subcommands + drift fix)
  What to do: (a) Add design-doc entries for `vct shutdown`, `vct reboot`, `vct update [pkg]`, `vct search <term>`, `vct launch <pkg>` to §4 — each with synopsis, flags (`--json`, `--help`, optional `--lang ko`), automation-default behavior, and the manual escape hatch (per AGENTS.md §3.3); state that `vct launch` and the DE launcher share the sponge_pkgd backend (§3.3 rule 5). (b) Fix the stale §8 status section (claims Phase 2 only) to reflect the real 10-command surface + the 5 new ones. (c) Reconcile §4.4: document the implemented positional `vct config <key> [value]` form (code wins over the `get/set` verb form). (d) Fix the vct README's stale "Phase 2" claim (repos/sponge/src/vct/README.md). Must NOT do: do not implement any command here; do not remove the documented `--no-color`/`--no-deps` flags without marking them "not yet implemented".
  Parallelization: Wave 0 | Blocked by: 1 | Blocks: 17, 18
  References: docs/06-vct.md §4.1-4.7 (command tables), §8 (stale status); repos/sponge/src/vct/command_router.cc:31-82 (actual dispatch surface); repos/sponge/src/vct/commands.cc (implementations); repos/sponge/src/vct/README.md; AGENTS.md §3.3 (vct extension rules).
  Acceptance criteria: `grep -n "shutdown\|reboot\|update\|search\|launch" docs/06-vct.md` shows all five with flag tables; §8 lists ≥15 commands; `grep -n "Phase 2" repos/sponge/src/vct/README.md` returns nothing.
  QA scenarios: happy — amended doc reviewed for §3.3 compliance of every new entry (automation default + escape hatch + --json + help); failure — confirm no §4 entry promises interactive [Y/n] prompts (forbidden by Scope OUT). Evidence docs/evidence/task-2-phase7-alpha.md
  Commit: Y | docs(vct): specify Phase 7 subcommands, fix Phase-2 drift
- [x] 3. seL4 media smoke test (minimal ISO + disk boot)
  What to do: Prove the grub2→bender→seL4 boot chain from media BEFORE building the full desktop. Create `run/sponge-media-smoke.run`: the sponge-minimal content (core, lib/ld, init, vct — copy the build/config shape from run/sponge-minimal.run) runnable with `RUN_OPT="--include image/iso"` and `RUN_OPT="--include image/disk"`; boot both headlessly in QEMU (`-boot d` for ISO per genode/repos/gems/run/sculpt_test.run:18; `-hda` for disk) and gate on the vct version banner. Install the missing host tools (e2tools for e2cp/e2mkdir; xorriso/sgdisk/mtools/dosfstools/e2fsprogs already present) — via apt, and add every host tool to docs/11-environment.md's bootstrap section with the exact package names. Must NOT do: do not build the full desktop here; do not patch the vendored genode tree unless a plugin bug is proven (then ledger it in docs/11 per AGENTS.md §5.2).
  Parallelization: Wave 1 | Blocked by: 1 | Blocks: 4-7
  References: run/sponge-minimal.run; genode/tool/run/image/iso, genode/tool/run/image/disk; genode/tool/run/iso.inc, genode/tool/run/grub2.inc; genode/tool/run/boot_dir/sel4:68-122 (image-include wiring); genode/repos/gems/run/sculpt_test.run:18 (`-boot d` pattern); genode/contrib/grub2-eb7172dee270fbd9f1bc862d46725fd1fb21d1ea/ (prepared grub2); docs/11-environment.md §7 (host deps) and §10.4 (seL4 image.elf); docs/09-roadmap.md Phase 1 lessons (≥1 GiB RAM for seL4 QEMU).
  Acceptance criteria: `make -C genode/build/x86_64 run/sponge-media-smoke KERNEL=sel4 BOARD=pc RUN_OPT="--include image/disk"` produces a `.img` and QEMU prints the vct banner; same for `image/iso` producing `.iso`; run log captured.
  QA scenarios: happy — both media reach `[init -> vct] vct` banner within the run-tool timeout; failure — remove one staged boot module (e.g. ld.lib.so) in a scratch copy and confirm the run FAILS loudly by timeout (validates the gate isn't vacuous). Evidence docs/evidence/task-3-phase7-alpha.log
  Commit: Y | feat(run): seL4 ISO/disk media smoke test
- [x] 4. Unified desktop scenario `run/sponge-alpha.run`
  What to do: Merge the proven component sets into ONE base-sel4 scenario: driver stack from run/sponge-de-sel4-interactive.run:79-87,91-326 (acpi/pci_decode/platform/vesa_fb/ps2/pc_usb_host/usb_hid/event_filter sub-init; QEMU args `-m` bumped to 2G, xhci + usb-tablet); wm + window_layouter + decorator from run/sponge-wm.run; sponge_configd + sponge_themed from run/sponge-theme.run; sponge_pkgd + pkg_runtime + staged pkgs from run/sponge-launcher.run (incl. its Qt lib/tar staging lines 165-192); the Leitzentrale subsystem from run/sponge-leitzentrale.run (lz_runtime/lz_bridge/lz_watch/lz_viewer). sponge-de must show panel + launcher fed by pkgd, themed via the live configd→themed pipeline. Verification = a new composite probe (extend test/sponge_de_probe or add test/alpha_probe) asserting: Capture pixel match of the themed panel/window, launcher report contains the pre-staged `hello` package, config report broadcast live, lz_viewer pixel check. Quota policy per Metis A3: sponge-de caps 1000, any Qt GUI child ≥1000; all other children sized per the merged scenarios' proven values; every gate via `run_genode_until` with bounded iterations. Note the qt6 stale-cmake recovery (docs/11 §10.3): if switching from a base-linux build, `rm -rf genode/build/x86_64/qt6/base` first. Must NOT do: do not add networking yet (W3); do not implement click-to-launch (W2); do not add a writable fs (persistence explicitly out, Metis A6).
  Parallelization: Wave 1 | Blocked by: 1, 3 | Blocks: 5, 6, 8
  References: run/sponge-de-sel4-interactive.run (drivers, caps lesson), run/sponge-wm.run, run/sponge-theme.run, run/sponge-launcher.run:92-192, run/sponge-leitzentrale.run; repos/sponge/src/leitzentrale/{subsys,lz_runtime,lz_bridge}.config; repos/sponge/src/test/{sponge_de_probe,wm_probe,launcher_probe,theme_probe,lz_viz_probe}/; docs/09-roadmap.md §11.1 (cap exhaustion); docs/11-environment.md §10.3, §10.4.
  Acceptance criteria: `make -C genode/build/x86_64 run/sponge-alpha KERNEL=sel4 BOARD=pc` completes with the composite probe logging `alpha-probe: PASS`; the run exits 0 via run_genode_until.
  QA scenarios: happy — PASS marker appears after all four sub-assertions; failure — deliberately drop the configd start node in a scratch copy and confirm the probe FAILS (not hangs) — validates fail-loud gating. Evidence docs/evidence/task-4-phase7-alpha.log
  Commit: Y | feat(run): unified alpha desktop scenario on base-sel4
- [x] 5. Disk-image media of the alpha desktop + QEMU boot QA
  What to do: Run the todo-4 scenario with `RUN_OPT="--include image/disk"`, producing `var/run/sponge-alpha.img`; boot it headlessly in QEMU from the image (not direct kernel) and gate on the same `alpha-probe: PASS`; copy the artifact to `var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img` with a `.sha256`. Must NOT do: do not sign/shim (QEMU-only); do not shrink the image below what build_boot_image needs.
  Parallelization: Wave 1 | Blocked by: 4 | Blocks: 7, 19
  References: todo 4; genode/tool/run/image/disk (sgdisk/mcopy/e2tools/resize2fs pipeline); genode/tool/run/boot_dir/sel4:68-122; todo 3's smoke-test run script for the QEMU-from-media invocation pattern.
  Acceptance criteria: QEMU booted from the `.img` logs `alpha-probe: PASS`; `sha256sum var/dist/*.img` matches its `.sha256` file.
  QA scenarios: happy — media boot == direct boot markers; failure — corrupt one byte in a scratch copy of the .img and confirm the boot does NOT produce PASS (media is actually being used). Evidence docs/evidence/task-5-phase7-alpha.log
  Commit: Y | feat(run): alpha desktop disk image + boot verification
- [x] 6. ISO media of the alpha desktop + QEMU boot QA
  What to do: Same as todo 5 with `RUN_OPT="--include image/iso"` producing `.iso`, booted via `-boot d`; artifact `var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso` + `.sha256`. Must NOT do: nothing beyond the media-mode difference.
  Parallelization: Wave 1 | Blocked by: 4 | Blocks: 7, 19
  References: todo 5; genode/tool/run/image/iso (xorriso eltorito); genode/tool/run/iso.inc; genode/repos/gems/run/sculpt_test.run:18.
  Acceptance criteria: QEMU booted from the `.iso` logs `alpha-probe: PASS`; sha256 file matches.
  QA scenarios: happy — PASS from CD boot; failure — boot the .iso with the probe's PASS string altered in a scratch build and confirm timeout failure. Evidence docs/evidence/task-6-phase7-alpha.log
  Commit: Y | feat(run): alpha desktop ISO + boot verification
- [x] 7. tool/dist (Mojo) + docs/08 manual steps + docs/11 host-tool rows
  What to do: Add `tool/dist.mojo` + thin bash launcher `tool/dist` (same pattern as tool/build: mojo from PATH or .venv/bin/mojo): it (a) checks all media host tools and prints exact apt install lines for missing ones, (b) runs the disk and ISO media builds (todos 5-6) sequentially, (c) copies artifacts + sha256 into `var/dist/`, (d) prints a summary with artifact sizes. Document the fully manual equivalent step-by-step in docs/08-development.md (control escape hatch, AGENTS.md §3.5). Add the host-tool package list (xorriso, gptfdisk/sgdisk, mtools, e2tools, dosfstools, e2fsprogs) to docs/11-environment.md §7. Must NOT do: tool/dist must not touch anything outside the repo (§3.5); no re-implementation of the Genode run tool — it shells out to `make -C genode/build/x86_64 run/...`.
  Parallelization: Wave 1 | Blocked by: 5, 6 | Blocks: 19
  References: tool/build (launcher pattern), tool/build.mojo (Mojo conventions — load the mojo-syntax skill before writing Mojo); docs/08-development.md; docs/11-environment.md §7; AGENTS.md §3.5.
  Acceptance criteria: `./tool/dist` exits 0 and `ls var/dist/` shows both artifacts + .sha256 files; docs/08 contains a numbered manual procedure producing the same artifacts.
  QA scenarios: happy — full run green on a clean var/dist; failure — temporarily rename `xorriso` in PATH and confirm tool/dist prints the apt line and exits non-zero BEFORE invoking any build. Evidence docs/evidence/task-7-phase7-alpha.log
  Commit: Y | feat(tool): dist command for alpha media artifacts

- [x] 8. sponge_pkgd runtime-config generator fixes
  What to do: In repos/sponge/src/sponge_pkgd/main.cc `_generate_runtime_config()` (lines 810-879): (a) emit `<binary name="..."/>` when metadata `<binary>` differs from `<name>` (default remains name); (b) emit the metadata `<config>` element's inner XML verbatim into the `<start>` node (schema from todo 1); (c) change declared-session routing: sessions with `default-route="parent"` (or naming an outer-system service such as nitpicker/vfs/event_filter) route via `<parent/>`, not `<child name=...>`; (d) extend pkg_runtime's `parent-provides` beyond ROM/PD/CPU/LOG/Timer with Gui, Input, Report, File_system, NIC, Timer (needed for the `<parent/>` routes to resolve); (e) materialize session `label`, `readonly` (label suffix `<pkg>-ro`), and `subpath` (vfs policy) per docs/12 §7.2 rules 2-3 as amended in todo 1; (f) raise the generated `<default>` caps to a GUI-safe floor and honor per-package `<quota>` (Metis A3: Qt apps need ~1000 caps on seL4). Verify with a new headless scenario `run/sponge-pkg-gui.run`: a small Qt test package (new test component with a colored window, e.g. test/pkg_gui_probe payload) installed and launched, pixel-verified through Capture. Must NOT do: do not change the resolver (DFS/cycle detection is proven); do not add networking sessions to packages yet (W3); do not break `run/sponge-pkg-install.run` — the session-free `hello` path must stay green.
  Parallelization: Wave 2 | Blocked by: 1, 4 | Blocks: 9, 10, 13-16
  References: repos/sponge/src/sponge_pkgd/main.cc:810-879 (generator), :830-836 (parent-provides); docs/12-package-format.md as amended in todo 1 (§4.1 config element, §7.2 materialization); run/sponge-launcher.run:126-136 (what a Qt app's start node/config looks like: libc + vfs + font/plugin tars); repos/sponge/src/test/sponge_de_probe/ (Capture pixel-check pattern to reuse); docs/09-roadmap.md §11.1 (caps sizing).
  Acceptance criteria: `make -C genode/build/x86_64 run/sponge-pkg-gui KERNEL=sel4 BOARD=pc` logs the probe PASS marker showing the GUI package's window pixel-verified; `make -C genode/build/x86_64 run/sponge-pkg-install` still passes (no regression).
  QA scenarios: happy — GUI package window appears post-launch; failure — install a package whose metadata declares Gui but omit the nitpicker sibling in a scratch scenario and confirm pkgd/the probe reports a clear error or bounded-timeout failure (never a silent hang). Evidence docs/evidence/task-8-phase7-alpha.log
  Commit: Y | feat(pkgd): binary/config/parent-route/label support in runtime config
- [x] 9. Installed-vs-running lifecycle model in sponge_pkgd
  What to do: Implement the todo-1 lifecycle: installed packages register in the set WITHOUT a `<start>` node (except packages marked `<autostart>` in metadata — `hello` keeps current behavior); add a `launch` request op to the pkgd Report/ROM channel (request: `launch <name>`; result: ok / not-installed / already-running); on launch, pkgd regenerates the pkg_runtime config with the new `<start>` node; the `installed` broadcast gains a `running` attribute per package so the launcher can show state. Update the pkgd README if it contradicts. Must NOT do: no stop/kill op (Alpha scope); no persistence changes (Metis A6); no concurrent-caller hardening (single-writer channel per Phase 4 note).
  Parallelization: Wave 2 | Blocked by: 8 | Blocks: 10, 17
  References: repos/sponge/src/sponge_pkgd/main.cc (request dispatch + `_generate_runtime_config` + installed-set broadcast); docs/12-package-format.md §9.2 as amended in todo 1 (lifecycle states); run/sponge-pkg-list.run (installed-set assertion pattern).
  Acceptance criteria: in a headless scenario (extend run/sponge-pkg-list.run or new run/sponge-pkg-lifecycle.run), install leaves the package absent from the component tree; a `launch` request makes it appear (asserted via init state report / `vct component list` output inside the scenario); broadcast shows `running="yes"`.
  QA scenarios: happy — install→launch→running transitions asserted; failure — `launch` on a not-installed package returns the `not-installed` result (asserted), and double-launch returns `already-running`. Evidence docs/evidence/task-9-phase7-alpha.log
  Commit: Y | feat(pkgd): installed-vs-running lifecycle with launch op
- [x] 10. Click-to-launch in Sponge DE launcher + `vct launch`
  What to do: (a) In repos/sponge/src/sponge-de/launcher/launcher_menu_view.cc (the deferred handler at ~line 114), replace the log-only click with a launch request to sponge_pkgd's channel (new LauncherController path), and render running state from the broadcast's `running` attribute; (b) implement `vct launch <pkg>` in repos/sponge/src/vct (new LaunchCommand wired to the SAME pkgd channel per AGENTS.md §3.3 rule 5) with `--json`/`--help`/`--lang ko`; (c) verify in the unified scenario: new probe step injects a synthetic click on the launcher menu entry for a pre-staged GUI test package and pixel-verifies its window; separately assert `vct launch` achieves the same (run/sponge-launch.run, may extend the alpha probe). Must NOT do: do not fork a second launch path for vct (same backend); do not add window-close/stop handling.
  Parallelization: Wave 2 | Blocked by: 9 | Blocks: 13-16, 19
  References: repos/sponge/src/sponge-de/launcher/launcher_menu_view.cc:114, repos/sponge/src/sponge-de/launcher/launcher_controller.*; repos/sponge/src/vct/command_router.cc:31-82 (add route), commands.h/cc (command pattern of InstallCommand at commands.cc:286-345); run/sponge-de-test.run (synthetic click injection pattern); run/sponge-alpha.run (todo 4).
  Acceptance criteria: run/sponge-launch.run (or extended alpha probe) logs PASS after: click → window pixel-verified, AND `vct launch pkg_gui` → same result; `vct launch --json` emits machine-readable output (asserted).
  QA scenarios: happy — both launch paths verified; failure — `vct launch` on an unknown package prints the not-installed error and exits non-zero (asserted in scenario log). Evidence docs/evidence/task-10-phase7-alpha.log
  Commit: Y | feat(de,vct): click-to-launch and vct launch via pkgd backend

- [x] 11. Depot-import spike + tool/pkg_import (host-side repackaging)
  What to do: SPIKE FIRST (timebox: one working session): import the cproc depot pubkey and run `genode/tool/depot/download cproc/pkg/qt6_textedit/<pinned>` and `cproc/pkg/falkon_qt6-jemalloc/2026-04-22` against the vendored tree; record exact archive versions + SHA-256 in docs/11-environment.md §5. THEN implement `tool/pkg_import.mojo` + bash launcher `tool/pkg_import`: given a depot pkg reference, it extracts the downloaded raw archives and writes a Sponge package directory `pkg/<name>/` with (a) metadata.xml generated per docs/12 (with a `<config>` carrying libc/vfs boilerplate), (b) payload binaries/libs staged for boot-module inclusion, (c) a `SOURCE` file recording depot user/pkg/version/sha256 (reproducibility, docs/11 §1). Document the manual equivalent in docs/08. PRE-DECIDED CONTINGENCY (no judgment call): if the spike fails (network/key/archive layout), textedit drops to the vim-deliverable path (todo 14) and Falkon drops to a documented known limitation (todo 16 becomes docs-only) — record the outcome in docs/plans/phase7-alpha-decisions.md and proceed. Must NOT do: pkgd must never fetch at runtime (Metis A1); no writes outside the repo (§3.5).
  Parallelization: Wave 3 | Blocked by: 1 | Blocks: 14, 16
  References: genode/tool/depot/download, genode/tool/depot/extract; genode/repos/gems/sculpt/deploy/falkon_web_browser (pkg references + route shapes); genode/repos/gems/recipes/pkg/system_shell/archives (how depot pkgs compose); docs/11-environment.md §1, §5; tool/build.mojo (Mojo patterns — load mojo-syntax skill).
  Acceptance criteria: spike log shows both depot pkgs downloaded + extracted; `tool/pkg_import cproc/pkg/qt6_textedit/<pin>` produces a valid `pkg/textedit/` (metadata parses against docs/12 rules — validated by staging it in a scratch scenario without error); SOURCE file contains version + sha256.
  QA scenarios: happy — both imports produce pkg dirs; failure — run tool/pkg_import with a bogus depot ref and confirm a clear non-zero error, no partial pkg/ directory left behind. Evidence docs/evidence/task-11-phase7-alpha.log
  Commit: Y | feat(tool): pkg_import host-side depot repackaging
- [x] 12. Networking spike: `run/sponge-net-probe.run` on base-sel4
  What to do: Prove the network stack on seL4 BEFORE Falkon: `prepare_port ipxe lwip` (record hashes in docs/11 §5); add `dde_ipxe` to REPOSITORIES (docs/11 §8.1 + build.conf); build the dde_ipxe nic driver (genode/repos/dde_ipxe/src/driver/nic) and libports fetchurl (genode/repos/libports/src/app/fetchurl) with the lwip socket stack; create run/sponge-net-probe.run: QEMU `-netdev user -device e1000`, guest fetchurl GETs `http://10.0.2.2:<port>/net-fixture.txt` from a host-side `python3 -m http.server` fixture and the run gates on the fetched content marker; also start `app/system_clock` (gems) feeding RTC — required later for TLS. Must NOT do: no nic_router, no wifi, no network config UI; do not wire networking into sponge-alpha.run yet (that is todo 16's job).
  Parallelization: Wave 3 | Blocked by: 1 | Blocks: 16
  References: genode/repos/dde_ipxe/src/driver/nic; genode/repos/libports/src/lib/lwip (socket.cc); genode/repos/libports/src/app/fetchurl/README; genode/repos/gems/src/app/system_clock (verify exact path at execution); docs/11-environment.md §5 (ports table), §8.1 (repositories); genode/repos/libports/run/fetchurl.run (if present — upstream usage example).
  Acceptance criteria: scenario logs the fixture file's content marker via fetchurl; run exits 0.
  QA scenarios: happy — content round-trip; failure — stop the host fixture and confirm the run fails by bounded timeout (no silent hang), proving the gate. Evidence docs/evidence/task-12-phase7-alpha.log
  Commit: Y | feat(run): networking probe on base-sel4 (dde_ipxe + lwip)
- [x] 13. Terminal package (`pkg/terminal`) — gems terminal + bash/vim
  What to do: Build the terminal stack from in-tree sources: gems `terminal` server (genode/repos/gems/src/server/terminal, LIBS=base vfs + Vera.ttf already in contrib), noux `bash-minimal` and `vim-minimal` (genode/repos/ports/src/noux-pkg/{bash-minimal,vim-minimal}; prepare required ports and record hashes in docs/11 §5); model the composition on genode/repos/gems/recipes/pkg/system_shell/archives. Create `pkg/terminal/metadata.xml`: quota caps ≥1000 (Qt-free but generous per A3), launcher category System, `<config>` (todo 8 schema) carrying the terminal's vfs + font ROM wiring; the terminal connects a bash noux child per the system_shell pattern. Stage in run/sponge-alpha.run's pkg set; verify: install → launcher shows Terminal → synthetic click → terminal window pixel-verified, and a synthetic keystroke echoes (assert via Capture region change or terminal report). Must NOT do: do not import the prebuilt system_shell depot pkg (source-built path is primary); no ssh, no tmux.
  Parallelization: Wave 3 | Blocked by: 10, (11 only if depot path chosen) | Blocks: 16
  References: genode/repos/gems/src/server/terminal/target.mk; genode/repos/gems/recipes/pkg/{terminal,system_shell}/archives; genode/repos/ports/src/noux-pkg/{bash-minimal,vim-minimal}; genode/contrib/ttf-bitstream-vera-* (font present); run/sponge-launcher.run:165-192 (staging pattern); todo 10's launch probe.
  Acceptance criteria: in run/sponge-alpha.run the probe logs PASS for: terminal installed→launched→window visible→keystroke round-trip; `vct list --json` shows terminal.
  QA scenarios: happy — full path; failure — remove the font ROM in a scratch staging and confirm bounded-timeout failure with an identifiable log (font missing), not a hang. Evidence docs/evidence/task-13-phase7-alpha.log
  Commit: Y | feat(pkg): terminal package with bash and vim
- [x] 14. Text editor package (`pkg/textedit`)
  What to do: PRIMARY (if todo-11 spike succeeded): `tool/pkg_import cproc/pkg/qt6_textedit/<pin>` → `pkg/textedit/`; write metadata (quota caps ≥1000, ram ≥64M, launcher category Editors, `<config>` with libc+vfs+fonts per run/sponge-launcher.run:126-136); stage + verify install→launch→textedit window pixel-verified in run/sponge-alpha.run. CONTINGENCY (pre-decided, only if spike failed): mark GUI editor as a known limitation and assert vim-in-terminal as the editor deliverable — acceptance becomes: `vct list` shows vim via the terminal package and docs/13 (todo 19) documents the limitation; no new code in that case. Must NOT do: do not write a second GUI editor; do not ship both paths.
  Parallelization: Wave 3 | Blocked by: 10, 11 | Blocks: —
  References: todo 11 (import tooling + pin); run/sponge-launcher.run:126-136 (Qt app config shape); todo 8 (config/metadata support); docs/plans/phase7-alpha-decisions.md A8.
  Acceptance criteria: primary — probe PASS for textedit window after launcher click; contingency — the limitation paragraph exists in docs/13-installation.md and `vct list` shows the terminal package providing vim.
  QA scenarios: happy — window appears; failure — textedit binary missing from staging → pkgd install reports a clear missing-binary error (asserted), not a crash. Evidence docs/evidence/task-14-phase7-alpha.log
  Commit: Y | feat(pkg): textedit package (qt6_textedit repackage)
- [x] 15. `sponge_files` — Sponge-native minimal Qt6 file manager
  What to do: New component `repos/sponge/src/sponge_files/` (target.mk per AGENTS.md §3.2, C++17, Genode conventions §3.1): a Qt6 Widgets app showing a directory tree/list of a designated File_system session (the scenario wires a vfs with a read-only ROM-backed demo directory + a RAM-backed writable area), with navigate (double-click directory), open (logs the file's first line to LOG / shows in a view), copy and delete (writable area only). Theme-aware via the same ThemeLoader pattern as sponge-de (read the themed `theme` ROM). Register as `pkg/files` (launcher category Utilities, caps ≥1000); verify: install→launch→window pixel-verified; synthetic double-click navigates (assert via window content/structure report); copy+delete asserted via fs-state report (fs_report pattern from run/sponge-leitzentrale.run). Must NOT do: no rename-undo, no search, no drag-drop (A-scope); do not reuse Sculpt's file browser (read-only upstream); minimize Qt modules (§3.4).
  Parallelization: Wave 3 | Blocked by: 10 | Blocks: 19
  References: repos/sponge/src/sponge-de/theme/theme_loader.{h,cc} (theme reuse pattern); repos/sponge/src/sponge-de/ (Qt6-on-Genode wiring, target.mk); run/sponge-leitzentrale.run (fs_report/fs_rom usage); AGENTS.md §3.1-3.4.
  Acceptance criteria: scenario probe logs PASS for: window pixel check + navigate + copy + delete assertions; `vct install files` → launcher entry appears.
  QA scenarios: happy — four assertions pass; failure — delete in the read-only area is refused and the refusal is surfaced in the UI/log (asserted), not silently ignored. Evidence docs/evidence/task-15-phase7-alpha.log
  Commit: Y | feat(files): minimal Qt6 file manager component
- [x] 16. Falkon browser package (`pkg/falkon`) + desktop networking — **delivered with documented limitation**: package complete (64 ROMs, 509MB payload), but the seL4 boot chain's ~256MB boot-module ceiling blocks booting it; known-limitation entry required in todo 19; resolution path = disk-based payload staging (post-Alpha)
  What to do: (After todos 11+12 succeeded — else docs-only per contingency.) Write the route-mapping table (falkon preset line → Sponge target) into the todo as work proceeds: mesa_gpu-cpu → in-scenario softpipe GPU ROM; wm → outer nitpicker/wm; black_hole → a stub Capture/Play/Record sink (or omit with config); mixer/system_clock → gems system_clock from todo 12; ram_fs → a small vfs ram fs child. `tool/pkg_import cproc/pkg/falkon_qt6-jemalloc/2026-04-22` → `pkg/falkon/`; metadata: caps ≥2000, ram ≥512M (A3 — WebEngine; tune empirically with fail-loud probes), sessions incl. NIC (todo 12 wiring) and label-routed clipboard/shape reports (todo 8e). Wire nic driver + system_clock into run/sponge-alpha.run. Verify: install→launch→falkon window appears AND loads `http://10.0.2.2:<port>/` host fixture (assert via falkon's stdout/log marker or a Capture pixel check of the rendered fixture text). QEMU `-m` for the alpha scenario must cover falkon (re-size; record in docs/11). Must NOT do: no HTTPS acceptance in Alpha if system_clock/TLS proves out of reach — then HTTP fixture only + honest limitation note; no depot fetch at runtime (A1).
  Parallelization: Wave 3 | Blocked by: 11, 12, 13 | Blocks: 19
  References: genode/repos/gems/sculpt/deploy/falkon_web_browser (full preset; route sources); todo 11 (pin/tool), todo 12 (nic/lwip/system_clock), todo 8 (label routes); docs/09-roadmap.md §11.1 (caps); genode/repos/gems/recipes/pkg/sculpt/README (falkon preset description).
  Acceptance criteria: probe PASS for falkon window + fixture page render in run/sponge-alpha.run; `vct list` shows falkon; contingency path (if spike failed): docs/13-installation.md known-limitations entry exists instead (assert by grep) — that makes this todo docs-only.
  QA scenarios: happy — page renders; failure — stop the host fixture and confirm falkon's error page / probe timeout fails the run loudly; caps regression: if falkon hangs with the quota-upgrade line as last log (the §11.1 signature), the run must fail by bounded timeout, and the fix is raising caps — never merge a hang. Evidence docs/evidence/task-16-phase7-alpha.log
  Commit: Y | feat(pkg): falkon browser package + alpha networking

- [x] 17. vct shutdown/reboot via platform System session
  What to do: Implement `vct shutdown` and `vct reboot` (repos/sponge/src/vct, new PowerCommand): open a `System` session routed to the platform driver in run/sponge-alpha.run (the driver stack already includes acpi/platform from the merged drivers sub-init); invoke poweroff/reset; `--json`/`--help` per §3.3; the command prints an audit line before acting (control philosophy). Verify in a headless scenario: `vct shutdown` → QEMU process exits (the run tool observes guest shutdown) — gate on clean QEMU exit; `vct reboot` → boot banner appears twice. Document the QEMU-monitor escape (Ctrl-A x / `-qmp`) in docs/13 as the fallback if ACPI poweroff is unreachable on a given setup. Must NOT do: do not expose a raw init kill; do not auto-shutdown anything (pure user-invoked command).
  Parallelization: Wave 4 | Blocked by: 2, 4 | Blocks: 19
  References: run/sponge-de-sel4-interactive.run:91-326 (platform/acpi in drivers sub-init — the route target); repos/sponge/src/vct/commands.{h,cc} (command pattern); docs/06-vct.md §4.7 as amended in todo 2; genode/repos/os/src/drivers/platform (System session implementation — verify exact session name at execution).
  Acceptance criteria: scenario run logs show `vct shutdown` audit line and QEMU exits 0 within the timeout; reboot scenario shows two boot banners.
  QA scenarios: happy — both commands observed; failure — `vct shutdown --json` on a scenario without the System route prints a clear "service unavailable" error, exits non-zero (asserted), and the guest keeps running. Evidence docs/evidence/task-17-phase7-alpha.log
  Commit: Y | feat(vct): shutdown and reboot via platform System session
- [x] 18. vct update + vct search
  What to do: Implement per todo-1/2 semantics: (a) `vct search <term>` scans the on-image repo metadata (pkg_index + pkg_<name>.xml ROMs) for name/description matches, prints results (with `--json`); (b) `vct update [pkg]` re-resolves installed roots against the on-image repo metadata, reports version deltas ("already current" / "repo carries X, installed Y — effective after next image build"), honestly stating that binaries are fixed at build time (A1); `--json`/`--help` per §3.3. New pkgd request ops only if the data is not already reachable via existing ROMs — prefer vct reading pkg ROMs directly (minimum privilege). Verify in a headless scenario with staged packages. Must NOT do: no network fetching, no version ordering heuristics beyond exact string inequality (report, don't guess); no auto-upgrade.
  Parallelization: Wave 4 | Blocked by: 1, 2 | Blocks: 19
  References: docs/12-package-format.md §5.4 (pkg_index staging), §9.2 as amended in todo 1 (update semantics); docs/06-vct.md §4.2 as amended in todo 2; repos/sponge/src/vct/commands.cc:746-760 (ListCommand — the read pattern to reuse); run/sponge-pkg-list.run.
  Acceptance criteria: scenario asserts `vct search hello` lists the hello package (JSON asserted); `vct update` on a current system prints "already current" (asserted); with a scratch repo carrying a bumped version string, `vct update hello` prints the delta line (asserted).
  QA scenarios: happy — three assertions pass; failure — `vct search` with zero matches prints an honest empty result, exit 0; `vct update nonexistent` exits non-zero with a clear error. Evidence docs/evidence/task-18-phase7-alpha.log
  Commit: Y | feat(vct): update and search commands

- [x] 19. Installation docs + known limitations + roadmap/README flips
  What to do: (a) Write `docs/13-installation.md`: prerequisites (host tools from todo 7), building media (`./tool/dist` + manual escape), booting in QEMU (exact commands for .img and .iso), writing the .img to a USB stick (`dd` with a bold data-loss warning), quick-start tour (boot → desktop → launcher → terminal/files/textedit/falkon → `vct status/config/theme/install/launch/shutdown` → Leitzentrale via `vct leitzentrale`); every quick-start claim must cite the scenario that proves it (AGENTS.md §5.1). (b) Known-limitations register (in docs/13 + linked from README): QEMU-only target; installs enable pre-staged packages only, no runtime binary delivery (A1); no install persistence on the seL4 media (A6); networking = QEMU slirp only; **Falkon is packaged but NOT bootable on the seL4 media — the boot chain's ~256MB boot-module ceiling vs its 509MB WebEngine payload (todo 16, D5); the fix is disk-based payload staging, post-Alpha**; lz_viewer shows the marker, not full Sculpt UI content (todo 4); no stability guarantee / possible data loss (per roadmap §9 "Excluded from Alpha"); no snapshots/backup; `--manual` prompts non-interactive. (c) Flip docs/09-roadmap.md §9 checkboxes with a traceability note per criterion (which run scenario proves it); the "default app set" criterion must be flipped HONESTLY with the falkon caveat stated inline (terminal/textedit/files verified, browser packaged-but-not-bootable); update the README status block to Alpha and the roadmap §11 current-focus. (d) Bump version to 0.1.0-alpha (tool/version_bump.mojo exists — use it; keep codename Archaeocyte). (e) Archive the final plan + decision log into docs/plans/ (git-ignored scratch under the orchestrator working dir holds transient orchestration state; durable records live under docs/). Must NOT do: do not claim real-hardware support; do not claim the browser works on the Alpha media; do not check the "Excluded from Alpha" boxes.
  Parallelization: Wave 5 | Blocked by: 5-7, 10, 15-18 | Blocks: 20
  References: docs/09-roadmap.md §9 (criteria), §11 (current focus); README.md (status block); docs/13-installation.md (new); tool/version_bump.mojo; docs/plans/phase7-alpha-decisions.md amendments A1/A6; all W1-W4 run scenarios (traceability sources).
  Acceptance criteria: `grep -c "\[x\]" docs/09-roadmap.md` increases for the §9 block; every flipped checkbox names its proving scenario in the adjacent text; README says Alpha 0.1.0; `grep -n "QEMU" docs/13-installation.md` and the limitations list are present.
  QA scenarios: happy — docs consistent with delivered scenarios (each cited scenario exists in run/); failure — `grep -n "real.hardware\|production.ready" docs/13-installation.md README.md` must return nothing (no overclaiming). Evidence docs/evidence/task-19-phase7-alpha.md
  Commit: Y | docs(alpha): installation guide, known limitations, Alpha status
- [x] 20. Full regression + release verification
  What to do: Run the ENTIRE scenario suite green: all 20 pre-existing run/*.run scenarios (base-linux where applicable) plus the new ones (sponge-media-smoke, sponge-alpha, sponge-pkg-gui, sponge-pkg-lifecycle, sponge-launch, sponge-net-probe) on their designated kernels; re-run `./tool/dist` from a clean var/dist and re-verify both media boot to `alpha-probe: PASS`; assemble the evidence index (docs/evidence/) and the PR-body numbers per AGENTS.md §5.1 (commands/clicks for each quick-start task). Must NOT do: do not skip flaky scenarios — a flake is a failure to fix or explicitly document as pre-existing with evidence.
  Parallelization: Wave 5 | Blocked by: 19 | Blocks: —
  References: run/README.md (scenario inventory); todos 3-18 acceptance criteria; AGENTS.md §4.2, §5.1.
  Acceptance criteria: every scenario exits 0; both media re-verified; evidence index lists one artifact per todo (1-19).
  QA scenarios: happy — suite green end to end; failure — any non-zero scenario blocks the release; the failing log + diagnosis goes into the evidence index and the todo that owns it is reopened. Evidence docs/evidence/task-20-phase7-alpha.md
  Commit: N (release commit is todo 19; this todo only verifies)

## Final verification wave
> Runs in parallel after ALL todos. ALL must APPROVE. Surface results and wait for the user's explicit okay before declaring complete.
- [x] F1. Plan compliance audit
- [x] F2. Code quality review
- [x] F3. Real manual QA
- [x] F4. Scope fidelity

## Commit strategy
- One logical change = one commit (AGENTS.md §4.2/§4.3), conventional commits with scopes `docs`, `run`, `tool`, `pkgd`, `de`, `vct`, `pkg`, `files`.
- Doc-first commits (todos 1-2) land before ANY code commit — doc hierarchy (AGENTS.md §0/§4.1).
- Each todo's Commit line is authoritative; todo 20 is verification-only.
- Any vendored-tree patch (only if a proven plugin bug) is an ordinary commit + ledger entry in docs/11-environment.md (§5.2).
- Never commit secrets; `var/dist/` artifacts stay git-ignored (only their sha256 + docs references are committed).

## Success criteria
All of docs/09-roadmap.md §9 checked, each traceable to a named run scenario:
1. Installable media — `var/dist/*.img` + `*.iso` boot in QEMU to `alpha-probe: PASS` (todos 3, 5, 6, 7).
2. DE usable after boot — unified scenario boots to themed panel+launcher with zero interaction (todo 4).
3. Default app set installable — terminal, textedit, files, falkon install+launch from the launcher and `vct launch` (todos 13-16, 10).
4. vct day-to-day — status/config/install/launch/update/search/shutdown all wired with `--json` (todos 10, 17, 18 + existing).
5. Leitzentrale integration — lz subsystem in the alpha image, pixel-verified (todo 4).
6. Installation docs + quick-start — docs/13-installation.md (todo 19).
7. Known limitations documented — register in docs/13 + README (todo 19).
Plus: zero regressions across the pre-existing 20-scenario suite (todo 20), and every todo's evidence file present in docs/evidence/.
