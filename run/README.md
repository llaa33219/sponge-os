# run/ — Genode run scenarios

Each `.run` file in this directory defines one boot scenario. The
Genode build system runs it like this (the wrapper script does the
same thing):

```bash
./tool/build run sponge-minimal
# Manual equivalent:
make -C genode/build/x86_64 run/sponge-minimal
```

A scenario defines:

- The list of binaries to build
- `init`'s boot configuration (component tree, resources, routing)
- The boot modules (files that go into the RAM disk)
- The QEMU setup (or the real-hardware run options)
- The verification logic (expected-output matching for automated tests)

## Current scenarios

The full catalog entry for every scenario (including all cautions and
limitations) lives in the grouped documents under
[`run/scenarios/`](scenarios/). This index lists each scenario's exact
`.run` name with a one-line summary and a link to its full entry.

**Adding a scenario:** append its full entry to the matching document
under `run/scenarios/`, then add one summary row below under that
document's heading. Keep the exact `.run` name in backticks so it stays
greppable from this index. Grouped documents:

- [`boot-storage.md`](scenarios/boot-storage.md): boot chain, disk/ISO media, persistence
- [`minimal-vct.md`](scenarios/minimal-vct.md): minimal boot, vct commands, power
- [`de-desktop.md`](scenarios/de-desktop.md): Sponge DE, window management, panel/theme config
- [`interactive-qmp.md`](scenarios/interactive-qmp.md): interactive drivers, QMP input
- [`pkg-lifecycle.md`](scenarios/pkg-lifecycle.md): package lifecycle, admission, launch
- [`pkg-apps.md`](scenarios/pkg-apps.md): terminal, textedit, files, calculator, pdf, falkon
- [`drivers-net.md`](scenarios/drivers-net.md): driver packages, fb switching, NIC, hw matrix
- [`alpha-dist.md`](scenarios/alpha-dist.md): Alpha product, media selection, compat contract
- [`supervisor-redteam.md`](scenarios/supervisor-redteam.md): supervision, containment, red-team probes
- [`debug-perf.md`](scenarios/debug-perf.md): debug, performance, field-report probes

### [Boot, storage, and media images](scenarios/boot-storage.md)

- `sponge-boot.run`: Phase 8 P1 storage-chain smoke test on base-sel4 (Tier-0 disk-read chain; AHCI default, `SPONGE_BOOT_NVME=1` NVMe variant). ([full entry](scenarios/boot-storage.md))
- `sponge-desktop-disk.run`: Phase 8 P2 the full Alpha desktop booted from disk; image.elf asserted ≤80 MiB; `SPONGE_DISK_FAIL=1` failure channel. ([full entry](scenarios/boot-storage.md))
- `sponge-persist-disk.run`: Phase 8 P3 pkgd installed-set persistence on the SPONGE-DATA P4 partition across reboot; `SPONGE_PERSIST_RO=1` failure channel. ([full entry](scenarios/boot-storage.md))
- `sponge-boot-i440fx.run`: Phase 12 storage variant, i440fx/PIIX4 IDE smoke (smoke-only; not a real i440fx machine claim). ([full entry](scenarios/boot-storage.md))
- `sponge-boot-multidisk.run`: Phase 12 q35/AHCI two-disk order check (marker only on the second disk's P3). ([full entry](scenarios/boot-storage.md))
- `sponge-desktop-disk-nvme.run`: Phase 12 NVMe desktop-from-disk variant (opt-in; AHCI stays the default product media). ([full entry](scenarios/boot-storage.md))
- `sponge-usb-boot.run`: Phase 12 BIOS-side USB-stick boot of the product ISO (honest scope note in the entry). ([full entry](scenarios/boot-storage.md))

### [Minimal boot, vct commands, and power](scenarios/minimal-vct.md)

- `sponge-minimal.run`: Phase 1 minimum boot skeleton (verified on base-linux and base-sel4). ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-help.run`: Phase 2 `vct help` prints the command summary. ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-version.run`: Phase 2 `vct version` prints the version. ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-status.run`: Phase 2 `vct status` reads live init state through a sub-init + `report_rom` relay. ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-component-list.run`: Phase 2 `vct component list` lists running components from the live init state report. ([full entry](scenarios/minimal-vct.md))
- `sponge-power.run`: Phase 7 `vct shutdown` powers the guest off through the real acpica S5 path. ([full entry](scenarios/minimal-vct.md))
- `sponge-power-fail.run`: Phase 7 failure channel: shutdown without acpica fails loudly with a QEMU-monitor escape-hatch hint. ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-search.run`: Phase 7 the real vct `search hello` end-to-end. ([full entry](scenarios/minimal-vct.md))
- `sponge-vct-search-json.run`: Phase 7 `search hello --json` structured match output. ([full entry](scenarios/minimal-vct.md))

### [Sponge DE, window management, and panel/theme config](scenarios/de-desktop.md)

- `sponge-de.run`: Phase 3 interactive Sponge DE demo on base-linux with fb_sdl + nitpicker. ([full entry](scenarios/de-desktop.md))
- `sponge-de-test.run`: Phase 3 headless GUI verification (Capture pixels + synthetic click round-trip). ([full entry](scenarios/de-desktop.md))
- `sponge-wm.run`: Sponge DE on the upstream wm + window_layouter + decorator stack with a verified title-bar drag. ([full entry](scenarios/de-desktop.md))
- `sponge-panel-config.run`: Phase 11 live configd panel keys on base-linux (7 subphases). ([full entry](scenarios/de-desktop.md))
- `sponge-panel-config-sel4.run`: Phase 11 panel-config flow on base-sel4 with the interactive driver stack. ([full entry](scenarios/de-desktop.md))
- `sponge-de-themed-chrome.run`: Phase 11 themed_decorator window chrome with live theme tint + QMP drag. ([full entry](scenarios/de-desktop.md))
- `sponge-configd-persist.run`: Phase 14 W1 configd persistence skeleton (expected FAIL at the gated marker until W6). ([full entry](scenarios/de-desktop.md))
- `run/compositor.inc`: shared Tcl include: the Tier-0 compositor selection seam (default native: unset or `SPONGE_COMPOSITOR=native` selects `sponge_compositor`; `SPONGE_COMPOSITOR=upstream` is the explicit nitpicker fallback; invalid values fail loudly). ([full entry](scenarios/de-desktop.md))
- `sponge-compositor-opaque.run`: Phase 16 G1 headless opaque golden smoke on the selected Tier-0 (`g1-capture: PASS`). ([full entry](scenarios/de-desktop.md))
- `sponge-compositor-diff.run`: Phase 16 G1 native-vs-upstream Capture differential (`g1-diff: MATCH`; requires `SPONGE_COMPOSITOR=native`). ([full entry](scenarios/de-desktop.md))
- `sponge-compositor-input.run`: Phase 16 G2 real-QMP input battery on the selected Tier-0 (alpha/mask fall-through, focus leave-before-enter, Event injection, pointer report; `g2-input: PASS` with an exact `trace-inject` match and a documented full-trace divergence). ([full entry](scenarios/de-desktop.md))
- `sponge-compositor-c16.run`: Phase 16 G3 (C16) three-arm native-compositor battery, sourcing `sponge-wm-native-qmp.run`, `sponge-wm-native-tasks.run`, and `sponge-wm-native-tiling.run` in three sequential boots with `tier0_kind=native` (`SPONGE_C16_UNIT=qmp|tasks|tiling|all`, default all; strict per-arm isolation; fail-fast). G1-G3 verified with a documented intermittent close-time input failure; receipts and the caution live in the full entry. ([full entry](scenarios/de-desktop.md))
- `sponge-wm-native-tiling.run`: Phase 16 G3 tiling acceptance on the native WM trio (W4 usb-tablet scale transform, title-focus/current-geometry close with pointer barriers and four-marker dispatch, conditional restore of both parked windows, >=20-cycle soak; the intermittent close-time failure is documented in the full entry). ([full entry](scenarios/de-desktop.md))

### [Interactive drivers and QMP input](scenarios/interactive-qmp.md)

- `sponge-de-sel4-interactive.run`: base-sel4 interactive driver stack with three bounded QMP observe phases (input/panel/launch). ([full entry](scenarios/interactive-qmp.md))
- `sponge-wm-qmp.run`: Phase 10 criterion 2 real-pointer window drag via QMP (`wm-probe: PASS`). ([full entry](scenarios/interactive-qmp.md))
- `sponge-terminal-qmp.run`: Phase 10 criterion 5a QMP keyboard input to a focused terminal. ([full entry](scenarios/interactive-qmp.md))
- `sponge-textedit-qmp.run`: Phase 10 criterion 5b QMP keyboard input to a focused text editor. ([full entry](scenarios/interactive-qmp.md))
- `run/qmp.inc`: shared Tcl QMP helper (connect, click, drag, send-key, bounded marker waits) sourced by the QMP scenarios. ([full entry](scenarios/interactive-qmp.md))
- `sponge-usb-kbd-via-qmp.run`: Phase 12 USB HID keyboard hotplug via QMP (`device_add`/`device_del` + send-key audit chain). ([full entry](scenarios/interactive-qmp.md))

### [Package lifecycle, admission, and launch](scenarios/pkg-lifecycle.md)

- `sponge-pkg-explain.run`: Phase 4a `vct install nano --explain` 4-step install plan flow. ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-install.run`: Phase 4b `vct install hello` starts hello under the nested pkg_runtime init. ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-config-regen-survival.run`: pkgd config regeneration across the 32 KiB reporter buffer boundary (children survive; positive control included). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-admission.run`: pkgd refuses launches that don't fit pkg_runtime's budget (`insufficient-resources`), with a live re-launch control. ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-resource-request.run`: runtime quota top-up behavior: immediate grant, parked request, config_bump resolution. ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-remove.run`: Phase 4b install→remove lifecycle driven by `pkg_seq_probe`. ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-list.run`: Phase 4c installed-set inspection (`list`). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-manual.run`: Phase 4c `vct install hello --manual` step-by-step path (explain→steps→execute). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-persist.run`: installed-set persistence across reboot on `lx_fs` (base-linux). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-gui.run`: Phase 7 GUI-package runtime-config generator verification (pkg_gui_demo green-window pixel check). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-lifecycle.run`: Phase 7 installed-vs-running lifecycle (six-step sequence against the `installed` broadcast). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-launch.run`: Phase 7 click-to-launch and `vct launch` share one pkgd backend (two interfaces, one backend). ([full entry](scenarios/pkg-lifecycle.md))
- `sponge-pkg-meta.run`: Phase 7 search/update assertion matrix (six steps, `pkg_meta_probe`). ([full entry](scenarios/pkg-lifecycle.md))

### [Application packages](scenarios/pkg-apps.md)

- `run/terminal_runtime.inc`: shared Tcl include for the three terminal-hosting scenarios (bash + UNIX toolset staging). ([full entry](scenarios/pkg-apps.md))
- `sponge-terminal.run`: Phase 7 terminal package with bash prompt + synthetic keystroke round-trip. ([full entry](scenarios/pkg-apps.md))
- `sponge-textedit.run`: Phase 7 Qt6 text-editor package with pixel verification and launch error paths. ([full entry](scenarios/pkg-apps.md))
- `sponge-files.run`: Phase 7 file-manager package (navigation, copy/delete, read-only refusal). ([full entry](scenarios/pkg-apps.md))
- `sponge-falkon.run`: Phase 7 Falkon package (tar_rom payload); KNOWN LIMITATION: seL4 boot-module ceiling. ([full entry](scenarios/pkg-apps.md))
- `sponge-falkon-disk.run`: Phase 8 P4 Falkon from disk via rom_pkg; the remaining cap-CNode blocker is documented in the entry. ([full entry](scenarios/pkg-apps.md))
- `sponge-calculator.run`: Phase 13 calculator package (in-tree qt6 calculatorform example). ([full entry](scenarios/pkg-apps.md))
- `sponge-pdf-view.run`: Phase 13 mupdf PDF viewer package with bundled `sample.pdf`. ([full entry](scenarios/pkg-apps.md))
- `sponge-terminal.run` toolset extension: Phase 13 W2 CLI toolset tars in the terminal vfs (plus the `<env>` content-form fix). ([full entry](scenarios/pkg-apps.md))

### [Driver packages, NIC, and the hardware matrix](scenarios/drivers-net.md)

- `sponge-pkg-driver.run`: driver packages: `pc_nic` installed through the stock package flow (e1000 bind + DHCP gates). ([full entry](scenarios/drivers-net.md))
- `sponge-pkg-driver.run` framebuffer exclusivity probes: honest scope note; no fb/exclusivity claim is taken from this scenario. ([full entry](scenarios/drivers-net.md))
- `sponge-desktop-disk.run` with `SPONGE_FB_SWITCH=on`: runtime framebuffer-driver switch proof in the product topology. ([full entry](scenarios/drivers-net.md))
- `sponge-net-probe.run`: Phase 7 networking probe (ipxe_nic + nic_uplink + fetchurl fixture GET round-trip). ([full entry](scenarios/drivers-net.md))
- `sponge-high-phys-probe.run`: high-phys IO_MEM gate (16 GiB maps, 2 TiB refused). ([full entry](scenarios/drivers-net.md))
- `sponge-pc-nic.run`: Phase 12 pc_nic/e1000 + nic_router DHCP smoke (QEMU-verified on `-device e1000` only). ([full entry](scenarios/drivers-net.md))
- `sponge-hw-matrix.run`: Phase 16 QEMU hardware-matrix gate driven by `./tool/hwtest` (env knobs per emulated-hardware axis). ([full entry](scenarios/drivers-net.md))

### [Alpha product, media selection, and compatibility contract](scenarios/alpha-dist.md)

- `sponge-media-smoke.run`: Phase 7 minimal seL4 ISO/GPT media smoke (grub2→bender→seL4 from removable media). ([full entry](scenarios/alpha-dist.md))
- `sponge-alpha.run`: Phase 7 unified Alpha desktop scenario (four criteria asserted by `alpha_probe`). ([full entry](scenarios/alpha-dist.md))
- `tool/dist --storage {ahci,nvme}`: Phase 12 product-media storage selector (default `ahci`). ([full entry](scenarios/alpha-dist.md))
- `docs/15-hardware-compatibility.md`: Phase 12 compatibility contract (5×5 matrix + tuple ledger, `tool/hw_compat.mojo assert`). ([full entry](scenarios/alpha-dist.md))

### [Supervision, containment, and red-team probes](scenarios/supervisor-redteam.md)

- `sponge-quota-deadlock.run`: quota-request wedge red→green (false `granted immediately` fix; 925x fewer RPCs). ([full entry](scenarios/supervisor-redteam.md))
- `sponge-containment-churn.run`: heartbeat respawn-loop bounds (four oracles + restart-budget exhaustion warning). ([full entry](scenarios/supervisor-redteam.md))
- `sponge-supervisor-takeover.run`: top-level supervisor red→green takeover proof (pixel oracle, console takeover, desktop restore). ([full entry](scenarios/supervisor-redteam.md))
- `run/supervisor.inc`: shared product wiring for top-level supervision (all five desktop-disk scenarios). ([full entry](scenarios/supervisor-redteam.md))
- `SPONGE_SUPERVISOR_TEST`: env-gated product-topology arms (takeover + control) inside `sponge-desktop-disk.run`. ([full entry](scenarios/supervisor-redteam.md))
- `SPONGE_APP_ISOLATION=on`: env-gated P5.1 app-isolation acceptance arm (decorator closer + wedge fixture). ([full entry](scenarios/supervisor-redteam.md))
- `run/sponge-startup-grace.run`: startup-grace red→green classification proof (STARTING vs wedge, three arms). ([full entry](scenarios/supervisor-redteam.md))

### [Debug and performance probes](scenarios/debug-perf.md)

- `sponge-debug-scrollstorm.run`: scroll-storm resource attribution (no leak; frame coalescing measured at field resolution). ([full entry](scenarios/debug-perf.md))
- `sponge-debug-firstpaint.run`: SPONGE DEBUG time-to-first-paint at field resolution (1.3 s against a 6 s budget). ([full entry](scenarios/debug-perf.md))
- `SPONGE_SLOW_STORAGE_KBPS`: env-gated slow-storage emulation to measure the QEMU/field startup gap. ([full entry](scenarios/debug-perf.md))

## Planned additions

- Spin a usable Sponge OS install workflow through Leitzentrale (the
  P5 stream — currently the install path is `./tool/dist` only).
- More per-package run scripts (`sponge-files.run`-style) for any new
  default-app addition.

For how to write a run script, see the
[Genode run framework](https://genode.org/documentation/developer-resources/run)
documentation.
