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

- `sponge-minimal.run` — Phase 1 goal: minimum boot (skeleton).
  Kernel-agnostic; verified on `base-linux` (no QEMU) and `base-sel4`
  (QEMU, production target).

- `sponge-vct-help.run` — Phase 2: `vct help` prints the command summary.
  Kernel-agnostic.

- `sponge-vct-version.run` — Phase 2: `vct version` prints the version.
  Kernel-agnostic.

- `sponge-vct-status.run` — Phase 2: `vct status` reads live init state
  through a sub-init + `report_rom` relay. Kernel-agnostic.

- `sponge-vct-component-list.run` — Phase 2: `vct component list` lists
  the running components from the live init state report. Kernel-agnostic.

- `sponge-de.run` — Phase 3: interactive Sponge DE demo (panel + themed
  demo window) on base-linux with fb_sdl + nitpicker. Opens an SDL
  window on the host display and accepts host mouse/keyboard. Ends on
  the `sponge-de: panel and window shown` marker; replace the final
  `run_genode_until` with `run_genode_until forever` for interactive
  viewing.

- `sponge-de-test.run` — Phase 3: automated, headless GUI verification.
  No framebuffer driver and no host display: the `sponge_de_probe`
  component reads nitpicker's composited pixels via a Capture session
  (window rendering check) and injects a synthetic click via the Event
  service (input round-trip check), then logs `sponge-de-probe: PASS`.
  Kernel-agnostic.

- `sponge-wm.run` — Sponge DE on the upstream window-management stack
  (wm + window_layouter + decorator). The panel bypasses the wm to a
  fixed nitpicker domain; app windows route through the wm and get
  decorated, movable frames. `wm_probe` verifies the decoration
  pipeline and injects a synthetic title-bar drag, confirming the
  window actually moves (`wm-probe: PASS`). Headless.

- `sponge-de-sel4-interactive.run` — base-sel4 (seL4 / QEMU) interactive
  GUI driver stack: the vendored `drivers_interactive-pc` set (vesa_fb,
  ps2, usb_hid, pc_usb_host, event_filter, platform, acpi, pci_decode)
  built from source and wired with `-device nec-usb-xhci -device
  usb-tablet` for absolute-pointer input. Headless verification matches
  vesa_fb setting the 1024x768 mode, usb_hid binding the QEMU usb-tablet
  as a `POINTER`, and `sponge-de-probe: PASS` — sponge-de (Qt6/Mesa
  softpipe) renders the themed window on seL4 (Capture pixel check) and
  a synthetic click round-trips through its `input` report. base-sel4
  only (`assert {[have_spec sel4]}`); see `docs/08-development.md` §3.8
  and `docs/09-roadmap.md` §11/§11.1 for the kernel-switch/host-tool
  requirements and the resolved Qt6/Mesa-on-sel4 capability-exhaustion
  root cause (sponge-de needs `caps: 1000` on seL4, not the base-linux
  300).

- `sponge-pkg-explain.run` — Phase 4a: end-to-end package-explain flow.
  `vct install nano --explain` writes a request report that `report_rom`
  relays to `sponge_pkgd`, which resolves the package + its `ncurses`
  dependency from staged `pkg_*.xml` metadata ROMs and returns the
  4-step install plan (docs/06-vct.md §5.2). Kernel-agnostic.

- `sponge-pkg-install.run` — Phase 4b: `vct install hello` actually
  starts the `hello` component under a nested `pkg_runtime` init.
  `sponge_pkgd` regenerates pkg_runtime's config (relayed as its `config`
  ROM by `report_rom`); pkg_runtime starts hello, verified by the
  payload's boot marker. Kernel-agnostic.

- `sponge-pkg-remove.run` — Phase 4b: full install→remove lifecycle.
  The `pkg_seq_probe` component drives the channel (vct is short-lived),
  installs hello, removes it, and confirms both results; verified by a
  PASS marker. Kernel-agnostic.

- `sponge-pkg-list.run` — Phase 4c: the `pkg_seq_probe` installs hello,
  then issues a `list` request and asserts hello appears in the result,
  verifying the installed-set inspection path. Kernel-agnostic.

- `sponge-pkg-manual.run` — Phase 4c: `vct install hello --manual`
  renders the install step-by-step (explain→steps→execute) and still
  starts hello under pkg_runtime; verified by hello's marker. Kernel-
  agnostic.

- `sponge-pkg-persist.run` — Phase 4 follow-up #2: installed-set
  persistence across a reboot. Two boots over the same `lx_fs`-backed
  host directory (modelled on upstream `depot_remove.run`): boot 1
  installs hello and writes the store; boot 2 boots a fresh `core`/init
  against the same directory and issues a `list`-only request, asserting
  hello is present — which can only be the restored store, since no
  install was issued this boot. `base-linux` only (lx_fs is a Linux-host
  wrapper); the component code itself is portable.

- `sponge-media-smoke.run` — Phase 7 todo 3: minimal seL4 media smoke
  test. Same payload as `sponge-minimal.run` (core, lib/ld, init, vct)
  but the boot image is materialised as a real El Torito ISO or GPT disk
  image and QEMU boots from that media file (not a direct kernel boot).
  Run with `RUN_OPT="--include image/iso"` or `RUN_OPT="--include
  image/disk"`; gates on vct's banner. Proves the grub2→bender→seL4
  boot chain from removable media before the full desktop exists.
  base-sel4 only.

- `sponge-alpha.run` — Phase 7 todo 4: the unified Alpha desktop
  scenario on base-sel4. Merges five proven component sets into ONE
  boot: the interactive-PC driver stack (vesa_fb/ps2/usb_hid/xHCI), the
  upstream `wm`+`window_layouter`+`decorator` window-management stack,
  the live `sponge_configd`→`sponge_themed`→sponge-de theme pipeline,
  the `sponge_pkgd`+`pkg_runtime` launcher feed (with pre-staged
  `hello`), and the Leitzentrale subsystem (`lz_runtime`/`lz_bridge`/
  `lz_watch`/`lz_viewer`). QEMU RAM bumped to 2G. The composite
  `alpha_probe` asserts all four Alpha criteria in bounded iterations
  (themed panel pixel, launcher report carries `hello`, configd
  broadcast ROM live, lz_viewer Leitzentrale window pixel) and logs
  `alpha-probe: PASS`; the run gates on that marker (fail-loud on
  timeout). base-sel4 only.

- `sponge-pkg-gui.run` — Phase 7 todo 8: GUI-package runtime-config
  generator verification. Installs a minimal Qt6 colored-window package
  (`pkg_gui_demo`) through `sponge_pkgd` and pixel-verifies its
  distinctive green (#00ff00) window through a Capture session, proving
  the generator correctly emits `<binary>` (when it differs from
  `<name>`), the inline `<config>` verbatim (Qt6/libc wiring), the
  `<parent/>` route for `default-route="nitpicker"` Gui sessions, the
  extended `parent-provides` (Gui/Input/Report/File_system/NIC), and
  the GUI-safe `<default>` caps floor (1000, per the §11.1 lesson).
  Kernel-agnostic topology (base-linux native or base-sel4 QEMU); the
  acceptance run uses `KERNEL=sel4 BOARD=pc`. Gates on
  `pkg-gui-probe: PASS`.

## Planned additions

- The end-to-end HOST-injected usb-tablet click proof for
  `sponge-de-sel4-interactive.run`: a host-side QMP `input-send-event`
  usb-tablet click observed through sponge-de's `input` report
  (exercising the real hardware input path end-to-end). The in-guest
  half is already verified — the Qt6/Mesa-on-sel4 capability-exhaustion
  hang is fixed (`docs/09-roadmap.md` §11.1) and `sponge_de_probe`'s
  synthetic click round-trips today.

For how to write a run script, see the
[Genode run framework](https://genode.org/documentation/developer-resources/run)
documentation.
