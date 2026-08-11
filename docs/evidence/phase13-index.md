# Phase 13 — Package Ecosystem Growth (Evidence Index)

> Phase plan: `docs/plans/phase13-package-ecosystem.md` (binding
> decisions D13.1–D13.6). Roadmap: `docs/09-roadmap.md` §10 Phase 13.
> All verification on base-sel4 in QEMU, 2026-08-11.

## Deliverables and receipts

### W1 — Port preparation

`tool/build.mojo` `port_list()` gained 13 ports: the noux CLI toolset
(coreutils, grep, sed, tar, less, findutils, diffutils, which, plus
pcre as the grep/sed lib dependency) and the mupdf stack (mupdf,
openjpeg, jbig2dec) plus qt6_tools (calculatorform example sources).
Fingerprints recorded in `docs/11-environment.md` §5 port table.

Operational note: `ftpmirror.gnu.org` served corrupted payloads for
sed/less/findutils on 2026-08-11 (hash-sum check failures). Workaround,
reusable: download the tarballs from `ftp.gnu.org` directly, verify
against the SHA in the `.port` file, drop them into
`genode/contrib/cache/<sha>_<name>` and delete any stale
`genode/contrib/<port>-<hash>.incomplete` dir before re-running
`prepare_port`. Logs: `var/setup-ports-phase13{,-b}.log`.

Host-tool addition: `gperf` is required to build coreutils (gnulib
regenerates `lib/iconv_open-*.h`). User-installed via pacman after an
AGENTS.md §5.5 stop-and-ask. Documented in the docs/11 coreutils row.

### W2 — Terminal CLI toolset (`run/sponge-terminal.run`)

`pkg/terminal` 1.0 → 1.1: eight toolset tars mounted in the vfs child,
`<env>` nodes converted to the content form, package quota 160M → 224M
(children sum 192M), bash start 32M → 64M, vfs start 32M → 48M.
`terminal_probe` gained the toolset assertion: it types `ls -l /bin`
through the synthetic Press_char path and requires the rendered listing
glyph jump (tall 9-line scan band; threshold 8x the echo increment).

PASS receipt: `var/w1-terminal-toolset10.log` —

```
terminal-probe: keystroke echo confirmed (glyphs 22 -> 584)
terminal-probe: toolset listing confirmed (glyphs 1712 -> 8858)
terminal-probe: PASS
```

Failure-mode margin check (from `var/w1-terminal-debug*.log`): with the
toolset absent or the command failing, the post-command band holds the
command echo plus a one-line error (observed 1994 against a 3605
threshold at the time), well under the gate; the success case renders
the listing (8858), well over it.

### Latent bugs found and fixed during W2

1. **`<env>` format bug (present since Phase 7 todo 13).** The terminal
   metadata used `<env name="PATH" value="/bin"/>`. libc's
   `populate_args_and_env`
   (`genode/repos/libports/include/libc/args.h`) accepts
   `<env name="K">value</env>` (content form) and the legacy
   `<env key="K" value="V"/>`; the `name`+`value` form matches the
   content-form parser, which then reads the (empty) node content —
   every variable came out EMPTY. Symptom chain: `PATH=` → bash's PATH
   search fails → `ls: command not found` while `/bin/ls` exists,
   `test -x /bin/ls` passes, and absolute-path `/bin/ls` executes.
   Pre-fix the Alpha terminal could never have run an external command;
   no scenario exercised one until Phase 13. Diagnosis receipts:
   `var/w1-terminal-debug3.log` (XOK vs command-not-found),
   `var/w1-terminal-debug5.log` (`PATH=` empty),
   `var/w1-terminal-debug8.log` (argv works: `Z=bash N=0`, isolating
   env). Fix: content-form env nodes in `pkg/terminal/metadata.xml`.
   Falkon is unaffected (it uses the legacy `key=` form).
2. **Prompt-detection race (terminal-qmp host timeout).**
   `_wait_for_prompt` accepted the baseline at the first non-zero glyph
   sample; the prompt settles in stages in QMP mode (recovery poke +
   vesa_fb mode set), so the baseline could be taken mid-settle and the
   later settle growth was misread as the keystroke echo (observed:
   baseline 98, then 355 at echo poll 0 with no host input), printing
   PASS before the host's expect protocol finished dispatching →
   120 s `awaiting PASS` timeout (`var/w7-regress-terminal-qmp{,2}.log`).
   Fix: accept the baseline only after 5 consecutive equal non-zero
   samples. `run/sponge-terminal-qmp.run` then passed end-to-end
   (`var/w7-regress-terminal-qmp3.log`).
3. **nitpicker double-press filtering.** Synthetic typing must submit
   `Press_char` + `Release` per character; without the Release,
   nitpicker drops repeats of the same keycode
   (`Warning: suspicious double press of KEY_UNKNOWN`).

### W3 — Calculator (`run/sponge-calculator.run`)

`pkg/calculator`: source-built Qt6 (`app/qt6/examples/calculatorform`
from the qt6_tools port; no depot pkg exists — probed
`depot.genode.org/cproc/pkg/` 2026-08-11, only `qt6_textedit` and the
falkon family are published as Qt6 app pkgs; this is why D13.3 chose
the source-built path and D13.4 ruled out further depot imports).
PASS receipt: `var/w3-calculator2.log` —

```
calculator-probe: calculator window detected (100% non-bg, 13 distinct color buckets)
calculator-probe: [6] not-installed reported
calculator-probe: [7] already-running reported
calculator-probe: PASS
```

### W4 — PDF viewer (`run/sponge-pdf-view.run`)

`pkg/pdf_view`: source-built mupdf `app/pdf_view` + bundled
`payload/sample.pdf` (632 bytes, one page, hand-generated and parsed
cleanly by pypdf host-side). Key wiring lesson (in the metadata header
and docs/16 §3.2): pdf_view finds its document via
`scandir("/")` for the first `*.pdf` name, so the vfs
`<rom name="sample.pdf"/>` mount and the staged boot module must share
the filename exactly. PASS receipt: `var/w4-pdfview.log` —
`pdf-view-probe: PASS`.

### W5 — Importer casing fix

`tool/pkg_import.mojo` `session_name()` now emits `Nic`/`Rtc`/`Gui`
(was `NIC`/`RTC`/`GPU`; the falkon header comment documented the
workaround). `./tool/pkg_import help` compiles and exits 0.

### W6 — Authoring guide

`docs/16-package-authoring.md` (three paths, verification contract,
four pitfalls). README documentation map gained entries 15 (was
missing) and 16. `run/README.md` gained the two new scenarios and the
terminal toolset note. `docs/12` §4.2/§4.3 example drift fixed (hello
sessions example contradicted §4.1; nano depended on `libncurses`
instead of `ncurses`).

### W7 — Regression sweep

Terminal-affected scenarios, all re-run against the final tree on
base-sel4:

| Scenario | Result | Log |
|---|---|---|
| `sponge-terminal` | PASS (exit 0) | `var/w7-regress-terminal.log` |
| `sponge-terminal-qmp` | PASS (exit 0) | `var/w7-regress-terminal-qmp3.log` |
| `sponge-usb-kbd-via-qmp` | PASS (exit 0) | `var/w7-regress-usbkbd.log` |
| `sponge-calculator` | PASS (exit 0) | `var/w3-calculator2.log` |
| `sponge-pdf-view` | PASS (exit 0) | `var/w4-pdfview.log` |

`sponge_pkgd` is byte-identical to pre-Phase-13 (debug instrumentation
added for W2 diagnosis was fully reverted; `git diff` empty).

## Open items carried forward

- Roadmap criterion 3 is checked with a **partial** note: the
  importer's common depot-pkg case was proven in Phase 7 and its
  casing defects are fixed, but non-Qt6 config shapes, session
  attributes, and `--dry-run` remain deferred (D13.5).
- The three terminal-hosting scenarios share a duplicated noux build
  list; a shared include (`run/qmp.inc`-style) would prevent future
  skew. Not done in Phase 13.
- `docs/13-installation.md` still describes the Alpha app set; the
  calculator/pdf_view packages are not yet wired into the Alpha media
  (Alpha media staging is a separate decision — package size vs the
  seL4 boot-module budget). Recorded as a Phase-14 candidate.
