# 12 - Package Format and Local Repository

> This document defines the Sponge OS package metadata format and the
> layout of the local package repository used in Phase 4 (the
> `sponge_pkgd` milestone). The format is intentionally minimal: it
> covers only what `vct install` needs in Phase 4, and it leaves remote
> repositories, signatures, and version constraints as documented future
> extensions (see §10).

---

## 1. Scope

Phase 4 reads packages from a single local directory and writes their
configuration into the live `init` component tree through the same path
that `vct component list` reads (see `docs/03-architecture.md` §4.1 and
`docs/06-vct.md` §5).

In scope for this document:

- The shape and content of one package's metadata file.
- The directory layout of the local repository on disk and inside the
  boot image.
- The deterministic algorithm for resolving dependencies.
- How a package's required sessions are routed by default, and how
  those defaults can be overridden.
- How the four `--explain` steps in `docs/06-vct.md` §5.2 are produced
  from a metadata file plus its expanded dependency graph.

Out of scope (see §10):

- Remote repositories, mirrors, and content addressing.
- Signatures, hashes, and trust.
- Version constraints (`>=`, `~=`, conflict sets).
- The RPC interface of `sponge_pkgd` (a parallel design task owns it).
- The architectural wiring between vct, sponge_pkgd, and init.

This document deliberately talks only about files, directories, and
resolution semantics, so it stays compatible with any RPC or process
shape the backend decides on.

---

## 2. Design Principles

Every format decision is grounded in the three philosophies
(`docs/02-philosophy.md`). They are not trade-offs here either.

| Principle | How the format satisfies it |
|---|---|
| **Convenience** | Sensible defaults are encoded in the metadata itself (route, quota, launcher category), so `vct install <pkg>` works with one argument. |
| **Control** | Every default the metadata declares is overridable. Users inspect and edit raw XML, change routes through `vct component route`, or skip the metadata entirely through `vct component start`. |
| **Automation** | The metadata is parseable deterministically. `sponge_pkgd` consumes the same file that `--explain` shows; there is no second source of truth. |
| **Transparency** | The metadata file is plain text, lives on disk, and is the only thing `vct install` reads about a package. There are no hidden indices or compiled caches. |

---

## 3. Format Choice

Three structured-text candidates were considered.

| Candidate | Pros | Cons |
|---|---|---|
| **INI** | Tiny, used by `docs/10-theme-format.md`. | Consumers (`sponge_pkgd`, the generated `init` config) are Genode components that already speak XML natively. INI would need a second parser. |
| **JSON** | Universally known. | Requires a JSON parser in Genode; the Genode `util/xml_node.h` helper is already linked into every component, and JSON has no first-class story for nested attributes. |
| **Genode-style XML** | Already used by every `init` config and every component runtime declaration. Parsed by `Genode::Xml_node` (`genode/repos/base/include/util/xml_node.h`). The same parser that reads `init`'s config can read package metadata. | Slightly more verbose. |

**Decision: Genode-style XML.** The metadata format must be consumable
by Genode components (`sponge_pkgd`, init config generation, and any
future tooling that lives inside the boot image). XML is the format
those components already parse, so adopting it removes one translation
layer and keeps the package metadata inspectable in the same tools
(`xml_node` introspection, the init `<config>` relay) that vct already
uses.

INI remains the right call for `docs/10-theme-format.md` because the
theme format is consumed only by `sponge-de` (a Qt component), where
human-editability outweighs parser reuse.

---

## 4. Metadata File Format

Every package carries exactly one metadata file: `metadata.xml`.

### 4.1 Grammar

The file is a single XML document rooted at `<package>`. Children are
listed below in the order they are expected to appear; parsers must not
rely on order, but authors should write in this order for readability.

| Child element | Cardinality | Purpose |
|---|---|---|
| `<name>` | 1 | Package identifier. Lower-case ASCII, `[a-z0-9_]+`. Also the directory name under `pkg/`. |
| `<version>` | 1 | Free-form string. Phase 4 does **not** interpret it for matching; it is recorded as-is for display. |
| `<description>` | 1 | One short sentence, ASCII or UTF-8. Surfaced in `vct list` and the launcher tooltip. |
| `<binary>` | 0..1 | Name of the component binary to start. Defaults to `<name>` if omitted. |
| `<quota>` | 0..1 | Resource hints for the generated component (`ram`, `caps`). Defaults: `ram="32M"`, `caps="512"`. |
| `<config>` | 0..1 | Optional inline Genode configuration. Its inner XML is emitted verbatim into the package's generated `<start>` node. This is used for application wiring such as a libc node and a `vfs` node with font or plugin tar files. |
| `<autostart>` | 0..1 | If present, the package is started automatically when it is installed and when its installed state is restored. Without it, installation registers the package but does not create a `<start>` node. |
| `<launcher>` | 0..1 | If present, the package gets a launcher entry. Carries one attribute `category`. |
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

A minimal GUI-free package. The `hello` binary only writes to `LOG`
and reads the user's terminal via `File_system`, so it declares nothing
beyond its name, version, and a single `File_system` session.

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

  <!-- Required sessions (omitted means: parent-provided services only) -->
  <sessions>
    <session name="File_system"
              default-route="vfs"
              readonly="yes"
              subpath="/app/hello"/>
  </sessions>
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
    <pkg>libncurses</pkg>
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

---

## 5. Local Repository Layout

Phase 4 keeps the repository as a plain directory tree. There is no
index file, no signing, no compression. Every package is a directory
holding its `metadata.xml` plus the payload files that are staged into
the boot image.

### 5.1 On the Developer Host

```
pkg/
├── README.md                    # contributor notes (optional, not parsed)
├── hello/
│   ├── metadata.xml             # the file format defined in §4
│   └── hello                    # the component binary (or a path into genode/contrib/ at build time)
├── nano/
│   ├── metadata.xml
│   ├── nano
│   └── share/                   # data files staged under /app/nano
├── libncurses/
│   ├── metadata.xml
│   └── libncurses.lib.so
├── nss_drv/
│   ├── metadata.xml
│   └── nss_drv
└── ...
```

Conventions:

- One directory per package, named exactly as `<name>` declares.
- `metadata.xml` is the only file the package manager parses.
- Any other file under the directory is treated as payload: it is
  staged into the boot image under `/pkg/<name>/...` and made
  available through the `vfs` service as `/app/<name>/...`.
- Payload binaries may be either source-built (referenced by a
  `target.mk` inside the package directory) or pre-built (checked in
  next to `metadata.xml` for the Phase 4 prototype).
- `pkg/README.md` is a free-form contributor note. It is not
  interpreted by `sponge_pkgd`.

### 5.2 Inside the Boot Image

`pkg/` is added to the boot image via `build_boot_image` alongside the
component binaries, the dynamic linker, and `init.xsd`. The runtime
view is identical to the on-disk view:

```
/pkg/<name>/metadata.xml
/pkg/<name>/<payload files>
```

`sponge_pkgd` reads `metadata.xml` through a `Rom_session` (consistent
with how vct already reads its own config, see `docs/06-vct.md` §3) and
passes each payload path through `vfs` for the component that needs
it.

### 5.3 Adding a New Package

The contributor steps are deliberately short. They mirror the manual
escape hatch (`vct install --manual`), so the format and the
contribution flow use the same vocabulary.

1. Create `pkg/<name>/metadata.xml` following §4.
2. Place the binary and any data files inside `pkg/<name>/`.
3. Add a `target.mk` (or a pre-built binary) so the package builds
   alongside the rest of `repos/sponge`.
4. Run `tool/build run <some-scenario>` to confirm the package still
   stages into the boot image.

No central index, no global lockfile. The directory **is** the
repository.

### 5.4 The Boot-Time Manifest (`pkg_index.xml`)

One platform constraint reshapes the "no index" rule inside the boot
image. Genode's base `Env` treats a parent-denied ROM session as
**fatal** (the component is stopped), and boot modules are not
enumerable from inside the system. `sponge_pkgd` therefore cannot
probe for a possibly-missing `pkg_<name>.xml` ROM: a wrong guess would
kill the daemon instead of producing a "package not found" error.

The run scenario therefore generates a **`pkg_index.xml`** boot module
at build time, listing the packages staged into the image. It is a
derived staging artifact (rebuilt from the `pkg/` directory contents on
every scenario run), not a per-system repository index: it carries no
version resolution, no locking, and no state that could disagree with
the directory. `sponge_pkgd` consults it before opening any metadata
ROM, so unknown packages fail gracefully with a clear error.

The §5.3 rule stands for the repository itself: `pkg/` on disk has no
index. The manifest exists only inside the boot image, and only because
of the ROM/denial model above.

### 5.5 Alpha Install Semantics on `base-sel4`

The Alpha media uses a single `image.elf` boot image. On `base-sel4`, all
package binaries and payloads are pre-staged into that image at build time,
so the on-image repository is fixed when the image is built. `vct install`
does not deliver a binary at runtime. It registers and enables a package
that is already present in the image, subject to the installed-versus-running
lifecycle in §9.2. A later package version becomes effective only after the
image is rebuilt with the new repository contents.

This is an Alpha media limitation, not a hidden fetch path: `sponge_pkgd`
never downloads package binaries at runtime.

---

## 6. Dependency Resolution

Resolution is a depth-first expansion with cycle detection, run
deterministically before any side effect is taken. The result is a
topologically ordered list of packages where each package appears
after every package it depends on.

### 6.1 Algorithm

```
resolve(root):
    plan = []
    installed = set(packages currently in the live init tree,
                    read via the same Report session vct status uses)
    visiting = empty set     # packages on the current DFS path
    done     = empty set     # packages already expanded

    visit(root)

    return plan               # in install order (deps first)

visit(name):
    if name in done:       return       # already fully expanded
    if name in installed:  return       # reuse, do not reinstall
    if name in visiting:   error("dependency cycle: <stack>")

    load pkg/<name>/metadata.xml
    add name to visiting
    for each <pkg> child in <dependencies>:
        visit(that_pkg_name)
    remove name from visiting
    add  name to done
    append name to plan
```

Properties:

- **Deterministic.** Same inputs produce the same plan, every run.
- **Cycle-safe.** A cycle in `<dependencies>` produces a clear English
  error naming the cycle path, never a stack overflow.
- **Reuses existing components.** A dependency that is already a child
  of `init` is skipped silently. This is what produces the
  `nss_drv (already present, reused)` line in
  `docs/06-vct.md` §5.2 step 2.
- **Single-pass.** Each package is parsed at most once.

### 6.2 What is Not in Scope for Phase 4

- Version constraints (`>= 1.0`, `~= 2.3`, conflict sets).
- Optional dependencies (`<pkg optional="yes">`).
- Conditional dependencies (`if arch == x86_64`).
- A way to request "do not auto-install this dependency" other than
  the global `--no-deps` flag.

These are listed again in §10 as future extensions.

### 6.3 Errors

Resolution fails with a clear message and no side effects in these
cases:

| Situation | Behavior |
|---|---|
| Requested package does not exist | `error: package '<name>' not found in repository` |
| A dependency names a non-existent package | `error: '<pkg>' requires '<missing>', which is not in the repository` |
| A cycle is detected | `error: dependency cycle: a -> b -> c -> a` (named in order of traversal) |
| A dependency graph contains a duplicate | Treated as one entry; duplicates are coalesced by `<name>` |

The user can then either edit the offending metadata file (control) or
adjust the request (e.g. drop a dependency through a finer-grained
subcommand).

---

## 7. Session Routing Defaults

A package declares the sessions it needs and the service each one
should land at by default. `sponge_pkgd` generates a routing entry per
session and writes it into the `init` configuration alongside the
component node it just created.

### 7.1 The Three Layers of Routing

Routing decisions are taken in this order, highest priority first:

| Layer | Source | Override semantics |
|---|---|---|
| 1. **User override** | `vct component route <pkg> --set <session>=<target>` | Wins always. The user has the final word. |
| 2. **Package metadata** | The `<session default-route="...">` attribute | The package author's recommendation. |
| 3. **System policy (`sponge_pkgd`)** | A built-in fallback table (e.g. `Gui -> nitpicker`, `Input -> input_drv`, `Report -> report_rom`) | Used only when metadata says nothing. |

This hierarchy is the "automation default, control escape hatch" rule
from `docs/02-philosophy.md` §3.3 applied to session routing. The
metadata is the "automation default"; the user override is the "door
that is always open".

### 7.2 Generation Rules

When `sponge_pkgd` materializes the `<session>` block from a metadata
file, the generated `init` config uses Genode's `<route>` notation,
mirroring the style used in `run/sponge-minimal.run`. These rules are
implemented by the Phase 7 runtime-config generator, not only defined by
the metadata format:

```xml
<start name="nano">
  <resource name="RAM" quantum="48M"/>
  <config> ... </config>
  <route>
    <service name="Gui">         <parent/>  </service>
    <service name="Input">       <parent/>  </service>
    <service name="File_system"> <parent/>  </service>
    <any-service> <parent/> </any-service>
  </route>
</start>
```

The conversion rules are:

1. **Session route selection.** `<session name="X" default-route="Y">`
   becomes `<service name="X"><child name="Y"/></service>` when `Y`
   names a child hosted inside the nested `pkg_runtime` init. The value
   `parent`, or the name of an outer-system service such as `nitpicker`,
   `vfs`, or `event_filter`, becomes `<service name="X"><parent/></service>`.
   It is never emitted as `<child name="nitpicker"/>`, `<child name="vfs"/>`,
   or another child route. The same parent-route notation applies to a
   user route override.
2. **Session labels and read-only materialization.** A session `label`
   attribute is emitted on the generated route service as Genode's label
   hint. For a read-only `File_system` session, `readonly="yes"` selects
   the package label suffix `<pkg>-ro`. An explicit `label` remains the
   label source when provided, while the generated `vfs` policy still
   records the package's read-only intent.
3. **VFS subpath materialization.** `subpath="..."` on a `File_system`
   session emits a matching policy in `vfs`'s generated configuration,
   with `root="<subpath>"` and `writeable="no"` for a read-only session.
   The policy label is the materialized session label, normally
   `<pkg>-ro`. This rule is generated as part of the same plan.
4. **Inline package configuration.** When metadata contains `<config>`,
   its inner XML is copied verbatim into the package's `<start>` node.
   The generator does not reinterpret or flatten the fragment, so Qt/libc
   applications can provide their required libc, vfs, font, and plugin
   configuration in one package metadata file.

For parent routes, `pkg_runtime` extends its `<parent-provides>` list so
these services can resolve through the outer system. The final list is
`ROM`, `PD`, `CPU`, `LOG`, `Timer`, `Gui`, `Input`, `Report`,
`File_system`, and `NIC`. Phase 7 adds `Gui`, `Input`, `Report`,
`File_system`, and `NIC` to the existing `ROM`/`PD`/`CPU`/`LOG`/`Timer`
list, retaining `Timer` rather than adding a duplicate.

### 7.3 Inspecting and Editing

Every routing decision is inspectable. The generated `<start>` block
becomes part of the live `init` configuration, which means:

- `vct component route <pkg>` prints the current routes for `<pkg>`.
- `vct component config <pkg>` prints the full `<start>` node.
- `vct leitzentrale` opens the Leitzentrale window for direct
  graphical editing (Phase 6; the format supports it from day one).

These three inspection paths correspond exactly to the three escape
hatch forms in `docs/02-philosophy.md` §3.3.

---

## 8. Mapping to `--explain` Output

The four-step plan in `docs/06-vct.md` §5.2 is produced directly from
a metadata file plus its expanded dependency graph. This section walks
the trace end-to-end so the format and the plan can never drift apart.

Take a `firefox` package whose `metadata.xml` declares:

```xml
<package>
  <name>firefox</name>
  <version>118.0</version>
  <description>Mozilla Firefox web browser.</description>

  <binary>firefox</binary>
  <quota ram="512M" caps="2000"/>

  <launcher category="Internet"/>

  <dependencies>
    <pkg>nss_drv</pkg>
    <pkg>libnss</pkg>
  </dependencies>

  <sessions>
    <session name="Gui"          default-route="nitpicker"/>
    <session name="Input"        default-route="input_drv"/>
    <session name="File_system"  default-route="vfs"
             readonly="yes" subpath="/app/firefox"/>
  </sessions>
</package>
```

### 8.1 Step 1: Fetch Package Metadata

Reads `pkg/firefox/metadata.xml`. Prints the three identity fields and
the `<dependencies>` list:

```
1. Fetch package metadata
   - firefox 118.0
   - Dependencies: nss_drv, libnss
```

### 8.2 Step 2: Add to Component Tree

Runs §6's resolver with `root = firefox`. `nss_drv` is already a child
of `init` (it shipped with the base image), so it is skipped via the
`installed` set and annotated as `(already present, reused)`. `libnss`
and `firefox` are new:

```
2. Add to component tree
   Under init:
     - firefox (requires Gui, Input, File_system sessions)
     - nss_drv (already present, reused)
```

The "(requires ... sessions)" parenthetical is generated from the
`<sessions>` block.

### 8.3 Step 3: Configure Session Routing

Walks every `<session>` in `firefox`'s metadata and emits the
corresponding `<service>` entry under the new `<start name="firefox">`
node:

```
3. Configure session routing
   firefox.Gui         -> nitpicker
   firefox.Input       -> input_drv
   firefox.File_system -> vfs (read-only: /app/firefox)
```

If a user override exists (from layer 1 of §7.1), it appears here
instead of the metadata default.

### 8.4 Step 4: Register Launcher Entry

Reads `<launcher category="...">` and registers an entry with
`sponge_configd` so the Sponge DE launcher shows a button in the right
group:

```
4. Register launcher entry
   Category: Internet
```

Omitting `<launcher>` makes step 4 disappear entirely from the plan.

### 8.5 Summary

| `--explain` step | Source in metadata | Resolution rule |
|---|---|---|
| 1. Fetch package metadata | `<name>`, `<version>`, `<description>`, `<dependencies>` | Direct read. |
| 2. Add to component tree | `<dependencies>` | §6 DFS expansion with reuse check. |
| 3. Configure session routing | `<sessions>` | §7 three-layer routing. |
| 4. Register launcher entry | `<launcher>` | Pass-through to `sponge_configd`. |

The same four sources power `vct install firefox`,
`vct install firefox --explain`, and `vct install firefox --manual`.
Only the level of automation differs.

---

## 9. Repository-Level Concerns

### 9.1 Where the Repository Lives

The repository path is a single configuration value held by
`sponge_pkgd`. In Phase 4 the default is `pkg/` at the repository root
(staged into the boot image). A future revision can override it
through a config ROM without changing any package metadata.

### 9.2 Installed, Running, Updates, and Removal

#### 9.2.1 Installed versus running

Phase 7 separates package registration from component execution. The
installed-vs-running rule is: **installed = registered in the package set,
with no `<start>` node; running = `<start>` node present in `pkg_runtime`.**

- **Installed** means the package is registered in the package set. An
  installed package has no generated `<start>` node in `pkg_runtime` unless
  it is marked `<autostart/>`.
- **Running** means the package has a generated `<start>` node in
  `pkg_runtime` and its component is eligible to run.
- `vct install [package]` resolves dependencies and registers the resulting
  packages. It creates `<start>` nodes only for packages that declare
  `<autostart/>`; `hello` uses this marker to preserve its old automatic
  behavior.
- `vct launch <package>` sends the new `launch` request operation to
  `sponge_pkgd`. For an installed package without a running node, the
  operation adds its `<start>` node and transitions it from installed to
  running. Launching a package that is not installed reports
  `not-installed`; launching one that is already running reports
  `already-running`.
- Alpha has no stop or kill operation. A running package can therefore be
  inspected and launched, but not stopped through the package manager.

The `installed` broadcast reports every package in the package set and adds
`running="yes"` or `running="no"` to each package entry. This lets the
launcher and other watchers distinguish a registered package from a running
component without inferring state from missing metadata.

Install semantics on `base-sel4` follow §5.5: all binaries are already in
the single image at build time. Installation changes runtime enablement, not
binary availability. The Alpha media does not enable the writable backing
store from §13. Deployments that opt into §13 retain its documented restore
behavior; that separate boot-time path does not change the installed and
running definitions above.

#### 9.2.2 Update semantics

`vct update [pkg]` re-resolves the installed root set against the repository
metadata present on the running image. With no package argument it checks
every installed root. With `pkg`, it checks that installed root and its
resolved dependency graph. The command reports version deltas without
pretending to upgrade anything, for example:

```
already current: hello 1.0
repo carries 2.0, installed 1.0, effective after next image build
```

Version strings are reported honestly and are not ordered by the Alpha
format. There is no fetching and no runtime binary delivery. The repository
is fixed at image build time, so a newer repository package becomes effective
only when the image is rebuilt and the updated package is pre-staged. An
update request does not silently mutate the installed set or running state.

`vct remove <package>` removes the package and any dependencies that are no
longer needed, then regenerates `pkg_runtime`. Removal is separate from the
Alpha launch lifecycle and does not introduce a stop operation.

### 9.3 No Hidden State

There are no lock files, no SHA-256 manifests, no per-system indices.
Everything visible about a package lives in its `metadata.xml`. If a
user wants to know what `vct install nano` would do, they can read
`pkg/nano/metadata.xml` directly, no tool required.

This rule is about the **package repository** (the static `metadata.xml`
inputs). It does not cover runtime installed-state: which packages a
*specific system* has actually installed is derived state, and its
on-disk representation is specified separately in §13.

---

## 10. Evolution Path

The following features were excluded from the original Phase 4 format.
Some are still future work, while Phase 7 has delivered the host-side depot
interop described below. They are listed here so future contributors know
what was considered, and so the metadata schema does not accidentally close
any door.

| Feature | Sketch | Why deferred |
|---|---|---|
| Remote repositories | A second config ROM names a list of remote roots fetched over a Sponge-controlled transport. | Phase 4 only needs a local directory; remote adds failure modes (offline, partial, signature) that are easier to reason about once the local path is stable. |
| Genode depot archive interop | **Delivered in Phase 7 as host-side repackaging.** `tool/pkg_import` converts a downloaded depot package into `pkg/<name>/`, writes the package metadata and payload layout, and records a `SOURCE` pin file for the depot reference and content fingerprint. | Runtime fetching is explicitly not part of Alpha. The downloaded archive must be available to the host-side import step, and `sponge_pkgd` never contacts a depot or other network source. |
| Signatures | An optional `<signature>` element with an Ed25519 signature over a canonical form of `metadata.xml` plus the payload hashes. | Requires a trust-root story that is bigger than Phase 4. |
| Version constraints | A `<dependencies>` extension with `<pkg name="foo" version=">=1.0,~=1.2"/>`. | Adds a constraint solver. Phase 4 matches by `<name>` only. |
| Content addressing | A `<hash>` element with the SHA-256 of the payload tree. | Requires a fetch cache that does not exist yet. |
| Mirrors and failover | Multiple roots with priority order. | Builds on the remote-repository support. |
| Per-system overrides | A separate `<pkg-override>` ROM that pkgd consults before applying metadata. | Adds a precedence story that competes with `vct component route`; keep one source of override for now. |
| Lockfiles | A `pkg/lock.xml` recording the resolved set for a run. | Useful for reproducible installs; not needed for single-image development. |

When any of these land, the rule is to extend the schema
additively: new child elements and new optional attributes are
allowed at any time. Removing or renaming an existing element
requires a major-version bump recorded in `<format version="2">`,
which a parser must respect (matching the
`docs/10-theme-format.md` §6 versioning rule).

---

## 11. Open Design Questions

Per `AGENTS.md` §6, items genuinely undecided at the end of this
document:

- **Repository root configurability.** Should the `pkg/` path be a
  compile-time constant, a `sponge_pkgd` config ROM, or a per-image
  `init` attribute? The format itself does not depend on the answer.
- **Whether `<quota>` is a hint or a hard cap.** `init` enforces the
  RAM and caps values regardless, so the metadata's role is
  documentary. Confirm whether `vct` should refuse to install a
  package whose quota would exceed available resources, or only warn.
- **Launcher category vocabulary.** Categories are free-form strings
  today. A closed enum (`Internet`, `Editors`, `Utilities`,
  `System`, `Games`) would make the launcher UI cleaner but locks the
  schema. Phase 4 keeps them open.
- **How `<dependencies>` interact with `init`'s own child tree.** If
  the user has manually added a `firefox` component under `init` that
  is **not** the one generated by `sponge_pkgd`, should `vct install
  firefox` reuse it, replace it, or refuse? Today the reuse check
  matches on name only, which may not be precise enough.
- **Whether `metadata.xml` is the only file a package carries, or
  whether multi-file packages are needed.** The current layout
  supports a payload directory, but the schema has no formal
  description of which payload files exist. A future
  `<payload manifest="...">` element could solve this without
  breaking the Phase 4 format.

---

## 12. References

- `docs/02-philosophy.md`: the three philosophies and the escape
  hatch rule.
- `docs/03-architecture.md` §4.1: the data-flow example for
  `vct install firefox`.
- `docs/04-components.md` §1.2: `sponge_pkgd` as a backend service.
- `docs/06-vct.md` §5.2: the canonical `--explain` output the format
  must reproduce.
- `docs/09-roadmap.md` §6: Phase 4 completion criteria.
- `docs/10-theme-format.md`: sister format spec. INI for human-
  authored themes, XML for Genode-consumed metadata.
- `genode/repos/base/include/util/xml_node.h`: the parser this
  format targets.

---

## 13. Installed-Set Store (Runtime Persistence)

> Phase 4 follow-up #2. Closes the "installs do not survive reboot"
> limitation from `docs/09-roadmap.md` §6.

§1–§12 define the *static* package inputs (`metadata.xml`, the
repository layout, the resolver). This section defines the one piece of
*runtime* state the package subsystem writes: the set of packages a
specific system has explicitly installed, so a reboot restores them
without user action.

The store holds **only the explicitly-installed root names**. The full
installed set (roots plus transitive dependencies) is never persisted:
it is re-derived on load by the same deterministic resolver
(`_sync_installed_from_roots`) that `install`/`remove` already use, so
the state→config generator is untouched and a dependency whose
metadata changed between reboots is resolved against the current
metadata, not a stale snapshot.

### 13.1 Format

A single XML file, by default `/store/installed.xml` on whatever
`File_system` session the deployment routes to `sponge_pkgd`:

```xml
<sponge-installed version="1">
  <root name="hello"/>
  <root name="nano"/>
</sponge-installed>
```

- The root element is always `sponge-installed`; the `version`
  attribute is the format version (currently `1`).
- One `<root name="…"/>` per explicitly-installed package, in the
  user's install history. Roots are stored **name-sorted** on write so
  the file is byte-stable for a given set (matching the determinism
  contract of the runtime-config generator, §7.2). Order is not
  semantically meaningful — the resolver re-sorts.
- There is no timestamp, no counter, no per-package version pin, and no
  dependency list. A future version may add version pins (§10); the
  `version` attribute is the upgrade seam.

The file is plain UTF-8 XML with no binary content, so it is
human-readable and `grep`/`diff`-able — a user can inspect exactly what
will be restored.

### 13.2 Failure Semantics

Every failure mode resolves to the **same safe state**: an empty root
set plus a `Genode::warning`, never a crash. Specifically:

| Condition | Behaviour |
|---|---|
| Store file absent (fresh system / first boot) | Start empty; logged as informational. |
| Store file empty or larger than the internal buffer | Start empty; warning. |
| Store unreadable (I/O error) | Start empty; warning. |
| Root element not `sponge-installed` | Start empty; warning. |
| `version` attribute missing or unsupported | Start empty; warning. |
| Malformed XML (truncated / torn write) | Start empty; warning. |
| A `<root>` names a package no longer in the repository | That root is dropped at resolution time (the resolver reports it missing); the rest are restored. |

There is deliberately **no checksum**: a torn write (power loss
mid-`save`) is detected as malformed XML on the next boot and the
daemon restarts from empty rather than trusting partial data. This
trades "lose the last install on a crash" for "never restore garbage".
Crash-consistent writes (write-temp-then-rename, or a checksum) are a
documented future improvement, not a Phase 4 requirement.

### 13.3 Write-Back Points

The store is rewritten **after every successful `install` and `remove`,
before the change is broadcast** to watchers (the pkg_runtime config and
the installed-set report). This ordering means a reboot between the
`install` return and a watcher reading the new state still observes the
install: the durable copy was flushed first.

A failed write (e.g. the backing file system is full or read-only) is
logged but **never blocks** the install/remove: the in-memory state and
the broadcast still reflect the requested change, only the across-reboot
durability is lost for that one mutation. The next successful mutation
re-persists the full current root set.

The store is **single-writer**: there is exactly one `sponge_pkgd` per
system and it serializes all requests, so concurrent writers are not a
concern. Multiple systems sharing one backing store is unsupported (the
format has no system identity and the last writer would win).

### 13.4 Activation and the Control Escape Hatch

Persistence is **opt-in per deployment**, following the project's three
philosophies (`docs/02-philosophy.md`):

- **Automation = default when configured.** Once `sponge_pkgd`'s
  `<config>` carries a `<vfs>` node mounting a writable `File_system`
  session, persistence is automatic: load-on-construct,
  save-on-mutation. No per-command flag, no user action per install.
- **Control = a door that is always open.** The store is a plain XML
  file on the backing `File_system`. A user can:
  - **Inspect** it directly (`cat …/installed.xml`) to see exactly what
    will be restored.
  - **Edit** it (add/remove `<root>` lines) to change what the next boot
    restores.
  - **Reset** it (delete the file, or empty it to `<sponge-installed
    version="1"/>`) to clear the installed set.
  - **Disable** persistence entirely by removing the `<vfs>` node from
    `sponge_pkgd`'s `<config>`, which reverts to the in-memory Phase 4
    behaviour (installs live only for the session).
- **Convenience.** The common case (installs should survive reboot)
  needs no user action beyond the one-time `<vfs>` wiring; the escape
  hatches are for the uncommon cases.

When `<config>` has no `<vfs>` node — the Phase 4 scenarios, or any
deployment that declines persistence — `sponge_pkgd` opens no
`File_system` session and behaves byte-identically to the in-memory
daemon. This keeps the non-persistent scenarios and any minimal
deployment working unchanged (minimum privilege, AGENTS.md §1.2).

The reference two-boot proof is `run/sponge-pkg-persist.run`
(base-linux, `lx_fs`-backed): boot 1 installs a package and writes the
store; boot 2 boots a fresh `core`/`init` against the same backing
directory, issues a `list`-only request, and asserts the package is
present — which can only be the case if it was restored from the store.
