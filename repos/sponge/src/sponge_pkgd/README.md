# sponge_pkgd — package backend daemon

The package-management backend for Sponge OS. It is a long-lived,
signal-driven Genode component that resolves package metadata and
dependency graphs and answers `vct install` requests.

## Communication channel (settled design)

`sponge_pkgd` does **not** expose an RPC interface. It communicates
with `vct` through Report/ROM sessions bridged by `report_rom`
(docs/04-components.md §5, docs/12-package-format.md):

```
vct --[Report "request"]--> report_rom --[ROM "request"]--> sponge_pkgd
sponge_pkgd --[Report "result"]--> report_rom --[ROM "result"]--> vct
```

The corresponding `report_rom` policies (full labels include the
child-name prefix):

```
policy | label: sponge_pkgd -> request | report: vct -> request
policy | label: vct -> result          | report: sponge_pkgd -> result
```

This is the same capability-based IPC idiom the rest of the system
uses; `report_rom` is the decoupling layer.

## What it does today (Phase 4a/4b)

- Watches the `request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses `<request op="..." pkg="..."/>`.
- Resolves the package and its dependencies (deterministic DFS with
  cycle detection, docs/12-package-format.md §6) by consulting the
  `pkg_index.xml` manifest, then opening each package's metadata ROM
  `pkg_<name>.xml`.
- `explain` (4a): emits the structured install plan, no side effects.
- `install` (4b / Phase 7 lifecycle): adds the resolved packages to an
  in-memory installed set and regenerates the pkg_runtime config (a
  nested init). Packages whose metadata declares `<autostart/>` get a
  `<start>` node immediately; everyone else registers as STOPPED and
  needs an explicit `launch` to run (docs/12-package-format.md §9.2.1).
- `launch` (Phase 7): transitions an installed-but-stopped package to
  running by adding its `<start>` node and regenerating. Result status
  is one of `ok` / `not-installed` / `already-running`. There is no
  stop operation in Alpha.
- `remove` (4b): drops the package and its now-unused dependencies,
  regenerates the config (also dropping the `<start>` node if the
  package was running).
- `list` (4c): emits the installed set (name-sorted, matching the config
  generator's order) with a per-package `running="yes"|"no"` attribute,
  so callers see exactly what will be regenerated and what is running.
- Emits a structured `<result>` per request.

## Runtime config generation (Phase 4b / Phase 7 lifecycle)

pkgd owns the ENTIRE pkg_runtime `<config>` and regenerates it on every
install/remove/launch. The generator is deterministic — `<start>` nodes
sorted by name, fixed attribute order, no volatile fields — so init's
config-diff leaves already-running children untouched across an
unrelated install. An empty-but-valid config is emitted at startup so
pkg_runtime boots clean before any install.

Phase 7 adds the installed-vs-running gate (docs/12 §9.2.1): the
generator emits a `<start>` node only for packages in the running set
(`_running`). The running set is the union of (a) installed packages
whose metadata declares `<autostart/>` and (b) packages explicitly
added by `launch`. It is re-derived by `_sync_running_state()` after
every install/remove/launch/restore, so the invariant
`_running ⊆ installed_names` always holds before the config is
regenerated.

## Persistent installed-set store (Phase 4 follow-up #2)

The set of explicitly-installed roots is mirrored to a tiny versioned
XML store on a `File_system` session, so installs survive a reboot.
The store holds **only the root names**; the full installed set is
re-derived on load by the same `_sync_installed_from_roots` path that
install/remove use, so the state→config generator is untouched.

Persistence is **opt-in per deployment**: it activates only when this
component's `<config>` carries a `<vfs>` node (see
`docs/12-package-format.md` §13 for the format, failure semantics, and
the inspect/edit/reset/disable escape hatches). With no `<vfs>` node
(the Phase 4 scenarios, or any deployment that declines persistence)
the daemon opens no `File_system` session and behaves byte-identically
to the in-memory build.

On construct: load the store → restore `_roots[]` → re-derive
`_installed[]` → regenerate the pkg_runtime config and the installed
broadcast, so a restored boot restarts the previously-installed
components with no user action. The store is rewritten after every
successful install/remove, before the change is broadcast.

The reference proof is `run/sponge-pkg-persist.run` (base-linux,
`lx_fs`-backed, two boots over the same host directory).

## What is deliberately not implemented

- `update`, version constraints (docs/12 §10).
- Launcher/sponge_configd integration (Phase 5).
- Crash-consistent writes (write-temp-then-rename / checksum). A torn
  write is detected as corrupt on the next boot and the daemon restarts
  empty with a warning, never crashes (docs/12 §13.2).

## Minimum privilege

The component requests `Report` and `ROM` sessions always, and an
optional `File_system` session (via the Vfs library) only when its
`<config>` carries a `<vfs>` node — i.e. only when the deployment has
explicitly enabled persistence. Everything it needs to read
requests/metadata and write results, and nothing more (AGENTS.md §1.2).
It is purely signal-driven and needs no Timer session.
