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

Related: the provider-owned Settings contract (Phase 16 Extension
target) has its own authoring workflow in §9 below. Its metadata
schema is [`docs/12-package-format.md`](12-package-format.md) §14 and
its vct surface is [`docs/06-vct.md`](06-vct.md) §4.8.

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
├── debug/                  # source-built in-tree: SPONGE DEBUG monitor
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


**Document parts.** The three authoring paths, the boot-verification
contract and pitfalls, and the settings-provider workflow are
maintained in part files under `docs/package-authoring/`. Section
numbers continue across this parent and its parts; every section below
either lives here or links to its part file.

| Sections | File |
|---|---|
| §1-§2, §6-§8, §10 | this file |
| §3 Three Authoring Paths | [`docs/package-authoring/authoring-paths.md`](package-authoring/authoring-paths.md) |
| §4 The Boot-Verification Contract, §5 Pitfalls | [`docs/package-authoring/boot-verification.md`](package-authoring/boot-verification.md) |
| §9 Shipping a Settings Provider (Phase 16 Extension Target) | [`docs/package-authoring/settings-provider.md`](package-authoring/settings-provider.md) |

---

## 3. Three Authoring Paths

"What am I packaging?" maps to three paths: depot pkg import (a),
source-built in-tree component (b), and noux CLI tools inside an
existing terminal (c). The paths, their worked examples, the importer's
current limitations, and the hand-edits they require are documented in
[`docs/package-authoring/authoring-paths.md`](package-authoring/authoring-paths.md)
(§3.1-§3.3).

---

## 4. The Boot-Verification Contract

Every package must boot. The run-scenario requirements, the probe
contract, and what a `<name>-probe: PASS` marker does and does not
prove are documented in
[`docs/package-authoring/boot-verification.md`](package-authoring/boot-verification.md)
(§4.1-§4.2).

---

## 5. Pitfalls

Hard-won lessons on the `<env>` attribute format, the 3072-byte config
serialization budget, synthetic input injection, and quota budgeting
are documented in
[`docs/package-authoring/boot-verification.md`](package-authoring/boot-verification.md)
(§5.1-§5.4).

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

## 9. Shipping a Settings Provider (Phase 16 Extension Target)

The authoring workflow for the provider-owned Settings contract in
[`docs/12-package-format.md`](12-package-format.md) §14: what you are
shipping, the six-step workflow, core and third-party examples, and
the provider test matrix. Documented in
[`docs/package-authoring/settings-provider.md`](package-authoring/settings-provider.md)
(§9.1-§9.4).

---

## 10. Open Questions

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

---

> §8 keeps its number but is placed last in this parent file, after
> §10, so References no longer appears to precede content that lives in
> the part files (§3, §4-§5, §9).

## 8. References

- [`docs/12-package-format.md`](12-package-format.md), the format
  spec. §4 grammar, §5 layout, §5.4 the boot-time manifest,
  §6 dependency resolution, §7 routing defaults, §14 the
  provider-owned Settings contract (Phase 16 Extension target).
- [`docs/06-vct.md`](06-vct.md) §4.8, the planned `vct settings
  list` / `vct settings open` surface on the same pkgd backend
  (Phase 16 Extension target).
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
