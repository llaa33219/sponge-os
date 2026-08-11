# Phase 13 — Package Ecosystem Growth (Work Plan)

> Status: active. Created 2026-08-11.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 13* (lines 620–635).
> Prior phase plan: `docs/plans/phase12-hardware-support.md`.
> Input evidence: two exploration passes on 2026-08-11 (port survey of the
> vendored Genode 26.05 tree + cproc depot index; package-authoring
> pipeline map of `pkg/`, `tool/pkg_import.mojo`, and the run-scenario
> staging idioms).

## Goal Restatement (docs/09-roadmap.md §10, Phase 13, verbatim)

The installable package set grows beyond the Alpha app set, and adding a
new package becomes a documented, low-friction process.

Three completion criteria (verbatim):

1. **Additional everyday packages shipped in `pkg/`** (choices driven by
   the daily-use goals of Phase 14).
2. **Package authoring documented end-to-end**: metadata format, payload
   staging, index generation, and testing (`docs/12` extended or a new
   authoring guide).
3. **`tool/pkg_import.mojo` (or successor) covers the common import
   cases**; each new package has a boot-verified run scenario.

## Baseline (2026-08-11, from exploration)

- `pkg/` ships 8 entries: `hello`, `ncurses`, `nano` (metadata-only
  format examples), `terminal` (source-built noux bash+vim sub-init),
  `files`, `pkg_gui_demo` (source-built), `textedit`, `falkon`
  (depot-imported from cproc).
- `tool/pkg_import` covers exactly one shape: a **Qt6 GUI app published
  as a cproc depot `pkg/` archive** with `default-route="parent"`
  sessions and a hardcoded libc+vfs config fragment. Known defects:
  session-name casing map emits `NIC`/`RTC`/`GPU` where Genode expects
  `Nic`/`Rtc`/`Gui` (worked around by hand-edit in
  `pkg/falkon/metadata.xml:90-95`); no `readonly`/`subpath`/`label`
  session attributes; no non-Qt6 config shapes.
- cproc depot pkg index (probed 2026-08-11): the only Qt6 application
  pkgs are `qt6_textedit` (already shipped) and the falkon family
  (already shipped). `qt6_calculatorform` exists only as a `src/`
  archive (built-from-source pattern in
  `genode/repos/libports/run/qt6_calculatorform.run`). Other
  user-interesting pkgs: `system_shell` (noux bash+coreutils+vim+tclsh
  bundle — overlaps our source-built terminal), `screenshot_trigger`,
  `morph_browser` (redundant with falkon), `arora`/`qt5_*` (Qt5 — off
  target). **Consequence: ecosystem growth must come primarily from
  source-built and noux-tar packages, not from more depot imports.**
- noux-pkg build targets already exist upstream for `coreutils-minimal`,
  `grep`, `sed`, `tar`, `less`, `findutils`, `diffutils`, `which`,
  `make`, `tclsh` (`genode/repos/ports/src/noux-pkg/`); the ports are
  defined but not yet prepared in `genode/contrib/`.
- `pdf_view` (mupdf-based) exists upstream
  (`genode/repos/libports/src/app/pdf_view`, run script
  `genode/repos/libports/run/mupdf.run`); ports `mupdf`, `openjpeg`,
  `jbig2dec` are defined but not prepared. `freetype`, `libpng`,
  `jpeg`, `zlib` are already prepared.

## Binding Decisions

| # | Decision | Rationale |
|---|---|---|
| D13.1 | **CLI tools extend `pkg/terminal` (vfs tar mounts), they are NOT standalone packages.** | A noux binary is only runnable inside a noux runtime; `sponge_pkgd` has no cross-package vfs composition, so a standalone `pkg/grep` would install-and-do-nothing — dishonest packaging that violates the "convenience must be proven in code" rule (AGENTS.md §5.1). The launcher vocabulary stays clean: one "Terminal" entry that now contains a real UNIX toolset. |
| D13.2 | **CLI toolset = coreutils, grep, sed, tar, less, findutils, diffutils, which.** | The daily-use set for a Phase-14 working terminal: file ops, search, streaming edits, archiving, paging. `make`/`tclsh`/`socat`/`gnupg` are deferred (developer/niche, can follow the same pattern later). |
| D13.3 | **New GUI packages = `calculator` (qt6_calculatorform, source-built from cproc `src/` depot archive) and `pdf_view` (mupdf, source-built in-tree).** | Both are everyday Phase-14 apps; both have upstream run scripts as ground truth; both exercise the *source-built* authoring path that the authoring guide must document (the depot-pkg path is already proven twice by textedit/falkon). |
| D13.4 | **No new depot-pkg imports in Phase 13.** | The cproc index has no Qt6 app pkg left that is both everyday-relevant and non-redundant (verified 2026-08-11). `screenshot_trigger` is a candidate follow-up but needs a capture-session wiring story first — deferred, recorded as open question. |
| D13.5 | **`pkg_import` fix scope = session-name casing map only (`nic→Nic`, `rtc→Rtc`, `gpu→Gui`), plus a regression note in the authoring guide.** | This is the documented falkon hand-edit friction; fixing it is small and makes the tool's output correct for the common Qt6 case. Broader rework (attribute support, non-Qt6 config shapes, `--dry-run`) is deferred — the authoring guide documents the hand-edit contract instead. |
| D13.6 | **Authoring documentation = new `docs/16-package-authoring.md`** (end-to-end: metadata → payload staging → pkg_index → run scenario), plus a drift fix to `docs/12` §4.2 (hello example contradicts §4.1's omit-sessions rule). | docs/12 stays the format *spec*; docs/16 is the *how-to*. README doc map gains the entry. |

## Work Items

- **W1 — Port preparation.** Add `coreutils grep sed tar less findutils
  diffutils which mupdf openjpeg jbig2dec` to `port_list()` in
  `tool/build.mojo`; run `./tool/build ports`; record the new port set
  and fingerprints in `docs/11-environment.md`.
- **W2 — Terminal toolset (D13.1/D13.2).** Extend
  `pkg/terminal/metadata.xml`'s vfs with the new noux tars; extend
  `run/sponge-terminal.run`'s build list and the `terminal_probe`
  assertions (run `ls`, `grep`, `sed`, `tar` inside bash and assert
  real output). Pass = `terminal-probe: PASS` with the tool assertions.
- **W3 — Calculator (D13.3).** Import `cproc/src/qt6_calculatorform`
  via the depot src pattern (as upstream
  `qt6_calculatorform.run` does), author `pkg/calculator/` (metadata +
  payload + SOURCE provenance note), and add boot-verified
  `run/sponge-calculator.run` following the `sponge-textedit.run`
  probe pattern. Launcher category: `Utilities`.
- **W4 — PDF viewer (D13.3).** Build `app/pdf_view` against
  `lib/mupdf` (+ openjpeg/jbig2dec), author `pkg/pdf_view/` with a
  bundled sample PDF payload, add boot-verified
  `run/sponge-pdf-view.run` asserting first render of the sample
  document. Launcher category: `Utilities`. If mupdf 0.9 fails to
  build on Genode 26.05, record the failure as a documented gap and
  substitute a second source-built Qt6 app rather than sinking the
  phase into a port rescue.
- **W5 — Importer fix (D13.5).** Fix the `session_name()` casing map in
  `tool/pkg_import.mojo`; verify by regenerating into a scratch dir
  that the emitted names match Genode service names.
- **W6 — Authoring guide (D13.6).** Write
  `docs/16-package-authoring.md`; fix `docs/12` §4.2; add the doc-map
  entry in `README.md`; sync `docs/11-environment.md` port ledger.
- **W7 — Close-out.** Roadmap §13 checkboxes, evidence writeup under
  `docs/evidence/`, full regression sweep (`./tool/build verify` plus
  the pkg scenarios), version notes if warranted.

## Verification Contract

Per AGENTS.md §4.2, every new package ships with a run scenario that
builds `lib/ld` + `core init <component>`, passes `[build_artifacts]`
to `build_boot_image`, and asserts a real behavioral marker (probe PASS
or equivalent) — no "staged but unbooted" packages. W2/W3/W4 are not
done until their scenarios pass on base-sel4 in QEMU. W7 runs the full
sweep.

## Open Questions (recorded, not blocking)

- `screenshot_trigger` depot pkg: needs a Capture-session routing story
  through `sponge_pkgd`'s session model before it can be more than a
  demo. Phase 13+ follow-up.
- Whether dependency payloads should compose into a dependent's vfs
  (would enable standalone CLI-tool packages) — parked; D13.1 is the
  Phase-13 answer.
- `make`/`tclsh` in the terminal toolset: deferred to keep W2 bounded;
  same pattern, one-line additions later.
