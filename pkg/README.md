# pkg/ — Sponge OS local package repository

Phase 4 keeps the package repository as a plain directory tree. There
is no index file, no signing, no compression: the directory **is** the
repository. See [`docs/12-package-format.md`](../docs/12-package-format.md)
for the full format specification.

## Layout

```
pkg/
├── README.md              # this file (not parsed by sponge_pkgd)
├── hello/
│   └── metadata.xml       # the format defined in docs/12 §4
├── ncurses/
│   └── metadata.xml
└── nano/
    └── metadata.xml
```

Conventions:

- One directory per package, named exactly as `<name>` declares.
- `metadata.xml` is the only file `sponge_pkgd` parses.
- Any other file under the directory is payload (staged into the boot
  image under `/pkg/<name>/...`). `hello`'s payload component lives at
  `repos/sponge/src/pkg_hello` (binary `hello`); `ncurses`/`nano` are
  metadata-only explain samples in Phase 4.

## How a package is consumed

At build time each `pkg/<name>/metadata.xml` is staged into the boot
image as a ROM module named `pkg_<name>.xml` (see
`run/sponge-pkg-explain.run` for the Tcl staging step).
`sponge_pkgd` opens it via a ROM session with that label and parses it
with `Genode::Xml_node`. Dependency metadata ROMs are opened the same
way during resolution (docs/12 §6).

## Driver packages

A package whose `metadata.xml` carries `<kind>driver</kind>` is a
**driver**: `sponge_pkgd` emits its `<start>` node into the
`driver_runtime` sandbox (a sibling of `pkg_runtime` inside `system`)
instead of into `pkg_runtime`, so the hardware sessions it declares
(`Platform`, `Dma`, `Irq`, `Io_mem`, `Io_port`, `Usb`, …) are granted
by a different `<parent-provides>` list that no application package can
reach. The full contract is docs/12-package-format.md §4.5; the
per-driver verification state is docs/15-hardware-compatibility.md §8.

The shipped set:

| package | binary | build target | notes |
|---|---|---|---|
| `pc_nic` | `pc_nic` | `driver/nic/pc` | `<autostart/>` — verified end to end in QEMU |
| `usb_net` | `usb_net` | `driver/usb_net` | needs a `Terminal` provider in `driver_runtime` |
| `pc_wifi` | `wifi` | `driver/wifi` | needs firmware you supply; none is bundled |
| `intel_gpu` | `intel_gpu` | `driver/gpu/intel` | conflicts with the static `vesa_fb` VGA policy |
| `pc_intel_fb` | `pc_intel_fb` | `driver/framebuffer/intel/pc/spec/x86_64` | same VGA conflict |

**No driver is listed in a bake profile.** A profile entry seeds the
package as a first-boot root, which would install every driver on every
boot. Drivers are opt-in (`vct install <driver>`); the product scenarios
stage their binaries, metadata and index entries through
`driver_runtime::stage` in `run/driver_runtime.inc`.

`<autostart/>` on a driver package means "verified end to end". An
unverified driver deliberately installs as STOPPED so that
`vct launch <driver>` is the user's explicit act (AGENTS.md §1.1).

## Adding a new package

1. Create `pkg/<name>/metadata.xml` following docs/12 §4.
2. Place the binary and any data files inside `pkg/<name>/` (payload —
   not needed for the Phase 4a `--explain` preview).
3. Add the `pkg_<name>.xml` staging step to the relevant `run/*.run`
   scenario.
4. Run `./tool/build run <scenario>` to confirm it stages and resolves.

No central index, no global lockfile. The directory **is** the
repository (docs/12 §5.3).
