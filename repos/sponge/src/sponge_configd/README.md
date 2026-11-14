# sponge_configd — configuration backend daemon

The configuration backend for Sponge OS. It is a long-lived,
signal-driven Genode component that owns the flat dotted key-value
store (`theme.active`, `panel.position`, ...) and answers `vct config`
requests plus the per-provider settings writes of the Criterion 18
provider split (each writer label on its own ACL-scoped channel).

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

## Per-writer-label request channels + key-domain ACLs (Criterion 18)

Every settings writer label owns **exactly one** `config_request` ROM
session and its own result label (D16.1). The writer label is the ACL
identity: every `set` is validated against the closed typed registry
**first** and the label's key-domain **second**. An out-of-domain
write returns the structured `key-outside-domain` error and leaves the
store and the broadcast unchanged (pattern keys included — the
registry pass never instantiates, so a rejected write cannot allocate
a slot either). `vct config` keeps every-domain admin access as the
manual escape hatch. Third-party packages get **no** shared label at
all and use package-private stores instead.

| writer label | request ROM label | result label | default key domain |
|---|---|---|---|
| `vct` | `config_request` (historical) | `config_result` (historical) | `*` (admin) |
| `panel_settings` | `panel_settings_config_request` | `panel_settings_config_result` | `panel.*` |
| `background_settings` | `background_settings_config_request` | `background_settings_config_result` | `background.*` |
| `theme_settings` | `theme_settings_config_request` | `theme_settings_config_result` | `theme.*` |
| `shortcuts_settings` | `shortcuts_settings_config_request` | `shortcuts_settings_config_result` | `shortcuts.*` |
| `defaults_settings` | `defaults_settings_config_request` | `defaults_settings_config_result` | `bake.applied` (the reset trigger) |
| `display_settings` | `display_settings_config_request` | `display_settings_config_result` | `display.*` (reserved) |
| `wm_settings` | `wm_settings_config_request` | `wm_settings_config_result` | `wm.*` (reserved for `wm.layout.mode`) |
| `terminal_settings` | `terminal_settings_config_request` | `terminal_settings_config_result` | `terminal.*` (reserved) |

Each provider reports its requests on its own `<label>_config_request`
label, so the `report_rom` policies keep the single-writer rule
(`AGENTS.md` §1.2). The vct admin channel keeps the historical
`config_request` / `config_result` names. For example:

```
policy | label: sponge_configd -> panel_settings_config_request | report: panel_settings -> panel_settings_config_request
policy | label: panel_settings -> panel_settings_config_result  | report: sponge_configd -> panel_settings_config_result
```

Provider request ROMs are opened only for labels explicitly declared
with a `<writer>` node in configd's component config. With no `<writer>`
nodes, no provider ROM sessions are opened and legacy scenarios without
provider routes keep their previous behavior. A configured label whose
route is absent is reported as unavailable; the scenario must route that
label before using the provider. The per-label result reporter opens
lazily on the label's first request.

### Key-domain allowlists

A domain is a comma/whitespace-separated allowlist of entries:

- `*` — every key (the admin domain),
- `prefix.*` — any key beginning with `prefix.` (so `panel.*` covers
  `panel.height`, `panel.ids`, and the pattern keys `panel.<id>.*`
  in one prefix domain),
- `exact.key` — exactly that one key (e.g. `bake.applied`).

The table above is the built-in Criterion 18 mapping; a deployment
can override any label's domain with an explicit declaration in
configd's own component config (parent-to-child configuration is
explicit and inspectable, `AGENTS.md` §1.2):

```xml
<config>
  <writer label="panel_settings" keys="panel.*,clock.*,launcher.*"/>
</config>
```

A declaration replaces that label's default domain (an empty `keys=""`
is a valid deny-all). Declarations for unknown labels and for the
admin `vct` label are ignored with a warning — vct's every-domain
access is unconditional, and third-party labels deliberately have no
shared channel. Without any `<writer>` node the built-in mapping is
retained for future configured writers, but no provider request ROMs
are opened. Each configured writer must also have its request/result
routes in the scenario. `clock.format`, `launcher.sort_by`, and
`leitzentale.enabled` sit in **no** provider default domain for now
(their key sets are a separate decision) and are therefore vct-only
until a deployment declares otherwise.

### Structured error shape

An out-of-domain `set` answers on the requesting label's own result
channel with the `reason` attribute carrying the code and the same
token prefixing the human-readable text:

```xml
<result status="error" op="set" key="panel.height"
        reason="key-outside-domain"
        error="key-outside-domain: key 'panel.height' is outside the key
               domain of writer label 'theme_settings' (allowed: theme.*)"/>
```

Unknown keys keep their existing structured error (with the F15
suggestion) on every label: the registry gate runs first, so an
unknown key is never reported as `key-outside-domain` and a known key
outside the domain is never reported as an unknown key.

### Migration caveat (universal writer removed)

The W4 provider split (D16.9) removes the `de_config` universal
writer rather than keeping it as a compatibility path: the legacy
sponge-de settings dialog and its shared `de_config_request` /
`de_config_result` writer are deleted, and no shipped component
writes through that channel. Provider-owned settings windows write
only through their own ACL-scoped labels (the table above), and
`vct config` keeps every-domain access as the admin escape hatch.
The universal writer was never a template: a new settings writer
gets a scoped label with a key-domain ACL.

### Deterministic ACL self-test

The gate logic (`key_domain_acl.{h,cc}`) is the same code the set
path consults, and the host self-test exercises it directly —
in-domain success, out-of-domain rejection without mutation, vct
admin success, unknown-key rejection, and the pattern-key panel
domain behavior, plus the label table and allowlist parsing. Build
and run it with host g++ only (no Genode tree build); the exact
command is in `self_test.cc`'s header comment. Exit code 0 with a
final `self-test: PASS (N checks)` line means every check matched.

## What it does today (Phase 5a + Phase 11 + Phase 14 W6 + Criterion 18)

- Watches the `config_request` ROM via `Attached_rom_dataspace` + `sigh`,
  plus one request ROM per provider writer label (Criterion 18).
- On change, parses `<request op="..." key="..." value="..."/>`.
- `get`: returns one key's value (error if the key is unknown).
- `set`: validates the value against the key's registered type (string,
  enum, uint range, enum-list, or structural format string), stores it,
  regenerates the broadcast, **persists the new store to a vfs-backed
  store.xml** (when `<vfs>` is present — see "Persistence" below),
  answers ok. Unknown key or invalid value → structured error. On a
  provider label the key must also lie inside that label's key domain,
  else → structured `key-outside-domain` with no mutation.
- `list`: enumerates every known key/value, name-sorted.
- De-duplicates identical requests by a per-channel `op|key|value`
  signature.
- Emits a structured `<result>` (ok/error) per request on the
  requesting channel's own result label.

## Key registry (Phase 11 + Phase 16 W2 + Phase 16 W6)

The store is a closed registry — an unknown key is a structured error,
never a silent write. Registry entries and list/broadcast output are
name-sorted (each key's writer-label domain follows the Criterion 18
mapping above):

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
| `wm.layout.mode`         | enum          | `floating`, `tiling`                          | `floating`      |

All fifteen stored keys run on both kernel tags and are
live-reloadable from the configd broadcast. `panel.position` is
documented as live in Phase 16 (U16.2 / D16.2) — the run script/domain
owns the panel placement and updates it on the next broadcast poll
without requiring a reboot.

`wm.layout.mode` (Phase 16 W6, D16W.11) is the window-manager layout
mode consumed by `sponge_layouter` from the configd broadcast. Its
`floating` default is applied at startup like every registry default,
so the key is always present in the broadcast even when it was never
written — consumers (the native layouter) read `floating` in that case
and never need a synthetic pre-write. Invalid values (unknown, empty,
case variants, surrounding whitespace) are rejected by the closed-enum
validator with the usual structured error and leave the stored value
and the broadcast unchanged. The write path is `vct config` (the
admin/manual escape hatch, D16.1); the `wm.*` key domain is reserved
for the later `wm_settings` provider label (D16W.11 / Criterion 18).

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

The component requests `Report` and `ROM` sessions always (the vct
`config_request`/`config_result` pair plus one request ROM per
provider writer label; a label's result report opens only on that
label's first request), and an
optional `File_system` session (via the Vfs library, gated by
`<config>`'s `<vfs>` node) only when persistence is enabled — i.e.
only when the deployment has explicitly turned it on. Everything it
needs to read requests, write results/broadcasts, and (optionally)
persist the store, and nothing more (AGENTS.md §1.2). It is purely
signal-driven and needs no Timer session.
