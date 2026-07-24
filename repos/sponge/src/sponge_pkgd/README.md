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

## What it does today (Phase 4a)

- Watches the `request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses `<request op="explain" pkg="..."/>`.
- Resolves the package and its dependencies (deterministic DFS with
  cycle detection, docs/12-package-format.md §6) by opening each
  package's metadata ROM `pkg_<name>.xml`.
- Emits a structured `<result status="ok">...plan...</result>` (or
  `<result status="error" error="..."/>`) via `Expanding_reporter`.

## What is deliberately not implemented

- Actual component installation / `init` tree mutation (Phase 4b).
- The `installed` set is empty in 4a (the init-state reuse check is a
  seam for 4b).
- `remove`, `update`, version constraints (docs/12 §10).

## Minimum privilege

The component requests only `Report` and `ROM` sessions —
everything it needs to read requests/metadata and write results, and
nothing more (AGENTS.md §1.2). It is purely signal-driven and needs no
Timer session.
