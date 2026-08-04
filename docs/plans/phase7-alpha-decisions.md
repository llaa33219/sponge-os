---
slug: phase7-alpha
status: awaiting-approval
intent: clear
pending-action: write docs/plans/phase7-alpha.md
approach: 5 waves — (W1) unified desktop boot scenario + installable media (image/disk + image/iso on base-sel4), (W2) pkgd runtime-config gaps + click-to-launch lifecycle, (W3) default app packages (terminal, editor, Falkon+networking, Sponge-native file manager), (W4) vct Alpha gaps (shutdown/reboot, update/search, launch, doc-drift fixes), (W5) installation docs + quick-start + known limitations.
---

# Draft: phase7-alpha

## Components (topology ledger)
<!-- id | outcome (one line) | status | evidence path -->
- C1 unified-desktop+media | One run scenario boots the full Sponge desktop (drivers+DE+wm+backends) and emits .img + .iso | active | run/sponge-alpha.run, var/dist/*.img/.iso
- C2 launch-lifecycle | Launcher click + `vct launch` start packages as GUI children (pkgd config gaps fixed) | active | repos/sponge/src/sponge_pkgd/main.cc, sponge-de launcher
- C3 apps-terminal | gems `terminal` + bash/vim installable as sponge package | active | pkg/terminal/, run scenario
- C4 apps-editor | qt6_textedit (or vim fallback) installable | active | pkg/textedit/
- C5 apps-browser | Falkon Qt6 installable + QEMU networking (nic + lwip) | active | pkg/falkon/, nic stack in scenario
- C6 apps-filemanager | Sponge-native minimal Qt6 file manager component | active | repos/sponge/src/sponge_files/
- C7 vct-alpha-gaps | shutdown/reboot, update/search, launch; doc drift fixed | active | repos/sponge/src/vct/, docs/06-vct.md
- C8 docs | installation guide, quick-start, known limitations | active | docs/13-installation.md (new), README, 09-roadmap

## Open assumptions (announced defaults)
<!-- assumption | adopted default | rationale | reversible? -->
- Kernel for Alpha media | base-sel4 (both media formats) | project lock "Kernel base locked: base-sel4"; user picked both-formats option under seL4 framing | hard to reverse later, matches docs
- Terminal payload | gems `terminal` server + bash/vim (system_shell pattern) | in-tree, low deps (libc+vfs+Vera.ttf already in contrib) | yes
- Editor payload | qt6_textedit via depot import; fallback vim-in-terminal | depot binary published by cproc; Qt6 base already prepared | yes
- Click-to-launch mechanism | fix sponge_pkgd `_generate_runtime_config` (emit `<binary>`, inline `<config>`, `<parent/>` session routes) | explore report identified exact gaps at sponge_pkgd/main.cc:810-879 | yes
- Networking for browser | QEMU slirp + pc_nic (dde_ipxe) + vfs lwip plugin | proven Sculpt pattern; QEMU-only for Alpha | yes
- vct gap scope | shutdown/reboot + update/search + launch only | other §4 gaps (hardware, snapshot, raw) stay post-Alpha | yes
- Test strategy | tests-after per feature; every wave ships a run/probe scenario (repo convention, AGENTS.md §4.2) | established pattern in run/ | yes
- File manager scope | navigation + open + copy/delete; no rename-undo, no search | minimal honest utility | yes

## Metis gap-analysis amendments (folded 2026-08-02, bg_c84d55d7)
- A1 (G2 BLOCKER): Alpha install semantics = "binaries pre-staged into image at build time; `vct install` toggles runtime enable". No runtime binary delivery on base-sel4 (single image.elf, docs/11 §10.4). Depot import is HOST-SIDE repackaging (tool/pkg_import.mojo) into pkg/<name>/ payload; pkgd never touches network. Recorded in docs/12 amendment (W0) + known limitations (W5).
- A2 (G1/G3/G11): W0 docs-first wave added — docs/12 schema: `<config>` element, parent-route notation, label routing materialization (§7.2 rules 2-3), installed-vs-running lifecycle (installed=registered/stopped, launch adds `<start>` node), update semantics (re-resolve vs repo, honest report); docs/06: shutdown/reboot/update/search/launch specs + §8 drift fix.
- A3 (G6 BLOCKER): quota policy — GUI app metadata caps>=1000 (§11.1 lesson), pkgd generated default caps raised, QEMU -m sized per scenario (alpha desktop 2G), every scenario gates on probe PASS (fail loud, never hang).
- A4 (G7/G19 BLOCKER): W3 leads with networking spike — prepare_port ipxe + lwip, enable dde_ipxe repo in REPOSITORIES, run/sponge-net-probe.run (dde_ipxe nic -> fetchurl -> http://10.0.2.2 host fixture) proves the stack on base-sel4 BEFORE Falkon. system_clock component added for TLS.
- A5 (G8): falkon pin = cproc/pkg/falkon_qt6-jemalloc/2026-04-22; qt6_textedit pin recorded likewise; depot pubkey import step + hashes into docs/11 §5 table.
- A6 (G10): install persistence NOT available on seL4 Alpha media (no writable fs component) — explicit known limitation; base-linux lx_fs persistence stays the dev-flow proof. Roadmap follow-up recorded.
- A7 (G9): pkgd fixes expanded — parent-provides += Gui/Input/Report/File_system/NIC; label-routed sessions (falkon clipboard/shape); readonly/subpath materialization per docs/12 §7.2.
- A8 (G15): editor ownership — GUI editor = qt6_textedit ONLY; vim is the terminal package's console editor (not a fallback deliverable). If depot spike fails: editor criterion = vim-in-terminal, GUI editor becomes known limitation. Explicit, no ambiguity.
- A9 (G23): shutdown mechanism = platform driver System session (ACPI poweroff/reset); acceptance = QEMU process exits in scenario; QEMU-monitor escape documented as fallback.
- A10 (G12): W1 leads with minimal seL4 media smoke test (ISO+disk) BEFORE the full desktop, isolating the grub2->bender->seL4 chain.
- A11 (G4): all new vct subcommands ship --json + --help (+--lang ko where runtime supports) + documented manual escape per AGENTS.md §3.3.
- A12 (G13): unified scenario todo notes the qt6 stale-cmake recovery (docs/11 §10.3).
- A13 (G24/G25): every todo carries an executable PASS marker; W5 flips docs/09 §9 checkboxes with scenario traceability.

## Findings (cited - path:lines)
- Media tooling: genode/tool/run/image/{iso,disk,uefi}; grub2 prepared at genode/contrib/grub2-eb7172dee270fbd9f1bc862d46725fd1fb21d1ea; boot_dir/sel4 supports image includes (lines 68-122); no sponge run script uses image/* (grep, 20 scripts).
- No unified scenario: run/sponge-de-sel4-interactive.run (drivers only), sponge-wm.run, sponge-theme.run, sponge-launcher.run, sponge-leitzentrale.run are disjoint.
- Click-to-launch deferred: repos/sponge/src/sponge-de/launcher/launcher_menu_view.cc:114 logs click only; main.cc header documents deferral.
- pkgd gaps: repos/sponge/src/sponge_pkgd/main.cc `_generate_runtime_config` lines 810-879 — no `<binary>` node, no `<config>` node, session routes point to `<child name=...>` inside pkg_runtime (Gui would fail).
- App ecosystem: terminal at genode/repos/gems/src/server/terminal (LIBS=base vfs); system_shell recipe wraps terminal+bash+vim+coreutils; Falkon preset at genode/repos/gems/sculpt/deploy/falkon_web_browser (cproc/pkg/falkon_qt6-jemalloc); qt6_textedit in cproc depot; NO file manager in ecosystem (Sculpt's is read-only per repos/gems/recipes/pkg/sculpt/README).
- vct: 10 subcommands all wired (command_router.cc:31-82); gaps: shutdown/reboot, update, search, component lifecycle, config export/import, launch, network; docs/06-vct.md §8 stale (claims Phase 2 only).
- Depot machinery: genode/tool/depot/{download,extract,create,publish}; depot_dir defaults to genode/depot.

## Decisions (with rationale)
- D1 media = disk image + ISO, both via run modes of one scenario on base-sel4 (USER DECIDED)
- D2 browser = Falkon Qt6 installable package + QEMU networking (USER DECIDED); heaviest workstream, scheduled last in W3
- D3 file manager = new Sponge-native Qt6 component `sponge_files` (USER DECIDED)
- D4 wave order W1→W5: media/unified desktop first so every later feature lands in the bootable image
- D5 (execution outcome, 2026-08-03): Falkon package COMPLETE but boot-blocked — seL4 boot chain (GRUB→Bender→seL4) has a ~256MB boot-module ceiling; falkon's WebEngine payload is 509MB (libQt6WebEngineCore alone 226MB stripped, irreducible). Three boot strategies tested (in-image.elf, separate tar module via tar_rom, phys_max variants), all bounded-loud failures. Pre-decided contingency invoked: falkon = docs-only known limitation in todo 19; resolution path = disk-based payload staging (block driver + fs + cached_fs_rom, the Sculpt pattern) as post-Alpha work. Evidence: docs/evidence/task-16-phase7-alpha.log; commit fcfec4a86b.

## Scope IN
- Unified desktop scenario (drivers + nitpicker + wm/decorator + sponge-de + configd/themed/pkgd + launcher feed + lz subsystem optional)
- image/disk + image/iso packaging of that scenario; QEMU boot verification of both
- pkgd runtime-config generator fixes (binary/config/parent routes) + launcher click-to-launch + `vct launch`
- Packages: terminal(+bash/vim), textedit (or vim fallback), falkon (+nic+lwip), sponge_files
- vct: shutdown, reboot, update, search, launch; fix docs/06-vct.md §8 + vct README drift
- docs/13-installation.md + quick-start + known-limitations; roadmap/README status updates

## Scope OUT (Must NOT have)
- Real-hardware support matrix (QEMU only; documented limitation)
- Stability/backup guarantees, snapshot/rollback, OTA updates
- Networking beyond QEMU slirp (no wifi, no config UI)
- base-hw kernel port of the media scenario
- Merge op for leitzentrale (already deferred in Phase 6)
- Interactive [Y/n] prompts for install --manual (still no terminal input channel for vct)
- Multi-monitor, login/session manager, wallpaper engine

## Open questions
- none blocking (contingencies for depot-spike failure are pre-decided: A8 for editor; browser -> known limitation per draft risk #2)

## Approval gate
status: execution-authorized (2026-08-02) — Momus review OKAY (bg_4d81e950, zero blocking issues); Codex second pass waived by user; user said "리뷰 한번 돌리고 시작하자" → start-work
<!-- Plan: docs/plans/phase7-alpha.md — 20 todos, 6 waves (W0 docs-first, W1 media, W2 launch, W3 apps, W4 vct, W5 docs+regression). -->
