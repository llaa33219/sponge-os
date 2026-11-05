# Phase 16 P2.2 — per-runtime target validation receipt

Date: 2026-10-28. Scope: run-script targets for the `simple`, `terminal`,
and `browser` runtime classes. This is QEMU evidence only, not a
real-hardware claim.

## Delivered structure

- `run/runtime-simple.inc`: real package-to-producer map, build targets,
  fail-loud simple-binary staging, and the 4512-cap / 1288M class row.
- `run/runtime-terminal.inc`: shared terminal toolset and VeraMono
  producer, sub-init binary staging, the exact 12-file terminal payload,
  and the 1100-cap / 224M class row.
- `run/runtime-browser.inc`: the single 30-entry Falkon/WebEngine
  exclusion list and the 200000-cap / 1024M class row. Browser payload is
  still excluded in P2.2; P2.6 owns staging.
- `run/runtime-quota.inc`: derives and verifies the current single
  `pkg_runtime` grant (`caps=230000`, `ram=512G`). The literal grant and
  last-start invariant remain in each scenario.

No pkgd or other C++ product code changed. The current single
`pkg_runtime` and `driver_runtime` topology, routes, and grouping are
unchanged. `Platform`, `Dma`, `Irq`, `Io_mem`, and `Io_port` remain
driver-runtime-only. Application emission continues through the P1 pure
`Sponge::Pkg_config` library.

Commits:

- `1e4e42030c` — includes and uefi-usb migration.
- `6dea5ad432` — opt-in hello-only alpha-probe acceptance mode.
- `35d2cb1ec1` — the other four product scenario migrations.

## Static equivalence evidence

All checks were run against saved pre-migration scenario baselines:

- `sponge-desktop-disk.run`, `sponge-desktop-disk-uefi.run`, and
  `sponge-desktop-disk-uefi-nvme.run`: composed build-target sets equal
  the baseline sets; browser exclusion lists are identical; each gains
  exactly the 12 terminal payload files (10 noux tars, `vct-bin.tar`,
  and `VeraMono.ttf`). No `/system/bin/textedit`, `vfs`, or
  `cached_fs_rom` was added by migration.
- `sponge-desktop-disk-nvme.run`: the hello-only build and package set is
  preserved; browser exclusions are identical; no terminal payload or
  additional package was introduced.
- `runtime_terminal::payload_files` contains exactly the same 12 files as
  the uefi-usb baseline.
- `runtime_quota::pkg_runtime_caps()` derives 230000 from the class rows
  (sum 205612 plus the 900-cap editor closure, headroom, and round-up),
  and `runtime_quota::verify 230000 512G` passes.
- Host-side pure emission self-test: `self-test: PASS (equivalence +
  golden snapshots)`.

## Serial runtime matrix

Every scenario was built and run serially on base-sel4/QEMU. The host's
8 MiB memlock limit required a non-committed io_uring queue-depth clamp
for QEMU 11.1.2; that host workaround is not part of the scenarios.

| scenario | result |
|---|---|
| `sponge-desktop-disk-uefi-usb` | 3/3 structural gates; `alpha-probe: PASS`; compositor first paint observed. |
| `sponge-desktop-disk` | `alpha-probe: PASS`; `Run script execution successful.` |
| `sponge-desktop-disk-uefi` | 3/3 structural gates. The run is a structure gate that does not exercise the desktop gate (D15.16), so no desktop PASS is claimed — not a QEMU boot block: the W^X/bender-NOP cause is retired (`docs/11-environment.md` §4 ledger rows 10 + 11; QEMU/OVMF boots reach Genode; the sibling `sponge-desktop-disk-uefi-usb` is a full `alpha-probe: PASS`) and the run's "QEMU boot blocked" banner is a classifier bug (sweep §8 B2). A pre-existing double-quoted banner regex was braced so the accepted gate executes. |
| `sponge-desktop-disk-nvme` | `partition-check: PASS (Number: 3)` and `alpha-probe: PASS` using the opt-in hello-only contract: install hello, live configd broadcast, launcher hello/Utilities, and themed-panel pixel. |
| `sponge-desktop-disk-uefi-nvme` | 3/3 structural gates. The same D15.16 structure-gate scope applies (no desktop gate exercised; not a boot block); the equivalent pre-existing regex defect was braced. |

The hello-only probe defaults are unchanged: without `minimal="yes"`, the
existing seven-package install, eight-key configd assertion, full launcher
set, and themed-panel checks execute as before.

## Boundary

P2.2 does not prove working terminal launch on media lacking the separate
`vct_tty` topology. It proves payload parity and unchanged runtime
structure. P2.3 subsequently verified the existing nested `binary_prefix`
rewrite on the fully staged UEFI-USB topology
(`docs/evidence/phase16-runtime-rom-failures.md` §9); P2.4 owns actual
runtime class grouping; P2.6 owns browser payload staging.
