# settings_providers — first-party settings providers (Criterion 18)

Eight separate Genode components, one per settings surface. Each
provider owns its UI, validation, writes, lifecycle, and error
reporting (docs/12-package-format.md §14). There is no universal
SettingsController and no arbitrary key/value relay anywhere in this
directory.

| Directory | TARGET | configd key domain | Entry (package, id) |
|---|---|---|---|
| `panel/` | `panel_settings` | `panel.*` (incl. `panel.<id>.*`, `panel.ids`) | (sponge-de, panel) |
| `background/` | `background_settings` | `background.*` | (sponge-de, background) |
| `theme/` | `theme_settings` | `theme.*` | (sponge-de, theme) |
| `shortcuts/` | `shortcuts_settings` | `shortcuts.*` | (sponge-de, shortcuts) |
| `defaults/` | `defaults_settings` | `bake.applied` (the reset trigger) | (sponge-de, defaults) |
| `display/` | `display_settings` | `display.*` (typed-key seam) | (sponge-de, display) |
| `wm/` | `wm_settings` | `wm.*` (`wm.layout.mode`) | (sponge-de, wm) |
| `terminal/` | `terminal_settings` | `terminal.*` (typed-key seam) | (terminal, terminal) |

## Channels

Each provider opens exactly these sessions (minimum privilege):

* **Config channel** (writes): Report `<label>_config_request` /
  ROM `<label>_config_result`, where `<label>` is the provider's
  writer label (`panel_settings`, ...). The labels are pinned to the
  Criterion 18 writer table in
  `repos/sponge/src/sponge_configd/key_domain_acl.cc`; configd
  enforces the per-label key-domain ACL on every set
  (`key-outside-domain` errors never touch the store). Requests use
  configd's shared wire shape: `<request op="set" key value/>` /
  `<result status="ok|error" .../>`.
* **Quick pair** (quick-menu change lifecycle,
  docs/05-sponge-de.md §5.4): ROM `<label>_quick_request` /
  Report `<label>_quick_result`. The vocabulary is closed and
  owned by each provider — a plain `<quick_request op="..."
  [id="..."] [value="..."]/>` document answered by
  `<quick_result status="ok|error" .../>`. Unknown operations get a
  bounded "unknown operation" error; this pair can never carry an
  arbitrary config key/value. The pair's sessions open only when
  the provider's package config carries `<settings quick="yes"/>`
  (supplied by `pkg/sponge-de/metadata.xml`); without that opt-in
  the provider is window-only and opens no quick session.
* **Broadcast** (prefill): ROM `configd`, the configd store
  broadcast (`<config><key name value/></config>`). Optional: rows
  fall back to their registry defaults when it is not routed.
* **Gui + Input** (window): one top-level window per provider,
  identity `(package, settings-id)`. With `<settings headless="yes"/>`
  in the component config the Gui session is skipped and the process
  serves quick edits only (the quick-menu ensure-start cold path).

## Lifecycle

* **Singleton + focus**: one process, one window per
  `(package, settings-id)`. A second open is resolved by
  sponge_pkgd's focus/raise path (docs/12 §14.7) — never a second
  window, never an `already-running` refusal.
* **Ensure-start**: the quick pair is live from process start, so a
  quick-menu action that cold-starts the provider is served
  immediately. Providers are not kept persistent for quick edits;
  the menu repeats ensure-start after a provider exit.
* **Errors**: every validation failure and refused write surfaces as
  bounded English text on the window status line and/or the quick
  result — never a silent drop (AGENTS.md §1.4).

## Validation

All values pass `common/sp_validate.h` locally before any request is
sent; the validators mirror sponge_configd's closed typed registry
(`_value_valid` / `_shortcuts_valid`) so provider-side and
configd-side answers match. configd remains the authority: its
structured error still surfaces when the two disagree. The exact
logic is host-tested (`tests/`), so the tested gate is the shipped
gate.

## Build

Plain Genode components (`LIBS := base`, no Qt, no libc). Build by
source directory (the Genode build goal is the `src/` directory; the
TARGET name is the produced binary):

```
make -C genode/build/x86_64 \
     settings_providers/panel settings_providers/background \
     settings_providers/theme settings_providers/shortcuts \
     settings_providers/defaults settings_providers/display \
     settings_providers/wm   settings_providers/terminal
```

Shared scaffolding is header-only under `common/`; each `main.cc`
is the provider's rows + closed quick vocabulary (~100-250 lines).

## Tests

```
./repos/sponge/src/settings_providers/tests/run.sh                 # host logic self-test
./repos/sponge/src/settings_providers/tests/structural_checks.sh   # label/metadata pins
```

## Display / Terminal typed-key seam

`display.*` and `terminal.*` are ACL-owned by their writer labels but
have NO typed keys in configd's closed registry yet. Those two
providers ship their full lifecycle with an honest empty write
surface; the exact typed-key additions are marked
`TODO(configd follow-up)` in `display/main.cc` and `terminal/main.cc`
(and repeated in each README). Nothing is invented in their place.

## Integration dependencies (outside this directory)

* sponge_configd per-label request ROM sessions + key-domain ACL
  (`key_domain_acl.{h,cc}` + provider channels in `main.cc`); the
  labels here match its writer table.
* sponge_pkgd `<settings>` parse/launch/focus (docs/12 §14.5-14.7).
  The `<settings .../>` component-config node
  (package/id/title/headless/quick) is the seam pkgd's runtime-config
  generator can fill.
* report_rom routing of the per-label request/result and quick pairs
  in the product scenarios (run scripts are owned elsewhere).
* The quick-menu bridge (sponge-de settings/ rewrite) consumes the
  quick pairs; its labels are the ones documented above.
