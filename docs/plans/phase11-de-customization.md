# Phase 11 — Sponge DE: Customization and Panel Strengthening (Work Plan)

> Status: awaiting execution (rev. 2 — Momus review fixes applied). Created 2026-08-07, revised 2026-08-07.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 11* (lines 471–490).
> Pre-planning consultation: `Metis — Phase 11 (full consultation, 2026-08-07)`.
> Prior phase plan: `docs/plans/phase10-interactive-desktop.md`.

## Goal Restatement (docs/09-roadmap.md §10, Phase 11, verbatim)

Users can meaningfully customize their desktop environment — theme, panel
layout, and behavior — without touching Genode internals. Automation
stays the default; every customization is also reachable manually
through `vct config` / theme files (control escape hatch).

Four completion criteria (verbatim):

1. **Panel customization through `sponge_configd`**: position, size,
   visible widgets, clock format, launcher organization.
2. **Expanded theme surface**: more themeable elements, additional
   shipped themes beyond `default.theme`, **live reload preserved**.
3. **Sponge-themed window chrome**: the `themed_decorator` drop-in
   (theme tar via VFS) replaces stock decorations with themed ones.
4. **All customization flows documented and scenario-verified**
   (config change → configd → themed/decorator → pixel repaint).

## Binding Decisions (from Metis, do not re-litigate)

| # | Decision | Phase 11 outcome |
|---|---|---|
| D2.1 | `panel.position` boot-time-only | configd persists; run script picks pre-defined domain; README row "boot-time"; live move deferred |
| D2.2 | Configd vs theme precedence = clean separation | `panel.height`/`panel_position` live ONLY in configd; theme keeps parsed-but-unused keys as documented no-ops for v1; removal in Phase 12 with deprecation note. Panel component hardcoded `DEFAULT_PANEL_HEIGHT = 28` fallback |
| D2.3 | themed_decorator live theme switch | Boot-time tar identity + live `color=` policy (decision (d)) via a small `sponge_decorator_bridge` component that regenerates layouter/decorator policy XML on `theme.active` change. No upstream patch in Phase 11; patch-ledger candidate logged for Phase 12 |
| D2.4 | Key surface = set B (4 new keys) | `panel.height` (uint 16–128, default 28), `panel.visible_widgets` (enum-list `{clock, launcher}`, default both), `clock.format` (string, Qt-format validation with try/catch fallback to `"HH:mm"` + `Genode::warning`), `launcher.sort_by` (enum `{manual, alpha}`, default `alpha`). Validator extensions: `uint range`, `enum-list`, `string format`. NO launcher entry-source changes (group_by deferred) |
| D2.5 | Two new shipped themes | `dark.theme` (palette + font) + `compact.theme` (palette + layout metric set). Total 4 (default + light + dark + compact). None of the themes carry `panel_height`/`panel_position` |
| D2.6 | themed chrome lives only in WM scenarios | `sponge-de-sel4-interactive.run` topology unchanged. New `sponge-de-themed-chrome.run` (seL4) bootstraps the WM + themed_decorator path |
| D2.7 | Hybrid kernel gating | configd + theme + panel config flows on base-linux; decorator drop-in + Phase-10 drag regression on base-sel4+QMP |

## Scope Guards

- **No edits to vendored `genode/` tree.** All Sponge code stays in
  `repos/sponge/`. The Phase 12 patch-ledger entry logging
  is the only durable record of the deferred live-tar-reload patch.
- **No vct or Leitzentrale architecture changes.** vct already routes
  `config <key> [<value>]` through `ReportRomClient` on the
  `config_request`/`config_result` channel; new keys get new
  validator rows, not new code paths.
- **No launcher entry source changes.** `sponge_pkgd`'s `installed`
  broadcast stays as-is; launcher sort/group is an ordering concern,
  not an entry-source concern.
- **No `sponge-de-sel4-interactive.run` topology change.** Panel
  bypasses WM by design; demo + package windows keep their direct
  nitpicker route. WM-stack introduction for the flagship is a
  Phase 12 candidate.
- **No new font files.** Phase 11 uses already-available fonts
  (including the upstream `themed_decorator` `theme/font.tff` which
  is **re-vendored unchanged**, not regenerated — see W4 step 2);
  the parsed-but-unused `title_family` stays parsed-but-unused.
- **Local patches to vendored tree are forbidden.** Boot-time tar
  identity + live color policy is the only decorator mechanism.
- **No TTF→TFF conversion step.** `genode/repos/libports/src/app/ttf2tff`
  is not in the vendored tree. `tool/decor_assets.mojo` copies the
  upstream `theme/font.tff` **byte-for-byte** into the generated
  `decor.tar`; PNG regeneration/resizing is the only host-side
  synthesis (see W4 step 2).

## Verified Ground Truth (two-codebase-inventory-validated — trust, do not re-derive)

- **Panel**: `repos/sponge/src/sponge-de/panel/panel_widget.cc` (118
  lines). Launcher button + clock only. Clock format hardcoded
  `HH:mm` (lines 89, 92). Height from `theme.panel_height()`
  (**default 28** per `default.theme` line 217). Panel is
  frameless; position owned by nitpicker domain in run script.
  `theme.panel_position()` parsed but never read by panel.
  Launcher toggle geometry at lines 61–62:
  `setFixedSize(theme.launcher_width(), theme.panel_height() - 2 * gap)`
  — i.e. the toggle's height tracks `panel_height - 2*gap`, so
  growing the panel visibly grows the toggle rect.
- **configd**: `repos/sponge/src/sponge_configd/main.cc`. Closed
  registry (lines 169–178) of exactly 3 keys: `leitzentrale.enabled`,
  `panel.position`, `theme.active`. `MAX_KEYS=16` (line 79),
  `MAX_ENUMS=8` (line 80). Validator `_value_valid` (lines 203–238)
  accepts only non-empty strings + enum-in-set. Defaults applied at
  startup (lines 472–474). Broadcast `_generate_broadcast`
  (lines 366–385). README registry table at
  `repos/sponge/src/sponge_configd/README.md:53–62`.
- **Theme pipeline**: `vct theme apply` → configd broadcast
  `theme.active` → `sponge_themed` (resolves `<name>.theme` ROM,
  publishes `theme` report, name-dedup) → sponge-de
  `ThemeController` (live mode gated by `<theme source="themed"/>`,
  ROM sigh + 250 ms QTimer pull + `QMetaObject::invokeMethod`
  GUI-thread marshal; name-dedup; re-styles attached panel/demo/
  launcher; publishes `applied_theme`). **2048-byte theme content
  cap** (`Genode::String<2048>` at `theme_controller.cc:155–157`).
- **Live-restyle gaps (constructor-only state)**:
  - `panel_widget.cc:52–62` (root margins, spacing, launcher button
    size) ctor-only; `restyle()` at 97–118 reapplies only
    style + outer geometry.
  - `launcher_menu_view.cc:38–44` (root margins, spacing) ctor-only.
  - `sponge_de_main.cc:79–84` (demo title font) ctor-only.
- **sponge-de's ONLY configd broadcast consumer today**:
  `ThemeController` watches the `theme` ROM (which is **sourced from
  `sponge_themed`**, a different report_rom slot). `sponge_de_main.cc`'s
  `Main` does **not** directly consume the `config` broadcast from
  `sponge_configd`. **Phase 11 adds a new in-sponge-de consumer —
  `ConfigController` — to bridge configd's `config` ROM to Qt**
  (see W2 step 3 + W6 docs).
- **Theme format**: 24 keys in `docs/10-theme-format.md`; shipped
  themes `default.theme` + `light.theme` (layout-identical,
  palette-different). Parsed-but-unused keys: `window_border`,
  `error`, `success`, `warning`, `icon_size`, `shadow_radius`,
  `title_family`, `panel_position`.
- **Launcher**: fed by `sponge_pkgd` `installed` broadcast
  (`launcher_controller.cc`); grouped by package `category`
  attribute (`launcher_menu_view.cc:144–156`); pkgd name-sorts.
  Popup hardcoded metrics: width=screen/3 (lines 199–212), entry
  min-height  50 px + padding 16 px 12 px (231–244), popup
  bg = panel_bg, entry hover = accent.
- **vct**: `vct config <key> [<value>]` routes ANY registry key with
  NO vct code change (`commands.cc:962–1019`, `ReportRomClient` on
  `config_request`/`config_result`).
- **WM stack**: `run/sponge-wm-qmp.run` (seL4+QMP, Phase 10
  verified): upstream `wm` + `window_layouter` + stock `decorator`
  (assets linked via `SRC_BIN`). Panel bypasses WM; demo/pkg windows
  route through wm. `run/sponge-de-sel4-interactive.run` has NO WM
  stack (everything direct to nitpicker) — must NOT gain one in
  Phase 11.
- **Upstream `themed_decorator`** (vendored Genode 26.05): binary at
  `genode/repos/gems/src/app/themed_decorator/`; needs libc + Timer
  + VFS `<tar name="decor.tar"/>`. Config (parsed by
  `genode/repos/gems/src/app/themed_decorator/config.h:65`):
  `motion` is parsed as **unsigned** (`motion="1"` or `motion="0"`,
  not `motion="yes"`/`motion="no"` — string `yes` is a parse error).
  Decorator assets: `theme/default.png` (stretchable 9-slice frame),
  `theme/closer.png`, `theme/maximizer.png`, `theme/font.tff`,
  `theme/metadata` (aura / decor margins + title/closer/maximizer
  geometry). Config: `<policy label_prefix="..." decoration="yes|no"
  motion="1|0" color="..."/>`. Reports `decorator_margins` to
  layouter. Assets cached in statics → NO live tar reload; config
  ROM reload re-applies policy colors. Runtime recipe:
  `genode/repos/gems/recipes/pkg/themed_decorator/runtime`.
- **`ttf2tff` host tool does not exist.** Zero hits in the repo for
  any `ttf2tff` binary, source, or recipe. The decorator's
  `theme/font.tff` is read by the binary directly via its built-in
  `Tff_font` (`genode/repos/gems/src/app/themed_decorator/theme.cc`),
  which parses TFF (Genode trimmed TTF) bytes — there is no
  build-time / host-time conversion step in the vendored tree or
  upstream. The shipped `theme/font.tff` in the upstream sample tar
  is the canonical asset; we re-vendor it byte-for-byte.
- **Probe path** (correcting the prior draft): the Sponge DE probe
  lives at `repos/sponge/src/test/sponge_de_probe/main.cc`
  (`test/` is a sibling of `sponge-de/`, not a subdirectory of
  it). All references below use this path.
- **Verification infra**: `run/sponge-config-probe.run`
  (pkg_seq_probe HID steps set/get/list/expect_status — extend by
  adding steps), `run/sponge-theme.run` (3-way coherence + Capture
  pixel, theme_probe), `run/sponge-launcher.run`,
  `run/sponge-alpha.run`. Test probes in `repos/sponge/src/test/`.
- **Kernel strategy**: base-linux fast loop; base-sel4+QMP
  production gate. configd/theme flows on base-linux; decorator
  drop-in + Phase-10 drag regression on seL4.

## Metis AI Failure Points (Risk Register — encoded as task guardrails)

| # | Trap | Mitigation |
|---|---|---|
| 1 | 2048-byte theme transport cap silently truncating expanded themes | Raise cap to `Genode::String<8192>` with version bump + `Genode::warning` on truncation; new `tool/test_theme_payload_size.mojo` asserts all four shipped themes ≤ 8192 bytes |
| 2 | GUI-thread marshal violation on configd callbacks | Explicit rule: any Qt-visible property mutation from a configd `sigh` callback must use `QMetaObject::invokeMethod` (codify in `panel_widget.h` and the new `ConfigController` header); review PR for the pattern |
| 3 | Constructor-only state silently not restyled | Before declaring any geometry key live-reloadable, the task must migrate the relevant constructor-only state to a `restyle()` path. Explicit list per task. Add a probe that detects geometry drift between two configd writes |
| 4 | `decorator_margins` shift breaks Phase-10 drag regression gate | themed_decorator frame geometry defaults must match upstream `decorator` defaults unless explicitly themeable. `sponge-wm-qmp.run` must pass with themed_decorator substituted in. Phase-10 drag scenario is a regression gate, not optional |
| 5 | `MAX_KEYS=16` overflow | Set B brings total to 7; 9 free slots. Phase-12 batch bumps if needed. Do not collapse `panel.visible_widgets` to a bitmask in Phase 11 |
| 6 | Single-writer Report/ROM channel collisions | `sponge_decorator_bridge` gets its own layouter `config` Report session; document channel ownership in the new component's README. `ConfigController` watches sponge_configd's `config` report via a new report_rom policy; document channel ownership in the new component README |
| 7 | Theme vs configd boot-ordering races | Run script documents boot order: `sponge_themed` first, then `sponge_configd`. configd waits for theme ROM before broadcasting overrides. Codified in `sponge-panel-config.run` step 0 |
| 8 | Demo window constructor-only title font | `title_family` stays parsed-but-unused in Phase 11 (defer to Phase 12 with restyle migration). Documented in `10-theme-format.md` §4.3 |
| 9 | Validator gaps for new types | Explicit per-type validator: `uint_range` (height), `enum_list` (visible_widgets), `format_string` (clock). Probe must include invalid format input (`"bogus"`), out-of-range height (`12`, `256`), empty enum-list (`""`), unknown enum token |
| 10 | Parsed-but-unused theme keys being claimed as "expanded surface" | Per task: every newly live-reloadable theme key must have a probe that proves a value change produces a pixel change in `sponge-theme.run`. Parsed-but-unused keys are either wired or removed with a deprecation note, never silently carried |
| 11 | Capability bloat for new sessions | enumerate new sessions explicitly: `sponge_decorator_bridge` requests only `ROM` (theme) + `Report` (decorator_config) sessions; the new in-sponge-de `ConfigController` requests one `ROM` session for `config`; minimum-privilege per session |
| 12 | Boot-time-only position with hidden run-script regeneration | Explicit forbidden action in W1 plan: configd does NOT regenerate run scripts. The manual escape hatch is "edit the run script per `docs/08-development.md` and reboot" |
| 13 | Two themes with identical layout values defeating the purpose | `compact.theme` MUST differ in at least one `[layout]` or `[fonts]` value from `default.theme`; `dark.theme` MUST differ in at least one `[fonts]` value. Validated by `sponge-theme.run` pixel diff probes |
| 14 | Launcher entry source change sneaking into Phase 11 | Explicit boundary: Phase 11 touches `panel_widget.cc`, `sponge_configd/main.cc`, the NEW `ConfigController`, `ThemeController`, `sponge_decorator_bridge`, `sponge-de` theme surface. Launcher entries source is out of scope |
| 15 | Probe subphase asserting an unrealizable pixel delta | The panel-config probe MUST only assert deltas against geometry that moves with configd writes (e.g. launcher-toggle height, clock glyph width). Do not assert against the panel/nitpicker background boundary (they share `#1e1e2e` by design) or against demo-window position (domain-owned) — see W2 step 8 |
| 16 | TTF→TFF conversion step invented because "it should exist" | There is no such step in the vendored tree. `tool/decor_assets.mojo` copies the upstream `theme/font.tff` byte-for-byte into the generated `decor.tar`. Documented in W4 step 2. **Do NOT introduce a host-side `ttf2tff` build target in Phase 11** |
| 17 | Adding "decor.*" theme keys duplicating existing `[fonts]` keys | `title_family`/`title_size` already exist in §4.3 `[fonts]`. Do not duplicate them as `decor.title_family`/`decor.title_size` in §4.5. The themed_decorator's `font.tff` is a literal asset, not theme-derived. See Must NOT Have |
| 18 | `motion="yes"` parses as `unsigned` and fails | Use `motion="1"` (or omit — default 0) — see W4 step 3 |

## Configd Registry Diff Sketch (target state)

```cpp
/* repos/sponge/src/sponge_configd/main.cc:169-178 — REGISTRY REPLACEMENT */
Sponge::Configd::Main::Key_def const Sponge::Configd::Main::_registry[MAX_KEYS] = {
    { "clock.format",          false, {  }, 0, "HH:mm" },
    { "leitzentrale.enabled",  true,  { "true", "false" }, 2, "false" },
    { "launcher.sort_by",      true,  { "manual", "alpha" }, 2, "alpha" },
    { "panel.height",          false, {  }, 0, "28" },  /* uint 16–128, validated */
    { "panel.position",        true,  { "top", "bottom", "left", "right" }, 4, "bottom" },
    { "panel.visible_widgets", false, {  }, 0, "clock,launcher" },  /* enum-list, validated */
    { "theme.active",          false, {  }, 0, "light" },
};
unsigned const Sponge::Configd::Main::_num_keys = 7;
/* MAX_KEYS stays at 16 — 9 free slots for Phase 12 batch */
```

Key_def struct extension (single extra field on the struct at
`main.cc:82-89`):

```cpp
struct Key_def {
    char const *name;
    bool        is_enum;
    char const *enum_values[MAX_ENUMS];
    unsigned    num_enums;
    char const *default_value;
    /* NEW: validation hint for non-enum, non-string keys */
    enum class Kind { String, Enum, UintRange, EnumList, FormatString } kind;
    unsigned    min_value;   /* UintRange only */
    unsigned    max_value;   /* UintRange only */
};
```

## Validator Extension Sketch (`_value_valid` at `main.cc:203-238`)

The validator branches on `kind` after the empty-value check:

| kind | sub-check |
|---|---|
| `String` | (current behavior) — accept any non-empty value |
| `Enum` | (current behavior) — reject anything outside `enum_values` |
| `UintRange` | parse base-10; reject if `< min_value` or `> max_value`; structured error carries both bounds |
| `EnumList` | split on `,`; trim each token; reject if any token outside `enum_values`; reject if list empty after split |
| `FormatString` | delegate to `Theme::qt_format_well_formed(const char *)` (new helper in `repos/sponge/src/sponge-de/theme/theme_loader.h`) — try/catch around `QDateTime::toString` with `"HH:mm"` fallback; emit `Genode::warning("clock.format: invalid 'BOGUS', falling back to HH:mm")` and accept the value (the panel will display the fallback; the operator is informed) |

## Theme Transport Cap Raise

`repos/sponge/src/sponge-de/theme/theme_controller.cc:155`:

```cpp
Genode::String<8192> const content = /* was 2048 */
    xml.decoded_content<Genode::String<8192>>();

if (content.length() == 8192 - 1) {
    Genode::warning("theme payload truncated at 8192 bytes; "
                    "raise cap if shipping a larger theme");
}
```

Version bump: `sponge-de + sponge_themed` get a `<config version="2"/>`
annotation in the run scripts carrying the wired theme path; both
components log their version on construction so a stale-binary boot
shows up immediately.

## new Scenario Names + Kernel Tags

```
run/sponge-panel-config.run         base-linux (configd + panel regression gate)
run/sponge-panel-config-sel4.run    base-sel4  (panel + QMP click on visible_widgets toggle)
run/sponge-de-themed-chrome.run     base-sel4  (themed_decorator drop-in + Phase-10 drag regression)
run/sponge-theme.run                base-linux EXTENDED (3-way + 3-theme probe)
tool/test_theme_payload_size.mojo   host        (size cap assertion for all four shipped themes)
```

## Decorator Theme-Tar Authoring Approach

Upstream `themed_decorator` consumes a VFS `<tar name="decor.tar"/>` with
five entries:

- `theme/default.png` — stretchable 9-slice frame (centered edges + corners)
- `theme/closer.png` — title-bar close button glyph
- `theme/maximizer.png` — title-bar maximizer button glyph
- `theme/font.tff` — TFF-format trimmed TTF (read literally by the
  upstream decorator's `Tff_font` parser in `theme.cc`; the bytes
  are passed straight through — **NO host-side TTF→TFF conversion
  step exists or is added in Phase 11**)
- `theme/metadata` — plain-text description of aura / decor margins +
  title / closer / maximizer geometry

PNG assets are produced by `tool/decor_assets.mojo` (with a thin bash
launcher `tool/decor_assets.sh` per AGENTS.md §3.5), which:

1. Reads the `metadata` text (hand-authored, in
   `tool/decor_assets_data/metadata.txt`) for the declared title-bar
   height, closer button rect, maximizer button rect, and 9-slice
   frame geometry — copy the upstream sample tar's `aura={...}` block
   verbatim so `decorator_margins` equals the stock `decorator`
   defaults, satisfying the Phase-10 drag regression gate.
2. Generates PNGs sized to that metadata using `mogrify`/`convert`
   (host tool, ImageMagick) when present; when absent, falls back to
   a small checked-in PNG asset set under `tool/decor_assets_data/`
   plus a Mojo-side resize-and-pad step. **Both paths are documented
   in `docs/08-development.md` §"themed_decorator assets" so the
   manual escape hatch is "edit `tool/decor_assets_data/metadata.txt`
   and re-run `./tool/decor_assets.sh`"**.
3. **Copies the upstream `theme/font.tff` byte-for-byte** from
   `genode/repos/gems/src/app/themed_decorator/theme/font.tff`
   (the same asset the upstream-themed_decorator sample tar ships).
   No TFF conversion is performed at build/host time; the
   vendored-tree asset is re-used unchanged. If the host cannot
   reach the vendored-tree path (e.g. fresh checkout without a
   build), the script falls back to an empty placeholder `font.tff`
   header and logs a warning — the decorator still boots, the title
   bar reverts to the Genode default font, and the Phase-11 pixel
   diff still exercises color (not glyphs).
4. Assembles the five entries into `decor.tar` (host-side `tar(1)`
   is fine; the Genode VFS `<tar>` loader handles a plain
   uncompressed tar).

The generated `decor.tar` is staged as a ROM in the new
`sponge-de-themed-chrome.run` only; the shipped `default.theme`
panel remains the same as today. The `sponge_decorator_bridge`
component regenerates the decorator's `config` ROM (`<policy ...
color="..."/>`) on `theme.active` change so palette colors flow
live without a tar reload.

## Scenario Architecture Decision

**Decision: 3 new or heavily extended scenarios + 1 host tool + 1
extended probe, one concern per scenario, following the Phase-10
convention.**

| Scenario | Criteria | Action | Kernel |
|---|---|---|---|
| `run/sponge-panel-config.run` (new) | 1 (panel config) | New: configd new-key probe steps + `sponge_de_probe` extended with `panel-config` observe phases | base-linux |
| `run/sponge-panel-config-sel4.run` (new) | 1 (panel config live on seL4) | New: panel + QMP click + visible_widgets toggle | base-sel4 |
| `run/sponge-theme.run` (extends 3-way coherence) | 2 (theme surface + new themes) | Extend: theme_probe gets 3-theme probe (default / dark / compact) + per-key pixel diff for the new live theme keys | base-linux |
| `run/sponge-de-themed-chrome.run` (new) | 3 (themed chrome) | New: wm + window_layouter + themed_decorator (replacing stock decorator) + panel + demo + pkg_gui_demo + `sponge_de_probe` (criterion 1) + `wm_probe` (criterion 2 drag regression) + `sponge_decorator_bridge` | base-sel4 |
| `tool/test_theme_payload_size.mojo` (new) | 2 (theme transport cap) | Host: assert all 4 shipped themes ≤ 8192 bytes | host |

**Tradeoff evaluation (why not one mega-scenario):**

- *Boot time:* the Phase-10 boot is already 600s+ per scenario; a
  merged `sponge-de-themed-chrome` + `panel-config` + `theme-3theme`
  would exceed the 2G QEMU cap and the §11.1 cap-accounting lesson.
- *Debuggability:* four focused scenarios fail independently; a
  mega-scenario failure is ambiguous.
- *Regression clarity:* per-criterion PASS markers map 1:1 to roadmap
  checkboxes; the Phase-10 drag scenario stays a separate probe.

## Task Dependency Graph

| Task | Depends On | Reason |
|---|---|---|
| W0: TDD-red baseline | None | Capture the current panel/configd state and the `sponge-wm-qmp` drag-regression green before any change. Per-criterion pass-marker log is the regression baseline |
| W1: Configd registry + validator + theme transport cap | W0 | Foundational; W2/W3/W4/W5 all consume the new keys/cap |
| W2: Panel restyle migration (constructor-only state) + new `ConfigController` | W1 | New keys are useless until the configd-`config`-ROM → Qt bridge exists (new ConfigController); constructor-only state must be reachable from `restyle()` |
| W3: Theme surface expansion + new shipped themes | W1, W2 | New themes must pixel-diff against the new geometry-restylable panel; restyle migration must be done first |
| W4: themed_decorator bridge + `sponge-de-themed-chrome` topology | W1 | Independent of W2/W3 in code; depends only on configd, but runs after W2 to keep the per-task partition clean for parallel critics |
| W5: Scenario + probe coverage (panel-config + theme-3theme + themed-chrome) | W2, W3, W4 | Probes depend on the production code paths being in place |
| W6: Docs sync + evidence + full regression | W2, W3, W4, W5 | Roadmap checkboxes require all four criteria green |

## Parallel Execution Graph

```
Wave 1 (start immediately):
└── W0: TDD-red baseline — read-only inventory + per-scenario green baseline

Wave 2 (after W0):
└── W1: configd registry + validator + theme transport cap
        (CRITICAL PATH — every other workstream needs the new keys/cap)

Wave 3 (after W1 — fire three IN PARALLEL, disjoint file sets):
├── W2: panel restyle migration + NEW in-sponge-de ConfigController
│       (panel_widget.{h,cc}, theme_controller.{h,cc}, launcher_menu_view.{h,cc},
│        sponge_de_main.{h,cc}, NEW config/config_controller.{h,cc},
│        NEW config/README.md, sponge-de target.mk)
├── W3: theme surface + new themes (docs/10-theme-format.md, dark.theme,
│       compact.theme, theme_loader.{h,cc}, theme_controller.cc —
│       shared with W2 only at theme_controller.cc, merge-by-protocol)
└── W4: themed_decorator bridge + new theme tar authoring
        (sponge_decorator_bridge/{main.cc,README.md,target.mk},
         tool/decor_assets.mojo, run/sponge-de-themed-chrome.run,
         genode/ contribution: NONE — vendored tree untouched, only
         referencing the upstream `themed_decorator` binary + its
         vendored theme/font.tff asset re-vendored unchanged)

Wave 4 (after Wave 3):
└── W5: scenario + probe coverage
        - run/sponge-panel-config.run (base-linux)
        - run/sponge-panel-config-sel4.run (base-sel4)
        - run/sponge-theme.run extensions (base-linux)
        - run/sponge-de-themed-chrome.run (base-sel4)
        - tool/test_theme_payload_size.mojo (host)

Wave 5 (after Wave 4):
└── W6: docs sync + evidence + full regression
        (docs/09-roadmap.md §10 checkboxes, docs/10-theme-format.md updates,
         run/README.md, docs/08-development.md, docs/11-environment.md,
         docs/evidence/phase11-index.md, docs/evidence/task-N-phase11-*.log)

Critical Path: W0 → W1 → W2 → W5 → W6
```

**Note:** scenario runs must be sequential (no concurrent `make` in
`genode/build/x86_64`); code edits may parallelize via disjoint file
sets, boots must serialize.

## Tasks

### W0: TDD-red baseline

Run the Phase-10 regression gate scenarios (the ones Phase 11 will
touch) on their documented kernels, and capture the green baseline +
the current per-criterion PASS marker. No code changes.

1. `make -C genode/build/x86_64 run/sponge-de-test KERNEL=linux BOARD=pc`
   → `sponge-de-probe: PASS` recorded.
2. `make -C genode/build/x86_64 run/sponge-config-probe KERNEL=linux BOARD=pc`
   → `config-seq-probe: PASS` (3 register rows, 2 error rows).
3. `make -C genode/build/x86_64 run/sponge-theme KERNEL=linux BOARD=pc`
   → `theme-probe: PASS` (3-way vct↔themed↔sponge-de coherence).
4. `make -C genode/build/x86_64 run/sponge-wm-qmp KERNEL=sel4 BOARD=pc`
   → `wm-probe: PASS` (drag +100,+100, the Phase-10 drag regression gate).
5. `make -C genode/build/x86_64 run/sponge-launcher KERNEL=linux BOARD=pc`
   → `launcher-probe: PASS` (current mark).
6. `make -C genode/build/x86_64 run/sponge-alpha KERNEL=sel4 BOARD=pc`
   → `alpha-probe: PASS`.

For each scenario, append the unchanged-pass log to
`docs/evidence/task-0-phase11-baseline.log`. If any scenario regresses
unexpectedly, stop and report — the Phase-10 green gate is the
Phase-11 starting point.

- **Category**: `quick`
- **Skills**: []
- **Depends On**: None
- **Kernel tags**: linux (scenarios 1–3, 5); sel4 (4, 6)
- **Acceptance Criteria**: all 6 scenarios green; per-scenario log
  captured in `docs/evidence/task-0-phase11-baseline.log` with the
  exact final-line marker.

### W1: Configd registry + validator + theme transport cap

**EXPECTED OUTCOME**: 4 new keys (`panel.height`, `panel.visible_widgets`,
`clock.format`, `launcher.sort_by`) are registered, validated, and
broadcast; the theme transport cap is raised to 8192 with a truncation
warning; the existing probe (`sponge-config-probe`) is extended with
the new-key set/get/list/error steps and still passes.

1. **Registry extension** (`repos/sponge/src/sponge_configd/main.cc`):
   - Replace the `_registry` array at lines 169–176 with the target
     state (7 entries, see "Configd Registry Diff Sketch" above).
   - Bump `_num_keys` to 7 at line 178.
   - Extend `Key_def` struct with `Kind` enum + `min_value`/`max_value`
     fields (see "Validator Extension Sketch").
2. **Validator extension** (`main.cc:203-238`, function `_value_valid`):
   - Empty-value check unchanged.
   - Branch on `d.kind`:
     - `String`: current behavior.
     - `Enum`: current behavior.
     - `UintRange`: parse base-10; reject if `< d.min_value` or
       `> d.max_value`. Error message: `"value 'NN' for key 'panel.height'
       out of range [16..128]"`.
     - `EnumList`: split on `,`, trim each token; reject if any token
       outside `d.enum_values`; reject if list empty after split.
       Error message: `"invalid token 'X' in list for key
       'panel.visible_widgets' (expected: clock, launcher)"`.
     - `FormatString`: delegate to new helper
       `Theme::qt_format_well_formed(const char *)` in
       `repos/sponge/src/sponge-de/theme/theme_loader.h` — try/catch
       around `QDateTime::toString` with `"HH:mm"` fallback; on
       failure, emit `Genode::warning("clock.format: invalid 'BOGUS',
       falling back to HH:mm")` and accept the value (the panel will
       display the fallback; the operator is informed).
3. **Theme transport cap raise** (`theme_controller.cc:155`):
   - Change `Genode::String<2048>` to `Genode::String<8192>`.
   - Add the truncation warning block (see "Theme Transport Cap Raise").
   - Version-bump `sponge-de` and `sponge_themed` start nodes in the
     new `sponge-panel-config-sel4.run` and `sponge-de-themed-chrome.run`
     with `<config version="2"/>`; both log the version on construction.
4. **Extend `sponge-config-probe`** (run script + `pkg_seq_probe`):
   add HID steps covering the new keys per the failure-point-9 probe
   spec:

   | step | op | key | value | expect / expect_status |
   |---|---|---|---|---|
   | 1 | set | panel.height | 64 | (ok) |
   | 2 | get | panel.height | — | 64 |
   | 3 | set | panel.height | 12 | expect_status: error (out of range, <16) |
   | 4 | set | panel.height | 256 | expect_status: error (out of range, >128) |
   | 5 | set | panel.height | -1 | expect_status: error (parse fail) |
   | 6 | set | panel.visible_widgets | launcher | (ok) |
   | 7 | get | panel.visible_widgets | — | launcher |
   | 8 | set | panel.visible_widgets | launcher,clock | (ok) |
   | 9 | set | panel.visible_widgets | "" | expect_status: error (empty list) |
   | 10 | set | panel.visible_widgets | tray | expect_status: error (unknown token) |
   | 11 | set | clock.format | "HH:mm:ss" | (ok) |
   | 12 | get | clock.format | — | "HH:mm:ss" |
   | 13 | set | clock.format | "bogus" | (ok with warning + fallback to "HH:mm") |
   | 14 | set | launcher.sort_by | manual | (ok) |
   | 15 | set | launcher.sort_by | random | expect_status: error (unknown enum) |
   | 16 | list | — | — | expect: panel.height (all 7 keys present) |
   | 17 | list | — | — | expect: panel.visible_widgets |
   | 18 | list | — | — | expect: clock.format |
   | 19 | list | — | — | expect: launcher.sort_by |
   | 20 | get | no.such.key | — | expect_status: error (unknown key) |

5. **README registry update** (`repos/sponge/src/sponge_configd/README.md`
   lines 53–62): replace the 2-row table with the 7-row table. Add rows
   stating kernel tag (none — runs on both), live-reload
   (yes for all 4 new keys), boot-time caveats (none — all are
   live-reloadable).

**Files**: edit `repos/sponge/src/sponge_configd/main.cc`,
`repos/sponge/src/sponge_configd/README.md`,
`repos/sponge/src/sponge-de/theme/theme_controller.cc`,
`repos/sponge/src/sponge-de/theme/theme_loader.h`,
`repos/sponge/src/sponge-de/theme/theme_loader.cc` (new
`qt_format_well_formed`),
`run/sponge-config-probe.run`.

- **Category**: `deep`
- **Skills**: [`debugging`] (TDD red→green; validator failure-case
  characterization)
- **Depends On**: W0
- **Kernel tags**: linux (probe runs on base-linux for speed)
- **Acceptance Criteria**: 20-step probe (`config-seq-probe: PASS` —
  20 steps); `sponge-config-probe.run` exit 0; the new keys appear
  in the `vct config list` round-trip; the truncation warning is
  emitted when an 8193-byte theme is synthesized (covered by
  `tool/test_theme_payload_size.mojo` in W5).

### W2: Panel restyle migration (constructor-only state) + new `ConfigController`

**EXPECTED OUTCOME**: every constructor-only state that is now
live-reloadable via a configd key can be re-applied via `restyle()`
without a rebuild; a new in-sponge-de `ConfigController` bridges the
`sponge_configd` `config` ROM to Qt signals that the panel and
launcher consume; the Phase-10 panel probe
(`repos/sponge/src/test/sponge_de_probe/main.cc`) is extended with a
`panel-config` observe phase that asserts geometry deltas are
physically realizable (failure-point 15); live configd updates flow
to the panel without a restart.

1. **NEW in-sponge-de `ConfigController`** (this task adds a
   previously-absent component — correcting the prior draft's false
   premise that `sponge_de_main.cc` already consumes the configd
   `config` broadcast; today only `ThemeController` consumes the
   `theme` ROM via `sponge_themed`, a *different* report_rom slot).
   - Create `repos/sponge/src/sponge-de/config/config_controller.h` +
     `config_controller.cc`. Header (mirrors the ThemeController
     architecture):

     ```cpp
     class ConfigController : public QObject {
         Q_OBJECT
     public:
         /* Constructed in sponge_de_main.cc::Main, BEFORE the panel. */
         ConfigController(Genode::Env &env, QObject *parent = nullptr);
         /* Attached by Main after the panel is constructed. */
         void attach_panel(Sponge_DE::PanelWidget *panel);
         void attach_launcher(Sponge_DE::LauncherMenuView *launcher);
     signals:
         void panel_height_changed(unsigned h);
         void panel_visible_widgets_changed(QString list);
         void clock_format_changed(QString format);
         void launcher_sort_by_changed(QString sort);
     };
     ```

   - The new component requests one new ROM session: `"config"`
     (relayed by `report_rom` from `sponge_configd`'s `config`
     report). It uses a `Genode::Constructible<Attached_rom_dataspace>`
     + sigh, exactly mirroring the pattern at
     `configd/main.cc:125-128` (lz_model_rom pattern) so that
     scenarios without the configd wire still boot
     unchanged. **Activation is gated by `<config_daemon="yes"/>` in
     sponge-de's component config** (same parallel as `<theme
     source="themed"/>` for ThemeController at
     `theme_controller.cc:81-92`).
   - Marshals to the GUI thread via `QMetaObject::invokeMethod` on
     every configd sigh; rule codified in the new header's top
     comment block (failure-point 2 enforcement).
   - Distinguishes changes per key by hashing the parsed XML; no-op
     when a key's value is byte-identical to the previous broadcast
     (avoids re-entrant restyle loops).
   - Reuses the `_value_valid` rule from W1: a configd-set value that
     fails validation is dropped at the configd side (never reaches
     this component), so ConfigController trusts the broadcast and
     only applies it.

2. **New report_rom policy** required in every Phase-11 scenario
   that wires sponge-de:

   ```
   + policy | label: sponge-de -> config | report: sponge_configd -> config
   ```

   This is the only new policy added in W2; everything else routes
   through existing channels.

3. **New ROM session in sponge-de** (the absence of which is the
   reason this controller is new, not a refactor): a single
   `Attached_rom_dataspace("config")` opened conditionally inside
   `ConfigController`'s constructor. The sponge-de `target.mk` does
   not need to change; the session is opened via the constructor's
   `_env` member.
4. **Panel ctor-only → restyle migration** (`panel_widget.cc:52-62`):
   - Extract the layout construction into a private
     `void _build_layout(Theme::Theme const &)` method that takes the
     current theme and creates the layout child widgets (launcher
     toggle, Sponge DE title, stretch, clock label).
   - Move the `_clock_label` format application out of the
     QTimer timeout lambda into a `void _apply_clock_format(Theme const &,
     QString const &format)` private method that reads both the theme
     defaults and the per-instance `clock.format` from the
     `ConfigController` signal (held as a `_clock_format` member on
     `PanelWidget`).
   - `restyle()` at lines 97–101 now calls `_apply_style`, then
     `_apply_geometry(theme)` (new), then `_apply_layout(theme)` (new),
     then `_apply_visibility(QString)` (new), then
     `_apply_clock_format(theme, _clock_format)`, then `update()`.
   - `_apply_height(unsigned)` reads from `ConfigController`'s
     `panel_height_changed` signal; default value 28 (the
     `default.theme` line-217 value — NOT 29; correcting the prior
     draft's typo).
   - `_apply_visibility(QString)` parses the comma-separated list from
     `panel_visible_widgets_changed`, hides the launcher toggle when
     `"launcher"` is absent, hides the clock QLabel when `"clock"` is
     absent.
5. **GUI-thread marshal rule** (`panel_widget.h`): all four private
   `_apply_*` helpers must be called from a `QMetaObject::invokeMethod`
   marshalled callback (the `ThemeController` already marshals via
   `invokeMethod` at `theme_controller.cc:134–137`; the panel adds a
   parallel `applyConfig` slot for the configd-broadcast path).
   Add a comment block above `restyle()` stating the rule.
6. **Launcher margin/spacing migration** (`launcher_menu_view.cc:38-44`):
   - Extract layout construction into a `void _apply_layout(Theme const &)`
     private method. Constructor calls it; `restyle()` (added to
     `LauncherMenuView`) calls it. The popup's hardcoded metrics
     (width = screen/3, entry min-height = 50 px, padding 16 px 12 px)
     become theme-bridgeable but stay hardcoded in v1 — see W3 for
     the corresponding theme-key promotion.
7. **Launcher sort_by migration** (`launcher_menu_view.cc:144-156`):
   - `LauncherMenuView::repopulate()` reads `_sort_by` from a
     `QString` member, populated by the
     `launcher_sort_by_changed(QString)` signal from `ConfigController`
     (default `alpha`). When `manual`, the entries are inserted in
     the order the pkgd `installed` broadcast reports them
     (currently alphabetical; pkgd sorts by name); when `alpha`,
     they are resort alphabetically in the popup model. The comparator
     is named `launcher_alpha_less_than` so the test probe can assert
     the order.
8. **Demo title font ctor-only** (`sponge_de_main.cc:79-84`): **out
   of scope for v1.** `title_family` remains parsed-but-unused; the
   reason is documented in code:
   `// title_family stays parsed-but-unused in Phase 11; the demo title
    font is set at construction. The Phase-12 restyle migration is
    tracked in docs/09-roadmap.md §11 follow-ups. (See Phase-11 plan
    W2 §8.)`
9. **Panel-config probe** (`repos/sponge/src/test/sponge_de_probe/
   main.cc` — corrected path; `test/` is a sibling of `sponge-de/`).
   Add a new observe phase `panel-config` (Phase-10 has `input`,
   `panel`, `launch`). **Capture regions and asserted deltas are
   chosen so every delta is physically realizable — failure-point
   15 / 3 enforcement:**

   | Sub | configd write | Capture region | Rationale (what physically moves) | Asserted delta |
   |---|---|---|---|---|
   | P1 | `panel.height=64` | launcher-toggle sub-rect `(0, 0, 48, h_toggle)` | Per `panel_widget.cc:61–62` the toggle height is `panel_height - 2*gap`; growing the panel grows the toggle height visibly (28 → 64; toggle 24 → 60). The launcher-toggle bg is `accent` (different from `panel_bg=#1e1e2e` and from nitpicker `#1e1e2e`), so its vertical extent is detectable via a non-background pixel run. | `toggle_bottom_y` moves by documented delta; sample-row non-background fraction in the toggle's column-30 scan increases |
   | P2 | `panel.height=28` | launcher-toggle sub-rect | Reverts | `toggle_bottom_y` returns to baseline; toggle-height scan returns to baseline non-background rows |
   | P3 | `panel.visible_widgets=launcher` | clock sub-rect `(W-clock_w, 0, clock_w, h_panel)` | Hiding the clock replaces the glyph-row non-background fraction with `panel_bg` | clock-sub-rect non-background fraction drops to `< 0.02` (cursor blink of zero width is excluded by the empty `_clock_label` test) |
   | P4 | `panel.visible_widgets=clock` | launcher-toggle sub-rect | Hiding the toggle | toggle sub-rect non-background fraction drops to `< 0.02` |
   | P5 | `panel.visible_widgets=clock,launcher` | both sub-rects | Both visible again | both sub-rects non-background fraction `> 0.10` |
   | P6 | `clock.format="HH:mm:ss"` | clock sub-rect | 5 → 8 glyphs | clock-sub-rect width of non-background pixels increases by ≥ 30% (verified by horizontal scan; HH:mm=5 monospace ~5×6 px ≈ 30 px, HH:mm:ss ≈ 48 px) |
   | P7 | `panel.visible_widgets=""` (validator rejects) | broadcast ROM read | Broadcast must NOT be regenerated | `vct config get panel.visible_widgets` still returns the previous value (e.g. `"clock,launcher"`) |

   **Forbidden in this phase** (failure-point 15 / 3 enforcement):
   do NOT assert deltas against the panel-background / nitpicker-background
   boundary (they share `#1e1e2e`); do NOT assert the demo-window top
   edge moves with `panel.height` (the demo domain is nitpicker-owned,
   per `sponge_de_probe/main.cc:137–138` documented gotcha; the
   demo-window position is `theme`-independent).

   Final marker: `sponge-de-probe: phase panel-config PASS`.

**Files**: create `repos/sponge/src/sponge-de/config/
config_controller.{h,cc}`, `repos/sponge/src/sponge-de/config/README.md`;
edit `repos/sponge/src/sponge-de/panel/panel_widget.{h,cc}`,
`repos/sponge/src/sponge-de/launcher/launcher_menu_view.{h,cc}`,
`repos/sponge/src/sponge-de/sponge_de_main.{h,cc}`,
`repos/sponge/src/test/sponge_de_probe/main.cc` (corrected path;
new phase), `sponge-de/target.mk` only if a new compile unit needs
explicit listing (likely yes for `config_controller.cc`).

- **Category**: `deep`
- **Skills**: [`debugging`]
- **Depends On**: W1
- **Kernel tags**: linux (probe runs on base-linux; the seL4
  counterpart is `run/sponge-panel-config-sel4.run` in W5)
- **Acceptance Criteria**: `sponge-de-probe: phase panel-config PASS`
  on base-linux (7 subphases, every asserted delta physically
  realizable per the table above); the new `ConfigController` logs
  its `applied_config` summary on every broadcast change; all 4 keys
  apply via Qt signals without restart; the GUI-thread marshal rule
  is documented in `panel_widget.h` + `config_controller.h` and
  enforced by a follow-up PR review checklist.

### W3: Theme surface expansion + new shipped themes

**EXPECTED OUTCOME**: `docs/10-theme-format.md` documents the expanded
surface (no duplicate keys per D2.2; 2 new themes); `dark.theme` and
`compact.theme` ship in `repos/sponge/src/sponge-de/themes/` and
pass the `sponge-theme.run` 3-theme probe plus per-key pixel diff;
the theme_probe is extended to assert the new key behaviors.

1. **`docs/10-theme-format.md` updates** — corrected to remove the
   duplicate `decor.title_family`/`decor.title_size` keys that the
   prior draft added in §4.5 (failure-point 17 enforcement).
   - §4.2 `[colors]`: add `error_text`, `success_text`, `warning_text`
     (the `error`/`success`/`warning` keys are split semantically into
     *bg* vs *text* — current keys become *bg*). Existing
     `error`/`success`/`warning` rows are renamed to `error_bg` /
     `success_bg` / `warning_bg` with a "Deprecated alias: same
     value as `error_bg`" row. Removal in Phase 12 with deprecation
     note.
   - §4.4 `[layout]`: add `panel.popup_width` (int, default 341 =
     1024/3) and `panel.popup_entry_min_height` (int, default 50)
     as **documented no-ops** — parsed, but the values are not yet
     routed to `LauncherMenuView`. Decided to keep hardcoded
     metrics in v1 (the panel-config scenario uses the existing
     hardcoded values; Phase-12 promotion requires the W2
     construction migration).
   - §4.5 `[window]` (footnote only): note that the decorator's
     title-bar font comes from the vendored upstream
     `theme/font.tff` asset in `decor.tar` (literal asset, not a
     theme key); there is no `decor.title_family` /
     `decor.title_size` key — those are already in §4.3 `[fonts]`.
2. **Shipped themes**: `dark.theme` + `compact.theme`:
   - `dark.theme`: palette differs from `default.theme` (Catppuccin
     Mocha → Catppuccin Macchiato, with `panel_bg=#181825`,
     `window_bg=#1e1e2e`, `accent=#89b4fa`). Font section unchanged
     (no new fonts in Phase 11).
   - `compact.theme`: both palette AND layout differ. `panel_height=24`
     (smaller than default **28** — correcting the prior draft's
     "29 px" typo to match `default.theme` line 217), `padding=4`,
     `margin=2`, `launcher_width=32`. Font section: `default_size=10`.
     This exercises the geometry surface (smaller geometry, smaller
     font) producing a measurable pixel diff in the panel region.
   - Both themes carry `version = 1` (no format change).
   - Neither theme carries `panel_height` / `panel_position` (per
     D2.2 — clean separation). The `panel_height` key in `compact.theme`
     is intentionally left in the theme file as a documented no-op
     for v1; the actual Phase-11 height comes from `configd` or
     default **28**. (Phase-12 promotion: the theme key becomes the
     source of truth again, with configd as override.)
3. **Loader extension** (`theme_loader.{h,cc}`) — corrected to omit
   the prior draft's `decor.title_family`/`decor.title_size` parser
   additions (failure-point 17 enforcement):
   - Add `panel.popup_width` and `panel.popup_entry_min_height`
     parsers (default = current hardcoded values).
   - Add `bool qt_format_well_formed(char const *format)` (helper
     for W1's `FormatString` validator kind).
   - Add `error_text` / `success_text` / `warning_text` parsers.
   - Add `error_bg` / `success_bg` / `warning_bg` parsers (default =
     the existing `error`/`success`/`warning` colors; the old keys
     become alias getters).
4. **theme_probe extension**
   (`repos/sponge/src/test/theme_probe/main.cc`; consumed by
   `run/sponge-theme.run`): a 3-theme probe instead of 2:
   - **Step 1**: write `theme.active=dark` via vct CLI; assert
     `theme.active` broadcast carries `dark`; assert the theme
     ROM opens with `dark.theme` content; assert `applied_theme`
     report carries `dark`; Capture-pixel-diff the panel region
     (0,0,width,28) against the `default` baseline; assert
     `delta > N_dark` (N_dark documented in the probe).
   - **Step 2**: write `theme.active=compact`; assert ROM
     `compact.theme`; Capture-panel-pixel-diff; assert
     `delta > N_compact` AND the panel height is 24 px (Capture
     scanline for the topmost non-background row).
   - **Step 3**: write `theme.active=light`; assert ROM
     `light.theme`; Capture-panel-pixel-diff; assert
     `delta < N_default` (light is closer to default than dark).
   - **Step 4**: write `theme.active=does-not-exist`; assert the
     `applied_theme` report remains unchanged (the upstream
     `sponge_themed` name-dedup rule preserves the previous theme).
   - `theme-probe: PASS`.

**Files**: edit `docs/10-theme-format.md`, create
`repos/sponge/src/sponge-de/themes/dark.theme`,
`repos/sponge/src/sponge-de/themes/compact.theme`,
edit `repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}`,
edit `repos/sponge/src/test/theme_probe/main.cc`,
edit `run/sponge-theme.run`. (The `default.theme` and `light.theme`
files are NOT modified — they're the Phase-10 baseline.)

- **Category**: `deep`
- **Skills**: [`debugging`] (pixel-diff empirical calibration)
- **Depends On**: W1, W2 (the panel-config probe must be done first so
  the expanded theme keys can be proven against the restyled panel)
- **Kernel tags**: linux (`sponge-theme.run` is base-linux)
- **Acceptance Criteria**: `theme-probe: PASS` (4 steps, 3 themes),
  all 4 shipped themes are ≤ 8192 bytes (asserted by the W5
  `tool/test_theme_payload_size.mojo`), `dark.theme` differs from
  `default.theme` in `panel_bg` + at least 2 other color keys,
  `compact.theme` differs from `default.theme` in `panel_height` +
  at least 1 other layout key + at least 1 font key.

### W4: themed_decorator bridge + theme tar authoring

**EXPECTED OUTCOME**: A new `sponge_decorator_bridge` component
regenerates the themed_decorator's `<policy color="..."/>` block on
`theme.active` change (and on the same for the live `theme` ROM sigh).
The tar identity is **boot-time only** and is built by
`tool/decor_assets.mojo`, which **re-vendors the upstream
`theme/font.tff` byte-for-byte** (no host-side TTF→TFF conversion — see
failure-point 16 enforcement). The Phase-10 drag scenario must pass
with themed_decorator substituted in. No vendored tree edits.

1. **New component `sponge_decorator_bridge`**:
   - `repos/sponge/src/sponge_decorator_bridge/`
     - `main.cc` — single Genode component, signal-driven, no Timer.
     - `target.mk` — `TARGET := sponge_decorator_bridge`, `SRC_CC :=
       main.cc`, `LIBS := base`.
     - `README.md` — channel ownership (single writer for the
       decorator `<policy>` config ROM), minimum-privilege session
       list (ROM "theme" + Report "decorator_config"; minimum-privilege
       per session, AGENTS.md §1.2).
   - Behavior: on `theme.active` change → bind the `theme` ROM (via
     `sponge_themed`'s `theme` ROM, which is the same channel
     `ThemeController` reads) → parse the `[colors]` section's
     `panel_bg` → emit a `<config>` to a `decorator_config` report
     with `<policy label_prefix="..." decoration="yes" motion="1"
     color="..."/>` (failure-point 18 enforcement: `motion="1"`,
     an unsigned-parsed attribute, NOT `motion="yes"`).
   - Tie into the existing configd → sponge_themed → theme ROM
     chain: the bridge reads the SAME `theme` ROM sponge-de reads.
   - **Failure mode (D2.3(d), not (c))**: the bridge does NOT
     re-extract the tar; it only updates the policy color. Full
     decorator re-skin (different frame texture, different button
     glyphs) is a Phase-12 concern with a patch-ledger entry.
2. **Theme tar authoring** (`tool/decor_assets.mojo` + bash launcher) —
   corrected to drop the prior draft's nonexistent `ttf2tff` step
   (failure-point 16 enforcement):
   - Reads the `metadata` text (a single file in
     `tool/decor_assets_data/metadata.txt`).
   - Generates four PNGs sized to the metadata's declared title-bar
     height, closer button rect, maximizer button rect, and 9-slice
     frame geometry.
   - PNGs are generated by `mogrify` / `convert` (host tool,
     ImageMagick) when present; otherwise by a checked-in PNG asset
     set under `tool/decor_assets_data/pngs/` plus a Mojo-side
     resize-and-pad step.
   - **Re-vendors the upstream `theme/font.tff` byte-for-byte**
     by copying from
     `genode/repos/gems/src/app/themed_decorator/theme/font.tff`
     (the same asset the upstream sample tar ships). **No TTF→TFF
     conversion is performed.** The vendored-tree path is the
     single source of truth; when the host cannot reach it (e.g.
     fresh checkout without an upstream Theme-decorator build), the
     script falls back to an empty-byte TFF placeholder (the
     decorator still boots; the title bar reverts to the Genode
     default font) and the tool logs:
     `decor_assets: warning — falling back to empty font.tff
      (vendored path missing); title bar reverts to default font`.
   - Assembles the five entries (`default.png`, `closer.png`,
     `maximizer.png`, `font.tff`, `metadata`) into
     `decor.tar` (host-side `tar(1)` is fine; the Genode VFS `<tar>`
     loader handles a plain uncompressed tar).
   - Output: `genode/build/x86_64/var/sponge/decor.tar` (path inside
     the build dir that the run script picks up via VFS).
   - Manual step documented in `docs/08-development.md` §"themed_decorator
     assets": "to redesign the chrome, edit
     `tool/decor_assets_data/metadata.txt` and re-run
     `./tool/decor_assets.sh`; the new `decor.tar` is staged by
     `run/sponge-de-themed-chrome.run` on the next build. To
     customize fonts, edit the upstream `theme/font.tff` in the
     vendored Genode tree and re-run; the asset is not regenerated."
3. **Run script scaffold** (`run/sponge-de-themed-chrome.run`):
   - Topology: `sponge-wm-qmp.run` minus the stock `decorator` child
     + `themed_decorator` (binary from `genode/repos/gems/recipes/pkg/
     themed_decorator/runtime`) + `sponge_decorator_bridge` + the
     `decor.tar` via VFS `<tar name="decor.tar"/>` + the panel + demo
     + pkg_gui_demo + a regenerated layouter `<assign
     label_prefix="pkg_runtime" target="screen" .../>` rule.
   - Decorator config (parsed by `genode/repos/gems/src/app/
     themed_decorator/config.h:65`; `motion` is `unsigned`):
     `<config><policy label_prefix="..." decoration="yes"
     motion="1" color="#1e1e2e"/></config>` (the color is replaced
     by the bridge on `theme.active` change; `motion="1"` because
     we want window dragging wired).
   - Init sub-init: `decorator` shares the same demo domain as
     `wm`; `pkg_runtime` Gui route gains `+ child wm` (mirrors the
     Phase-10 W3 fix).
   - Gates: fb `using 1024x768` → usb_hid `POINTER` → wm-launched
     `pkg_gui_demo` window shown → themed_decorator's
     `decorator_margins` report (assertion: top=20, sides=4,
     bottom=4 — identical to the stock `decorator` defaults, the
     Phase-10 drag regression gate) → QMP drag → `wm-probe: PASS`.
   - **Mandatory**: the demo window's chrome must visually take its
     colors from the theme palette (the bridge's policy color =
     `panel_bg`). Capture-pixel-diff the title-bar region against the
     stock-decorator baseline; assert `delta > 0` (the themed chrome
     is colored, not the default).
4. **README / evidence updates**:
   - `repos/sponge/src/sponge_decorator_bridge/README.md`: channel
     ownership table, minimum-privilege session list, fault-tolerance
     (the bridge is not on the boot critical path; if it fails,
     layouter / decorator still boot with the default policy color).
   - `repos/sponge/src/sponge-de/config/README.md`: channel
     ownership table for the NEW ConfigController (single ROM
     consumer of the `config` report), minimum-privilege session
     list (one `ROM` session for `"config"`), and the
     `<config_daemon="yes"/>` activation flag (mirrors the
     `<theme source="themed"/>` precedent).
   - `docs/evidence/task-4-phase11-themed-chrome.md` + `.log`
     capturing the per-gate marker + the QMP drag + the title-bar
     pixel diff.

**Files**: create `repos/sponge/src/sponge_decorator_bridge/`
(`main.cc`, `target.mk`, `README.md`); create
`repos/sponge/src/sponge-de/config/`
(`config_controller.h`, `config_controller.cc`, `README.md`); create
`tool/decor_assets.mojo`, `tool/decor_assets.sh`,
`tool/decor_assets_data/metadata.txt`,
`tool/decor_assets_data/pngs/*.png` (checked-in fallback asset set);
create `run/sponge-de-themed-chrome.run`; potentially edit
`run/sponge-wm-qmp.run` if the staged `decorator` binary is shared
(no edit expected — the new scenario stands alone).

- **Category**: `deep`
- **Skills**: [`debugging`, `mojo-syntax`] (the host tool is Mojo; the
  `mojo-syntax` skill is required per AGENTS.md §3.5)
- **Depends On**: W1 (the bridge reads `theme` ROM regardless of W2
  status); W2 (Phase-10 drag regression gate still passes — no
  scenario overlap)
- **Kernel tags**: sel4 (the new scenario is seL4+QMP, the Phase-10
  drag regression gate)
- **Acceptance Criteria**: `sponge-de-themed-chrome.run` prints all
  gates + `wm-probe: PASS` + `Run script execution successful.`; the
  Phase-10 drag scenario (`sponge-wm-qmp.run`) still passes
  unchanged (the bridge is a new component, not a swap-in); the
  title-bar pixel diff is measurable; `decorator_margins` matches
  the stock decorator defaults.

### W5: Scenario + probe coverage

**EXPECTED OUTCOME**: 3 new scenarios + 1 extended scenario + 1 host
tool land, each with specific data + specific gates + specific PASS
markers. The TDD discipline is preserved: each scenario is run first
(red), then the code change makes it green.

1. **`run/sponge-panel-config.run`** (base-linux, kernel-agnostic):
   - The `sponge-de-test.run` topology + `sponge_configd` +
     `sponge_themed` + the new keys wired + the new report_rom
     policy for `sponge-de -> config` + `<config config_daemon="yes"/>`
     + `<config version="2"/>` on sponge-de.
   - `sponge_de_probe` is instantiated with `<config phases="panel-config"/>`.
   - Gates: `sponge-de-probe: phase panel-config PASS` (7 subphases
     from W2 step 9, every delta physically realizable).
   - Acceptance: exit 0 + the probe marker + the per-subphase
     assertion log.
2. **`run/sponge-panel-config-sel4.run`** (base-sel4):
   - The `sponge-de-sel4-interactive.run` topology + `sponge_configd`
     + `sponge_themed` + the new keys wired + the new report_rom
     policy + `run/qmp.inc` QMP socket.
   - `sponge_de_probe` is extended with a `panel-config` phase that
     emits `QMP-TARGET click <panel_y> <panel_x>` on the launcher
     toggle (when `panel.visible_widgets=clock` only) — covering the
     "panel toggles from host input" criterion.
   - Gates: fb → usb_hid → QMP click → `sponge-de-probe: phase panel-config PASS`.
   - Acceptance: exit 0 + the probe marker + the QMP choreography
     log.
3. **`run/sponge-theme.run`** extended (base-linux): the 3-theme
   probe from W3 step 4. Acceptance: `theme-probe: PASS` (4 steps).
4. **`run/sponge-de-themed-chrome.run`** (base-sel4): W4's scenario.
   Acceptance: per W4.
5. **`tool/test_theme_payload_size.mojo`** (host): a small Mojo
   script that reads each of the four shipped themes'
   `repos/sponge/src/sponge-de/themes/*.theme`, asserts each is
   ≤ 8192 bytes, and exits non-zero if any exceed. The script has
   a daemon-side counterpart: a `_assert_transport_cap` check in
   `theme_controller.cc` that emits `Genode::warning("theme payload
   truncated at 8192 bytes")` when the ROM sink is exactly at the cap.
   The two together close the §3 #1 trap.

**Files**: create `run/sponge-panel-config.run`,
`run/sponge-panel-config-sel4.run`, `tool/test_theme_payload_size.mojo`,
`tool/test_theme_payload_size.sh` (bash launcher); extend
`run/sponge-theme.run`.

- **Category**: `deep`
- **Skills**: [`debugging`]
- **Depends On**: W2, W3, W4
- **Kernel tags**: linux (panel-config + theme-3theme); sel4
  (panel-config-sel4 + themed-chrome); host (theme payload size)
- **Acceptance Criteria**: 4 new scenarios green; host tool exits 0;
  the 4 Phase-10 regression scenarios (sponge-de-test,
  sponge-config-probe, sponge-wm-qmp, sponge-theme) still pass
  unchanged on their documented kernels.

### W6: Docs sync + evidence + full regression

**EXPECTED OUTCOME**: all four Phase-11 checkboxes flipped in
`docs/09-roadmap.md` §10 with per-criterion traceability; the
expanded theme surface is documented; the new components are
recorded; the per-task evidence is captured; the regression list
is split into NEW Phase-11 scenarios (must pass with new behavior)
and Phase-10 regression gates (must pass UNCHANGED from W0 baseline).

1. **Docs sync (AGENTS.md §5.4 — code and docs land together)**:
   - `docs/09-roadmap.md` §10 Phase 11: flip all four checkboxes
     with scenario traceability (criterion → scenario → PASS marker).
   - `docs/10-theme-format.md`: update §4.2 `[colors]`, §4.4 `[layout]`,
     add the §4.5 `[window]` footnote (decorator font is a literal
     asset, not a theme key — failure-point 17).
   - `run/README.md`: add entries for `sponge-panel-config.run`,
     `sponge-panel-config-sel4.run`, `sponge-de-themed-chrome.run`;
     extend `sponge-theme.run` entry with the 3-theme probe. Update
     the "Planned additions" section.
   - `docs/08-development.md`: new subsection "Phase-11 DE
     customization" — configd new keys, theme surface expansion,
     ConfigController + report_rom policy, themed_decorator bridge,
     manual escape hatches (edit run script for boot-time position,
     edit theme file for theme keys, edit
     `tool/decor_assets_data/metadata.txt` for chrome), Mojo tool
     usage (`decor_assets.mojo`, `test_theme_payload_size.mojo`).
   - `docs/11-environment.md`: add a **patch-ledger candidate** row
     for "Phase-12: themed_decorator live tar reload" — log the
     intent (option 2.3(c)), the location in the vendored tree
     (`genode/repos/gems/src/app/themed_decorator/`), the why
     (assets cached in statics — no live reload), and the drop
     procedure ("delete this row once upstream absorbs the patch").
     No patches ship in Phase 11.
   - `repos/sponge/src/sponge_configd/README.md`: update the
     registry table to 7 rows.
   - `repos/sponge/src/sponge-de/README.md`: current status section
     mentions "Phase 11" for the panel/new-keys + ConfigController
     sections.
   - `repos/sponge/src/sponge-de/config/README.md`: written in W2/W4
     step 4.
   - `repos/sponge/src/sponge_decorator_bridge/README.md`: written
     in W4 step 4.
   - `README.md`: update the Phase-10 → Phase-11 status note (the
     bullet "Phase 11 is done" once W6 closes).
   - `docs/evidence/`: create `phase11-index.md` (same table format
     as `phase10-index.md`) + per-task artifacts
     `task-<n>-phase11-{name}.{md,log}` for W0–W5 + the suite results
     for W6. Do NOT reference `.omo/` paths.
2. **Full regression re-run** — failure-point 8 corrected list
   (deduped, separated into two classes):

   **A. NEW Phase-11 scenarios (must pass with the new behavior
   introduced in Phase 11):**

   | Scenario | Kernel | Phase-11 source | Acceptance marker |
   |---|---|---|---|
   | `sponge-panel-config.run` | linux | W5 step 1 | `sponge-de-probe: phase panel-config PASS` |
   | `sponge-panel-config-sel4.run` | sel4 | W5 step 2 | `sponge-de-probe: phase panel-config PASS` + QMP click log |
   | `sponge-theme.run` | linux | W5 step 3 | `theme-probe: PASS` (4 steps including 3 themes + not-found fallback) |
   | `sponge-de-themed-chrome.run` | sel4 | W5 step 4 | `wm-probe: PASS` + title-bar pixel diff + `decorator_margins` match |

   **B. Phase-10 / earlier-phase regression gates (must pass
   UNCHANGED — same per-scenario scenario file, same per-scenario
   PASS marker as captured in `docs/evidence/task-0-phase11-baseline.log`):**

   | Scenario | Kernel | Phase-10 source | Required marker (per W0 baseline) |
   |---|---|---|---|
   | `sponge-de-test.run` | linux | Phase 3 | `sponge-de-probe: PASS` (existing) |
   | `sponge-wm-qmp.run` | sel4 | Phase 10 W3 | `wm-probe: PASS` (drag +100,+100) — **THE DRAG REGRESSION GATE** |
   | `sponge-de-sel4-interactive.run` | sel4 | Phase 10 W1+W2 | `sponge-de-probe: PASS` (3 phases) |
   | `sponge-config-probe.run` | linux | Phase 5a (now extended in W1) | `config-seq-probe: PASS` (now 20 steps) |
   | `sponge-launcher.run` | linux | Phase 5 | `launcher-probe: PASS` (current) |
   | `sponge-alpha.run` | sel4 | Phase 7 | `alpha-probe: PASS` |
   | `sponge-launch.run` | sel4 | Phase 7 | `launch-probe: PASS` |
   | `sponge-pkg-gui.run` | sel4 | Phase 7 | `pkg-gui-probe: PASS` |
   | `sponge-pkg-lifecycle.run` | linux | Phase 7 | `lifecycle-probe: PASS` |
   | `sponge-terminal.run` | sel4 | Phase 7 | `terminal-probe: PASS` |
   | `sponge-textedit.run` | sel4 | Phase 7 | `textedit-probe: PASS` |
   | `sponge-wm.run` | linux | Phase 5 | `wm-probe: PASS` |
   | `sponge-terminal-qmp.run` | sel4 | Phase 10 W4 | `terminal-probe: PASS` |
   | `sponge-textedit-qmp.run` | sel4 | Phase 10 W5 | `textedit-probe: PASS` |

   **Phase-10-recorded known issues (BLOCKED-pre-existing per
   `docs/evidence/task-1-phase10-interactive.md` §"Step 5 Regression";
   NOT a Phase-11 regression):**

   | Scenario | Kernel | Status |
   |---|---|---|
   | `sponge-de-test.run` | sel4 | BLOCKED-pre-existing (Qt6 staging ordering; out of Phase-11 scope) |
   | `sponge-launcher.run` | sel4 | BLOCKED-pre-existing (same root cause) |

   Record all results in `phase11-index.md`; any red scenario in
   column A is a Phase-11 regression to root-cause and fix before
   the roadmap checkboxes land; any red scenario in column B is
   a Phase-10 regression and is an immediate halt-and-report
   (the Phase-10 green gate is the Phase-11 starting point).
3. **PR body numbers per AGENTS.md §5.1** (convenience proven in
   code):
   - "panel customization in 1 command (`vct config set panel.height 64`)
     and 0 reboots for the 4 new keys; 0 vct code changes; theme
     switch in 1 command (`vct theme apply dark`) with live repaint;
     themed chrome in 1 image rebuild (`sponge-de-themed-chrome.run`),
     live color updates flow without restart".
   - "Automation default: `vct config set panel.height 64` repaints
     the panel; control escape hatch: `vct config set panel.position
     top` + edit run script + reboot (boot-time only, documented)".
   - "Manual escape hatch: every theme file is hand-editable INI;
     `tool/decor_assets.sh` regenerates `decor.tar` from the
     `metadata` text + the vendored upstream `font.tff`".

**Files**: edit `docs/09-roadmap.md`, `docs/10-theme-format.md`,
`run/README.md`, `docs/08-development.md`, `docs/11-environment.md`,
`repos/sponge/src/sponge_configd/README.md`,
`repos/sponge/src/sponge-de/README.md`, `README.md`; create
`docs/evidence/phase11-index.md`,
`docs/evidence/task-<n>-phase11-{name}.{md,log}` (per task).

- **Category**: `writing`
- **Skills**: []
- **Depends On**: W2, W3, W4, W5
- **Acceptance Criteria**: all listed scenarios in column A green
  with new behavior; all listed scenarios in column B green
  UNCHANGED from W0 baseline (verified by exact-marker match); the
  Phase-11 roadmap checkboxes flipped with traceability; no doc
  references to `.omo/`; `run/README.md` updated; the Phase-12
  patch-ledger candidate row recorded.

## Must NOT Have (Metis exclusions + Momus corrections, verbatim)

The following are explicitly **excluded from Phase 11** and deferred
to Phase 12+:

- **System tray / taskbar applets** — `panel.visible_widgets` enum is
  `{clock, launcher}` only. Adding a tray slot is Phase 12+.
- **Icon loading** — `icon_size` in theme remains parsed-but-unused.
  No icon assets shipped.
- **Font shipping** — no new TTF files in `var/distfiles` for Phase
  11. The decorator's `font.tff` is the vendored upstream asset,
  **copied byte-for-byte**, not regenerated.
- **WM stack in `sponge-de-sel4-interactive.run`** — topology change
  deferred (D2.6).
- **themed_decorator live asset re-skin** — option 2.3(c) is a
  Phase-12 patch-ledger entry, not Phase 11.
- **Per-user themes / theme marketplace / theme install** — single
  global `theme.active`; shipped themes only.
- **Animation / transitions on theme switch** — instant repaint only.
- **Multiple panel instances** — single panel.
- **Custom CSS-like styling** — theme format stays INI.
- **Configd state persistence across reboot** — configd is in-memory;
  persistence is a separate concern.
- **Hot-reload of launcher entry list** — entries are build-time.
  Phase 11 adds sort, not entry source modification.
- **Window border width as live-reloadable** — `window_border` is a
  parsed-but-unused theme key in v1; it's a *boot-time* theme value
  when promoted (Phase 12+).
- **Per-widget visibility animations** — instant hide/show only.
- **Theme inheritance / layered themes** — flat single-file themes.
- **Launcher entry source changes** — `sponge_pkgd`'s `installed`
  broadcast is not modified; `launcher.group_by` is deferred.
- **Demo title font restyle migration** — `title_family` is
  parsed-but-unused in Phase 11; the ctor-only font in
  `sponge_de_main.cc:79–84` is NOT migrated (tracked as a Phase-12
  follow-up).
- **patched vendored Genode tree** — no `genode/` commits in Phase 11.
  The Phase-12 patch-ledger entry is the only durable record of the
  deferred live-tar-reload patch.
- **configd regenerating run scripts** — explicit forbidden action per
  §3 #12. The manual escape hatch is "edit the run script per
  `docs/08-development.md` and reboot".
- **TTF→TFF host conversion tool** — `ttf2tff` does not exist in the
  vendored tree. `tool/decor_assets.mojo` re-vendors the upstream
  `theme/font.tff` byte-for-byte; do NOT introduce a host-side
  TTF→TFF build target in Phase 11. (Failure-point 16 enforcement.)
- **decor.title_family / decor.title_size theme keys** — these
  duplicate the existing `[fonts] title_family` / `title_size`
  keys (`docs/10-theme-format.md` §4.3) with identical defaults and
  violate D2.2 clean separation; and the
  "themed_decorator consumes theme keys for font" claim is false —
  the decorator reads literal `font.tff` bytes via `Tff_font`. Do
  NOT add these keys to §4.5. (Failure-point 17 enforcement.)
- **Panel-config probe asserting deltas against unrealizable
  geometry** — the panel/nitpicker background is `#1e1e2e` by
  design in both, so panel rect vs nitpicker rect is undetectable;
  the demo-window position is nitpicker-domain-owned and is
  `theme`-independent (per `sponge_de_probe/main.cc:137-138`). The
  panel-config probe MUST assert against geometry that physically
  changes with configd writes (launcher-toggle height
  `panel_height - 2*gap` per `panel_widget.cc:61-62`, clock glyph
  count, etc.). (Failure-point 15 enforcement.)
- **`motion="yes"`** in themed_decorator config — `motion` is
  parsed as `unsigned` in `genode/repos/gems/src/app/themed_decorator/
  config.h:65`. Use `motion="1"` or omit (default 0). (Failure-point
  18 enforcement.)

Any planner deviation must be justified in an open question in the
relevant `docs/` file.

## Commit Strategy

One logical change per commit (conventional commits). Land in
dependency order:

1. `docs(theme-format): document v1 theme surface (no-op keys,
   error_text/success_text/warning_text split, decor.font asset
   footnote)` + `docs(evidence): task-0 phase11 baseline log`.
2. `feat(configd): register 4 new keys (panel.height, panel.visible_
   widgets, clock.format, launcher.sort_by); extend validator with
   UintRange, EnumList, FormatString kinds` + `docs(configd): 7-row
   registry table; README updates`.
3. `test(config-probe): extend pkg_seq_probe with 20-step new-key
   matrix (set/get/list/error)` + `feat(run): update
   sponge-config-probe HID steps`.
4. `feat(theme-de): raise theme transport cap to 8192 with truncation
   warning; bump component version to 2`.
5. `feat(sponge-de): add ConfigController — configd `config` ROM
   → Qt signal bridge (panel.heightChanged, panel.visibleWidgets
   Changed, clock.formatChanged, launcher.sortByChanged); add
   report_rom policy; gate activation on <config_daemon="yes"/>`
   + `docs(sponge-de): config/README.md (channel ownership,
   minimum-privilege session list)`.
6. `refactor(panel-de): migrate ctor-only state to restyle() paths
   (panel layout, launcher button size, clock format); wire
   ConfigController signals to PanelWidget + LauncherMenuView` +
   `feat(panel-de): support panel.height / panel.visible_widgets /
   clock.format / launcher.sort_by live reload via ConfigController`.
7. `test(de-probe): add panel-config observe phase (7 subphases,
   assert realizable geometry deltas)` + `feat(run): add
   sponge-panel-config (base-linux) + sponge-panel-config-sel4
   (base-sel4)`.
8. `feat(theme): ship dark.theme + compact.theme (palette + layout +
   font differences)` + `test(theme-probe): 3-theme pixel diff +
   theme-not-found fallback` + `feat(run): extend sponge-theme with
   3-theme probe`.
9. `chore(tool): add decor_assets.mojo + test_theme_payload_size.mojo`
   (Mojo host tools, per AGENTS.md §3.5; decor_assets re-vendors
   upstream font.tff byte-for-byte — NO host-side TFF conversion).
10. `feat(decorator-bridge): add sponge_decorator_bridge component
    (live color policy via layouter config ROM; motion="1" — unsigned
    attribute)` + `docs(environment): patch-ledger candidate row for
    Phase-12 themed_decorator live tar reload`.
11. `feat(run): add sponge-de-themed-chrome (base-sel4, themed_decorator
    drop-in + Phase-10 drag regression gate)` +
    `docs(evidence): task-4 phase11 themed-chrome log`.
12. `docs(roadmap): close phase 11 — checkboxes with per-criterion
    traceability, run/README, evidence index, regression results
    (NEW vs Phase-10 regression gates deduped + separated), PR body
    numbers`.

TDD discipline per workstream: run the scenario first (red), make
the minimal change, re-run to green; every gate is a bounded PASS
marker — never a silent hang.

## Success Criteria

1. **C1 (panel customization)**: `run/sponge-panel-config.run`
   (linux) + `run/sponge-panel-config-sel4.run` (sel4) pass; the
   7-subphase `panel-config` probe covers all 4 new keys plus
   error paths, with every asserted delta physically realizable
   per the W2 step 9 table; `vct config list` shows all 7 keys.
2. **C2 (expanded theme surface + new themes)**: `run/sponge-theme.run`
   (extended) passes 4 steps (3 themes + not-found fallback);
   `dark.theme` + `compact.theme` ship; `tool/test_theme_payload_size.mojo`
   exits 0 on all 4 themes; `dark.theme` differs in palette from
   `default.theme`; `compact.theme` differs in layout + font.
3. **C3 (themed chrome)**: `run/sponge-de-themed-chrome.run` (sel4)
   prints all gates + `wm-probe: PASS` + title-bar pixel diff;
   `decorator_margins` matches stock decorator defaults;
   `sponge-wm-qmp.run` (the Phase-10 drag regression gate) still
   passes unchanged.
4. **C4 (documented + scenario-verified)**: `docs/09-roadmap.md`
   §10 checkboxes flipped with traceability; run/README.md updated;
   `docs/evidence/phase11-index.md` + per-task artifacts present;
   no `.omo/` references in durable docs.
5. **Full regression**: ALL scenarios in W6 §2 column A green with
   new behavior; ALL scenarios in column B green UNCHANGED from W0
   baseline (same PASS markers, same scenario files).
6. **No scope violations**: vendored `genode/` tree untouched;
   `ttf2tff` not introduced; no duplicate `decor.title_family`/`decor.
   title_size` keys; `motion="1"` (not `"yes"`) in the decorator
   config; `ConfigController` is the new in-sponge-de consumer for
   configd `config` ROM; the panel-config probe capture regions
   hit geometry that physically moves with configd writes.
