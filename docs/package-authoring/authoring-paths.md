# 16 - Package Authoring: Three Authoring Paths (§3)

> **Breadcrumb:** [`docs/16-package-authoring.md`](../16-package-authoring.md)
> holds §1-§2, §6-§8, and §10; this file holds §3. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

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

The wrapper script ([`tool/pkg_import`](../../tool/pkg_import) →
[`tool/pkg_import.mojo`](../../tool/pkg_import.mojo)) handles seven
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
