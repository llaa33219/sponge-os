> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 13.

### Phase 13: Package Ecosystem Growth

#### Goal

The installable package set grows beyond the Alpha app set, and
adding a new package becomes a documented, low-friction process.

#### Completion Criteria

- [x] Additional everyday packages shipped in `pkg/` (choices driven
  by the daily-use goals of Phase 14). — **Phase 13 W2–W4**: the
  `terminal` package now ships a UNIX CLI toolset
  (coreutils/grep/sed/tar/less/findutils/diffutils/which, mounted as
  noux tars in its vfs, per the D13.1 decision that CLI tools are not
  standalone packages), plus two new source-built GUI packages:
  `calculator` (qt6_calculatorform) and `pdf_view` (mupdf). All three
  are boot-verified on base-sel4: `run/sponge-terminal.run`,
  `run/sponge-calculator.run`, `run/sponge-pdf-view.run`.
- [x] Package authoring documented end-to-end: metadata format,
  payload staging, index generation, and testing (`docs/12` extended
  or a new authoring guide). — `docs/16-package-authoring.md`
  (three authoring paths, boot-verification contract, pitfalls
  incl. the latent Phase-7 `<env>` format bug found and fixed in W2).
- [ ] `tool/pkg_import.mojo` (or successor) covers the common import
  cases; each new package has a boot-verified run scenario. — **partial**:
  the scenarios are delivered (above), and the importer's session-name
  casing defects were fixed (`Nic`/`Rtc`/`Gui`), but the broader
  importer coverage (non-Qt6 config shapes, session attributes,
  `--dry-run`) is deferred per D13.5 — the common depot-pkg import
  case was already covered in Phase 7, and the Phase 13 additions
  exercised the source-built path instead.

## Status note (moved from README)

- The terminal package includes **vim** alongside bash and the UNIX
  CLI toolset (the README's "terminal with bash/vim" wording; the
  toolset rows above list coreutils, grep, sed, tar, less, findutils,
  diffutils, which).
- The mupdf PDF viewer ships with a **bundled sample document**.
