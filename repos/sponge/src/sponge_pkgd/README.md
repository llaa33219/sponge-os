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
- `install` (4b): adds the resolved packages to an in-memory installed
  set and regenerates the pkg_runtime config (a nested init that hosts
  the components) via the `runtime` Expanding_reporter.
- `remove` (4b): drops the package and its now-unused dependencies,
  regenerates the config so the nested init abandons the child.
- Emits a structured `<result>` (ok/error) per request.

## Runtime config generation (Phase 4b)

pkgd owns the ENTIRE pkg_runtime `<config>` and regenerates it on every
install/remove. The generator is deterministic — `<start>` nodes sorted
by name, fixed attribute order, no volatile fields — so init's
config-diff leaves already-running children untouched across an
unrelated install. An empty-but-valid config is emitted at startup so
pkg_runtime boots clean before any install.

## What is deliberately not implemented

- Persistence of the installed set across reboots (in-memory only).
- `update`, version constraints (docs/12 §10).
- Launcher/sponge_configd integration (Phase 5).

## Minimum privilege

The component requests only `Report` and `ROM` sessions —
everything it needs to read requests/metadata and write results, and
nothing more (AGENTS.md §1.2). It is purely signal-driven and needs no
Timer session.
