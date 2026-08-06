# Phase 11 W0 — TDD-red baseline (6/6 GREEN)

> Captured 2026-08-07 on a fresh environment (CachyOS, QEMU 11.0.2,
> Genode toolchain 25.05, KVM accel). Each scenario was run on the
> kernel it gates; the exact final PASS marker is quoted per row.
> Full per-scenario logs live next to this file as
> `task-0-phase11-baseline-<scenario>.log`.

| # | Scenario | Kernel / Board | PASS marker | Log |
|---|----------|----------------|-------------|-----|
| 1 | `run/sponge-wm-qmp.run` | sel4 / pc | `wm-probe: PASS` (drag (50,320) -> (149,419), QMP-verified) | `task-0-phase11-baseline-sponge-wm-qmp.log` |
| 2 | `run/sponge-alpha.run` | sel4 / pc | `alpha-probe: PASS` (all four criteria; lz_viewer 660 marker pixels) | `task-0-phase11-baseline-sponge-alpha.log` |
| 3 | `run/sponge-de-test.run` | linux / linux | `sponge-de-probe: PASS` (pixel + injected click round-trip) | `task-0-phase11-baseline-sponge-de-test.log` |
| 4 | `run/sponge-config-probe.run` | linux / linux | `config-seq-probe: PASS` (8 steps incl. 2 expected-error) | `task-0-phase11-baseline-sponge-config-probe.log` |
| 5 | `run/sponge-theme.run` | linux / linux | `theme-probe: PASS` (3-way match 'light' + capture) | `task-0-phase11-baseline-sponge-theme.log` |
| 6 | `run/sponge-launcher.run` | linux / linux | `launcher-probe: PASS` (hello/Utilities + panel band pixel) | `task-0-phase11-baseline-sponge-launcher.log` |

## Fresh-environment fixes that landed before this baseline

These are the deltas between "clean clone" and "6/6 green" on this
machine. All were committed separately; see git log around this file's
commit.

1. **Ports**: `stb` and `ttf-bitstream-vera` were missing from the
   default port set (`tool/build.mojo` `port_list()`); the alpha
   scenario's `check_ports` / ROM staging flagged them. Both added
   (docs/11 §5 rows updated).
2. **`tool/build ports` skip guard**: `prepare_port` re-runs install
   steps for prepared ports; `dde_rump`'s git `update` step makes its
   own patches fail on re-apply. `tool/build.mojo` now skips ports
   whose `contrib/<port>-<hash>/` dir exists.
3. **`tool/build prepare` BOARD clobber**: re-running `prepare` on a
   seL4-configured build.conf silently reset `BOARD ?= pc` to `linux`
   (the template marker `BOARD ?= pc` is indistinguishable from the
   user's seL4 setting). The KERNEL/BOARD rewrite now only fires when
   the pristine-template marker `#KERNEL ?= nova` is present.
4. **REPOSITORIES order**: the create_builddir template puts
   `repos/base-$(KERNEL)` before `repos/base`, which makes
   forwarding-only target dirs (`base-sel4/src/timer/hpet`, target.mk
   only) shadow repos/base's buildable variant — `hpet_timer` failed
   to link with `cannot find component.o`. `prepare` now moves the
   kernel repo after the base/os/demo block (idempotent, marked).
5. **`run/sponge-de-test.run` route fix**: Phase-10 commit 1c602a8a2f
   made the probe open a ROM labeled `result` unconditionally, but
   this scenario never routed it — pre-existing bit-rot of the same
   class as the W6-documented blockers. Added the `result` policy +
   probe route (report_rom; no pkgd producer in this scenario, the
   empty ROM is tolerated by the probe's `valid()` check).

## Notes

- `KERNEL=linux BOARD=pc` is NOT a valid combo in this tree (the run
  tool attempts an initramfs boot flow); base-linux scenarios run
  `KERNEL=linux BOARD=linux`.
- QEMU runs use `-accel kvm` (build.conf default enabled on this host;
  `/dev/kvm` present). The four Phase-10 QMP scenarios' choreography
  is marker-driven, so acceleration does not affect gates.
