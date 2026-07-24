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

The broadcast is regenerated on every successful `set` and emitted once
at startup with the registry defaults.

## What it does today (Phase 5a)

- Watches the `config_request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses `<request op="..." key="..." value="..."/>`.
- `get`: returns one key's value (error if the key is unknown).
- `set`: validates the value against the key's type (free-form string
  or enum), stores it, regenerates the broadcast, answers ok. Unknown
  key or invalid enum value → structured error.
- `list`: enumerates every known key/value, name-sorted.
- De-duplicates identical requests by an `op|key|value` signature.
- Emits a structured `<result>` (ok/error) per request.

## Key registry (Phase 5a)

The store is a closed registry — an unknown key is a structured error,
never a silent write:

| key             | type   | allowed values             | default |
|-----------------|--------|----------------------------|---------|
| `theme.active`  | string | any non-empty value        | `light` |
| `panel.position`| enum   | `top`, `bottom`, `left`, `right` | `bottom` |

## What is deliberately not implemented

- Persistence across reboots (in-memory only; the state->broadcast path
  is a pure function so a future FS-backed store can replace it without
  touching the generator).
- Notification of watchers beyond the broadcast ROM (watchers poll the
  ROM; report_rom already signals them on change).
- Additional keys land in later Phase 5 slices.

## Minimum privilege

The component requests only `Report` and `ROM` sessions — everything it
needs to read requests and write results/broadcasts, and nothing more
(AGENTS.md §1.2). It is purely signal-driven and needs no Timer session.
