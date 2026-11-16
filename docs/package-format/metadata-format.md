# 12 - Package Format: Metadata File Format (§4)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §4. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 4. Metadata File Format

Every package carries exactly one metadata file: `metadata.xml`.

### 4.1 Grammar

The file is a single XML document rooted at `<package>`. Children are
listed below in the order they are expected to appear; parsers must not
rely on order, but authors should write in this order for readability.

| Child element | Cardinality | Purpose |
|---|---|---|
| `<name>` | 1 | Package identifier. Lower-case ASCII, `[a-z0-9_-]+`. Also the directory name under `pkg/`. |
| `<version>` | 1 | Free-form string. Phase 4 does **not** interpret it for matching; it is recorded as-is for display. |
| `<description>` | 1 | One short sentence, ASCII or UTF-8. Surfaced in `vct list` and the launcher tooltip. |
| `<binary>` | 0..1 | Name of the component binary to start. Defaults to `<name>` if omitted. |
| `<quota>` | 0..1 | Resource hints for the generated component (`ram`, `caps`). Defaults: `ram="32M"`, `caps="512"`. |
| `<resizeable>` | 0..1 | Phase 16 W9 opt-in marker (closed-form string content: `yes` or `no`). Default: `yes`. Documents whether the launched window should accept resize affordances (drag on edge/corner sizers, wm `resize_request` propagation). The Qt6 QPA registers `gui_connection.info_sigh()` for every `QPlatformWindow` (genode/contrib/qt6_base-.../src/plugins/platforms/genode/qgenodeplatformwindow.cpp:665), so every Qt6 app is automatically resizeable — the `<resizeable>` attribute is a forward-compatible explicit declaration rather than a runtime gate. Foreground control (Phase 17+, non-Qt apps): an explicit `<resizeable>no</resizeable>` would tell `sponge_pkgd`'s runtime-config generator to add `<signal>` plumbing that suppresses the Gui session's resize signal. For Phase 16 the attribute is documentary; the criterion-9 release-media gate (`run/sponge-de-release-resize.run`) and themed-chrome sibling (`run/sponge-de-themed-chrome-resize.run`) exercise the propagation through the existing wm + window_layouter + motif decorator stack. |
| `<config>` | 0..1 | Optional inline Genode configuration. Its inner XML is emitted verbatim into the package's generated `<start>` node. This is used for application wiring such as a libc node and a `vfs` node with font or plugin tar files. |
| `<kind>` | 0..1 | `app` (the default) or `driver`. Closed-form, exactly like `<resizeable>`. Decides **which sandbox** `sponge_pkgd` emits the package's `<start>` node into: one of the app runtimes (`pkg_runtime_simple`, `pkg_runtime_terminal`, `pkg_runtime_browser`) for an app, picked by `<runtime_class>`, or `driver_runtime` for a driver (§4.5). `<kind>` is the security and capability boundary and outranks `<runtime_class>`: a driver package always lands in `driver_runtime`. Any other value is warned about and treated as `app`, so a typo can never silently land a driver in an application runtime. |
| `<runtime_class>` | 0..1 | App packages only: `simple` (the default), `terminal`, or `browser`. Closed-form, exactly like `<kind>`. Selects which app runtime hosts the `<start>` node (§4.5): `pkg_runtime_simple`, `pkg_runtime_terminal`, or `pkg_runtime_browser`. Any other value is warned about and treated as `simple`. On a `<kind>driver</kind>` package the element is ignored with a warning; placement stays `driver_runtime`, never an app runtime. |
| `<exclusive>` | 0..1 | Carries one attribute, `group`. At most one **running** package may carry the same group at a time; an install or launch that would create a second one is refused with `exclusive-conflict` naming the incumbent (§4.6). An absent or `group`-less element means "no exclusive claim". The framebuffer drivers use `group="fb"`. |
| `<autostart>` | 0..1 | If present, the package is started automatically when it is installed and when its installed state is restored. Without it, installation registers the package but does not create a `<start>` node. |
| `<launcher>` | 0..1 | If present, the package gets a launcher entry. Carries one attribute `category`. |
| `<settings>` | 0..N | **Provider-owned Settings entries (§14).** One entry per settings provider, parsed and validated by `sponge_pkgd`. A separate element family from `<launcher>`, which is never overloaded for settings. |
| `<dependencies>` | 0..1 | Container of `<pkg>` children, each naming another package by `<name>`. Empty container means "no dependencies". |
| `<sessions>` | 0..1 | Container of `<session>` children, each declaring a required session and its default route. Omit entirely for components that need only parent-provided services (`LOG`, `ROM`, `PD`, `CPU`, `Timer`). |

A `<session>` element carries the following attributes:

| Attribute | Required | Meaning |
|---|---|---|
| `name` | yes | Session type as Genode names it (`Gui`, `Input`, `File_system`, `ROM`, `Report`, ...). |
| `default-route` | yes | The service or component name the session should be routed to when no user override exists. `parent`, `nitpicker`, `vfs`, and other named outer-system services use the parent route described in §7.2. |
| `readonly` | no (default `no`) | Only meaningful for `File_system`. `yes` mounts the package's directory read-only. |
| `subpath` | no | Only meaningful for `File_system`. The path inside the `vfs` subtree (typically `/app/<name>`). |
| `label` | no | Optional session label, forwarded as Genode's `<policy label="...">` hint. |

All attributes are lower-case, ASCII, and quoted with double quotes,
following Genode convention. Comments use `<!-- ... -->`.

The optional `<config>` element is a configuration fragment, not a
package-manager setting. Its child elements are preserved as XML and
emitted verbatim inside the generated package `<start>` node by
`sponge_pkgd`'s runtime-config generator. This lets Qt and libc-based
applications carry inline wiring such as `<libc>` and `<vfs>` nodes with
font or plugin tar files. The optional `<autostart/>` marker is empty and
has no attributes.

### 4.2 Annotated Example: `hello`

A minimal GUI-free package. The `hello` binary only writes to `LOG`,
so it declares nothing beyond its identity: omitting `<sessions>`
entirely means "parent-provided services only" (§4.1), which is why
this package installs and starts even in a minimal scenario with no
vfs or nitpicker.

```xml
<package>
  <!-- Identity (required, in this order) -->
  <name>hello</name>
  <version>1.0</version>
  <description>Prints hello to the log. The smallest possible Sponge OS package.</description>

  <!-- Component wiring -->
  <binary>hello</binary>                             <!-- defaults to <name> when omitted -->
  <autostart/>                                       <!-- Alpha example: start at install and boot -->
  <quota ram="8M" caps="256"/>                       <!-- overrides only when smaller than defaults -->

  <!-- Launcher integration (optional) -->
  <launcher category="Utilities"/>

  <!-- Dependency graph (empty here; hello has no deps) -->
  <dependencies/>

  <!-- No <sessions> element: parent-provided services only (§4.1) -->
</package>
```

### 4.3 Annotated Example: `nano`

A more realistic package with one dependency and three sessions. This
is the shape a typical GUI application has.

```xml
<package>
  <name>nano</name>
  <version>6.4</version>
  <description>Small, friendly text editor.</description>

  <binary>nano</binary>
  <quota ram="48M" caps="600"/>

  <launcher category="Editors"/>

  <dependencies>
    <pkg>ncurses</pkg>
  </dependencies>

  <sessions>
    <session name="Gui"          default-route="nitpicker"/>
    <session name="Input"        default-route="input_drv"/>
    <session name="File_system"  default-route="vfs"
             readonly="yes" subpath="/app/nano"/>
  </sessions>
</package>
```

### 4.4 Error Handling

`sponge_pkgd` is best-effort about malformed input, mirroring the
theme parser's behavior (`docs/10-theme-format.md` §9).

- Unknown child elements of `<package>` are logged with
  `Genode::warning("pkg: unknown element <foo>")` and ignored.
- Unknown attributes on a known element are logged and ignored.
- Missing **required** elements (`<name>`, `<version>`,
  `<description>`) cause the package to be **rejected**: `vct install`
  fails with a clear English message naming the offending field.
- A `<dependencies>` entry that names a non-existent package is
  treated as a hard error at resolution time, not at parse time (the
  repository is only walked when needed).
- A `<session name="...">` whose `name` is not a Genode session type
  is rejected with a clear error.

A rejected package does not appear in `vct list`, `vct install`, or
`vct search`. The parser continues with the rest of the repository.

### 4.5 Package Kinds: `app` and `driver`

A package declares what it is with `<kind>`:

```xml
<kind>driver</kind>
```

An app package can additionally say which runtime class it belongs to
with `<runtime_class>` (Phase 16 P2.4):

```xml
<runtime_class>terminal</runtime_class>
```

Class placement is enabled by an explicit `<grouped_runtimes/>` marker in
`sponge_pkgd`'s own config. Without that marker, the legacy mode keeps
one `pkg_runtime` for every app package and ignores the class for placement
(with a diagnostic); this preserves older headless topologies.
Grouped mode is the shipped product topology: all five desktop-disk
product scenarios emit the three per-class configs, and the grouped
runtimes are acceptance-verified on the product disk media (P5.1:
three consecutive all-green suite passes with real launcher first
paint + input for every built-in desktop package —
`docs/evidence/phase16-runtime-acceptance.md`,
`docs/evidence/phase16-p51-grouped-app-matrix.md`).

| Kind | Sandbox that hosts the `<start>` node | Generated `<parent-provides>` carries |
|---|---|---|
| `app` (default) | `pkg_runtime_simple`, `pkg_runtime_terminal`, or `pkg_runtime_browser`, per `<runtime_class>` | the fixed application service set (§7.2) plus the sessions the packages **in that class** declared |
| `driver` | `driver_runtime` | `ROM`, `PD`, `RM`, `CPU`, `LOG` plus the sessions the installed **driver** packages declared |

**Runtime classes.** `<runtime_class>` subdivides app packages into
three runtimes. The values are closed-form (`simple`, `terminal`,
`browser`), exactly like `<kind>`:

| Class | The class's packages are | Runtime that hosts the `<start>` node | Class membership currently used by the tree |
|---|---|---|---|
| `simple` (default) | plain ROM start nodes | `pkg_runtime_simple` | `hello`, `textedit`, `files`, `calculator`, `pdf_view`, `debug` |
| `terminal` | noux / sub-init packages needing `binary_prefix` and tar payloads | `pkg_runtime_terminal` | `terminal` |
| `browser` | heavy WebEngine closures | `pkg_runtime_browser` | `falkon` |

An app package that omits `<runtime_class>` is `simple`. An unknown
value is warned about and treated as `simple`, mirroring `<kind>`'s
typo handling, so a typo can never silently land a package in a
heavier runtime. On a `<kind>driver</kind>` package the element is
ignored with a warning: `<kind>` stays the security and capability
boundary, `driver_runtime` stays the driver sandbox, and a
`<runtime_class>` never moves a driver into an app runtime.

The classes are grouping, not a per-package-init framework: each
runtime is a single nested `init` hosting every `<start>` node of its
class, and the class describes how those members are wired, nothing
more. Membership is a plain string match of the package's class token
against the runtime's, so each runtime's generated `<parent-provides>`
unions exactly its own members' declared sessions.

All four runtime configs are generated by the same `sponge_pkgd` pass
but regenerated dirty-scoped: an install / remove / launch rewrites
only the runtime config the change lands in. A change in one app class
therefore never regenerates another app class or `driver_runtime`, and
a driver change never regenerates an app runtime. The sandboxes cannot
disagree about the installed set because each config covers exactly
its own members. `driver_runtime`
is a nested `init` inside the `system` sandbox, a **sibling** of
the three app runtimes; its own `<config>` ROM is likewise owned by `sponge_pkgd`
through the same `report_rom` relay pattern
(`docs/04-components.md` §5).

**Why the split is the security boundary.** An application package
cannot obtain a `Platform`, `Irq`, `Io_mem` or `Io_port` session, no
matter what it declares or asks for: its session list is unioned into
its class runtime's generated `<parent-provides>`, which contains no
hardware service, and each `<start>` node's `<route>` is emitted from
the package's declared `<sessions>` alone. All three app classes keep
this boundary; the split changes grouping only, and parent-provides
hardware services remain driver-only. Hardware capabilities reach
driver packages and nothing else (AGENTS.md §1.2).

**Admission control applies to both.** A driver's declared quota is
checked against `driver_runtime`'s **own** live budget (its sandbox
state report), never against an app runtime's; each app class is
checked against its class runtime's **own** report, never another
class's. Each sandbox has its own grant, so reading the wrong one
would compare a quota against a pool it is never subtracted from. The
gate is armed per sandbox with config markers — `<driver_admission/>`
for `driver_runtime`, and per class in grouped mode
(`<simple_admission/>`, `<terminal_admission/>`, `<browser_admission/>`
for `runtime_simple_state`, `runtime_terminal_state`,
`runtime_browser_state`) — because each relay has to exist in the
deployment and a denied `ROM` session is fatal. In grouped mode
(`<grouped_runtimes/>`) `<admission/>` is the **simple-class alias**,
arming exactly what `<simple_admission/>` arms; in legacy mode it stays
the single-app gate on `runtime_state`. The per-class nodes are inert
(with a warning) when `<grouped_runtimes/>` is absent.

**Autostart policy for drivers.** A driver package carries
`<autostart/>` **only when it has been verified end to end** on some
shipped media — `pc_nic` (QEMU `-device e1000`), `fb_vesa` (BIOS media)
and `fb_boot` (UEFI media) are the three. An unverified driver
deliberately installs as STOPPED, and `vct launch <driver>` is the
user's explicit act that attempts a start (AGENTS.md §1.1: automation
by default, control as a door that is always open).
`docs/15-hardware-compatibility.md` records the per-driver verification
state.

**What a driver package still needs from the operator.** The Tier-0
`platform` device policy is a *grant* published whether or not a driver
is installed; it is not something a package install can add. The
packaged drivers whose hardware class is not yet granted (see
`run/driver_runtime.inc`) are marked as bring-up steps in
`docs/15-hardware-compatibility.md`.

### 4.6 Exclusive groups — at most one running package per group

```xml
<exclusive group="fb"/>
```

Some hardware can only be driven by one component at a time. The PCI
`VGA` class is the shipped case: the `platform` driver offers a PCI
device to exactly one policy, so a second framebuffer driver does not
add a capability, it takes the display away — silently, with nothing on
the console to say why the screen went dark.

`<exclusive group="G">` declares that claim. The rule is enforced by
`sponge_pkgd` on both paths that move a package into the running set:

* **`install`** — an `<autostart/>` package enters the running set the
  moment the install lands, so an install *is* a spawn and is gated
  before any state is mutated (no root, no installed entry, no config
  regeneration on a refusal).
* **`launch`** — the same gate on the explicit path. Unreachable
  through `vct` for the shipped `fb` group today (the install pre-flight
  already refuses), and kept as the invariant's guard for any future
  peer or any deployment that marks a package not-autostart.

The refusal is a **status, not an error**:

```
status="exclusive-conflict" op="install" pkg="fb_boot"
group="fb" peer="fb_vesa" remedy="remove the running package, then retry"
```

**Why refuse rather than auto-stop the incumbent.** A package is never
taken away by an operation the user did not name; the swap is two
explicit commands, both visible:

```
vct remove fb_vesa
vct install fb_boot
```

`vct` renders the refusal with that remedy inline, and `--json` emits
the structured `group` / `peer` fields.

**The scope is the RUNNING set, not the installed set.** Both fb
packages are staged on every product medium so the switch is one
`remove` + `install` away; only the active one holds the group.

**Proof.** The refusal mechanism is proven red-first + green by
`run/sponge-pkg-exclusive.run` on a synthetic pair (`pkg/excl_a`,
`pkg/excl_b`, group `testfb`): the first-execution refusal
(`status="exclusive-conflict"` naming `peer=excl_a group=testfb`, no
broadcast entry, no state mutation) and a byte-identical replay window
(`docs/evidence/phase16-runtime-p33.md` §2). The runtime handover of
the real `fb` pair — remove, install, and the composited screen coming
back — is `run/sponge-desktop-disk.run` with `SPONGE_FB_SWITCH=on`
(`test/fb_switch_probe`). **Honest scope note:** `run/sponge-pkg-driver.run`
stages both fb packages and carries the two fb exclusivity probes
(`fb_lock_probe` / `fb_conflict_probe`), but on current trees those
probes stop earlier on a pre-existing `report_rom` policy gap
(`stop because parent denied ROM-session`) and the scenario's final
PASS string is unconditional — **no fb or exclusivity claim is taken
from that scenario** (`docs/evidence/phase16-runtime-p33.md` §8.4,
`docs/evidence/forwarded-create-starvation.md` §7).
