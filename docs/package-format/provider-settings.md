# 12 - Package Format: Provider-Owned Settings Entries (§14)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §14. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 14. Provider-Owned Settings Entries

> **Status: contract of the in-tree implementation.** Sections 1
> through 13 describe the format and the lifecycle as they exist
> today; this section does the same for provider-owned Settings
> entries. `sponge_pkgd` parses and validates every `<settings>`
> entry (the pure `settings_rules` module), launches provider
> binaries, and answers the open requests of the `sponge_settings`
> control center ([`docs/05-sponge-de.md`](../05-sponge-de.md) §5.4),
> `vct settings` ([`docs/06-vct.md`](../06-vct.md) §4.8), and the DE
> quick menus (§14.8). The control center is the companion UI: it
> lists and opens providers and carries no settings UI of its own.
> The authoring workflow is
> [`docs/16-package-authoring.md`](../16-package-authoring.md) §9.
> This section documents the contract the code enforces; it claims
> no runtime acceptance and cites no receipts.

### 14.1 What a Settings Entry Is

A package may declare **0..N `<settings>` entries**. Each entry names
one settings *provider*: a separate component binary inside the
package's staged payload that owns its window, its validation, its
persistence, and its errors. Consumers (the control center, vct, the
DE quick menus) only list entries and open them.

Three rules shape everything below.

1. **The stable identity is the pair `(package, settings-id)`.** A
   bare `target="settings"` is deliberately not addressable. One
   package can ship several entries (the §14.2 example ships two),
   and each one is listed, focused, and removed on its own.
2. **`sponge_pkgd` resolves and launches the provider binary.** The
   control center and vct never send command strings, never hold a
   package `File_system` capability, and never interpret target names
   themselves.
3. **`<launcher>` keeps its exact meaning.** The singular
   `<launcher category>` element (§4.1) remains the app launcher
   entry and is never overloaded for settings registration. A package
   may carry a `<launcher>`, any number of `<settings>` entries, both,
   or neither.

### 14.2 Metadata Schema

One `<settings>` element per provider entry, as a child of
`<package>` alongside (never inside) `<launcher>`:

| Attribute | Required | Meaning |
|---|---|---|
| `id` | yes | Stable entry identity inside the package. Bounded identifier, `[a-z0-9_-]{1,32}`. Unique among the package's entries and stable across reinstall: the id is authored metadata, so a remove/install cycle returns the same identity and never invents a new one. |
| `title` | yes | Human-readable title, shown in the control center listing and used as the provider window's display title. |
| `category` | no | Closed enum `desktop`, `hardware`, `system`, `applications`, `other`. Groups entries in the control center. An absent or unknown value warns and falls back to `other` (the `<kind>` typo rule, §4.5). |
| `icon` | no | Name of a staged payload image used as the entry icon. Absent means the control center falls back to a default glyph. The name is display metadata only: callers hold no payload access (§14.5), so the contract specifies no icon-pixel delivery channel and the control center renders an icon-present marker. |
| `target` | yes | The provider binary, named by the same convention as `<binary>` (§4.1) but validated harder (§14.3): exactly one bare name of a file in the package's staged payload. |

Each entry may carry **0..N `<keyword>` children**, short free-form
search terms published in discovery for any consumer that searches
on them. Keywords carry no semantics beyond search. An entry keeps
its first eight; further `<keyword>` children are ignored.

Annotated example (illustrative), one package with **two** entries
that share one provider binary:

```xml
<package>
  <name>sponge-de</name>
  <version>1.0</version>
  <description>Core Sponge DE settings-provider package.</description>

  <!-- Two independent settings entries sharing one provider binary. -->
  <settings id="panel"    title="Panel"          category="desktop"
            icon="panel.png" target="panel_settings">
    <keyword>height</keyword>
    <keyword>widgets</keyword>
  </settings>

  <settings id="position" title="Panel Position" category="desktop"
            target="panel_settings">
    <keyword>edge</keyword>
    <keyword>multi-panel</keyword>
  </settings>

  <sessions>
    <session name="Gui"         default-route="nitpicker"/>
    <session name="Report"      default-route="report_rom"/>
    <session name="ROM"         default-route="parent"/>
    <session name="File_system" default-route="vfs"
             readonly="yes" subpath="/app/panel"/>
  </sessions>
</package>
```

The two entries deliberately share `target="panel_settings"`:
sharing a binary is legal, and the lifecycle identity stays distinct
per entry anyway (§14.5). `panel.png` is an ordinary staged payload
file. The example carries zero app launcher entries and two settings
entries: a settings-only package is first-class, the `<settings>`
family stands on its own, and nothing here implies the DE is an
ordinary launcher app. Shipped packages use the identical schema:
`pkg/sponge-de/metadata.xml` declares seven first-party entries
(Panel, Background, Theme, Shortcuts, Defaults, Display, WM),
`pkg/terminal/metadata.xml` the Terminal entry, and
`pkg/settings_demo/metadata.xml` two third-party entries with
package-private persistence (§14.6).

### 14.3 Parse and Validation Rules

Additive evolution (§10) governs the element family. Parsers older
than this one do not know `<settings>`: they log a bounded
`unknown element <settings>` warning and ignore the entry exactly as
§4.4 prescribes for unknown children, so metadata carrying
`<settings>` still installs on an older parser, just without offering
the entries. The current parser accepts old metadata unchanged (0
entries is valid) and accepts the §14.2 schema closed: unknown
attributes and unknown child elements warn and are ignored, keeping
the door open for future additive extension. One exception:
`exclusive` is known vocabulary (§4.6) and is a hard rejection on
this element family.

The following forms are **hard rejections**. A rejected entry
invalidates its package: the package is refused with a clear English
error naming the offending entry and the failing rule (for example
`id must match [a-z0-9_-]{1,32}` or `target must be one binary name,
not a command string`), the same treatment a missing `<name>` gets in
§4.4. Checks run in a fixed order (id: missing, malformed, duplicate;
then title, the `exclusive` attribute, the target, and an
`<exclusive>` claim child), so a multiply-broken entry reports one
deterministic reason.

| Rejected form | Why |
|---|---|
| Missing `id`, an `id` outside `[a-z0-9_-]{1,32}`, or the same `id` twice in one package | The `(package, settings-id)` identity must be unique per package (§14.1). |
| Missing `title` | The title is the listing and window text; an untitled entry cannot be presented. |
| Missing `target`, or a `target` that is not one bare payload binary name | The target is data, never a command line. An absent or malformed target is refused at parse time; a valid-looking name whose binary is missing from the image is refused at open time as `binary-missing` (the §9.2.1 pre-flight). |
| Absolute paths (`/bin/...`, backslash or drive forms) | The target resolves inside the package's staged payload only. Nothing outside that payload is addressable. |
| Payload escapes (`..`, any path separator in the name) | The same boundary from the other side: a target can name the package's own payload files and nothing else. |
| Shell or interpreter strings (`sh -c ...`, embedded whitespace, quotes, `;`, `\|`, `&`, `$`, backticks, any command-line shape) | There is no Exec-style field in this schema and there will not be one. The launch path starts exactly one binary; it never assembles or interprets a command string. |
| An `exclusive` attribute or `<exclusive>` claim child carried by the entry itself | `<exclusive>` (§4.6) stays a running-set concept for app and driver packages and remains valid on a package that also declares settings entries: it gates only that package's join of the running set at install or launch, so the §4.6 behavior is untouched (any package claiming a free group joins, a second claimant is refused). A settings launch has its own `(package, settings-id)` lifecycle identity and never joins the app's exclusive running-set check, so settings targets are never exclusive. A settings entry point must always be openable, because the focus contract (§14.7) promises exactly that; an entry that could answer `exclusive-conflict` would break the promise. |

Bounds overflow is warn-and-ignore, never a rejection: a package
keeps its first eight `<settings>` entries (later ones are dropped
with one bounded warning each) and each entry keeps its first eight
`<keyword>` children.

### 14.4 Identity and Discovery

**Identity.** A settings entry is identified by `(package,
settings-id)` and by nothing else. The `target` value is a pointer,
not an identity: re-pointing an entry at a new binary keeps its
identity, and two entries may share one binary without colliding.

**Discovery.** `sponge_pkgd` publishes each installed package's
`<settings>` entries inside the `<package>` node of its `installed`
broadcast and of its `list` results (one emitter serves both views,
so they cannot drift). The per-entry representation is the §14.2
field set in fixed attribute order, with keywords as children in
declaration order:

```
<installed ...>
  <packages>
    <package name="..." ...>
      <settings id="..." title="..." category="..." [icon="..."] target="...">
        <keyword>...</keyword>
      </settings>
    </package>
  </packages>
</installed>
```

Watchers, including the `sponge_settings`
control center and `vct settings list`, refresh on that ROM's update
signal. Consequences:

- An install adds its entries to discovery immediately: no reboot and
  no Settings or control-center code change, because consumers render
  whatever `sponge_pkgd` publishes.
- A removal deletes its entries immediately.
- There is no polling requirement. The ROM update signal is the
  change notification.

### 14.5 Launch Path and Privileges

`sponge_pkgd` resolves a `(package, settings-id)` request against the
installed metadata, checks the staged payload, and launches the
`target` binary by emitting the same kind of `<start>` node the app
launch path emits. There is no second package manager and no second
result format: settings opens ride the existing launch machinery
(§9.2.1) behind a `target="settings"` discriminator.

**Request wire.** A settings open is an ordinary `launch` request
carrying the discriminator and the identity pair:

```
<request op="launch" target="settings" pkg="sponge-de" id="panel"/>
```

`id` is the primary attribute name (the metadata vocabulary);
`settings-id` is accepted as an alias. A bare `target="settings"` with
no id is deliberately not addressable (§14.1) and is answered with a
structured error, never a lifecycle answer. Each caller transport
carries its own single-writer request/result label pair (report_rom
is single-writer per label): the control center on `settings_request`
/ `settings_result`, `vct` on its shared request/result labels, the
quick menus on their own launch labels (§14.8). The result answers on
the channel the request arrived on (§14.7).

- **The settings target has a distinct `<start>` and lifecycle
  identity from the app, even when both share one binary.** The
  generated start name is `settings_start_name()`, the deterministic
  projection `"<pkg>.<id>"` of the pair (bounded by the sandbox's
  63-character child-name width). A package name (`[a-z0-9_-]+`) and
  a settings id (`[a-z0-9_-]{1,32}`) contain no `.`, so the
  projection can never alias an app `<start>` name. The generated
  view carries the entry's `target` as its binary and the host
  package's quota, inline `<config>` (emitted verbatim), and
  `<sessions>` declarations: the settings schema adds no second
  quota, config, or sessions vocabulary.
- **Payload target resolution.** The `target` resolves inside the
  package's staged payload and nowhere else: one bare binary name
  (§14.3), composed with the deployment's `binary_prefix` at
  emission, and pre-flighted against the optional `pkg_binaries`
  manifest exactly like the §9.2.1 launch path. A name whose binary
  is not staged answers `binary-missing`.
- **Callers hold nothing.** The control center and vct send an
  identity, never a command string, and hold no package
  `File_system` capability. Every payload lookup happens inside
  `sponge_pkgd`. The control center's whole capability surface is
  the `installed` ROM, its own request/result labels, and Gui/Timer
  sessions: no configd write channel, no package filesystem, no
  provider code loading. On a shared result label (vct's), a caller
  accepts an answer only when it carries the `target="settings"`
  discriminator and a matching identity, so a plain launch result
  can never satisfy a settings open.
- **Providers are separate components and separate windows**, one
  address space each (`AGENTS.md` §1.2), requesting only the sessions
  the provider actually needs. Session declarations stay in the
  package's existing `<sessions>` block (§4.1) and route through the
  same three layers (§7); the settings schema deliberately adds no
  second sessions vocabulary.
- **No in-process plugins and no embedding.** A provider is never
  loaded into the control center, into `sponge-de`, or into another
  provider.

### 14.6 Configuration Ownership

Who may write configuration is decided by label and enforced by
`sponge_configd`, not left to convention:

- **First-party providers** (panel, background, display, WM,
  terminal, and similar) write through a per-provider configd request
  label, and configd enforces a key-domain ACL per label: the panel
  provider's label writes `panel.*`, the background provider's writes
  `background.*`, and so on. No provider writes another provider's
  domain.
- **Third-party providers cannot write shared DE keys at all.** They
  persist through package-private stores scoped to their own package
  and never touch the shared desktop configuration: one store file
  per provider on the package's own `File_system` session (for
  example `subpath="/app/<pkg>"`, §4.1), written crash-consistently
  (temp file, sync, rename) with a provider default plus one warning
  on load failure (§13.2). The store mount lives in the package's
  inline `<config>` (`<vfs><fs/></vfs>`), which the provider's
  `<start>` node receives verbatim; dropping the `<vfs>` node is the
  documented persistence escape hatch (§13.4), leaving the provider
  with in-memory state only. `pkg/settings_demo` is the shipped
  example.
- **The control center has no configuration write path.** It is
  neither a direct writer nor an arbitrary relay: it lists entries
  and opens providers, and that is all. A provider that wants a value
  changed writes it on its own label.
- **`sponge_pkgd` launches only.** It never relays configuration
  writes.
- **Typed-key seam (Display, Terminal).** The `display.*` and
  `terminal.*` key domains are ACL-owned by their writer labels but
  carry no typed keys in configd's closed registry yet. The two
  providers ship their full lifecycle over an honest empty write
  surface; the typed-key additions are marked `TODO(configd
  follow-up)` in their sources, and nothing is invented in their
  place.

### 14.7 Lifecycle, Focus, Failure, and Removal

**Singleton and focus.** A settings target is singleton per
`(package, settings-id)`: at most one live instance. Opening an entry
with no live instance starts one and answers `ok`, `action="started"`;
opening an entry whose instance is live answers `ok`,
`action="focused"` and raises and focuses the existing window (the
delivery mechanism below). A second open is therefore a success (the
entry is reachable), never an `already-running` error and never a
second window. The window identity (its wm/nitpicker label) derives
from the stable pair, and `title` supplies the display text. This
rule is scoped to settings targets only: app launch semantics
(`already-running`, and the separate same-program multi-instance
follow-up) are unchanged. The singleton claim is add-once and bounded
(32 claims); a full claim set answers a structured `error` rather
than a false `started`.

**Focus hint (opt-in).** `sponge_pkgd` must not write `focus_request`:
sponge-de's `TasklistController` is its sole writer (`docs/05` §4.6).
The raise-and-focus half of a focused open therefore rides the opt-in
`settings_focus_hint` report channel, one document per focused open
(never on `started`):

```
<settings_focus_hint pkg="sponge-de" id="panel" start_name="sponge-de.panel" seq="N"/>
```

sponge-de's `TasklistController` consumes the relayed ROM and performs
the raise. Both sides arm the channel only under a
`<settings_focus_hint/>` marker in their own `<config>`, and without
it nothing changes beyond the missing raise: the open still answers
`ok action="focused"`. `seq` is a monotonic emission counter, so a
repeated focused open of the same entry re-raises instead of
replaying a byte-identical stale document.

**Results and errors.** Every open produces exactly one `<result>` on
the answering channel. All results carry `op="launch"`, `pkg`, the
settings `id`, and the `target="settings"` discriminator
(`settings-id` is accepted in place of `id` on the wire):

```
<result status="ok" op="launch" target="settings"
        pkg="sponge-de" id="panel" action="started"/>
```

| Condition | Result |
|---|---|
| Open of a not-installed package, an unknown `settings-id`, or a stale entry after removal | `not-installed` |
| Target binary absent from the image | `binary-missing` (same pre-flight as §9.2.1) |
| Declared quota does not fit the hosting runtime's budget | `insufficient-resources` (same pre-flight), carrying `need_ram` / `avail_ram` / `need_caps` / `avail_caps` figures |
| Open of an entry with no live instance | `ok`, `action="started"` |
| Open of an entry with a live instance | `ok`, `action="focused"` (raise and focus the existing window) |
| Malformed request (no package or no id), a full request queue, a full claim set, or a failed `<start>` emission | `error`, carrying a bounded `error` message |

The named statuses are §9.2.1 launch statuses reused as-is.
`package-manager-unavailable` is deliberately **not** a wire status:
it is the caller's own fail-fast surface, derived when the pkgd
request channel cannot be opened or a bounded one-shot result wait
elapses (the control center waits 6 s and never polls). A topology
without pkgd therefore still lists entries and answers opens
immediately. The resource pre-flight is the opt-in admission gate
(§9.2.1): where the hosting sandbox's state report is not armed or
not yet published, that gate fails open and admits.

**Fresh processing.** Settings opens are processed fresh on every
delivery and never enter the request de-duplication/replay machinery
(§9.2.1): a byte-identical replay cannot express the true singleton
state (a second open must answer `focused`, not replay `started`).
Re-processing is idempotent (the claim is add-once, and the focused
path emits nothing) and touches only the claim set, never the plan,
installed, or running sets.

**Liveness and crash (opt-in).** A crashed provider is contained in
its own address space and takes nothing else with it. The singleton
claim covers a live instance only, so the next open must start a
fresh instance; whether `sponge_pkgd` can see "live" is opt-in. With
`<settings_liveness/>` in its `<config>`, pkgd watches the hosting
sandbox's state report and releases a claim whose
`<child name="<pkg>.<id>">` node there carries an explicit `exited`
attribute. Only an explicit `exited` counts: a clean exit
(`exited="0"`) is still an exit, while mere absence of the child node
is never one (the report can predate the provider's first paint). A
released claim dirties the host's emission slot, regenerates the
runtime config and the installed broadcast, and drops the stale
`<start>` node so the next open takes the `started` path again.
Without the marker, or for a hosting runtime whose state report is
not watched (in practice a driver-hosted claim in a deployment
without `<driver_admission/>`), no report is opened and the claim
survives the provider's exit until the package is removed; pkgd logs
a bounded warning when it creates such a claim. That is the honest
limit of the gate.

**Removal.** A removal deletes the entry from discovery at once and
prunes the entry's claim with the package's exit from the installed
set, so the next regeneration drops the corresponding `<start>` view.
A later open of that entry returns `not-installed` (the stale-open
case), and a reinstalled entry starts clean. This contract introduces
no stop or kill operation, so the Alpha lifecycle of §9.2.1 stays
untouched.

### 14.8 Quick-Menu Operations

The DE quick menus change live settings through the owning providers,
never through a configd write channel of their own. The binding UI
lifecycle is [`docs/05-sponge-de.md`](../05-sponge-de.md) §5.4; the
package-format contract has three parts.

**Ensure-start.** A quick-menu action first ensures the owning
first-party provider is running, over the ordinary §14.5 open request
(each transport carries its own single-writer launch labels; the
quick menu's are separate from the control center's and vct's).
`sponge_pkgd` only starts or focuses the provider: it never carries
the operation or its value. Providers are not kept persistent for
quick edits; if the provider has exited, the menu repeats
ensure-start, and a change is never refused because the provider is
closed.

**Closed quick operations.** The change itself rides a dedicated
per-provider quick pair, ROM `<label>_quick_request` and Report
`<label>_quick_result` (for example `panel_settings_quick_request` /
`panel_settings_quick_result`):

```
<quick_request op="height" value="40"/>
<quick_request op="height" id="alpha" value="40"/>
<quick_request op="reset"/>
```

answered by `<quick_result status="ok" op="height" value="40"/>` or
`<quick_result status="error" op="height" error="..."/>`. Each
pair's vocabulary is closed and owned by its provider: an unknown
operation gets a bounded "unknown operation" error, and the pair can
never carry an arbitrary config key/value. The provider validates the
operation, performs the write on its own ACL-limited label (§14.6),
and answers success or a bounded error, never a silent drop. The pair
is live from process start, so a cold quick-menu action is served
immediately.

**Scope and routing.** Quick pairs exist only for the named
first-party domains; third-party packages get no quick-control
channel. With `<settings headless="yes"/>` in its component config a
provider skips the Gui session and serves quick edits only. Every
channel named in §14.5 through §14.8 must be routed by the deployment
(the report_rom policies of §7), because a denied ROM or Report
session is fatal under Genode's base Env policy: the pkgd-side
markers (`<settings_request/>`, `<settings_focus_hint/>`,
`<settings_liveness/>`) are exactly the opt-in routing declarations,
and the control center opens its own labels lazily so a topology
without pkgd still lists entries (§14.7).

**Session opt-in gates.** Three declarations complete the same
opt-in pattern, one per settings session family, and each is an
explicit capability/routing declaration rather than hidden global
state (`AGENTS.md` §1.2: parent-to-child configuration stays
explicit and inspectable):

- sponge-de's component config `<quick_settings source="providers"/>`
  gates QuickMenuController/quick provider session construction and
  attachment (the §14.8 quick pairs open only under it).
- The control center's component config
  `<settings_launch source="pkgd"/>` gates `settings_request` /
  `settings_result` channel opening; without it the listing remains
  usable and activation fails boundedly as
  `package-manager-unavailable` (§14.7).
- The first-party provider package config `<settings quick="yes"/>`,
  supplied by `pkg/sponge-de/metadata.xml`, gates provider
  quick-pair sessions; without it the provider serves its window
  only and opens no quick session.

Together with the pkgd-side markers above, these gates keep every
settings session declared in inspectable configuration, so a
topology that routes none of them runs on instead of dying in base's
denied-session `sleep_forever` path.
