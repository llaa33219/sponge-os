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

## Planned additions

- A base-sel4 interactive GUI scenario (visible desktop under QEMU),
  wiring the `drivers_interactive-pc` driver set (vesa_fb, ps2,
  usb_hid, event_filter, platform, acpi, pci_decode). Phase 3 itself is
  already covered on both kernels by the headless `sponge-de-test.run`.

For how to write a run script, see the
[Genode run framework](https://genode.org/documentation/developer-resources/run)
documentation.
