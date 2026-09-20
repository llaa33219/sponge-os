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

## Key registry (Phase 11 + Phase 16 W2)

The store is a closed registry — an unknown key is a structured error,
never a silent write. Registry entries and list/broadcast output are
name-sorted:

| key                      | type          | allowed values / constraint                    | default         |
|--------------------------|---------------|-----------------------------------------------|-----------------|
| `background.color`       | hex color     | `#RRGGBB` (7 chars, hex digits)               | `#1e1e2e`       |
| `background.image`       | allowlist     | closed set of staged image paths (default: `/system/background/default.png`) | `/system/background/default.png` |
| `bake.applied`           | enum          | `yes`, `no` (`no` is the reset trigger)       | `no`            |
| `bake.profile`           | string        | manifest profile; read-only to users          | `none`          |
| `bake.version`           | uint          | manifest `profile_config_version`; read-only  | `0`             |
| `clock.format`           | format string | non-empty, ≤64 printable ASCII characters    | `HH:mm`         |
| `leitzentrale.enabled`   | enum          | `true`, `false`                               | `false`         |
| `launcher.sort_by`       | enum          | `manual`, `alpha`                             | `alpha`         |
| `panel.height`           | uint range    | base-10 integer in `[16..128]`               | `28`            |
| `panel.ids`              | comma-list    | each token matches `[a-z0-9_-]{1,16}`        | empty           |
| `panel.position`         | enum          | `top`, `bottom`, `left`, `right`              | `bottom`        |
| `panel.visible_widgets`  | enum-list     | comma-separated `clock`, `launcher`, `tasklist` tokens | `clock,launcher` |
| `shortcuts.bindings`     | structured    | one binding per line: `<action>\t<key_sequence>` (see below) | shipped initial binding list |
| `theme.active`           | string        | any non-empty value                           | `light`         |

All fourteen stored keys run on both kernel tags and are
live-reloadable from the configd broadcast. `panel.position` is
documented as live in Phase 16 (U16.2 / D16.2) — the run script/domain
owns the panel placement and updates it on the next broadcast poll
without requiring a reboot.

### Pattern keys (Phase 16 W2, U16.5 / D16.5 / D16.10)

In addition to the flat registry, the daemon supports three pattern
templates — `panel.<id>.height`, `panel.<id>.position`,
`panel.<id>.visible_widgets`. The `<id>` placeholder is a wildcard
segment that matches `[a-z0-9_-]{1,16}`; on first write of a
matching per-id key, the pattern registry clones the corresponding
flat key's `Key_def` (its `kind`, range, default, and enum-list
fields are copied verbatim) into the instantiated slot pool and runs
the cloned validator on the value. **Instantiation is synchronous at
write time** (the broadcast and the persistent store only ever see
the cloned per-id names; the templates themselves are never visible).

The runtime instance count is open-ended up to `MAX_PATTERN_KEYS = 32`
(the 3 templates share the slot pool with the instantiated entries,
so the practical ceiling is `MAX_PATTERN_KEYS - _num_templates =
29` live per-id keys). Reads of an uninstantiated template name find
nothing in the broadcast — the key is only visible after a
successful write. Re-writing an already-instantiated key is a no-op
at the structure level (the existing slot is reused; only the value
changes).

Three failure modes are distinguished at write time:

* **shape miss** (no template matches) — the daemon falls through to
  the unknown-key path with the F15 Levenshtein suggestion.
* **charset violation** (the key has the right `panel.<id>.<suffix>`
  shape but the `<id>` segment contains a character outside
  `[a-z0-9_-]` or exceeds 16 chars) — the daemon emits
  `Genode::warning` and a structured error mentioning the charset
  rule. The value is not touched.
* **registry full** (template matched but `MAX_PATTERN_KEYS` slots
  are taken) — the daemon emits `Genode::warning` and a structured
  error naming the limit. The value is not touched.

The `panel.ids` flat key is the comma-list of the per-id keys the
user has declared active (e.g. `panel.ids=alpha,beta`). The
validator is the same `[a-z0-9_-]{1,16}` charset per token (the
wildcard charset) so the same parser handles both lists.

### Structured shortcuts key (Phase 16 W2, U16.4 / D16.5 / D16.10)

`shortcuts.bindings` is the **deliberate exception to the
one-key-per-setting rule**. The closed registry covers every other
configd key with a one-key-per-setting contract; this key instead
carries an extensible multi-line binding list so the keyboard
shortcut framework can grow without further registry changes. The
format and validator contract:

* One binding per line; the line separator is `\\n`.
* Each line is `<action_token>\\t<key_sequence>` exactly. Any other
  separator shape (zero TABs, multiple TABs, missing TAB) is
  rejected with a structured error mentioning the offending line.
* `<action_token>` is a member of the closed enum `{launcher,
  focus_next, dismiss}`. Unknown action tokens are rejected with a
  structured error naming the token.
* `<key_sequence>` is a dash-separated list of Genode Input-event
  keycodes. Each token is resolved via a synonym table first
  (`Super` → `KEY_LEFTMETA`, `Meta` → `KEY_LEFTMETA`, `Esc` →
  `KEY_ESC`, `Escape` → `KEY_ESC`, `Tab` → `KEY_TAB`, `Return` /
  `Enter` → `KEY_ENTER`, `Backspace` → `KEY_BACKSPACE`, `Alt` →
  `KEY_LEFTALT`, `AltR` → `KEY_RIGHTALT`, `Ctrl` → `KEY_LEFTCTRL`,
  `Shift` → `KEY_LEFTSHIFT`, `Space` → `KEY_SPACE`) and then via
  `Genode::Input::key_code()`. Any token resolving to `KEY_UNKNOWN`
  is rejected with a structured error naming the token.
* Any failed line rejects the **whole write** (no partial-success
  path). Every rejection emits `Genode::warning` AND a structured
  error — never a silent drop.

The initial shipped bindings (Phase 16 W2):

```
launcher\tSuper\nfocus_next\tAlt-Tab\ndismiss\tEscape
```

Future bindings can be added by writing the configd key directly
(no code change). Phase 17 work adds the key-event subscriber that
dispatches these actions to the Sponge DE widgets; Phase 16 only
ships the storage and validation contract.

## Baked defaults (Phase 15 W3)

A deployment opts in with `<bake/>` in configd's component config and routes
`/system/bake/config.defaults` and `bake_manifest.json` as ROM labels
`bake_config_defaults` and `bake_manifest`. If the restored store does not
carry `bake.applied=yes`, configd validates each baked key through the same
closed registry used by user writes, skips invalid/unknown lines with a
warning, records profile/version/applied metadata, and saves once. A restored
`bake.applied=yes` suppresses seeding so user changes survive reboot.

Setting `bake.applied=no` is the explicit reset request used by `vct bake
reset`: configd reapplies only the baked keys plus `theme.active`, leaving
all other user keys untouched, then persists and broadcasts with
`bake.applied=yes`. Without `<bake/>`, no bake ROM sessions are requested and
pre-Phase-15 behavior is unchanged.

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
