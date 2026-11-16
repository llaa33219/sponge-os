# 12 - Package Format: Repository Layout and Concerns (§5, §9)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §5 and §9. Section numbers
> are preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

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
with no `<start>` node; running = `<start>` node present in the runtime
that hosts the package's class (`pkg_runtime_simple`,
`pkg_runtime_terminal`, `pkg_runtime_browser`, or `driver_runtime`, §4.5).**

- **Installed** means the package is registered in the package set. An
  installed package has no generated `<start>` node in its hosting runtime unless
  it is marked `<autostart/>`.
- **Running** means the package has a generated `<start>` node in
  its hosting runtime and its component is eligible to run.
- `vct install [package]` resolves dependencies and registers the resulting
  packages. It creates `<start>` nodes only for packages that declare
  `<autostart/>`; `hello` uses this marker to preserve its old automatic
  behavior.
- `vct launch <package>` sends the new `launch` request operation to
  `sponge_pkgd`. For an installed package without a running node, the
  operation adds its `<start>` node and transitions it from installed to
  running. Launching a package that is not installed reports
  `not-installed`; launching one that is already running reports
  `already-running`; launching one whose binary is not staged in the
  image reports `binary-missing`; launching one whose declared quota
  does not fit its hosting runtime's remaining budget reports
  `insufficient-resources` with the four figures involved. Both refusals
  leave the package out of the running set and never regenerate the
  config.

  `binary-missing` exists because an installed package's metadata can be
  staged while its binary was not — and reporting `ok` then is a silent
  failure: the hosting runtime's `init` catches `Rom_connection_failed`,
  `Out_of_ram` and `Out_of_caps` around child creation and only *warns*
  (`repos/os/src/lib/sandbox/library.cc`), so the launch evaporates with
  nothing on screen and nothing in the result. The pre-flight therefore
  runs before the `<start>` node is ever generated. It is gated on an
  optional `pkg_binaries` manifest ROM (enabled by `<binaries/>` in
  `sponge_pkgd`'s `<config>`) rather than a probe-style ROM open, because
  a missing boot module is a *fatal* ROM-session denial under the base
  `Env` policy — a probe would kill `sponge_pkgd` precisely in the case
  it is meant to diagnose. The manifest mirrors `pkg_index.xml` in
  shape; without it the check is inert (AGENTS.md §1.1 escape hatch).

  `insufficient-resources` is the resource twin of `binary-missing`, and
  it exists for the same reason: reporting `ok` for a package that
  cannot fit is a silent failure, because Genode's sandbox clamps the
  child's quota to whatever the parent has left
  (`repos/os/src/lib/sandbox/child.cc:483-486`) and the child then dies
  before its first instruction. The budget is read from the hosting
  runtime's own sandbox state report (`<ram avail=/>`, `<caps avail=/>`) rather
  than from a configured copy of its grant, so it is always the real
  remaining pool and needs no grant arithmetic. It is gated per sandbox
  by a config marker in `sponge_pkgd`'s `<config>` plus that sandbox's
  state-report ROM route: `<admission/>` (the `runtime_state` relay) in
  legacy mode — and, in grouped mode, `<admission/>` as the
  **simple-class alias** of `<simple_admission/>`, alongside
  `<terminal_admission/>` and `<browser_admission/>` for their classes
  (`runtime_simple_state`, `runtime_terminal_state`,
  `runtime_browser_state`), with `<driver_admission/>` unchanged for
  `driver_runtime`. Each class arms, and fails open, on its own — an
  unarmed class or a not-yet-published report admits with a log line,
  never by borrowing another class's budget: the report only appears
  ~2 s into boot when the budget is largest, so failing closed would
  refuse legitimate first launches, and failing open leaves un-wired
  deployments byte-identical to before (AGENTS.md §1.1). Admission
  applies to `install` as well, since an `<autostart/>` package gets its
  `<start>` node the moment an install lands; that pre-flight runs over
  the install plan before any mutation, so a refused install leaves
  nothing half-applied. In grouped mode the install pre-flight also
  refuses an unparsable `<quota>` outright (independent of arming) for
  every newly-installed `<autostart/>` package — app-class and driver
  alike — naming the offending package in the refusal, so an install
  cannot push a malformed quota into the emitted config. That is
  exactly the pre-flight's scope: quotas of packages arriving by other
  paths (e.g. restored from the §13 store) are not re-parsed here.
  Full receipts:
  `docs/evidence/pkg-admission-control.md`.

- Every request produces exactly one `<result>`. Requests are
  de-duplicated on the `op|pkg|seq` signature so a re-delivered ROM
  signal cannot re-run `install`/`remove` side effects, but a duplicate
  is still *answered* — the cached result is replayed on the channel the
  duplicate arrived on. This matters because the Sponge DE launcher
  sends no `seq`, so a double-click is a genuine duplicate; without the
  replay the launcher would poll until its 6 s timeout with no feedback.
- Alpha has no stop or kill operation. A running package can therefore be
  inspected and launched, but not stopped through the package manager.

The `installed` broadcast reports every package in the package set and adds
`running="yes"` or `running="no"` to each package entry. This lets the
launcher and other watchers distinguish a registered package from a running
component without inferring state from missing metadata. The flag is keyed
by package name only; which class runtime holds the `<start>` node is not
part of this contract, so launcher behavior is unchanged by the
runtime-class split.

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
longer needed, then regenerates the affected runtime config (dirty-scoped
per §4.5). Removal is separate from the
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
