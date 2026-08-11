# sponge_configd — configuration backend daemon

The configuration backend for Sponge OS. It is a long-lived,
signal-driven Genode component that owns the flat dotted key-value
store (`theme.active`, `panel.position`, ...) and answers `vct config`
requests.

## Communication channel (settled design)

`sponge_configd` does **not** expose an RPC interface. It communicates
with `vct` through Report/ROM sessions bridged by `report_rom`
(docs/04-components.md §5), using **distinct labels** so it does not
collide with `sponge_pkgd`'s `request`/`result` slots (report_rom is a
single-writer slot per label):

```
vct --[Report "config_request"]--> report_rom --[ROM "config_request"]--> sponge_configd
sponge_configd --[Report "config_result"]--> report_rom --[ROM "config_result"]--> vct
```

The corresponding `report_rom` policies (full labels include the
child-name prefix):

```
policy | label: sponge_configd -> config_request | report: vct -> config_request
policy | label: vct -> config_result             | report: sponge_configd -> config_result
```

A **second** `Expanding_reporter` (node `config`, label `config`)
broadcasts the entire store as a ROM so future watchers
(`sponge_themed`, sponge-de) can react to config changes without
issuing requests:

```
policy | label: <watcher> -> config | report: sponge_configd -> config
```

The broadcast is regenerated on every successful set and emitted once
at startup with the registry defaults.

## What it does today (Phase 5a + Phase 11 + Phase 14 W6)

- Watches the `config_request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses `<request op="..." key="..." value="..."/>`.
- `get`: returns one key's value (error if the key is unknown).
- `set`: validates the value against the key's registered type (string,
  enum, uint range, enum-list, or structural format string), stores it,
  regenerates the broadcast, **persists the new store to a vfs-backed
  store.xml** (when `<vfs>` is present — see "Persistence" below),
  answers ok. Unknown key or invalid value → structured error.
- `list`: enumerates every known key/value, name-sorted.
- De-duplicates identical requests by an `op|key|value` signature.
- Emits a structured `<result>` (ok/error) per request.

## Key registry (Phase 11)

The store is a closed registry — an unknown key is a structured error,
never a silent write. Registry entries and list/broadcast output are
name-sorted:

| key                      | type          | allowed values / constraint                    | default         |
|--------------------------|---------------|-----------------------------------------------|-----------------|
| `clock.format`           | format string | non-empty, ≤64 printable ASCII characters    | `HH:mm`         |
| `leitzentrale.enabled`   | enum          | `true`, `false`                               | `false`         |
| `launcher.sort_by`       | enum          | `manual`, `alpha`                             | `alpha`         |
| `panel.height`           | uint range    | base-10 integer in `[16..128]`               | `28`            |
| `panel.position`         | enum          | `top`, `bottom`, `left`, `right`              | `bottom`        |
| `panel.visible_widgets`  | enum-list     | comma-separated `clock`, `launcher` tokens  | `clock,launcher`|
| `theme.active`           | string        | any non-empty value                           | `light`         |

All seven keys run on both kernel tags and are live-reloadable from the
configd broadcast. `panel.position` remains a boot-time placement choice:
configd persists the value in memory, while the run script/domain owns the
actual panel placement and a reboot is required after changing it.

## Persistence (Phase 14 W6)

When this component's `<config>` carries a `<vfs>` node, the in-memory
key-value store is mirrored to a vfs-backed file at `/store.xml` so
settings survive a reboot. Persistence is **opt-in per deployment** —
without a `<vfs>` node the daemon behaves byte-identically to the
Phase 5a in-memory build.

Activation contract:

- The scenario provides a writable `File_system` session routed from
  sponge_configd to a vfs child (RAM vfs in the W6 headless scenario,
  SPONGE-DATA on the product media via `sponge-desktop-disk`).
- On construct: `_load_store()` reads `/store.xml` if present and
  restores every key found. A corrupted/torn file is detected as
  malformed XML and the daemon restarts from defaults with a warning
  — **never a crash, never trusts partial data** (docs/12 §13.2 contract).
- On every successful `set`: `_save_store()` writes the new store to
  `/store.xml.tmp` first, then renames it over `/store.xml`. The
  rename is atomic on the single-writer vfs; a power loss mid-write
  leaves the previous store intact and the `.tmp` as garbage for the
  next boot's loader to warn-and-discard.

The on-disk format (version 1):

```xml
<sponge-config version="1">
  <entry name="clock.format" value="HH:mm"/>
  <entry name="panel.height" value="28"/>
  ...
</sponge-config>
```

Keys are name-sorted on write so the file is byte-stable for a given
store (matching the determinism contract of the broadcast generator).
The `<lz_diverged>` mirrored key is **not** persisted — it is computed
live from the `lz_model` ROM on every broadcast and would be stale on
the next boot.

Reference proof: `run/sponge-configd-persist.run` (writes three values
through the channel and verifies the on-disk store carries the
most-recent value plus the intermediate write — no clobber between
writes). The corrupt-store variant
`run/sponge-configd-persist-corrupt.run` pre-stages a torn store.xml
via `test/configd_corrupt_seed/` and verifies the daemon recovers with
defaults rather than crashing.

## What is deliberately not implemented

- Notification of watchers beyond the broadcast ROM (watchers poll the
  ROM; report_rom already signals them on change).

## Minimum privilege

The component requests `Report` and `ROM` sessions always, and an
optional `File_system` session (via the Vfs library, gated by
`<config>`'s `<vfs>` node) only when persistence is enabled — i.e.
only when the deployment has explicitly turned it on. Everything it
needs to read requests, write results/broadcasts, and (optionally)
persist the store, and nothing more (AGENTS.md §1.2). It is purely
signal-driven and needs no Timer session.
