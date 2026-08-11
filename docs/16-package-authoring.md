# 16 - Package Authoring Guide

> The end-to-end how-to for adding a package to `pkg/`. The format
> reference lives in [`docs/12-package-format.md`](12-package-format.md);
> this document is the contributor workflow on top of it.
>
> Phase 13 (`docs/plans/phase13-package-ecosystem.md`) criterion 2,
> verified against the repository on 2026-08-11.

---

## 1. Audience and Scope

A contributor who has never packaged for Sponge OS should be able to
add a new package to `pkg/` from this document alone. Read it through
once before touching any file; the three authoring paths in §3 share
orientation but diverge in workflow.

Out of scope:

- The XML schema for `metadata.xml`. See
  [`docs/12-package-format.md`](12-package-format.md) §4 for every
  child element and its cardinality.
- The runtime-config generator that materializes metadata into
  `pkg_runtime`'s `<start>` nodes. See `docs/12` §7 and
  [`docs/06-vct.md`](06-vct.md) §5.
- The dependency resolver itself. See `docs/12` §6.

---

## 2. Orientation

### 2.1 The Repository

`pkg/` is the repository. One directory per package, named exactly
as `<name>` declares (`docs/12` §5.1):

```
pkg/
├── README.md
├── hello/                  # metadata-only example, no payload
│   └── metadata.xml
├── textedit/               # depot-imported (Phase 7 todo 14)
│   ├── metadata.xml
│   ├── SOURCE              # depot pin + sha256 (provenance)
│   ├── PAYLIST             # staged vs missing payload inventory
│   └── payload/            # binary + content ROMs copied from the depot
│       ├── textedit
│       └── textedit.config
├── calculator/             # source-built in-tree (Phase 13 W3)
│   └── metadata.xml
├── pdf_view/               # source-built in-tree (Phase 13 W4)
│   ├── metadata.xml
│   └── payload/
│       └── sample.pdf      # booted as the `<rom name="sample.pdf"/>` vfs mount
└── terminal/               # source-built sub-init (Phase 7 todo 13, Phase 13 W2)
    └── metadata.xml
```

`sponge_pkgd` parses exactly one file per package: `metadata.xml`.
Everything else under `pkg/<name>/` is payload staged into the boot
image (`docs/12` §5.1).

### 2.2 The Two Files `metadata.xml` Depends On at Boot

The metadata itself, plus a build-time boot manifest. Inside the
boot image:

- `pkg_<name>.xml` is the staged copy of one package's metadata,
  served as a ROM with that label. `sponge_pkgd` opens it through a
  `ROM` session (the Genode `parent-denied ROM = fatal` rule applies).
- `pkg_index.xml` is the boot-time manifest of every staged package.
  `sponge_pkgd` consults it before opening any metadata ROM, so an
  unknown package name fails gracefully with a clear error instead
  of killing the daemon.

Both are derived staging artifacts produced by the run scenario at
build time. `pkg/` on disk carries no index (`docs/12` §5.4).

### 2.3 The Tcl Staging Idiom

Every package scenario stages its own metadata + the `pkg_index.xml`
the same way. Quoted verbatim from `run/sponge-textedit.run:171-186`
(unchanged in `sponge-terminal.run`, `sponge-pdf-view.run`,
`sponge-files.run`, `sponge-calculator.run`, `sponge-pkg-gui.run`):

```tcl
set staged_pkgs { textedit }
foreach pkg $staged_pkgs {
    set src "${genode_dir}/../pkg/$pkg/metadata.xml"
    if {![file exists $src]} {
        error "package metadata not found: $src"
    }
    exec cp -L $src "bin/pkg_$pkg.xml"
}

set index_fh [open "bin/pkg_index.xml" w]
puts $index_fh "<packages>"
foreach pkg $staged_pkgs {
    puts $index_fh "  <pkg name=\"$pkg\"/>"
}
puts $index_fh "</packages>"
close $index_fh
```

The same scenario also extends `build_boot_image [build_artifacts]`
with any staged extras (Qt6 shared libs, content ROMs, payload
files). The defensive explicit-listing pattern is mandatory on
base-sel4's `boot_dir/sel4` plugin: every module must be named. See
`run/sponge-textedit.run:247-260` for the full list, and
`run/sponge-pdf-view.run:229-237` for the leaner mupdf variant.

---

## 3. Three Authoring Paths

The contributor-facing question is "what am I packaging?" Each shape
maps to one of three paths. The `metadata.xml` schema is identical
for all three; what differs is where the binary comes from, where the
payload lives, and how the run scenario consumes it.

| Path | Source of the binary | Worked example | Section |
|---|---|---|---|
| (a) Depot pkg import | A downloaded `cproc/pkg/...` archive | `pkg/textedit`, `pkg/falkon` | §3.1 |
| (b) Source-built in-tree component | An in-tree Genode `app/...` or `lib/...` target | `pkg/calculator`, `pkg/pdf_view` | §3.2 |
| (c) Noux CLI tools inside an existing terminal | Tar mounts in `pkg/terminal`'s vfs | `pkg/terminal` toolset tars | §3.3 |

### 3.1 Path (a): Depot Pkg Import

#### 3.1.1 What it is and when to use it

A Genode depot `pkg/<recipe>/<version>` archive is a recipe plus a
`runtime` file declaring binary name, ram/caps, required sessions,
and content ROMs. The actual binary lives in a separate
`bin/<arch>/<recipe>/<version>` archive that the importer fetches on
demand. `tool/pkg_import` repackages both into `pkg/<name>/` for the
Sponge boot image. The importer never contacts the depot at Sponge
runtime (Metis amendment A1, all binaries are baked in at build
time). See `docs/08-development.md` §12 for the full contract.

Use this path when:

- A cproc-depot-published Qt6 (or otherwise Genode-native) binary
  exists for the application you want to ship.
- The application has no cross-component vfs composition. It is a
  self-contained process whose `runtime` declares everything it
  needs.

Do **not** use this path when:

- No depot-published binary exists for the application. Switch to
  §3.2 (source-built in-tree). This is the case for the Phase-13 W3
  calculator: only the `src/` archive is published, and the binary
  is built from sources into an in-tree
  `app/qt6/examples/calculatorform` target.

#### 3.1.2 One-command flow

```bash
# Step 1 (once per machine): set up an in-repo GNUPGHOME holding the
# cproc pubkey. See docs/08-development.md §12.1 for the rationale
# (modern GnuPG ignores --keyring <file>; importing into a fresh
# keybox under var/scratch/gnupg sidesteps that).
mkdir -p var/scratch/gnupg && chmod 700 var/scratch/gnupg
export GNUPGHOME="$PWD/var/scratch/gnupg"
gpg --no-tty --import genode/repos/gems/sculpt/depot/cproc/pubkey

# Step 2: fetch the pkg + its transitive deps.
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# Step 3: repackage into pkg/<name>/. The importer downloads the
# matching bin archive on demand.
./tool/pkg_import cproc/pkg/qt6_textedit/2025-10-27 \
    --bin-version 2025-10-12
```

The wrapper script ([`tool/pkg_import`](../tool/pkg_import) →
[`tool/pkg_import.mojo`](../tool/pkg_import.mojo)) handles seven
steps: verify the pkg is downloaded, read the `runtime`, hash the
original `pkg.tar.xz`, resolve the matching `bin/<arch>/...` archive,
download it on demand, stage every `<content>` ROM into
`payload/`, and atomically rename the staged tree to `pkg/<name>/`.
A bogus depot reference never leaves a partial directory behind
(failure-channel discipline).

The hand-written equivalent for every step is in
`docs/08-development.md` §12.2. Use it as the control escape hatch
when the importer misclassifies a depot archive.

#### 3.1.3 Output files

After a successful import, `pkg/<name>/` carries four files:

| File | Purpose | Tool that writes it |
|---|---|---|
| `metadata.xml` | The Sponge package metadata (Phase 13 fixed session casing) | `tool/pkg_import` (hand-editable) |
| `SOURCE` | Depot pin + sha256 of `pkg.tar.xz` and `bin.tar.xz` | `tool/pkg_import` |
| `PAYLIST` | Inventory of staged vs missing payload entries | `tool/pkg_import` |
| `payload/` | Files copied from the local depot tree | `tool/pkg_import` |

The `SOURCE` file is the reproducibility anchor (Phase 7 reviewer's
contract): `sha256sum genode/public/cproc/pkg/<recipe>/<version>.tar.xz`
must match `depot_pkg_archive_sha256` in the record.

#### 3.1.4 Current Limitations and the Hand-Edits They Require

`tool/pkg_import` is correct for the common Qt6 case but does not
cover every shape. The Phase-13 baseline (2026-08-11) carries the
following gaps, each with the worked hand-edit that resolves it:

- **Hardcoded libc+vfs config fragment.** The emitted `<config>` is
  modeled on `run/sponge-launcher.run:126-136` and assumes a Qt6
  QPA plugin need plus `<tar name="qt6_dejavusans.tar"/>` and
  `<tar name="qt6_libqgenode.tar"/>`. Non-Qt6 applications need the
  config re-authored by hand.

- **All sessions default to `parent`.** Every `<session
  default-route="parent"/>` is emitted without `readonly`,
  `subpath`, or `label`. The route materializes as `<service
  name="X"><parent/></service>` through `pkg_runtime`'s extended
  `parent-provides` (`docs/12` §7.2). A package that needs `vfs`
  with `readonly="yes" subpath="/app/foo"` (the `nano` shape in
  `docs/12` §4.3) needs the session block re-written.

- **No session-name casing for unrecognized tags.** Tags not in the
  `session_name()` table (see `tool/pkg_import.mojo:378-412`) fall
  through to `upper_first(...)`, which is wrong for any tag whose
  canonical service name does not start with an upper-case letter.
  No current package hits that path, but if it does, hand-edit the
  `<session name=...>` to the exact Genode service name.

Any imported package whose capabilities exceed the depot default
(for example Falkon's 200000 caps; see `pkg/falkon/metadata.xml:44`)
needs the quota hand-raised. The importer's GUI-safe caps floor of
1000 (`tool/pkg_import.mojo:546-548`) only covers the
caps-exhaustion bottom from `docs/09-roadmap.md` §11.1; heavier
applications need more, and only the run-scenario's first paint can
prove the right number.

The Phase-13 fix (D13.5) corrected the casing map for `nic`, `rtc`,
and `gpu`: the emitter now produces `Nic`, `Rtc`, and `Gui` (where
`<gpu/>` from a depot runtime is served by Genode's `Gui` service.
GPU sessions are a Gui subinterface). Pre-Phase-13 imports emitted
`NIC`/`RTC`/`GPU` and required the same hand-edit the falkon
metadata records:

> `Sessions use the EXACT Genode service names (Nic, Rtc, not
> NIC/RTC; pkg_import's default mapping uppercases incorrectly).`
> `pkg/falkon/metadata.xml:90-95`

Use that header comment as the reference for the hand-edits another
imported package might still need.

#### 3.1.5 Worked example: `pkg/textedit`

`pkg/textedit` is the cleanest imported package, small footprint,
single content ROM, no `readonly`/`subpath` need. Worked walk-through:

1. `gpg --import` the cproc pubkey once (one machine).
2. `./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27`
   fetches the pkg, src, raw, and api archives.
3. `./tool/pkg_import cproc/pkg/qt6_textedit/2025-10-27 --bin-version 2025-10-12`
   writes `pkg/textedit/{metadata.xml,SOURCE,PAYLIST,payload/}`.
4. The four files land as described in §3.1.3. The metadata's
   `<config>` is the standard Qt6 boilerplate; `caps=1000` meets the
   Qt6-on-seL4 floor; sessions are all `default-route="parent"`.
5. `run/sponge-textedit.run` builds `lib/ld init timer
   server/report_rom server/nitpicker sponge_pkgd test/textedit_probe
   qt6/base ...`, copies `pkg/textedit/payload/textedit` and
   `textedit.config` into `bin/`, generates `bin/pkg_textedit.xml`
   and `bin/pkg_index.xml` per §2.3, then `build_boot_image [build_artifacts]`
   plus the full Qt6 closure plus `pkg_textedit.xml pkg_index.xml`.
6. The probe (Phase 7 todo 14) drives install + launch, pixel-
   verifies the rendered window via `Capture`, and exercises the
   pkgd error paths (`not-installed`, `already-running`).

`pkg/falkon` follows the same path plus the deck of hand-edits called
out in §3.1.4 (long rationale header, custom `<config>` for
WebEngine, `Nic`/`Rtc`/`Gui` casing, 200000 caps). Its
`metadata.xml` header comment is the worked example for an importer
output that needs substantial post-import surgery.

### 3.2 Path (b): Source-Built In-Tree Component

#### 3.2.1 What it is and when to use it

A Genode component the Sponge build tree compiles from sources
against the vendored Genode 26.05 toolchain. The binary name matches
some `app/<thing>` or `lib/<thing>` target that already exists in
`genode/repos/` (or in `repos/sponge/` for Sponge-owned components).
No `pkg/<name>/payload/` is needed: the run script's `build {}` list
builds the binary, and `[build_artifacts]` stages it.

Use this path when:

- The component is already a Genode target (no need to add a new
  build target).
- The application needs no depot-published binary (Phase 13 W3
  calculator; Phase 13 W4 PDF viewer).
- You want the binary's payload to be reproducible from in-tree
  sources rather than from a vendored upstream archive.

#### 3.2.2 Steps to add the package

1. **Pick the build target.** Confirm the upstream or Sponge-owned
   target exists and produces the binary you want:
   `genode/repos/libports/src/app/qt6/examples/calculatorform/target.mk`
   for the calculator;
   `genode/repos/libports/src/app/pdf_view/target.mk` for the PDF
   viewer; `repos/sponge/src/pkg_hello/target.mk` for `pkg_hello`.
2. **Author `pkg/<name>/metadata.xml` by hand.** See the schema in
   `docs/12-package-format.md` §4. Three sub-steps:
   - Set `<name>`, `<version>`, `<description>`, `<binary>` (defaults
     to `<name>` when omitted), `<quota>`, optional `<launcher>`, and
     `<sessions>`.
   - If the binary needs libc+vfs wiring, write the `<config>` by
     hand. The Qt6 boilerplate in `pkg/calculator/metadata.xml:77-88`
     is the canonical Qt6 example (libc `/dev/log`+`/pipe`+`/dev/rtc`,
     DejaVuSans + libqgenode tars). The mupdf example in
     `pkg/pdf_view/metadata.xml:70-76` shows the leaner variant
     (libc `/dev/log` only, no font tars, no pipe).
   - Add extensive header comments explaining WHY each section
     exists. Comments are NOT parsed and never balloon the
     serialization budget (§5.2 below). The 12-line header in
     `pkg/calculator/metadata.xml:1-68` and the 56-line one in
     `pkg/pdf_view/metadata.xml:1-57` are the worked model.
3. **Stage any payload files into `pkg/<name>/payload/`.** Copy the
   file from its source (a contrib port, a hand-generated fixture,
   a downloaded corpus) into the package's payload directory. Do
   NOT add a build target for `pkg/<name>/` itself. Payload is
   data, not a Genode component.
4. **Wire the package into a run scenario.** Three additions to a
   new or existing `run/<scenario>.run`:
   - Add the component target to the `build {}` list.
   - Add `set staged_pkgs { <name> ... }` plus the §2.3 Tcl block.
   - Copy each payload file from `pkg/<name>/payload/` into `bin/`
     with `exec cp -fL $src $dst`, and append the boot-module names
     to the explicit `foreach m { ... } { lappend boot_modules $m }`
     list right before `build_boot_image $boot_modules`.

   The calc and PDF-view staging idioms are the worked references:
   `run/sponge-pdf-view.run:213-218` (`bin/sample.pdf`) and
   `run/sponge-calculator.run` for the Qt6 closure (mirrors
   `sponge-textedit.run`).

#### 3.2.3 The `<rom name="..."/>` matching rule

If the metadata `<vfs>` mounts a `<rom name="X"/>` (the format
`<rom name="X"/>` resolves to a boot module of the same name), the
run scenario MUST stage a boot module of exactly that name. The
filename is the lookup key.

`pkg/pdf_view` is the worked lesson: the metadata mounts
`<rom name="sample.pdf"/>`, and `run/sponge-pdf-view.run:213-218`
copies `pkg/pdf_view/payload/sample.pdf` into `bin/sample.pdf`. The
component's `scandir("/", ...)` then finds it (its `*.pdf` filter
matches the extension). Renaming the boot module silently breaks
first paint; `pdf_view_probe` then observes "failed to find a PDF to
open" (`main.cc:251`) and the run fails loud by bounded
`run_genode_until` timeout.

#### 3.2.4 Worked example: `pkg/calculator`

The calculator (Phase 13 W3) is the simplest source-built Qt6 case:

- Build target: `app/qt6/examples/calculatorform` (in-tree, no
  change needed; the upstream `qt6_calculatorform.run` proves the
  build).
- `<binary>` matches the upstream target's output name
  (`<binary>calculatorform</binary>`), so the runtime-config
  generator emits `<start name="calculatorform">` and `pkg_runtime`
  looks up `calculatorform` as a ROM.
- `<quota ram="64M" caps="1000"/>` is the Qt6/Mesa softpipe GUI
  floor from `docs/09-roadmap.md` §11.1.
- `<config>` is the standard Qt6 libc+vfs+tar boilerplate (no
  external `textedit.config`-style content ROM; calculatorform does
  not read a config file).
- `<sessions>` declares `Gui`, `Report`, `ROM`, `Timer` (all
  `default-route="parent"`, minimum privilege).
- No `payload/` directory: the binary is built, not staged.

`run/sponge-calculator.run` builds the calc target plus the same Qt6
closure `run/sponge-textedit.run` uses, then runs `calculator_probe`
which pixel-verifies the rendered widget via `Capture` (the
`calculator-probe: PASS` marker) and exercises the pkgd error
paths.

#### 3.2.5 Worked example: `pkg/pdf_view`

The PDF viewer (Phase 13 W4) is the simplest source-built non-Qt6
case:

- Build target: `app/pdf_view` (in-tree; the upstream
  `run/mupdf.run` is the ground truth for the closure).
- `<binary>pdf_view</binary>` matches the upstream binary name, so
  the runtime-config generator emits a plain `<start
  name="pdf_view">`.
- `<quota ram="1G" caps="256"/>` is the upstream `run/mupdf.run:81`
  verbatim. Mupdf is memory-hungry on first paint
  (`docs/13-installation.md` §6 register entry).
- `<config>` is the leaner variant: libc `stdout/stderr="/dev/log"`
  (no `pipe`/`rtc`), and a vfs with only `<dir name="dev"><log/></dir>`
  and `<rom name="sample.pdf"/>`.
- One declared session: `Gui default-route="parent"`. The upstream
  `Gui::Connection` bundles both `Gui` and `Input`, so no separate
  Input declaration is needed. The run scenario routes Input
  explicitly to nitpicker for clarity, but `<any-service><parent/>
  <any-child/></any-service>` would also reach it.
- `payload/sample.pdf` is a 632-byte one-page PDF 1.4 generated by
  hand (validated by `pypdf`'s `extract_text` returning "Hello Sponge
  PDF / Phase 13 W4 sample.").

### 3.3 Path (c): Noux CLI Tools Inside a Terminal

#### 3.3.1 What it is and when to use it

Noux binaries are only runnable inside a noux runtime. Phase 13
(D13.1) records that `sponge_pkgd` has no cross-package vfs
composition, so a standalone `pkg/grep` would install-and-do-nothing,
dishonest packaging that violates the "convenience must be proven in
code" rule from `AGENTS.md` §5.1. CLI tools therefore extend
`pkg/terminal` as vfs tar mounts.

Use this path when:

- The tool is a noux-pkg build target that the upstream Genode 26.05
  tree already provides (`coreutils-minimal`, `grep`, `sed`, `tar`,
  `less`, `findutils`, `diffutils`, `which`, `make`, `tclsh`).
- The tool's natural caller is a shell that already exists in a
  Sponge terminal.

Do **not** use this path when:

- The tool needs network, a window, or any capability a noux binary
  can't get. Standalone GUI/CLI packages must use §3.1 or §3.2.

#### 3.3.2 Why a tar mount, not a dependency

D13.1 codifies this point. `sponge_pkgd` resolves `<pkg>`
dependencies into separate `<start>` nodes (or into a nested
`pkg_runtime`'s siblings), but it has no concept of one package's
vfs being composed into a dependent's vfs. A `pkg/grep` would
install into the host's installed set; nothing would mount its tar
into `pkg/terminal`'s vfs; bash would still find no `grep` on its
`PATH`.

The terminal's vfs is already a single sub-init child (`pkg/terminal`
metadata, the `vfs` server `<start>` at line 140-180). Adding more
`<tar name="..."/>` entries there costs one configuration line per
tool and gives bash the binary on `PATH` automatically. The
"convenience" half of the project's three philosophies is met by
exactly this. One Tarball That Already Works Instead Of A Chain Of
Installs That Don't.

#### 3.3.3 Steps to add a tool to `pkg/terminal`

Take "add `make`" as a worked example; the same recipe applies to
`make`, `tclsh`, or any future noux-pkg target.

1. **Confirm the noux-pkg target exists upstream.**
   `genode/repos/ports/src/noux-pkg/<tool>/` should already contain
   the recipe. The Phase 13 baseline has
   `coreutils-minimal`, `grep`, `sed`, `tar`, `less`, `findutils`,
   `diffutils`, `which`; add a similar line for `make` once the
   `make` port is prepared.
2. **Add the port to `port_list()` in `tool/build.mojo`** so
   `./tool/build ports` prepares the source archive into
   `genode/contrib/`. Run `./tool/build ports` and verify the new
   archive appears under `genode/contrib/<port>-<hash>/`.
3. **Append the noux-pkg/<tool> target to the build list of ALL THREE
   terminal scenarios.** Each stage ships a `bin/<tool>.tar` (the
   noux-pkg build product), so every run that boots the terminal
   needs the new build target:
   - `run/sponge-terminal.run` (the fast regression)
   - `run/sponge-terminal-qmp.run` (the keyboard-input scenario;
     exercises `make` via real QMP keystrokes)
   - `run/sponge-usb-kbd-via-qmp.run` (the Phase-12 USB-keyboard
     scenario, same terminal stack)

   Each `build {}` block currently lists the eight Phase-13 CLI tools
   inline. Add the new one to all three lists in the same order.
4. **Add `<tar name="<tool>.tar"/>` to `pkg/terminal/metadata.xml`'s
   vfs block.** The current block is at `pkg/terminal/metadata.xml`
   lines 152-159, eight `<tar name="...">` lines inside the
   `vfs`'s `<vfs>` config. Add a ninth for `<tool>`.
5. **Add a probe assertion** (optional but recommended). The
   `terminal_probe` (`repos/sponge/src/test/terminal_probe/main.cc`)
   can issue `Press_char` events for `/bin/<tool> --flag` and assert
   the rendered glyph count increases. Follow the existing pattern
   (`bash <command>` echoes, observed as glyph-count growth).

The seven current toolset tars are the worked example:

```xml
<tar name="coreutils-minimal.tar"/>
<tar name="grep.tar"/>
<tar name="sed.tar"/>
<tar name="tar.tar"/>
<tar name="less.tar"/>
<tar name="findutils.tar"/>
<tar name="diffutils.tar"/>
<tar name="which.tar"/>
```

---

## 4. The Boot-Verification Contract

Every package, regardless of which path in §3 produced it, must boot.
AGENTS.md §4.2 requires "build verification" and "run scenarios" for
every logical change. For packages that means a `run/<scenario>.run`
that:

1. Builds `lib/ld` (the dynamic linker; missing it from any boot
   image produces silent `ld.lib.so` ROM-denied failures at child
   load time) alongside `core init <component>`.
2. Stages the §2.3 `pkg_<name>.xml` plus `pkg_index.xml`.
3. Passes `[build_artifacts]` to `build_boot_image` and adds every
   staged extra (payload, lib.sos, content ROMs, font/plugin tars)
   by name to the explicit module list.
4. Asserts a behavioral marker, `<name>-probe: PASS`, bounded by
   `run_genode_until`. The run tool fails loud on timeout (fail-loud;
   no silent hangs).

### 4.1 The probe contract

Probes are plain Genode components (`repos/sponge/src/test/<name>_probe/`).
They drive `sponge_pkgd` over the `request`/`result`/`installed`
ROM/Report relay and assert whatever makes the package behaviorally
real. The minimum pattern, from `repos/sponge/src/test/calculator_probe/main.cc`:

1. **Install via `sponge_pkgd`.** Submit
   `<request op="install" pkg="<name>"/>`; poll the `result` ROM for
   a `<result op pkg status="..."/>` reply. Fail loud on missing
   reply or non-`ok` status (`calculator_probe/main.cc:381-388`).
2. **Verify the installed broadcast.** Read the `installed` ROM,
   find `<package name="<name>" ... running="..."/>`, assert the
   expected `running` value. For calculator (no `<autostart/>`) this
   is `"no"` after install and `"yes"` after launch
   (`calculator_probe/main.cc:392-436`).
3. **Launch via `sponge_pkgd`.** Submit
   `<request op="launch" pkg="<name>"/>`; poll; assert `status="ok"`.
4. **Assert the actual behavior.** The calculator polls
   nitpicker's `Capture` for a non-background fraction ≥ 0.30 AND
   ≥ 8 distinct 4-bit-per-channel color buckets
   (`calculator_probe/main.cc:131-168`, `341-365`); the PDF viewer
   samples for R+G+B > 200 white with non-bg fraction ≥ 0.50; the
   text editor, terminal, and Falkon follow the same pattern
   (Capture-side pixel verification distinct from the nitpicker
   background). Bare exit 0 must NOT pass; that is the
   `misleading_success_output` adversarial class.
5. **Exercise the error paths.** Submit
   `<request op="launch" pkg="nosuchpkg-13"/>` and assert
   `status="not-installed"`. Submit a second `launch calculator`
   and assert `status="already-running"`. Both must be returned by
   `sponge_pkgd` as clean statuses, never as crashes
   (`calculator_probe/main.cc:457-479`).

The probe logs `<name>-probe: PASS` on success; the run scenario
gates its `run_genode_until` on that regex; any timeout or FAIL line
becomes a non-zero exit. Missing-binary failure channels are
verified separately (e.g.
`run/sponge-textedit-fail.run` for the textedit case).

Probes in observe mode can also gate on
`run_genode_until` emitting QMP-TARGET markers consumed by the
`run/qmp.inc` helper. The pattern is documented in
[`docs/08-development.md`](08-development.md) §4.4.

### 4.2 What "PASS" actually proves

The marker proves the package's behavioral contract end-to-end:
metadata parsed, dependency graph resolved (if any), `<start>` node
materialized in `pkg_runtime`, binary loaded, runtime reached a
visible/computable state, and pkgd's error paths return clear
statuses without crashing. It does NOT prove that the application
is correct or complete; that is the upstream component's own test
surface. The marker proves the *package wiring* is right.

---

## 5. Pitfalls

Hard-won lessons from Phase 7 through Phase 13. Each one has
bitten at least one package shipped in this repository.

### 5.1 `<env>` Attribute Format

`genode/repos/libports/include/libc/args.h:64-68` accepts the
**content form** (`<env name="K">V</env>`) and the **legacy form**
(`<env key="K" value="V"/>`). The two branches are
disambiguated by `has_attribute("key")`:

```cpp
auto with_env = [] (Node const &node, auto const &fn)
{
    if (node.has_type("env") && node.has_attribute("name") &&
        !node.has_attribute("key"))
        fn(node);
};

auto with_legacy_env = [] (Node const &node, auto const &fn)
{
    if (node.has_type("env") && node.has_attribute("key") &&
        node.has_attribute("value"))
        fn(node);
};
```

The plausible-looking hybrid `<env name="K" value="V"/>` matches
neither branch:

- `has_attribute("name")` is true, `has_attribute("key")` is true,
  so `with_env`'s guard fails (the `!has_attribute("key")` clause is
  false).
- `has_attribute("key")` is true, `has_attribute("value")` is true,
  so `with_legacy_env`'s guard could pass, but the legacy reader
  looks for `key` and `value` attributes; here they're attributes
  on a `name`-bearing element, which the reader skips because its
  own presence check is `<has_type("env") && has_attribute("key")
  && has_attribute("value")>`, but only when the value is read
  from `key`/`value` (not `name`/`value`).

Either way, the `K=V` pair is not consumed. **Symptom:** every
variable reads back empty (`PATH=`); PATH-search commands fail with
"command not found" while absolute paths still work.

**Fix.** Use one of the two accepted forms and be consistent. The
terminal package uses the content form (`<env name="TERM">screen</env>`
and friends, `pkg/terminal/metadata.xml:210-213`), and carried this
latent bug from Phase 7 to Phase 13, the symptom was dormant
because bash was always launched with its binary's full path, which
doesn't touch `PATH`.

### 5.2 Config Serialization Budget

`sponge_pkgd` serializes each package's metadata `<config>` into a
3072-byte buffer
(`repos/sponge/src/sponge_pkgd/main.cc:641`):

```cpp
char cfg_buf[3072] { };
Genode::Xml_generator::Result const cfg_res =
    Genode::Xml_generator::generate(
        Genode::Byte_range_ptr(cfg_buf, sizeof(cfg_buf)),
        Genode::Xml_generator::Tag_name("config"),
        [&](Genode::Xml_generator &g) {
            (void)g.append_node_content(child, { .value = 16 });
        });
if (cfg_res.ok())
    out.config_xml = Genode::String<3072>(cfg_buf);
else
    Genode::warning("pkg: <config> for ", name,
                    " exceeded serialization buffer");
```

A package whose serialized `<config>` exceeds 3072 bytes trips the
"exceeded serialization buffer" warning and **silently drops the
config**. The generated `<start>` node then runs with no libc/vfs
wiring, and the package fails to open libc-required files at startup
with no upstream-visible cause.

**The terminal package currently serializes to ~2956 bytes.** Its
verbose header comments do NOT count (they're metadata-side
`<!-- ... -->` markup that `append_node_content` strips), but the
inline `<vfs>` with nine `<tar>` entries plus four `<env>` plus
libc plus the nested sub-init's `<start>` graph add up fast. **Keep
metadata configs lean; put long rationale in XML comments.** Any
future tool added to the terminal toolset pushes that budget closer
to the ceiling; if you reach it, the silent-drop behavior is the
failure mode you'll see, and trimming the `<config>` is the fix
(comments remain free).

### 5.3 Synthetic Input Injection

Probes that drive nitpicker's `Event` service to type strings into
a focused terminal must submit `Press_char` AND `Release` per
character. Nitpicker drops a repeated `Press_char` with the same
keycode unless a `Release` intervenes (logged as `suspicious double
press of KEY_UNKNOWN`).

The terminal probe's keystroke pattern
(`repos/sponge/src/test/terminal_probe/main.cc:499-501` and the
`/bin/ls` block at 596-609) submits `Press_char` + `Release`
together per character. Drop the `Release` and the second `Press_char
KEY_A` disappears; the bash prompt then receives only the first 'a',
and the probe's glyph-count assertion fails by one.

QMP-driven recipes via `run/qmp.inc` (`qmp_type`, `qmp_send_key`)
get paired key-down/key-up for free. Synthetic `Event`-session
probes must emit the pair explicitly.

### 5.4 Quota Budgeting

A package's `<quota ram>` must cover the SUM of its sub-init
children plus init overhead. Phase 7 packaged `terminal` at 160M
and squeezed bash to "configured RAM exceeds available RAM, proceed
with ..."; the < 224M ceiling was the practical floor. Phase 13 W2
raised it to 224M to fit:

| Child | Quota |
|---|---|
| `terminal` (gems server) | 48M |
| `vfs` (with nine tar mounts and `<terminal/>` plugin) | 48M |
| `vfs_rom` (cached_fs_rom, serves `/bin/*`) | 32M |
| `/bin/bash` (noux-pkg) | 64M |
| **Sum of children** | **192M** |
| Package quota (`pkg/terminal/metadata.xml:52`) | 224M |

The 32M headroom covers init's bookkeeping. Below that, init aborts
the child at start time with the "exceeds available RAM" warning,
and the probe's terminal window never renders. If you add another
sub-init child to the terminal (an `xinit` configd bridge, a
sound), raise `pkg/terminal/metadata.xml`'s `<quota ram>` by at
least that child's quota + 8M for init overhead, then re-run
`run/sponge-terminal.run` and verify the boot is still clean.

Caps budgets follow the same rule (Phase 9 falkon lesson,
`docs/09-roadmap.md` §11.1). The repository's falkon metadata
records the empirical floor for a Qt6-WebEngine class application;
no other app in the Phase-13 baseline approaches that ceiling.

---

## 6. Where Things Live: Quick Map

| Path | What lives here | Notes |
|---|---|---|
| `pkg/<name>/metadata.xml` | The parsed file (one per package) | `sponge_pkgd`'s only input |
| `pkg/<name>/payload/` | Staged files (depot-imported or hand-authored) | Copied into `bin/` by the run script |
| `pkg/<name>/SOURCE` | Depot pin + sha256 (imported packages only) | Reproducibility anchor |
| `pkg/<name>/PAYLIST` | Staged vs missing payload inventory (imported only) | The probe reads nothing from here |
| `run/<scenario>.run` | The Tcl script that builds, stages, and verifies | One scenario per package minimum |
| `repos/sponge/run/<scenario>.run` | Committed symlink to `run/<scenario>.run` | A new run scenario MUST be symlinked here, or the build reports `Error: No run script for <name>` from `genode/tool/builddir/build.mk:445` |
| `repos/sponge/src/test/<name>_probe/main.cc` | The headless Genode probe | Logs `<name>-probe: PASS` consumed by `run_genode_until` |
| `repos/sponge/src/test/<name>_probe/target.mk` | The build target for the probe | Picked up by `test/<name>_probe` in the run's `build {}` list |
| `genode/repos/<repo>/src/app/<thing>/target.mk` | The upstream component's build target (source-built paths) | `app/<thing>` in the run's `build {}` list |
| `genode/repos/ports/src/noux-pkg/<tool>/` | The noux-pkg build target (`pkg/terminal` toolset) | `noux-pkg/<tool>` in the run's `build {}` list |
| `genode/contrib/<port>-<hash>/` | Prepared port sources (built once via `./tool/build ports`) | Git-ignored; rebuilt from upstream on demand |
| `tool/pkg_import.mojo` | The depot-pkg importer | Atomic rename on success; tmpdir rollback on any error |
| `tool/build.mojo` `port_list()` | Where new port names go (`make` for §3.3 example) | Re-run `./tool/build ports` after editing |

The `repos/sponge/run/` symlink rule is the most-forgotten step for
contributors who add a new scenario. Genode's build repo discovery
walks `repos/sponge/run/` for run scripts; without a symlink, the
file under `run/` is invisible to the build system. `commit
symlink` not `cp`.

---

## 7. Quick Checklist for a New Package

Pick the path from §3, then follow this checklist. Every box ticked
before opening a PR.

### 7.1 Depots (Path a)

- [ ] Import runs to completion with `./tool/pkg_import <ref> --bin-version <ver>`.
- [ ] `pkg/<name>/{metadata.xml,SOURCE,PAYLIST,payload/}` exist after import.
- [ ] Required hand-edits applied (read §3.1.4 for what hand-edits
      `tool/pkg_import` may still need).
- [ ] Scenario `<scenario>.run` written: `build {}` lists all
      library closure + the new build targets; `pkg_<name>.xml` and
      `pkg_index.xml` generated per §2.3; staged payload named
      explicitly in the `foreach m { ... }` block; `build_boot_image
      $boot_modules`.
- [ ] `<scenario>.run` symlinked at `repos/sponge/run/<scenario>.run`.
- [ ] Probe installed-and-launches-and-pixel-verifies-and-exercises-error-paths.
- [ ] `./tool/build run <scenario>` produces
      `<name>-probe: PASS` and `Run script execution successful.`.

### 7.2 Source-built in-tree (Path b)

- [ ] Build target exists under `genode/repos/<repo>/src/...` (or
      `repos/sponge/src/...`).
- [ ] `pkg/<name>/metadata.xml` authored by hand with extensive
      header comments.
- [ ] `pkg/<name>/payload/<files>` staged if the component needs vfs
      mounts named in the metadata (`<rom name="X"/>`).
- [ ] Scenario `<scenario>.run` written per §3.2.2.
- [ ] Symlink at `repos/sponge/run/<scenario>.run`.
- [ ] Probe drives install + launch + behavioral check + error paths.
- [ ] `./tool/build run <scenario>` produces
      `<name>-probe: PASS`.

### 7.3 Noux CLI tool inside terminal (Path c)

- [ ] `noux-pkg/<tool>` target exists upstream.
- [ ] Port added to `port_list()` in `tool/build.mojo`.
- [ ] `./tool/build ports` prepared the new source archive.
- [ ] `noux-pkg/<tool>` added to the `build {}` list of all three
      terminal scenarios (`sponge-terminal.run`,
      `sponge-terminal-qmp.run`, `sponge-usb-kbd-via-qmp.run`).
- [ ] `<tar name="<tool>.tar"/>` added to
      `pkg/terminal/metadata.xml`'s vfs block.
- [ ] Probe assertion added if the tool is exercised (optional but
      recommended).
- [ ] Config serialization budget still within 3072 bytes (see §5.2).
- [ ] Package quota still covers all sub-init children (see §5.4).
- [ ] `./tool/build run sponge-terminal` produces
      `terminal-probe: PASS`.

---

## 8. References

- [`docs/12-package-format.md`](12-package-format.md), the format
  spec. §4 grammar, §5 layout, §5.4 the boot-time manifest,
  §6 dependency resolution, §7 routing defaults.
- [`docs/08-development.md`](08-development.md), §12 the depot
  importer contract and manual escape hatch. §4 run-script anatomy.
- [`docs/plans/phase13-package-ecosystem.md`](plans/phase13-package-ecosystem.md),
  the Phase-13 plan; D13.1 (CLI tools extend terminal),
  D13.2 (toolset), D13.3 (calculator + pdf_view), D13.5 (casing map
  fix), D13.6 (this document).
- `tool/pkg_import.mojo`, the importer source, including the
  `session_name()` casing map (lines 378-412) and the metadata
  generator (lines 518-599).
- `repos/sponge/src/sponge_pkgd/main.cc`, the daemon; lines 641-657
  are the 3072-byte serialization budget referenced in §5.2.
- `repos/sponge/src/test/calculator_probe/main.cc`, the minimal
  probe pattern (install, broadcast, launch, pixel-verify,
  error-paths).
- `genode/repos/libports/include/libc/args.h`, the `<env>` parser.
  Lines 58-68 are the two-form disambiguation referenced in §5.1.
- `genode/tool/builddir/build.mk:445`, the source of the
  "Error: No run script for <name>" message that requires the
  `repos/sponge/run/` symlink.
- `pkg/textedit/metadata.xml`,
  `pkg/falkon/metadata.xml`,
  `pkg/calculator/metadata.xml`,
  `pkg/pdf_view/metadata.xml`,
  `pkg/terminal/metadata.xml`, the five worked-example metadata
  files for each path in §3.
- `run/sponge-textedit.run:171-186`, the canonical Tcl staging
  idiom (quoted in §2.3).
- `run/sponge-pdf-view.run:213-218`, the `<rom name="..."/>`
  matching example.

---

## 9. Open Questions

Per `AGENTS.md` §6, items genuinely undecided at the end of this
document:

- **Multi-stage metadata.** A future `<payload manifest="...">`
  element could enumerate which payload files exist, letting
  `sponge_pkgd` validate the staged boot modules against the
  metadata without guessing. The current format has no formal
  description of payload files beyond "anything under the directory
  is staged"; the working assumption is that `tool/pkg_import` and
  the run-scenario `foreach m` loop keep them in sync.
- **Dependency vfs composition.** D13.1 settled Phase 13 by saying
  CLI tools extend the terminal. A future revision could compose one
  package's vfs into a dependent's vfs (the `pkg/ncurses` model),
  enabling standalone CLI packages. The schema change is additive
  and would not break this document's worked examples.
- **`pkg_import`'s full rewrite.** The Phase-13 fix was casing only.
  Broader rework (`readonly`/`subpath`/`label` support, non-Qt6
  config shapes, `--dry-run`, multi-version bin selection) is
  deferred. Each gap currently requires a hand-edit per package;
  documenting the hand-edit contract here is the Phase-13 answer,
  not a tool rewrite.
- **`pkg/README.md` vs `docs/12` drift.** The repo's
  `pkg/README.md` (§5.3 of `docs/12`) still describes the layout
  with the Phase 4 wording; it has not been brought forward to
  match `docs/12` §13's persistence semantics or to reference this
  authoring guide. A future cleanup should either retire
  `pkg/README.md` in favor of `docs/12` + this doc, or sync it.
