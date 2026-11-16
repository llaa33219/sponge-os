> Part of [09 - Roadmap](../09-roadmap.md) §9.

## 9. Phase 7: Alpha

### Goal

A minimum version that an everyday user can install and use. The stage
at which one can honestly say "I have used Sponge OS".

### Completion Criteria

- [x] Installable media, both an ISO and a disk image, are provided and boot-verified in QEMU (`run/sponge-media-smoke.run`, `run/sponge-alpha.run`; release artifacts from todos 5 and 6).
- [x] Sponge DE is usable automatically after boot, with the themed panel and launcher visible (`run/sponge-alpha.run`).
- [x] A default app set (browser, terminal, text editor, file manager) is installable, with caveats: terminal, text editor, and file manager are verified booting (`run/sponge-terminal.run`, `run/sponge-textedit.run`, `run/sponge-files.run`); Falkon is packaged but not bootable on the seL4 media because of the boot-module ceiling, as documented in `docs/13-installation.md` (`docs/evidence/task-16-phase7-alpha.log`).
- [x] vct supports everyday tasks including install, configure, status, list, launch, update, search, shutdown, and reboot (`run/sponge-vct-status.run`, `run/sponge-pkg-install.run`, `run/sponge-config.run`, `run/sponge-launch.run`, `run/sponge-pkg-meta.run`, `run/sponge-power.run`).
- [x] Leitzentrale integration is present in the Alpha image and reachable through vct (`run/sponge-leitzentrale.run`, `run/sponge-alpha.run`).
- [x] Installation documentation and a quick-start guide are provided (`docs/13-installation.md`; media and desktop flow proven by `run/sponge-alpha.run`).
- [x] Known limitations are clearly documented, including the QEMU-only target, pre-staged install semantics, non-persistence, networking boundary, Falkon caveat, and limited Leitzentrale viewer (`docs/13-installation.md`; behavior references `run/sponge-net-probe.run`, `run/sponge-pkg-persist.run`, and `run/sponge-alpha.run`).

## Status note (moved from README)

- **Precise limitation scoping (README wording, kept as-is):** "Alpha
  installs enable pre-staged packages and do not persist across
  reboots on seL4 media; networking is QEMU slirp only." The explicit
  "on seL4 media" scoping and the "QEMU slirp only" phrasing appeared
  only in the README; the known-limitations criterion above records
  the same limitation family more generally.

### Explicitly **Excluded** From Alpha

- Stability guarantees (the possibility of data loss is stated).
- Backup / recovery automation.
- Wide hardware support (initially a limited hardware target).
