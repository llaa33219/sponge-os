> Part of [09 - Roadmap](../09-roadmap.md) §6.

## 6. Phase 4: Package Management (`sponge_pkgd`)

### Goal

`vct install` and `vct remove` work with automatic dependency
resolution. Initially only a small package set is supported (for
example, `nano`, `bash`, simple apps).

### Completion Criteria

- [x] `sponge_pkgd` backend component implemented (Report/ROM channel,
  deterministic config generator, minimum privileges).
- [x] Package metadata format defined (`docs/12-package-format.md`).
- [x] Dependency resolution algorithm implemented (deterministic DFS
  with cycle detection).
- [x] `vct install <pkg>` adds a component under the `init` tree
  (nested `pkg_runtime` init; verified by `run/sponge-pkg-install.run`).
- [x] `--explain` previews the work (`run/sponge-pkg-explain.run`).
- [x] `--manual` runs step by step (per-step visibility; interactive
  [Y/n] confirmation deferred to the future shell, reported honestly —
  `run/sponge-pkg-manual.run`).
- [x] Packages are read from a package repository (a local directory at
  first — `pkg/`, staged as ROM modules with a build-time
  `pkg_index.xml` manifest).

Additionally delivered: `vct remove` (with dependency GC), `vct list`
(installed-set introspection), and the config-driven `pkg_seq_probe`
test component.

### Known Limitations (documented, deferred)

- Installed-set persistence is delivered (Phase 4 follow-up #2, see
  `docs/12-package-format.md` §13): the explicitly-installed roots are
  mirrored to a versioned XML store on a `File_system` session, so
  installs survive a reboot. The store is opt-in per deployment — it
  activates only when `sponge_pkgd`'s `<config>` carries a `<vfs>` node
  — and is inspectable/resettable by the user (control escape hatch).
  Remaining limitation: **the store is single-writer**. There is exactly
  one `sponge_pkgd` per system and it serializes all writes, so
  concurrent writers are not a concern in practice, but the format
  carries no checksum and is not crash-consistent against a power loss
  mid-write (a torn write is detected as corrupt on the next boot and
  the daemon restarts empty with a warning, never crashes — see
  `docs/12-package-format.md` §13.2).
- Report/ROM channel is single-writer; concurrent callers would
  collide (request-id + backend mutex deferred until concurrent
  callers exist).
- Launcher entry registration needs `sponge_configd` (Phase 5); the
  install output reports it as informational.
