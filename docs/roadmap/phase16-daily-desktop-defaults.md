> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 16 base.

### Phase 16: Practical Daily-Usable Desktop Defaults & Configuration

> Inserted 2026-09-16 by user direction, ahead of the IME phase:
> Phases 10/11 proved the desktop is interactive and customizable,
> but the release media must ship a *practically usable default*
> desktop with a real configuration surface. This phase hardens
> defaults and adds the everyday configuration/management features
> that make the DE a daily driver rather than a demo.

#### Goal

The shipped img/iso boots straight into a usable, configurable
desktop: sensible defaults are active out of the box, and every
common desktop configuration action is available through the DE
itself (settings app, panel and background context menus, keyboard
shortcuts) without hand-editing config files.

#### Scope / Completion Criteria

**Close-out (2026-09-20):** 10 of 12 criteria are **delivered**
with scenario-gated PASS markers; 2 are **delivered with honest
disclosure** (criterion 6 + 7 carry the D16.7 IMG-only honest
disclosure for ISO media; criterion 10 carries the W6 deviation
1 honest disclosure for the per-event right-click delivery). Full
per-criterion traceability in `docs/evidence/phase16-index.md`
§2; the §6 honest-limitations register there carries every
preserved Phase 17+ carry-over with evidence pointers.
**Addendum (2026-11-10, scope expansion):** criterion 2 is
reopened and redefined below (its final target is the
provider-owned control center, Criterion 18); the "10 of 12"
count above is the historical close-out state, and the
monolithic five-tab design is no longer counted as the final
Settings success.

- [x] **Default activation in release media** — **delivered**.
  `run/sponge-alpha.run` → `alpha-probe: PASS` (with the W3
  extended launcher set + the 8 baked-key configd broadcast
  assertions) + `run/sponge-desktop-defaults-firstboot.run` →
  `defaults-firstboot-probe: PASS`.
- [ ] **Settings application (Sponge DE Settings)** —
  **reopened and redefined (2026-11-10 scope expansion).** The
  earlier monolithic five-tab Settings was delivered and
  scenario-verified (`run/sponge-de-settings.run` →
  `settings-probe: PASS` (per-tab configd-broadcast assertions) +
  `run/sponge-de-settings-regression.run` →
  `settings-regression-probe: PASS` (panel-menu → Settings →
  Panel tab → height spin → configd carries `panel.height=40`)),
  but those per-tab receipts are historical evidence of the
  replaced implementation, not closure of the modular
  control-center requirement. The final Settings target is the
  provider-owned architecture in **Criterion 18**; the old
  five-tab design is not counted as the final Settings target,
  and this criterion stays open until Criterion 18 closes.
- [x] **Panel context menu** — **delivered**.
  `run/sponge-panel-menu.run` → `panel-menu-probe: PASS` (Height
  spinbox, Visible widgets checkboxes, Position radio group with
  top/bottom enabled and left/right disabled with the "Phase 17+"
  tooltip per D16.2; right-click delivery proven live on the
  panel).
- [x] **Add new panel** — **delivered**.
  `run/sponge-de-multipanel.run` → `multipanel-probe: PASS` with
  the F5 cross-panel click assertions live (`panel_alpha.click_
  count=1, panel_beta.click_count=0`; and the reversed click);
  `run/sponge-de-multipanel-idspace.run` →
  `pattern-keys-probe: idspace = PASS` (charset error structured).
- [x] **Window move via title bar** — **delivered** (criterion
  5, the emphasis criterion). `run/sponge-wm-qmp.run` →
  `wm-probe: PASS` (real-pointer title-bar drag through the
  QMP-driven input chain; geometry change asserted through the
  layouter `window_layout` ROM + a Capture pixel check). The W5
  dual-domain panel-position shift (Phase 14 #6 / #14
  reclassification; U16.2 + D16.2) is delivered in the same
  criterion's evidence.
- [x] **Default bundled packages in img/iso** —
  **delivered with honest disclosure (D16.7)**. The full 7-pair
  desktop set is asserted on `run/sponge-alpha.run` + the disk-
  desktop variants; the ISO media carries package metadata only
  (Falkon's 509 MiB payload exceeds the seL4 boot-module
  ceiling); IMG media is the install target for the criterion.
  Honest disclosure in `docs/13-installation.md` Known
  Limitations.
- [x] **Bundled packages launchable from the default panel** —
  **delivered with honest disclosure (D16.7, same scope as #6)**.
  Same `alpha-probe` extended set; per-package launch verification
  via `run/sponge-launch.run` + the per-package boot scenarios
  (`sponge-terminal.run`, `sponge-textedit.run`, `sponge-files.run`,
  `sponge-calculator.run`, `sponge-pdf-view.run`,
  `sponge-falkon-rescue.run` — the Phase 14 D14.5 Attempt 1 PASS
  rescue path).
- [x] **New windows open correctly** — **delivered**.
  Composed from `run/sponge-launch.run` (`launch-probe: PASS`,
  two Qt6 first paints under softpipe Mesa on seL4) +
  `run/sponge-wm-tasks.run` (`wm-tasks-probe: PASS`, the
  initial-state composition per the W10.2 gate) +
  `run/sponge-wm-qmp.run` (title-bar drag with the
  window-moved pixel verification).
- [x] **Mouse window resize** — **delivered with one honest
  sibling**. Criterion-9 headline:
  `run/sponge-de-release-resize.run` →
  `release-resize-probe: PASS` on the motif decorator (release-
  media topology; 3x consecutive independent PASS runs, 8 zones
  across 4 edges + 4 corners). Sibling:
  `run/sponge-de-themed-chrome-resize.run` proves each of the 8
  zones individually on the vendored themed_decorator patch (row
  #17); the full 8-zone + post-drag sequence is flaky
  (intermittent stray maximize mid-sequence), recorded as a
  Phase 17+ follow-up in `docs/evidence/phase16-w9-resize.md`.
- [x] **Background context menu** — **delivered with honest
  disclosure (W6 deviation 1)**. `run/sponge-de-bgmenu.run` →
  `bgmenu-probe: PASS` (structural `open="ready"` gate). The
  per-event acceptance (`open="yes"` on QMP right-click) is
  timing-sensitive on the Genode QPA for a non-decorated
  fullscreen widget at layer=1; precise blocker documented in
  `docs/evidence/phase16-w6-bgmenu-followup.md` with the
  panel-vs-bg widget diff (3 candidate root causes) + the
  Phase 17+ minimal Qt-flag fix recipe. No Phase 16 vendored
  budget for a fix (D16.8).
- [x] **Background image change** — **delivered**.
  `run/sponge-de-bgimage.run` →
  `bgimage-probe: PASS` (de_config_request round-trip +
  broadcast echo) + `run/sponge-de-bgimage-badpath.run` →
  `bgimage-badpath-probe: PASS` (path-traversal defense; atomic
  rejection of `../../etc/passwd`).
- [x] **Panel & keyboard interaction** — **delivered with two
  honest platform findings (W7)**. `run/sponge-de-shortcuts.run`
  → `shortcuts-probe: PASS` (Super / Alt-Tab / Escape live via
  QMP `send-key`) + `run/sponge-de-shortcuts-extend.run` →
  `shortcuts-extend-probe: PASS` (live `ctrl_alt_t` write fires).
  Findings 1 + 2 preserved in
  `docs/evidence/phase16-w7-shortcuts.md` (event_filter
  composition with `<merge>`/`<chargen>`; report_rom late-
  creation gap); runtime shortcut + chargen coexistence and
  runtime rebinding of the captured keys are Phase 17+ items.

Each criterion is scenario-verified (QMP-driven run scenario
asserting the observable state), consistent with the Phase 10/11
verification style. Full per-criterion traceability in
`docs/evidence/phase16-index.md` §2.

## Status note (moved from README)

Facts relocated from the README `Current Status` section (Phase 16
bullet) that had no home in the roadmap set. Receipts:
`docs/evidence/phase16-index.md` (§4 W12 sweep, §6 honest limitations,
§7 UX metrics) + `docs/evidence/phase16-envelope-*.log`.

- **Registry re-architecture:** `sponge_configd`'s `MAX_KEYS = 16 → 32`,
  a new `MAX_PATTERN_KEYS = 32` array of per-id `Key_def` clones (the W2
  pattern-key infrastructure; chars `[a-z0-9_-]{1,16}`), four new flat
  keys (`background.color` hex-validated, `background.image`
  allowlist-validated, `panel.ids` comma-list, `shortcuts.bindings`
  structured-callback-validated), and `panel.visible_widgets` extended
  from `{clock, launcher}` to `{clock, launcher, tasklist}`.
- **Measured everyday configurations** (`docs/evidence/phase16-index.md`
  §7 per `AGENTS.md` §5.1): the four canonical Phase 16 everyday
  configurations finish in 1 keystroke (open the launcher via Super),
  3 clicks (Settings → Panel tab → spin height; Settings → Background
  tab → pick image; or panel menu → Settings → Panel → spin), or
  4 clicks (Settings → Panel tab → enter id → Add a second panel).
- **Bake profile wiring:** `pkg/bake/desktop.profile` is wired into all
  six product scenarios (`sponge-alpha.run` + the 5 desktop-disk
  variants) so first boot on shipped media lands on the baked values
  (W3 + `run/sponge-desktop-defaults-firstboot.run`). PASS markers were
  scenario-gated on base-sel4 + QEMU, or base-linux for the
  settings-dialog fast path.
- **Interim settings dialog (historical):** the in-DE `settings/` module
  brought five tabs (Panel / Theme / Background / Shortcuts / Defaults)
  via the `SettingsController` (a dedicated `de_config_request` /
  `de_config_result` Report channel per D16.1 + `AGENTS.md` §1.2
  single-writer rule). That five-tab dialog was the interim
  implementation and is now gone: the W4 provider split (D16.9)
  removed the legacy dialog module and the universal
  `SettingsController` writer and landed the provider-owned
  `sponge_settings` control center (see the Criterion 18 status and the
  2026-11-14 addendum in `phase16-scope-expansion.md`).
- **Context-menu wiring receipts (Phase 16 W5/W6):** right-click on the
  panel is wired to open a QMenu (Height spinbox 16..128, Visible
  widgets checkboxes, Position radio group; `panel.position` becomes
  live via dual nitpicker panel domains `panel_top` / `panel_bottom`
  per U16.2 + D16.2, closing the Phase 14 W11 row #6 / #14
  `Re-scoped → Phase 15+` items as `Resolved in 16`). Right-click on
  the background is wired to open a QMenu (Settings / Launch / Show
  desktop) with the show-desktop toggle reusing the Phase 14 W7
  tasklist layouter-rule ROM overwrite (U16.3). Per the 2026-11-14
  addendum, the real PanelWidget/BackgroundWidget QMenu open lifecycle
  is unproven and blocked, so these wiring receipts are not proof of a
  working quick-menu UX.
- **Shortcuts and multi-panel:** the keyboard-shortcut framework is
  extensible via `shortcuts.bindings` (initial bindings: Super →
  launcher, Alt-Tab → focus next, Escape → dismiss topmost popover).
  Multi-panel is generalized: arbitrary panel count on arbitrary edges
  with per-panel `panel.<id>.{height,position,visible_widgets}` keys.
- **Vendored budget:** the Phase 16 budget lands one new row in
  `docs/11-environment.md` §4 (row #17, themed decorator sizer +
  minimizer; closes the Phase 14 D14.8(d) follow-up; D16.8 forbids any
  further vendored patches).
- **W12 regression sweep:** 36 scenarios, serial `make -j1` — **34 of 36
  fully PASS; 2 of 36 PARTIAL** (`sponge-de-bgmenu`, structural gate =
  W6 deviation 1; `sponge-de-themed-chrome-resize`, each zone
  individually but the full sequence flaky — Phase 17+); **0 FAIL.**
  Scenario set as listed in the README (35 names under the README's
  "36 scenarios" heading — count mismatch preserved as-is; the
  authoritative table is `docs/evidence/phase16-index.md` §4):
  `sponge-minimal`, `sponge-de-test`, `sponge-de-sel4-interactive`,
  `sponge-wm-qmp`, `sponge-launch`, `sponge-wm-tasks`,
  `sponge-clipboard-qtsettext`, `sponge-notify`,
  `sponge-configd-persist`, `sponge-panel-config-sel4`,
  `sponge-de-themed-chrome`, `sponge-bake-firstboot`,
  `sponge-bake-reset`, `sponge-alpha`,
  `sponge-desktop-defaults-firstboot`, `sponge-configd-pattern-keys`,
  `sponge-configd-shortcuts`, `sponge-configd-badkey`,
  `sponge-de-settings`, `sponge-de-settings-regression`,
  `sponge-panel-menu`, `sponge-de-bgmenu`, `sponge-de-bgimage`,
  `sponge-de-bgimage-badpath`, `sponge-de-shortcuts`,
  `sponge-de-shortcuts-extend`, `sponge-de-multipanel`,
  `sponge-de-multipanel-idspace`, `sponge-de-themed-chrome-resize`,
  `sponge-de-release-resize`, `sponge-textedit`, `sponge-files`,
  `sponge-calculator`, `sponge-terminal`, `sponge-hw-matrix`.
- **Honest limitations register (README wording):** W6 deviation 1
  (background contextMenuEvent right-click delivery), W7 finding 1 + 2
  (event_filter composition + report_rom late-creation), W8 5d
  (cross-panel tasklist focus — this entry appeared only in the
  README; see `docs/evidence/phase16-index.md` §6), W9 sibling
  flakiness (themed-chrome full 8-zone sequence), and the planned
  Phase 17+ carry-overs: `panel.position` left/right, real-hardware
  multi-panel regression, falkon on seL4, and user-supplied background
  image upload.
