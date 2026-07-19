# run/ — Genode run scenarios

Each `.run` file in this directory defines one boot scenario. The
Genode build system runs it like this:

```bash
cd /path/to/genode/build/<target>
make run/sponge-minimal
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

## Planned additions

- `sponge-de-minimal.run` — single-window Sponge DE demo (Phase 3).

For how to write a run script, see the
[Genode run framework](https://genode.org/documentation/developer-resources/run)
documentation.
