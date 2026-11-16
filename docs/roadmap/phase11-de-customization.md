> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 11.

### Phase 11: DE Customization and Panel Strengthening

#### Goal

Users can meaningfully customize their desktop environment — theme,
panel layout, and behavior — without touching Genode internals.
Automation stays the default; every customization is also reachable
manually through `vct config` / theme files (control escape hatch).

#### Completion Criteria

- [x] Panel customization through `sponge_configd`: position, size,
  visible widgets, clock format, launcher organization. **Delivered:**
  configd registry grew to 7 keys — `panel.height` (uint 16–128),
  `panel.visible_widgets` (enum-list {clock,launcher}), `clock.format`
  (structural format string), `launcher.sort_by` (enum {alpha,manual})
  — with per-kind validators; a new in-sponge-de `ConfigController`
  applies them live (restyle-migrated panel/launcher).
  `panel.position` is persisted but boot-time-only (nitpicker domains
  own placement; live move is a Phase-12 open question).
  **Scenarios:** `run/sponge-config-probe.run` (20-step key matrix,
  base-linux), `run/sponge-panel-config.run` (7-subphase probe P1–P7,
  base-linux), `run/sponge-panel-config-sel4.run` (same on seL4).
  **PASS markers:** `config-seq-probe: PASS`,
  `sponge-de-probe: phase panel-config PASS`.
- [x] Expanded theme surface: more themeable elements, additional
  shipped themes beyond `default.theme`, live reload preserved.
  **Delivered:** error/success/warning split into `*_bg`/`*_text`
  (old names kept as deprecated aliases), `panel.popup_*` documented
  no-op keys, 2 new shipped themes (`dark.theme` = Catppuccin
  Macchiato palette, `compact.theme` = palette + tighter layout),
  theme transport cap raised 2048→8192 with truncation warning, and
  the unknown-theme path hardened: a `label_suffix=".theme"` catch-all
  route serves an empty ROM so `sponge_themed` takes its graceful
  keep-previous branch instead of freezing on the base-lib
  session-denial path. **Scenario:** `run/sponge-theme.run` (5-step
  3-theme probe + does-not-exist fallback + liveness proof,
  base-linux). **PASS marker:** `theme-probe: PASS`. **Host gate:**
  `./tool/test_theme_payload_size`.
- [x] Sponge-themed window chrome: the `themed_decorator` drop-in
  (theme tar via VFS) replaces stock decorations with themed ones.
  **Delivered:** `run/sponge-de-themed-chrome.run` (seL4+QMP) swaps
  the stock decorator for upstream `themed_decorator` (child named
  `decorator` so the WM policy set applies verbatim), the theme tar
  is authored by `./tool/decor_assets` (upstream geometry metadata +
  byte-vendored font.tff), and the new `sponge_decorator_bridge`
  delivers the decorator's whole config live via report_rom with the
  policy color taken from the active theme's palette. The title bar
  verifiably tints (RGB(180,180,191) → RGB(91,91,100)) and the QMP
  drag moves the window through the themed chrome.
  **PASS markers:** `wm-probe: PASS` (both the new scenario and the
  `run/sponge-wm-qmp.run` Phase-10 drag regression).
- [x] All customization flows documented and scenario-verified
  (config change → configd → themed/decorator → pixel repaint).
  **Evidence:** `docs/evidence/phase11-index.md`,
  `docs/evidence/task-0-phase11-baseline.md` (W0),
  `task-4-phase11-themed-chrome.md` (W4),
  `task-5-phase11-scenarios.md` (W5 sweep).

## Status note (moved from README)

- The four shipped themes are named `default` / `light` / `dark` /
  `compact` (all four exist under `repos/sponge/src/sponge-de/themes/`;
  the criteria above name only default, dark, and compact).
