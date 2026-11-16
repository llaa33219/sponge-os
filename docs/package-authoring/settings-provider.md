# 16 - Package Authoring: Shipping a Settings Provider (§9)

> **Breadcrumb:** [`docs/16-package-authoring.md`](../16-package-authoring.md)
> holds §1-§2, §6-§8, and §10; this file holds §9. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 9. Shipping a Settings Provider (Phase 16 Extension Target)

> **Status: contract of the in-tree implementation.** This section is
> the authoring workflow for the provider-owned Settings contract in
> [`docs/12-package-format.md`](../12-package-format.md) §14. The vct
> side is [`docs/06-vct.md`](../06-vct.md) §4.8. `sponge_pkgd` parses
> and launches providers, and `sponge_settings` opens them. Runtime
> acceptance and the blocked QMenu surface are recorded separately in
> `docs/evidence/criterion18-settings.md`; this authoring guide is not
> a runtime receipt.

### 9.1 What You Are Shipping

A settings *provider* is a separate component with its own window,
its own address space, its own validation, its own persistence, and
its own errors. In packaging terms it is an ordinary payload binary
plus one `<settings>` entry in `metadata.xml`. The difference from a
regular app is only how it gets opened: through the `sponge_settings`
control center or `vct settings open`, instead of (or in addition to)
the app launcher.

The contract is pure Sponge metadata. There is no Xfce code involved,
no panel-plugin ABI, and no `.desktop` file dependency of any kind.
If you find yourself reaching for any of those, stop: the entry schema
in `docs/12` §14.2 is the whole registration surface.

### 9.2 Workflow

1. **Declare the entry.** Add one `<settings>` element per provider to
   `pkg/<name>/metadata.xml` (schema and validation: `docs/12` §14.2
   and §14.3). Give each entry a stable `id` (`[a-z0-9_-]{1,32}`),
   unique within the package and stable across reinstall, plus a
   `title`, a `category`, an optional `icon` and `keywords`, and a
   `target` naming the provider binary in the package's staged
   payload. Leave `<launcher>` alone: it registers the app, never the
   settings entry.
2. **Build and stage the provider binary.** The `target` is one bare
   payload binary name. Path (b) of §3 builds it in-tree, path (a)
   imports it; a single binary may back several entries (`docs/12`
   §14.2's example does exactly that).
3. **Own UI, validation, store, and errors.** The provider draws its
   own window, validates every input before persisting anything,
   persists through its own store, and surfaces its own errors in its
   own window. First-party providers persist through their configd
   key domain (`panel.*`, `background.*`, and so on, one label per
   provider); third-party providers use a package-private store and
   can never write shared DE keys (`docs/12` §14.6). The control
   center is not a validation proxy, not an error relay, and not a
   write path.
4. **Declare only the sessions the provider needs.** Sessions live in
   the package's `<sessions>` block (`docs/12` §4.1) and route through
   the normal three layers (`docs/12` §7). A windowed provider
   typically needs `Gui` plus whatever its persistence uses (`Report`
   and `ROM` for a configd-backed domain, `File_system` for a
   package-private store) and nothing more. Minimum privilege is the
   rule (`AGENTS.md` §1.2), and a settings target is a separate
   component, never an in-process plugin inside the control center.
5. **Test the six behaviors** (§9.4).
6. **Offer direct vct open.** `vct settings list` and
   `vct settings open <package> <settings-id>` (`docs/06` §4.8) must
   work with no control center present. That pair is the manual and
   automation path for the same backend, and it is the escape hatch
   when the UI is the thing that broke.

### 9.3 Core and Third-Party Examples

The core first-party providers the DE ships follow one shape. Panel,
Background, Theme, Shortcuts, Defaults, Display, and WM make up the
full core set, all declared by `pkg/sponge-de/`, so their stable
identity is `(sponge-de, <settings-id>)`; Terminal is not core DE and
stays in `pkg/terminal/`. The table below is an illustrative subset of
that set, not the full roster. Ids and targets below are illustrative;
the schema is identical for all of them:

| Provider | Example entry | Category | Owns (configd key domain) |
|---|---|---|---|
| Panel | `<settings id="panel" title="Panel" category="desktop" target="panel_settings">` | `desktop` | `panel.*` |
| Background | `<settings id="background" title="Background" category="desktop" target="background_settings">` | `desktop` | `background.*` |
| Display | `<settings id="display" title="Display" category="hardware" target="display_settings">` | `hardware` | its own display domain |
| WM | `<settings id="wm" title="Window Management" category="desktop" target="wm_settings">` | `desktop` | its own window-management domain |
| Terminal | `<settings id="terminal" title="Terminal" category="applications" target="terminal_settings">` | `applications` | its own terminal domain |

Each writes only its own domain, through its own configd label, with
configd enforcing the per-label key ACL (`docs/12` §14.6). A WM
provider can no more write `panel.*` than a third-party package can.

Third-party packages use the identical schema and differ only in
persistence. Take a `weather` package shipping an app plus
`<settings id="weather" title="Weather" category="applications"
target="weather_settings">`: it keeps its API key and unit preference
in a package-private store, discovers in the control center like
everything else, and cannot write `panel.*`, `background.*`, or any
other shared key even by mistake.

### 9.4 Testing a Provider

Extend the probe contract of §4.1 with the six provider behaviors.
All of them are observable through the pkgd metadata/launch backend
and the window stack:

| Behavior | What to assert |
|---|---|
| Discovery | After install, `vct settings list` (and the installed metadata) carries the entry; after remove, it does not. Install and remove need no reboot. |
| Open | `vct settings open <package> <id>` (or the control center entry) starts the provider and its window appears. |
| Change | A value changed in the provider window survives a reopen, landing in the provider's own configd domain or package-private store and never in the control center. |
| Focus | A second open while the provider runs raises and focuses the existing window: `ok` with `action="focused"`, no duplicate window, no error. |
| Crash | With the provider killed, the next open starts a fresh instance and nothing else in the system restarts or breaks. |
| Removal | After `vct remove`, opening the stale entry returns `not-installed` and the entry is gone from discovery. |

Two refusals are worth asserting as well, because they protect the
boundary rather than the feature: a `metadata.xml` carrying a
duplicate `id` or a shell-string `target` is rejected at parse time
(`docs/12` §14.3), and a provider missing a session it needs fails
visibly instead of silently (fail-loud, per §4's probe contract).
