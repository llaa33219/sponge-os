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
  image under `/pkg/<name>/...`). Phase 4a ships metadata only.

## How a package is consumed

At build time each `pkg/<name>/metadata.xml` is staged into the boot
image as a ROM module named `pkg_<name>.xml` (see
`run/sponge-pkg-explain.run` for the Tcl staging step).
`sponge_pkgd` opens it via a ROM session with that label and parses it
with `Genode::Xml_node`. Dependency metadata ROMs are opened the same
way during resolution (docs/12 §6).

## Adding a new package

1. Create `pkg/<name>/metadata.xml` following docs/12 §4.
2. Place the binary and any data files inside `pkg/<name>/` (payload —
   not needed for the Phase 4a `--explain` preview).
3. Add the `pkg_<name>.xml` staging step to the relevant `run/*.run`
   scenario.
4. Run `./tool/build run <scenario>` to confirm it stages and resolves.

No central index, no global lockfile. The directory **is** the
repository (docs/12 §5.3).
