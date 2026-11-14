# settings_demo — third-party settings providers (Criterion 18 demo)

`settings_demo` is the arbitrary third-party package of the
provider-owned Settings architecture
(`docs/12-package-format.md` §14, `docs/16-package-authoring.md` §9,
D16.10 tier (b)). It declares **two** `<settings>` entries in
`pkg/settings_demo/metadata.xml`, each backed by its own package-
payload provider binary in this directory. The providers own their
window, their input validation, their persistence and their errors:
they persist through a **package-private store** and have **no
shared-DE configd access at all**.

## Entries

| (package, settings-id) | binary | window | value | store file |
|---|---|---|---|---|
| `(settings_demo, greeting)` | `settings_demo_greeting` | "Greeting - settings_demo" | greeting text, 1..24 chars | `greeting.xml` |
| `(settings_demo, about)` | `settings_demo_about` | "About - settings_demo" | note text, 0..32 chars (empty allowed) | `about.xml` |

Value charset for both: `[A-Za-z0-9` space `. , ! ? -]` (closed,
XML-attribute-safe). Each entry is an independent `(package,
settings-id)` identity with its own binary, its own start/lifecycle
identity and its own store file, so the two never alias (§14.1,
§14.4). The 5x7 display font folds lowercase to uppercase at render
time (the `sponge_console` precedent); the stored value keeps the
typed case.

## Provider behavior

- **First paint** — the window (header, labeled value field with
  caret, hint line, status bar) is painted at startup, before any
  input.
- **Input + validation** — typing edits the value (Backspace
  deletes, Enter saves). Every keystroke is validated before it
  becomes the value and the value is validated again before anything
  is persisted (D16.10 tier (b): "each provider validates its own
  values in its own code"). Failures surface as a bounded English
  sentence in the provider's own window and one bounded log line —
  e.g. `Greeting must not be empty`,
  `Greeting is too long (max 24 characters)`,
  `Greeting has an invalid character (allowed: A-Z a-z 0-9 space . , ! ? -)`.
- **Persistence** — the value lives in the package-private store
  (the package's `File_system` session, subpath
  `/app/settings_demo`), one byte-stable XML file per provider:

  ```xml
  <settings-demo version="1"><entry name="greeting" value="Hello"/></settings-demo>
  ```

  Writes are crash-consistent (tmp file, sync, rename) and every
  load failure falls back to the provider default with one warning
  (docs/12 §13.2). State therefore survives provider restart, and
  survives reboot wherever the hosting vfs is disk-backed (a ram vfs
  keeps it across restarts only — the run topology decides).
- **Lifecycle** — closing the window commits any pending valid
  change and exits, reclaiming every session. The next open starts a
  fresh instance (the §14.7 crash path). Singleton/focus (a second
  open must answer `ok action="focused"`, never a duplicate window)
  is `sponge_pkgd`'s contract; each provider creates exactly one
  view and never a second one.

## Capability surface (minimum privilege, §14.5)

| Session | Purpose |
|---|---|
| Gui | the provider window and its input channel |
| File_system (via `<config><vfs><fs/>`) | the package-private store only |

No Report session, no configd label, no ROM beyond the parent
config, no Timer, no GL, no plugins, no interpreter, no command
strings. Errors stay inside the provider's own window, per docs/16
§9.

## Escape hatches

- **Persistence off** — drop the `<vfs>` node from the package's
  inline `<config>` (docs/12 §13.4): the providers then run with
  in-memory state only and say so (`Saved (memory only).`).
- **Manual open** — `vct settings open settings_demo greeting`
  reaches the same provider as the control center does (the vct
  surface, not owned here).

## Files

- `common/store.h` — pure value validation + store XML parse/serialize.
- `common/store_io.h` — the `Vfs::Root` store gate/load/save
  (the `sponge_configd` persistence pattern).
- `common/font5x7.h` — embedded 5x7 glyph table (adapted from
  `src/sponge_console/font5x7.h`, same project, SPDX Apache-2.0).
- `common/paint.h` — window painting (nitpicker_gfx `Box_painter`).
- `common/provider.h` — the shared provider lifecycle.
- `greeting/`, `about/` — one thin `main.cc` + `target.mk` per entry.
- `tests/` — host checks (below).

## Host checks

`tests/check.sh` (from anywhere, repository checkout layout):

1. structural capability surface — the sources open no forbidden
   channel (configd, Report, command execution, Qt/GL/plugins, raw
   `File_system::Connection`);
2. `pkg/settings_demo/metadata.xml` against the docs/12 §14.2/§14.3
   schema (two entries, distinct ids, bare payload targets, closed
   categories, payload icons present, writable package-private store
   session);
3. **adapter seam** — `tests/seam_check.cc` feeds the real metadata
   through the shipped `sponge_pkgd` settings rules
   (`repos/sponge/src/sponge_pkgd/settings_rules.cc`) and asserts
   both entries are accepted with the documented identity. If the
   pkgd rules API moves, this check fails loudly at the seam;
4. `tests/self_test.cc` — the store validation and store-XML cases
   against the shipped `common/store.h`.

## Integration dependencies (not owned here)

- `sponge_pkgd` must parse the `<settings>` entries, publish them in
  the installed metadata, and launch a provider per `(package,
  settings-id)` request: one `<start>` node per entry (name
  `settings_demo.<id>` per `settings_start_name()`), carrying this
  package's inline `<config>` and declared `<sessions>` routes, with
  the §14.7 singleton/focus behavior.
- A run topology must route each provider's `File_system` session to
  a vfs instance serving `/app/settings_demo` **writable** for the
  package's session label (the generated route is a `<parent/>`
  route to the `vfs` service, docs/12 §7.2), stage
  `pkg/settings_demo/metadata.xml` + icons + the two binaries in the
  boot image, and wire `sponge_settings` / `vct` discovery. No run
  scenario is added by this component's change.
