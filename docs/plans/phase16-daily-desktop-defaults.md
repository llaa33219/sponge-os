# Phase 16, Practical Daily-Usable Desktop Defaults & Configuration (Work Plan)

> Status: active. Created 2026-09-19.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 16* (lines 787-840).
> Prior phase plan: `docs/plans/phase15-real-hardware-boot.md`.
> Input evidence: release-media + packaging exploration (2026-09-19,
> tooling/dirstate/categories walk) and the Metis pre-planning
> consultation (2026-09-19, 12-criterion ambiguity + failure-point
> walk). The two operational inputs that drove most of the design are
> `docs/evidence/phase14-index.md` (the Phase 14 close-out, every
> configd key, panel tasklist state machine, themed-chrome decorator
> contract) and `docs/evidence/phase15-index.md` (the bake profile
> machinery, first-boot sentinel, release-media topology).
>
> Scope update (2026-11-10, user-directed; revised 2026-11-11): the
> Phase 16 Extension gained twelve further completion criteria
> (`docs/09-roadmap.md` §10, Criteria 16-27). The settings design in
> this plan is rewritten in place to the provider-owned control-
> center architecture: D16.1 / D16.3 / D16.9 / D16.10 and W4 now
> specify a separate on-demand `sponge_settings` shell, package-
> declared settings entries, provider-owned writes, and configd
> per-label key ACLs, and every dependent cross-reference (criterion
> 2, D16.2 / D16.4, W5-W8, W10, W12, UX metrics, dependencies, ODQ)
> follows. The old universal-writer / monolithic-dialog wording is
> replaced in place, not kept as a live alternative. Evidence
> receipts are untouched.

---

## Goal Restatement (`docs/09-roadmap.md` §10, Phase 16, verbatim)

> Phases 10/11 proved the desktop is interactive and customizable,
> but the release media must ship a *practically usable default*
> desktop with a real configuration surface. This phase hardens
> defaults and adds the everyday configuration/management features
> that make the DE a daily driver rather than a demo.
>
> The shipped img/iso boots straight into a usable, configurable
> desktop: sensible defaults are active out of the box, and every
> common desktop configuration action is available through the DE
> itself (settings app, panel and background context menus, keyboard
> shortcuts) without hand-editing config files.

Twelve completion criteria (ordered as in the roadmap; criterion 2
is reworded in place to the modular settings target, see the
rewording note below):

1. **Default activation in release media**: the desktop
   (panel + compositor + defaults) is enabled by default in the
   img/iso; first boot lands on a usable desktop with no manual
   setup.
2. **Settings application (Sponge DE Settings)**: an on-demand
   settings control center discovers package-declared settings
   entries (identity `(package, settings-id)`) and opens
   provider-owned settings windows; each provider owns its UI,
   validation, writes, and errors (shared DE keys via per-label
   configd key ACLs, package-private stores otherwise).
3. **Panel context menu**: right-click on the panel opens a menu
   that changes panel settings (height, widgets, position) live.
4. **Add new panel**: the user can add an additional panel
   (multi-panel), each independently configurable.
5. **Window move via title bar** (emphasized): clicking and
   dragging a window's top bar moves the window, the primary,
   discoverable window-move interaction.
6. **Default bundled packages in img/iso**: a curated default
   package set is baked into the release media.
7. **Bundled packages launchable from the default panel**: the
   bundled packages appear under the default DE panel's *Utilities*
   menu and launch correctly.
8. **New windows open correctly**: launched applications open
   their own windows reliably (focus, stacking, decoration).
9. **Mouse window resize**: dragging window edges/corners
   resizes the window.
10. **Background context menu**: right-click on the background
    opens a desktop menu (settings, launch, arrange).
11. **Background image change**: the desktop background image is
    changeable (persisted via configd).
12. **Panel & keyboard interaction**: keyboard shortcuts drive
    the desktop (e.g. Super opens the Utilities menu), plus further
    shortcut coverage.

Each criterion is scenario-verified (QMP-driven run scenario
asserting the observable state), consistent with the Phase 10/11
verification style.

**Rewording applied at plan time** (criterion 2 is rewritten to the
provider-owned settings architecture per the 2026-11-11 direct
rewrite (D16.1 / D16.3 / D16.9 / D16.10); the criterion-7 wording
"Utilities menu" is ambiguous, see Recorded Scope Ruling U16.1
below):

- **Criterion 2 (rewritten):** *an on-demand settings control
  center discovers package-declared settings entries (identity
  `(package, settings-id)`) and opens provider-owned settings
  windows; each provider owns its UI, validation, writes, and
  errors (shared DE keys via per-label configd key ACLs,
  package-private stores otherwise).* The roadmap's original
  "first-party settings app exposes ... persists via
  `sponge_configd`" wording described a single universal settings
  surface and is replaced.
- **Criterion 7 (rewritten):** *bundled packages appear in the
  default panel's launcher menu (grouped by their metadata category)
  and launch correctly.*

All other criteria keep the verbatim wording.

---

## Recorded Scope Rulings U16.1-U16.6 (decided by the user up-front, 2026-09-19)

These six rulings constrain the plan; they are not reopened here.
The plan is consistent with each one and explicitly cites the ruling
where it applies.

- **U16.1, Criterion 7 "Utilities menu" reworded.** The existing
  launcher popup with per-category section headings IS the menu
  (criterion 7 in `docs/09-roadmap.md` §10 Phase 16 describes it as
  a *Utilities* menu, but the panel implements a single popup with
  one section heading per `<launcher category="..."/>` value, e.g.
  *Utilities*, *System*, *Editors*, *Internet*). The criterion
  wording is reworded to "bundled packages appear in the default
  panel's launcher menu (grouped by their metadata category) and
  launch correctly", no new dropdown, no re-categorization. Super
  opens this launcher popup. **Effective re-scope:** no `<launcher
  category>` values change, no panel layout changes for criterion 7.
  Categories are honoured exactly as today's metadata declares.
- **U16.2, Criterion 3 panel.position is LIVE, not boot-time.**
  Phase 14 W11 paper-cut row #6 classified `panel.position` as
  `Re-scoped → Phase 15+` (boot-time-only; would need dual-domain
  toggle or WM-managed placement). Criterion 3 of Phase 16 brings
  position back into scope as a live, click-to-apply panel-menu
  action. **Path chosen (P-B):** panel position changes LIVE via
  dual nitpicker panel domains (`panel_top` + `panel_bottom`) with
  visibility toggling driven by the configd key. Concretely: the
  sponge-de start config declares two Gui sessions with distinct
  `label_last` suffixes (`Sponge Panel` and `Sponge Panel Bottom`),
  routed to two `<domain>` blocks (`panel_top` + `panel_bottom`); the
  panel context menu flips the `panel.position` configd key; the
  PanelWidget observes the broadcast and shows/hides the
  corresponding QWidget. The unused panel domain is hidden via a
  `<domain><visible/>` toggle or by destroying that widget's Gui
  session temporarily (sub-design decision lands in W5). This
  formally relocates Phase 14 W11 row #6 / row #14 into Phase 16
  scope; the Phase 14 disposition matrix must be reclassified from
  `Re-scoped → Phase 15+` to `Resolved in 16` once W5 lands.
- **U16.3, Criterion 10 "arrange" = show-desktop toggle.** The
  "arrange" entry on the background context menu is a
  minimize-all / restore-all toggle (Show desktop), reusing the
  Phase 14 W7 panel tasklist state machine
  (`docs/plans/wm-state-table.md`), write the same layouter-rule
  ROM overwrite that the tasklist uses for minimize, just applied
  to every focused/visible window. **No tiling, no icon grid.**
  The toggle is two-state (show vs restore); the menu shows the
  current state and the click flips it. Implementation piggybacks
  on the tasklist state machine; no new code beyond a small
  `BackgroundController::show_desktop()` slot in W6.
- **U16.4, Criterion 12 shortcuts = extensible framework driven
  by configd.** An EXTENSIBLE keyboard-shortcut framework driven by
  `sponge_configd`, not a hardcoded set. Initial shipped bindings:
  - `Super` → open launcher popup (U16.1).
  - `Alt-Tab` → cycle focus forward (forward-only, wrapping).
  - `Escape` → close topmost popover (launcher / settings /
    context menu).
  More bindings can be added by writing the configd key; no code
  change. Because the closed registry (`MAX_KEYS = 16`,
  `sponge_configd/main.cc:91`, currently 10 used) cannot hold one
  key per binding, the design uses a single structured-value
  configd key `shortcuts.bindings` holding a delimited
  `action=sequence` list with a strict validator. The validator
  spec is pinned in D16.5: it accepts one binding per line; each
  line is `<action_token><TAB><key_sequence>`; the action token is
  a member of a closed enum (`launcher`, `focus_next`, `dismiss`);
  the key sequence is a Genode Input-event keycode list. Lines that
  fail validation emit `Genode::warning` and are skipped, never
  silently accepted. **This is the deliberate exception to the
  one-key-per-setting rule** and is documented as such in the
  registry table (`sponge_configd/README.md`).
- **U16.5, Criterion 4 multi-panel FULLY GENERALIZED.** Arbitrary
  panel count on arbitrary edges. Per-panel config namespace:
  `panel.<id>.{height,position,visible_widgets}`. This forces a
  configd registry re-architecture: pattern / prefix-based key
  definitions (registry entries with `<id>` wildcard segments,
  validated charset `[a-z0-9_-]{1,16}`) instead of purely flat names.
  The implementation pattern is documented in D16.6. **MAX_KEYS is
  bumped 16 → 32 as an interim ceiling** (no semantic change to
  flat keys; the new pattern keys allocate a separate fixed slot
  range of `MAX_PATTERN_KEYS = 32`). **RAM budget:** each additional
  panel costs ~128 MB (sponge-de Qt6 instance weight per
  `run/sponge-alpha.run:680`); the multi-panel scenario
  (`run/sponge-de-multipanel.run`) runs at `-m 4G`. **Distinct Gui
  label suffix rule:** every panel instance uses a unique
  `label_last` suffix (e.g. `Sponge Panel 1`, `Sponge Panel 2`,
  `Sponge Panel Bottom 2`), routed to a matching `<domain>`. This
  avoids the nitpicker `label_prefix` trap (F5) where two
  identically-prefixed Gui sessions land on the same domain. The
  distinct-label rule is enforced by the run-script (no two
  `<start>` blocks for panel instances may share the same
  `label_last`), and a focused assertion in the multi-panel
  scenario proves each panel's widget receives clicks independently.
- **U16.6, Criterion 1 keeps `pkg/bake/desktop.profile`.** No new
  daily profile. The existing `desktop` profile
  (`pkg/bake/desktop.profile`, 7 packages + 4 configd keys +
  theme=default) is the daily default. A new profile would create
  two different curated sets, violating the criterion's "curated
  default package set" singular and would force every downstream
  scenario to pick which profile to gate on. The criterion is
  satisfied when `pkg/bake/desktop.profile` is the default and
  first boot lands on the baked values.

---

## Baseline (2026-09-19)

### Components and code

- **`sponge-de` is a single Qt6 component** at
  `repos/sponge/src/sponge-de/` with internal modules `panel/`,
  `launcher/`, `theme/`, `config/`, plus `sponge_de_main.{h,cc}`.
  Modules referenced by the Phase 16 plan at plan time but **not**
  yet present: `settings/` (the original in-DE slot; superseded by
  the separate `sponge_settings` component + package-declared
  providers per D16.3), and `background/`. `panel/panel_widget.{h,cc}` is a singleton
  (`main.cc:168`); the constructor builds the launcher + clock +
  tasklist layout in ctor; the restyle + apply* methods are
  re-entrant. `panel.position` lives in `sponge_configd`'s closed
  registry (`sponge_configd/main.cc:245-247`, default `bottom`,
  currently boot-time-only per Phase 14 W11 row #6), U16.2 makes
  it live.
- **Window stack** is upstream Genode's `wm` + `window_layouter` +
  `decorator` (release media ships the **plain motif decorator** in
  all 6 product scenarios, `sponge-alpha.run`,
  `sponge-desktop-disk.run`, `sponge-desktop-disk-nvme.run`,
  `sponge-desktop-disk-uefi.run`, `sponge-desktop-disk-uefi-nvme.run`,
  `sponge-desktop-disk-uefi-usb.run`). themed_decorator remains a
  QMP-proven sibling (`sponge-de-themed-chrome.run`), Phase 16
  does NOT switch the release media to themed_decorator (decision
  deferred; documented in W10 release-media gate).
- **Backends in production**: `sponge_pkgd` (Phase 4/7),
  `sponge_configd` (closed registry, 10 live keys, MAX_KEYS=16,
  `<vfs>` persistence; `sponge_configd/main.cc:91,230-251`),
  `sponge_themed` (resolves theme names), `sponge_notifier`
  (Phase 14 W4). The report_rom single-writer per label rule
  (`AGENTS.md` §1.2) is the constraint that drives D16.1 below.

### Vendored Genode 26.08, what is in scope and what is not

- **Mouse resize is mostly upstream-complete**:
  - Upstream `window_layouter` already supports resize geometry:
    `genode/repos/gems/src/app/window_layouter/window.h:225-271`
    (state + hover flags), `main.cc:332-362 + 364-406`
    (resize_request + assignment updates), `user_state.h:342-378`
    (hover consumes sizer flags).
  - The plain motif decorator already draws edge / corner sizers:
    `genode/repos/gems/src/app/decorator/window.cc:322-325`.
  - **Missing:** themed_decorator (`genode/repos/gems/src/app/
    themed_decorator/`) has ZERO sizer code: `theme.h:53` `Element_type`
    enum only has `closer` + `maximizer`; no sizer PNGs; no
    `<sizer>` metadata entries in
    `tool/decor_assets_data/metadata.txt`. **Same files** are
    also the Phase 14 deferred D14.8(d) `<minimizer/>` button
    follow-up. **Phase 16 closes both with ONE vendored patch**
    (ledger row #17; drop-when: "upstream Genode adds resize +
    minimizer to themed_decorator"). Release media stays on the
    motif decorator for Phase 16.
- **First-boot bake path is code-complete, product-scenario-WIRING
  incomplete.** `sponge_configd/main.cc:636-674` opens the bake
  ROMs (`bake_config_defaults`, `bake_manifest`) only when the
  component config carries `<bake/>`. Lines 677-779 parse the
  manifest and seed each baked key through the closed-registry
  validator, then set the `bake.applied=yes` sentinel.
  `vct/src/bake_command.cc` exposes `vct bake list/show/reset`.
  `run/bake.inc:920-1005` is the staging-time include.
  `tool/bake.mojo` is the post-build P3 injector.
  `run/sponge-bake-firstboot.run` proves the mechanism on
  base-linux + lx_fs. **Gap:** the six product scenarios
  (`sponge-alpha.run:627-638` and the five desktop-disk variants)
  ship sponge_configd with NO `<bake/>` and NO bake ROM routes.
  Lines 805-811 explicitly comment "bake defaults are *not* boot
  modules... today we just plant them on disk so future scenarios
  can pick them up". First boot on the shipped media today lands
  on configd's in-code defaults (`theme.active=light`,
  `panel.position=bottom`) rather than the bake profile values
  (`theme.active=default`, `panel.height=28`,
  `panel.visible_widgets=clock,launcher`, `clock.format=HH:mm`,
  `launcher.sort_by=alpha`).

### Closed-registry state (`sponge_configd`)

| key                      | type         | default         | live? |
|--------------------------|--------------|-----------------|-------|
| `bake.applied`           | enum         | `no`            | no (sentinel only) |
| `bake.profile`           | string       | `none`          | no (manifest read-only) |
| `bake.version`           | uint         | `0`             | no (manifest read-only) |
| `clock.format`           | format string| `HH:mm`         | yes (Phase 11) |
| `leitzentrale.enabled`   | enum         | `false`         | yes (Phase 14) |
| `launcher.sort_by`       | enum         | `alpha`         | yes (Phase 11) |
| `panel.height`           | uint range   | `28`            | yes (Phase 11) |
| `panel.position`         | enum         | `bottom`        | **no today; YES in Phase 16 (U16.2)** |
| `panel.visible_widgets`  | enum-list    | `clock,launcher`| yes (Phase 11) |
| `theme.active`           | string       | `light`         | yes (Phase 11) |

`MAX_KEYS = 16` (`sponge_configd/main.cc:91`). 10 used. 6 free.
Phase 16 needs ~6 more flat keys plus pattern keys for multi-panel
(U16.5): 16→32 is the interim ceiling.

### Run scenarios already boot-verified (regression anchors)

- `run/sponge-alpha.run` (umbrella, base-sel4 + interactive-PC
  drivers + WM stack + `alpha-probe: PASS`; Phase 7 + Phase 11).
- `run/sponge-de-sel4-interactive.run` (Phase 10 QMP-driven input
  three phases: input / panel / launch).
- `run/sponge-wm-qmp.run` (Phase 10 criterion 2; real-pointer title-
  bar drag; `wm-probe: PASS`).
- `run/sponge-de-themed-chrome.run` (Phase 11 criterion 3; themed
  decorator; `wm-probe: PASS`).
- `run/sponge-wm-tasks.run` (Phase 14 W7; tasklist minimize/restore
  state machine from `docs/plans/wm-state-table.md`).
- `run/sponge-panel-config-sel4.run` (Phase 11 criterion 1; 7
  subphases).
- `run/sponge-bake-firstboot.run` (Phase 15 W3 sentinel on
  base-linux + lx_fs; not the product kernel path).
- `run/sponge-bake-reset.run` (Phase 15 W3 reset path).
- `run/sponge-configd-persist.run` (Phase 14 W6 `<vfs>` persistence).
- Per-package boot scenarios: `sponge-terminal.run`,
  `sponge-textedit.run`, `sponge-files.run`, `sponge-calculator.run`,
  `sponge-pdf-view.run`, `sponge-falkon-rescue.run`.

### Carryover inventory (Phase 14 → Phase 16)

The Phase 14 disposition matrix (`docs/plans/phase14-daily-desktop.md`
§"Paper-cut Disposition Appendix", 50 rows) is the starting point.
Items that TOUCH a Phase 16 path:

- **#6 / #14 `panel.position` boot-time-only**, Phase 14
  classified `Re-scoped → Phase 15+`. **U16.2 formally relocates
  this to Phase 16 scope.** Reclassification: `Resolved in 16`
  once W5 lands.
- **D14.8(d) `<minimizer/>`**, Phase 14 deferred. **Closed by
  the W9 vendored patch (same files as the resize patch, one
  ledger row #17).**
- **#18 parsed-but-unused theme keys** (`title_family`,
  `icon_size`, `popup_width`, `popup_entry_min_height`), Phase
  14 W11 cleanup. Phase 16 inherits; no new work.
- **QTimer leak suspects #47-#50**, Phase 14 W11 leak audit.
  Phase 16 introduces new timers (configd 250 ms is unchanged;
  keystroke-capture is event-driven, no new QTimer; panel-position
  apply uses the existing 250 ms broadcast poll). Re-audit in W11.

### Items NOT in Phase 16 scope (per U16.x + Phase 15 carryovers)

- Hardware-specific items (i440fx real, multi-namespace NVMe, Wi-Fi,
  rtl8169, USB-Ethernet, USB keyboard glyph-delta fix,
  USB mouse beyond the QEMU envelope) are Phase 15+ (already
  carried by `docs/15-hardware-compatibility.md`). Exception:
  basic touchpad support (i2c_hid driver path) is now Phase 16
  Extension criterion 25 — see the Non-Goals exception below.
- Sponge IME is Phase 17.
- GUI installer is Phase 18.
- Multi-monitor is Phase 15+ (Phase 14 D14.9, unchanged).
- System tray / additional panel applets are deferred (Phase 14
  D14.7, unchanged).

---

## Binding Decisions

| # | Decision | Rationale |
|---|---|---|
| **D16.1** | **Single-writer-per-label transport stays; the universal `de_config_request` model is replaced by per-provider config request labels plus configd label-to-key-domain ACLs.** The Phase 14 launcher precedent still applies: `report_rom` is single-writer per label (`AGENTS.md` §1.2). Every settings writer gets its OWN label routed to its own `config_request` ROM session in `sponge_configd`: `vct -> config_request`, `panel_settings -> config_request`, `background_settings -> config_request`, `theme_settings -> config_request`, `shortcuts_settings -> config_request`, `defaults_settings -> config_request`, `display_settings -> config_request`, `wm_settings -> config_request`, `terminal_settings -> config_request`. configd's component config declares, per label, an allowlist of key domains (e.g. `panel_settings → panel.*, panel.<id>.*, panel.ids`), and enforces it on EVERY `set`: an out-of-domain key returns the structured `key-outside-domain` error and never touches the store. Validator parity with vct is unchanged: every accepted write goes through the closed registry (`_apply_validated_value`), so an unknown / malformed key returns the same structured error on every label. **No universal DE writer:** sponge-de holds no config write channel (D16.9); its panel/background quick menus follow the quick-menu change lifecycle (`docs/05-sponge-de.md` §5.4, "Quick-menu change lifecycle"): ensure-start of the owning first-party provider via pkgd on the quick menu's own single-writer launch labels (normal `(package, settings-id)` path; pkgd starts/focuses only and never carries the change or its value), then a closed domain operation (for example `Panel height=40`, never an arbitrary config key/value) over the dedicated per-provider quick-request / quick-result Report/ROM pair. The provider validates the operation and performs the write on its own label. Each quick pair's vocabulary is closed and owned by its provider: this is not a universal `SettingsController` and not an arbitrary config relay, and the quick menu never gets a configd write channel. `sponge_pkgd` never relays config writes. `vct config` remains the intentional admin / manual escape hatch (allowed on every domain). | `AGENTS.md` §1.2 (single-writer per label) + minimum privilege: a label-to-key-domain allowlist turns "no provider can write outside its domain" into a transport-enforced rule rather than a convention. Per-label ROM sessions are the established report_rom shape (the Phase 14 launcher + notifier precedents). |
| **D16.2** | **panel.position becomes live via dual nitpicker panel domains (`panel_top` + `panel_bottom`).** Per U16.2. The `PanelWidget` observes the `panel.position` broadcast on the existing 250 ms configd poll (no new poll timer; same `ConfigController` path as `panel.height` / `panel.visible_widgets`). When the key flips `top↔bottom`, the widget shows / hides the two pre-built QWidgets, which the run scenario's nitpicker config has routed to `<domain name="panel_top">` and `<domain name="panel_bottom">`. The unused panel domain's QWidget is hidden via `QWidget::hide()` (no Gui session churn); the layer order is fixed at scene graph init. **Right and left positions are registered in the configd enum** (the `_value_valid` validator already accepts them) but NOT mapped to live widget show/hide in Phase 16, the panel-menu Position item exposes them as disabled (greyed) entries with a tooltip "Phase 17+". This is the honest disclosure path (AGENTS.md §1.1 / §1.4), no silent stub. **Position is a CLOSED ENUM of 4 members (`top` / `bottom` / `left` / `right`), no free-form string.** | U16.2 path P-B. The dual-domain approach reuses the proven `gui -> nitpicker` label-routing from `run/sponge-alpha.run:693-694`. Two pre-built widgets avoids ctor-only-state risk (F4). Honest disclosure on right/left matches the philosophy rule. |
| **D16.3** | **Settings is a separate on-demand `sponge_settings` control-center component plus package-declared provider windows (NOT a sponge-de module, NOT a universal dialog).** The shell is a standalone Genode component, started on demand; closing it reclaims its resources. It holds only the installed-metadata ROM, its own settings launch request/result labels, and Gui/Timer sessions as needed — NO configd write channel, NO package File_system session, NO provider code loading, NO GL-heavy requirement (the shell is on-demand and the requirement keeps it light; resource quotas are MEASURED and DECLARED per component — provider and shell components do not inherit sponge-de's 128 MB Qt6 + Mesa quota by default). **Package-declared entries:** each package declares `0..N` `<settings>` entries (separate from the singular `<launcher>`; launcher and settings entries are independent). Stable identity is `(package, settings-id)`, unique per package and stable across reinstall. Each entry declares a title, a category from a closed enum (`desktop`, `hardware`, `system`, `applications`, `other`; unknown/absent → `other`), optional icon/keywords, and a package-payload binary target — NO absolute paths, NO shell commands, NO interpreters, NO arbitrary Exec strings. Settings targets have their own start/lifecycle identity distinct from the main app even when they share a binary, and settings targets are never exclusive. **Discovery:** `sponge_pkgd` validates and resolves every entry and republishes the installed set via installed metadata; `sponge_settings` refreshes on the ROM update signal (never polling) and groups entries by category. **Launch:** the shell sends only `package` + `settings-id` over its own dedicated launch channel; pkgd starts the declared binary target as its own component. Any package can join with no shell code changes and no central allowlist; there is no in-process plugin or embedding path. **Focus:** window identity derives from `(package, settings-id)`; a second activation of an already-open target focuses/raises its existing singleton window — it is not an already-running refusal and creates no duplicate. **Providers:** the required first-party providers are Panel, Background, Theme, Shortcuts, Defaults, Display, WM, Terminal. The historical tab surfaces split among their owning providers (panel settings to Panel, theme to Theme, background to Background, shortcuts to Shortcuts, reset-to-defaults to Defaults) — the tab set is not preserved as a monolith. Terminal settings belong to the terminal package; Konsole or any arbitrary package provides its own provider the same way. Provider writes follow D16.1 / D16.9 / D16.10. **Direct launch:** `vct settings list` / `vct settings open` share the same backend (installed metadata + launch channel) with separate single-writer labels and `--json` support. | Xfce-like control-center architecture: the shell is a launcher, not a writer; provider-owned windows give failure containment and let any package extend settings without shell code changes (convenience + automation without hiding control — `vct config` stays the manual door). On-demand lifecycle keeps the resource cost proportional to use; `AGENTS.md` §1.2 minimum privilege is structural (the shell literally cannot write config). |
| **D16.4** | **Background widget lives in sponge-de as a new `background/` module (NOT reuse of `app/backdrop`).** The existing `app/backdrop` is built by THREE scenarios that reference the same static `genode_logo.png`: `run/sponge-alpha.run:127` (build list) + `:194-195` (cp command) + `:820` (boot modules list); `run/sponge-leitzentrale.run:41` (build list) + `:101-102` (cp command); `run/sponge-usb-boot.run:235` (build list) + `:302-303` (cp command) + `:846` (boot modules list). All three paint a static image (`genode_logo.png`) with no context menu, no live configd, and no per-theme reload. The in-DE widget is the chosen path because: (a) per-theme live reload needs an owned surface (the theme's `panel_bg` is the default solid colour; switching themes should update the desktop), (b) the right-click context menu needs an owned surface to receive the click, (c) `app/backdrop` is upstream-owned and rewiring it would be a vendored patch outside `genode/repos/gems/src/app/themed_decorator/`. The widget is a fullscreen frameless `QWidget` registered in nitpicker's default domain BELOW windows (via a Gui session with `label_last="Sponge Background"` routed to `<domain name="default"/>` and `<layer>` ordering), painting solid color (`background.color`) or QImage (`background.image`). `contextMenuEvent` opens a `QMenu` (Settings / Launch / Show desktop). **The existing `app/backdrop` is removed from all three build lists and the `genode_logo.png` cp commands are dropped** (W6); the new in-DE widget replaces it. The removal is documented in `docs/13-installation.md` Known Limitations as a Phase-16 acceptance criterion (no behavior loss; backdrop is replaced by the new widget). | Per-theme live reload + context menu require an owned surface; reusing a vendored `app/backdrop` would require a vendored patch outside the allowed scope. Removing `app/backdrop` from all three scenarios is the precondition for the in-DE `background/` module to be the SINGLE live surface (any stale `genode_logo.png` paint would mask the dynamic widget). |
| **D16.5** | **Closed-registry extension: pattern keys + MAX_KEYS bump + structured-value shortcuts key.** Three coordinated changes in `sponge_configd/main.cc`: (1) **bump `MAX_KEYS` from 16 to 32** (interim; the registry stays a flat array of `Key_def`s for the existing 10 + ~6 new flat keys). (2) **add a new `MAX_PATTERN_KEYS = 32` array for pattern keys** with the same `Key_def` shape but a name template `panel.<id>.{height,position,visible_widgets}`; on each new key write the registry parser scans for a matching template, validates the `<id>` against `[a-z0-9_-]{1,16}`, instantiates a per-id `Key_def` clone in the pattern array (or rejects if full), and applies the validator. The set of valid `<id>`s is computed from a new `panel.ids` configd key (a comma-list string the user writes via the multi-panel UI; default empty). (3) **add `shortcuts.bindings` as a single structured-value key** with a NEW `shortcuts_validator` callback added in W2 #6; the callback's shape is modeled on the existing `FormatString` validator pattern that lives in `sponge_configd/main.cc` (the `clock.format` validator, a tier-2 structured-value parser the Phase 11 registry introduced per the `Key_def` sketch at `docs/plans/phase11-de-customization.md` lines 185-217). The `shortcuts_validator` implementation is itself novel (no per-line parser exists in Phase 11's `Key_def` sketch) and lands as a new callback signature in `sponge_configd.h`: splits on `\n`, then on `\t`, validates the action token against the closed enum `launcher` / `focus_next` / `dismiss`, and validates the key sequence against Genode Input-event keycodes. **Initial shipped bindings (D16.5b):** `launcher\tSuper`, `focus_next\tAlt-Tab`, `dismiss\tEscape`. **Validator parity test** (F15): writing `panel.visble_widgets` (typo) returns the structured error list with `panel.visible_widgets` as the suggestion, exactly as `vct config` does today. **The validator emission is never a silent drop:** every skipped / unknown line emits `Genode::warning("shortcuts.bindings: skipped line N: <reason>")`. | U16.5 + U16.4. The bump is conservative (32 is a power of 2 round number, cheap for the linear scan). The pattern-key array is small (32 slots × ~5 `Key_def`s = bounded) and avoids the `panel.1.height`, `panel.2.height` ... explosion. The shortcuts key is the documented exception to the one-key-per-setting rule (with validator parity). `shortcuts_validator` is W2's own addition; the FormatString-pattern reference is the structural inspiration, NOT a verbatim block. |
| **D16.6** | **Mouse window resize lands in the release-media topology via the plain motif decorator; themed_decorator gets the vendored sizer + minimizer patch as ONE ledger row #17.** Per `AGENTS.md` §5.2 (one vendored patch budget for Phase 16). The vendored patch touches `genode/repos/gems/src/app/themed_decorator/{theme.h:53, theme.cc, window.h:136-137, window.cc:280-283}` plus 4 new PNGs in `tool/decor_assets_data/pngs/` plus 4 `<sizer>` metadata entries in `tool/decor_assets_data/metadata.txt`. The same patch closes the Phase 14 D14.8(d) `<minimizer/>` follow-up. **Drop-when:** upstream Genode adds both resize affordance and `<minimizer/>` to themed_decorator. **Release media stays on the plain motif decorator for Phase 16.** The motif decorator already draws sizers (`genode/repos/gems/src/app/decorator/window.cc:322-325`); the criterion's regression gate is therefore the release-media topology (which already ships the motif decorator). Per-package metadata opt-in: every package that opts into resize adds `<resizeable="yes"/>` to its `<rom>` element (new metadata attribute, see `docs/12-package-format.md`); `sponge_pkgd`'s launcher-config generator carries `resizeable="yes"` to the layouter's window_list. **App-side resize handling:** every opt-in app must also subscribe to the `resize_request` ROM; the Phase 16 work is the metadata convention + a documentation update for package authors; the apps that need explicit handling (`textedit`, `files`, `calculator`) each gain a small `ResizeSubscriber` shim. **Open question (W9):** does Qt6 QPA already resize content on Gui window resize? If yes, the shim is a no-op for Qt6 apps and only the metadata opt-in is needed. | U16 + `AGENTS.md` §5.2 + the resize + minimizer consolidation noted in the Metis consultation (H10). The motif decorator choice keeps Phase 16 within the single-vendored-patch budget and avoids runtime regressions on real hardware. |
| **D16.7** | **Bundled-package criterion (6 + 7) is restricted to IMG media.** Per `pkg/bake/README.md` + `run/bake.inc:36-49` + `sponge-alpha.run:805-811`: ISO media is metadata-only (payloads don't fit in the boot-module ceiling). The criterion says "bundled packages appear in the launcher and launch correctly". **IMG media is the install target** and ships the full 7 desktop packages' payloads via `bake::stage img`; criterion 6 + 7 are satisfied by `run/sponge-desktop-disk.run`'s full-topology regression. **ISO media** keeps the metadata-only bake (the `bin/pkg_*.xml` + `bin/pkg_index.xml` files) so a user can `vct install` from a future remote repo, but the criterion is NOT satisfied by ISO alone. **Honest disclosure** lands in `docs/13-installation.md` Known Limitations as: "ISO media carries package metadata only; bundled packages are pre-launchable on the disk (.img) media. ISO installs require a future remote-repo follow-up (Phase 18+)." No silent re-scope. | The bake format contract (D15.5 + run/bake.inc) is binding; ISO payloads exceed the boot-module ceiling. Honest disclosure per AGENTS §1.4. |
| **D16.8** | **No silent vendored-tree patches. The base Phase 16 one-patch budget (row #17, resize + minimizer to themed_decorator) is historical, not a cap.** Per `AGENTS.md` §5.2. Any additional patch required by Extension work must be (a) individually justified in its ledger entry, (b) recorded in the `docs/11-environment.md` §4 ledger BEFORE application (row first, patch second), (c) independently revertable (one patch per concern, each with a drop-when note), and (d) re-audited at the final Criterion 27 cleanup. Prefer Sponge-owned components and public interfaces — especially the custom Sponge WM (Extension criterion 16) — over vendored changes. No specific new vendored patch is claimed required or approved at plan time. | `AGENTS.md` §5.2. The one-patch budget predates the Extension criteria; treating it as a hard cap would silently push every Extension patch out of scope or into a Phase-17-only limbo. The per-patch rule keeps reproducibility (ledger-first, independently revertable, re-audited at close-out) without freezing the vendored tree. |
| **D16.9** | **The universal `de_config_request` / `SettingsController` writer is removed when the provider split lands — it is NOT retained as a second compatibility writer.** Per D16.1 + D16.3. When W4's provider split lands, the `sponge-de -> de_config_request` / `de_config_result` routes, the `SettingsController` class (`repos/sponge/src/sponge-de/config/settings_controller.{h,cc}`), and every DE-side direct write path are deleted; sponge-de consumes configd domain broadcasts but is never a settings writer. The panel/background quick menus follow the quick-menu change lifecycle (`docs/05-sponge-de.md` §5.4, "Quick-menu change lifecycle"): ensure-start of the owning first-party provider via pkgd on the quick menu's own single-writer launch labels when it is not running (pkgd starts/focuses only, never carries the change), then a closed domain operation over the per-provider quick-request / quick-result pair; the provider validates the operation in its own code and writes on its own label (e.g. `panel_settings -> config_request`), which configd's label-to-key-domain ACL limits to that provider's domain. A start failure, a provider-unavailable condition, or a validation failure is surfaced without hanging the DE and leaves the config unchanged; this is not a replacement universal `SettingsController` and not an arbitrary config relay (each quick pair has a closed operation vocabulary owned by its provider). Migration is a replacement, not a coexistence: no parallel writer is kept for compatibility, so there is exactly one writer per label and one writer per key domain at all times. vct keeps `vct -> config_request` / `vct -> config_result` as the admin/manual escape hatch (D16.1). | D16.1 + D16.3. Removing the universal writer is what makes "no provider can write outside its domain" enforceable (F1 multi-writer collision and F3 validator bypass cannot recur through a back-door writer); keeping it as a compatibility writer would preserve exactly the hole the provider split closes. |
| **D16.10** | **Two validation/storage tiers: shared DE keys stay in configd's closed typed registry with typed validators + per-label ACLs; package-private provider settings are validated and stored by the provider itself.** Tier (a), **shared DE keys** (first-party providers only): Phase 16 keeps the closed-registry closed; every new shared key lands with a typed validator (Phase 11 W1 + Phase 14 W6 discipline, `sponge_configd/main.cc` `_value_valid`), and every set is additionally gated by the D16.1 label-to-key-domain ACL. New flat keys: `background.color` (hex `#RRGGBB`), `background.image` (validated against a closed allowlist of ROM-staged image paths, default `[ "/system/background/default.png" ]`), `shortcuts.bindings` (the structured validator per D16.5). Pattern keys (U16.5): `panel.<id>.height` (cloned `panel.height` validator), `panel.<id>.position` (cloned `panel.position` validator), `panel.<id>.visible_widgets` (cloned `panel.visible_widgets` validator), plus `panel.ids` (the comma-list of instantiated `<id>`s). Total new flat slots = 4; total pattern-key slots = 4 × up to 32 ids = up to 128, bounded by `MAX_PATTERN_KEYS`. **Related change inside an existing key:** extend the `panel.visible_widgets` enum-list at `sponge_configd/main.cc:248-250` from `{clock, launcher}` to `{clock, launcher, tasklist}` (the `tasklist` token is today accepted by `panel_widget.cc:325` but rejected by the configd validator; the write fails). Also fix the stale comment at `panel_widget.cc:311-314` which falsely claims the validator already accepts `tasklist`, that comment must be reworded to either (a) accept the Phase 16 change (`sponge_configd` accepts `tasklist` after W2 #7) or (b) drop the claim and let the panel-side comment say `Phase 14 W7 reserved the tasklist token; Phase 16 W2 extends the validator to accept it`. Total new flat slots = 4 + the enum-list extension = the validator widens by 1 token. Tier (b), **package-private provider settings** (third-party providers and package-scoped knobs): these are NOT in configd's registry at all. Each provider validates its own values in its own code and persists them in its own package-private store (a dedicated File_system-backed directory); the closed registry and the shared-DE ACLs do not apply, and third-party providers never receive a shared-DE configd write label. `sponge_pkgd` never relays config writes. | `AGENTS.md` §1.2 minimum privilege + Phase 11 validator parity for tier (a); tier (b) keeps package-private settings out of the global registry entirely (no central schema growth per package, no shared-DE write access for third parties, provider-owned validation and persistence). `background.image` allowlist is the F10 (path traversal) defense. The `tasklist` token extension closes a pre-Phase 16 latent bug (panel writes round-trip through configd today ONLY because no UI emits `tasklist` in the value; the new panel context menu's checkbox WOULD silently fail on the validator side without this change). |

---

## Non-Goals (explicitly excluded from Phase 16)

These items are EXPLICITLY out of scope. They are NOT absorbed
into Phase 16, NO code is written for them in any W item, and
DOCUMENTATION is amended only where it currently claims Phase 16
would deliver them (the `>>` markers below). Plan-discipline per
`AGENTS.md` §1.4 ("`Re-scoped` items carry a target phase; not
absorbed silently") and the Phase 14 U5 carryover matrix.

- **Sponge IME (multi-language / CJK input).** Phase 17 (the
  explicitly inserted next phase per `docs/09-roadmap.md`). No IME
  work in Phase 16; the shortcuts framework (D16.5) is a plain
  keyboard-shortcut layer with NO text-input composition.
- **GUI installer.** Phase 18 (the 0.3.0 release phase). The
  install path remains `./tool dist` + the existing run-script
  ISO/disk assembly (per `docs/13-installation.md`).
- **Multi-monitor.** Phase 15+ open (Phase 14 D14.9 carries it).
  Single-monitor baseline per `run/sponge-alpha.run`'s 1024x768
  fb. The dual `panel_top` / `panel_bottom` topology in D16.2 is
  ONE nitpicker screen with TWO panel domains, not two monitors.
- **System tray / additional panel applets.** Phase 15+ (Phase 14
  D14.7 carries it). The panel context menu (W5) offers
  height / visible_widgets / position only; clock + launcher +
  tasklist are the three shipped widgets; nothing else fits in
  Phase 16.
- **Hardware-specific items.** i440fx real (Phase 12 smoke-only),
  multi-namespace NVMe (Phase 12 deferred), Wi-Fi / rtl8169 /
  USB-Ethernet (Phase 12 deferred), USB keyboard glyph
  delta (Phase 12 deferred). All carried by `docs/15-hardware-
  compatibility.md`; not touched by Phase 16. **Exception: basic
  touchpad support** — including the required driver path — is
  now Phase 16 Extension criterion 25 and remains open until
  physical pointer movement + primary click is verified on real
  hardware. All other hardware deferrals are unchanged.
- **Per-app shortcut UI beyond the framework.** The Phase 16
  shortcuts framework (U16.4 / D16.5) ships with three initial
  bindings (Super / Alt-Tab / Escape). The user can write more
  bindings to `shortcuts.bindings` directly; the Shortcuts
  settings provider (its own provider window, its own
  `shortcuts.*` key domain per D16.1) provides a table-based
  editor for the same value (Phase 16 scope). There is NO
  per-app shortcut surface (e.g. "F5 in terminal opens an
  inline help overlay"), that is a per-app extension hook
  pattern that lives in Phase 17+ once the first consumer
  demands it.
- **themed_decorator as the release-media decorator.** The
  release media continues to ship the plain motif decorator
  (D16.6). The vendored sizer + minimizer patch lands as ledger
  row #17 and unblocks `sponge-de-themed-chrome-resize.run`
  (sibling acceptance), but the release-media regression gate
  for criterion 9 is `sponge-de-release-resize.run` on the motif
  decorator. Switching the release media to themed_decorator is
  a separate decision (decorator policy + boot-image size
  + theme-tar staging scope) recorded in
  `docs/05-sponge-de.md` §7 ODQ as Phase 15+ deferred.
- **ISO payload staging.** Falkon's 509 MiB payload (and any
  future package that exceeds the boot-module ceiling) does NOT
  land on ISO media (D16.7). ISO media remains metadata-only;
  IMG media is the install target. Future remote-repo payload-
  fetch (Phase 18+) is the longer-term resolution.
- **User-supplied background image upload.** The
  `background.image` allowlist (D16.10) defaults to a single
  ship-time path (`/system/background/default.png`). Adding more
  images requires packaging them (Phase 13 conventions); a
  runtime image picker + writable allowlist is Phase 17+.
- **Real-hardware multi-panel regression.** W8 proves the
  multi-panel topology on QEMU at `-m 4G`. The LG gram 17ZD90N
  real-hardware boot at `-m 8G` is Phase 17+ (the panel instance
  weight × 2 with heavy payload packages exceeds the QEMU
  envelope but is well within the hardware budget).
- **`vct live resource stats` (`vct status --resources`).**
  Phase 14 W11 #43 carried this; the implementation lands in
  Phase 17 (unrelated to the settings split; direct
  `vct settings list` / `vct settings open` already lands in
  W4 on the shared settings backend).

(W11's Paper-cut Disposition Appendix carries every Re-scoped
carryover with its target phase; this list is the
plan-level counterpart.)

---

## Work Items

### TDD convention (applies to every work item)

Every W item follows **test-first**: a focused run scenario stub
is added to `run/` BEFORE any source change; the stub fails on the
current tree (no PASS marker, or an explicit "expected FAIL"
sentry). The implementation commits then make the scenario PASS.
The final commit of every W item flips the scenario's PASS marker;
the W item is not "done" until that marker is reproduced on
base-sel4 in QEMU (`KERNEL=sel4 BOARD=pc`). Every new scenario
follows `run/qmp.inc` conventions; new QMP helpers (notably
`qmp_right_click`) land in `run/qmp.inc` first.

A scenario skeleton commit must include a **committed relative
symlink** at `repos/sponge/run/sponge-<name>.run -> ../../../run/
sponge-<name>.run` (mirrors the Phase 10/11/14/15 convention so the
Genode build repo-discovery picks the scenario up from the
`repos/sponge/` view too).

### Wave 1, Design, decisions ledger, scenario backbones

#### W1, Design-doc amendments, decisions ledger, criterion-7 reword

**Goal**: lock the design before any UI work. Every W2+ work
package reads its constraints from this ledger.

**Files touched**:

- `docs/09-roadmap.md` §10 Phase 16: rewrite criterion 7 to
  "bundled packages appear in the default panel's launcher menu
  (grouped by their metadata category) and launch correctly" (U16.1
  reword). Reclassify Phase 14 W11 paper-cut row #6 / row #14 from
  `Re-scoped → Phase 15+` to `Re-scoped → Phase 16` in the
  cross-reference table (U16.2).
- `docs/05-sponge-de.md` §3 module tree: add `background/` and
  rewrite the `settings/` slot as the settings bridge (the
  quick-menu change lifecycle only: ensure-start + per-provider
  quick request/result pairs per `docs/05-sponge-de.md` §5.4
  "Quick-menu change lifecycle", no config writes; the control
  center is the separate on-demand `sponge_settings` component
  per D16.3). §5 panel: add
  the context menu sub-section (U16.2 + D16.2 dual-domain
  topology). §5.4: the Settings control-center subsection
  (discovery → launch → provider-owned change, privileges, config
  ownership, direct launch, failure/removal). §7 ODQ: settle
  "Settings GUI backend" (D16.3 provider-owned control center),
  "Background surface" (D16.4), "Keyboard shortcut framework"
  (U16.4 + D16.5), "Multi-panel feasibility" (U16.5 + D16.5),
  "panel.position live" (U16.2 + D16.2). Strike `Re-scoped →
  Phase 15+` from those items.
- `docs/07-leitzentrale.md`: no change (Leitzentrale is unchanged;
  the criteria touch the DE, not lz).
- `docs/11-environment.md` §4: add a preamble paragraph stating the
  Phase 16 vendored policy: the base one-patch budget (row #17 for
  resize + minimizer) is historical, not a cap; any additional
  Extension patch must be individually justified, ledgered before
  application, independently revertable, and re-audited at the
  final Criterion 27 cleanup (D16.8). The actual row #17 entry
  lands in W9 with the patch, not here.
- `docs/13-installation.md` Known Limitations: add the IMG-only
  honest disclosure for bundled packages (D16.7); add the
  "Settings / panel context menu / multi-panel / keyboard shortcuts
  added in Phase 16" entry to the release-media feature list.
- `docs/plans/phase16-daily-desktop-defaults.md`: this plan lands
  first (the W1 commit), reviewed by Momus / Prometheus before W2.

**Tasks**:

1. Re-read `docs/09-roadmap.md` §10 Phase 16 verbatim, lock the 12
   criteria text into the plan's Goal Restatement (criterion 2
   lands reworded in place to the modular settings target per the
   2026-11-11 direct rewrite; criterion 7 per U16.1).
2. Apply the criterion-7 reword + the row #6 / #14 reclassification
   (U16.2).
3. Apply every docs/05 / docs/07 / docs/11 / docs/13 amend listed
   above.
4. Lock U16.1-U16.6 verbatim into the plan's Recorded Scope
   Rulings section; lock D16.1-D16.10 into the Binding Decisions
   table.
5. Author the TDD-convention paragraph + the per-W commit unit
   overview + the task dependency graph + the parallel execution
   graph + the verification contract + the commit strategy +
   the open questions + the UX-metrics appendix.

**Pass conditions**:

- `git diff --stat` matches exactly: `docs/09-roadmap.md`,
  `docs/05-sponge-de.md`, `docs/11-environment.md`,
  `docs/13-installation.md`, `docs/plans/phase16-daily-desktop-
  defaults.md`. No spurious edits.
- The plan's Goal Restatement carries the 12 criteria in roadmap
  order; byte-for-byte `diff` against the roadmap shows only the
  criterion-2 reword (modular settings target) and the criterion-7
  reword (U16.1) as differences.
- `docs/09-roadmap.md` row #6 / #14 reclassification is committed
  in the same commit as the criterion-7 reword.

**Commit units**:

- (docs) `docs(roadmap): reword Phase 16 criterion 7 per U16.1 +
  reclassify W11 #6/#14 to Phase 16 per U16.2`.
- (docs) `docs(sponge-de): §3 settings-bridge role + §5.4
  control-center subsection + §5 context menu + §7 ODQ closures
  (D16.2-D16.5, D16.3 provider-owned control center)`.
- (docs) `docs(env): §4 preamble, Phase 16 vendored policy
  (base row #17 historical; per-patch rule for Extension work
  per D16.8)`.
- (docs) `docs(install): Known Limitations, IMG-only bundled-
  packages honest disclosure (D16.7) + Phase 16 feature list`.
- (docs) `docs(plans): add phase16-daily-desktop-defaults.md`.

#### W2, configd registry re-architecture (pattern keys + MAX_KEYS + new keys + validators)

**Goal**: extend `sponge_configd` with the pattern-key array, the
MAX_KEYS bump, the four new flat keys, and the structured shortcuts
validator. Every new key is closed-registry-pure, with a typed
validator and a per-write probe scenario.

**Files touched**:

- `repos/sponge/src/sponge_configd/main.cc` (`MAX_KEYS = 32`,
  `MAX_PATTERN_KEYS = 32`, `_pattern_registry`, `_instantiated_ids`,
  `_apply_pattern_value`, `_shortcuts_validator`).
- `repos/sponge/src/sponge_configd/main.h` (new public types).
- `repos/sponge/src/sponge_configd/README.md` (registry table
  update; pattern-key paragraph; shortcuts key documented as the
  deliberate exception).
- `repos/sponge/src/test/configd_registry_probe/main.cc` (new
  probe; extends `run/sponge-configd-persist.run`'s probe family).
- `run/sponge-configd-pattern-keys.run` (new scenario stub first;
  PASS conditions below).
- `run/sponge-configd-shortcuts.run` (new scenario; structured
  shortcuts key validator).
- `run/sponge-configd-badkey.run` (new scenario; F15 validator-
  parity test: write `panel.visble_widgets` (typo), assert the
  structured error mentions `panel.visible_widgets` as the
  suggestion).
- `repos/sponge/run/sponge-configd-pattern-keys.run ->
  ../../../run/sponge-configd-pattern-keys.run` (symlink).
- `repos/sponge/run/sponge-configd-shortcuts.run ->
  ../../../run/sponge-configd-shortcuts.run` (symlink).
- `repos/sponge/run/sponge-configd-badkey.run ->
  ../../../run/sponge-configd-badkey.run` (symlink).

**Tasks**:

1. **Scenario stubs FIRST** (three commits, all FAIL on the current
   tree with the expected sentry marker):
   - `test(configd): add configd-pattern-keys probe + failing run
     scenario`, boot `sponge_configd` with `<vfs>` + bake, write
     `panel.ids=alpha,beta`, then write `panel.alpha.height=40`,
     assert the broadcast carries `panel.alpha.height=40` (the
     registry scan instantiates the pattern entry on first use);
     then write `panel.bogus!!id.height=40` and assert the
     structured error contains the charset rule. Final gate:
     `pattern-keys-probe: PASS`.
   - `test(configd): add configd-shortcuts probe + failing run
     scenario`, boot, write `shortcuts.bindings=launcher\tSuper
     focus_next\tAlt-Tab dismiss\tEscape`, assert the broadcast
     carries the value; write `shortcuts.bindings=garbage_action
     \tSuper`, assert the structured error and the
     `Genode::warning` line. Final gate: `shortcuts-probe: PASS`.
   - `test(configd): add configd-badkey probe + failing run
     scenario`, write `panel.visble_widgets=clock`, assert the
     structured error mentions `panel.visible_widgets`. Final
     gate: `badkey-probe: PASS`.
2. **MAX_KEYS bump**, change `MAX_KEYS = 16` to `MAX_KEYS = 32`
   in `sponge_configd/main.cc:91`; verify every existing scenario
   (`sponge-configd-persist.run`, `sponge-bake-firstboot.run`,
   `sponge-bake-reset.run`, the Phase 11/14 panel-config scenarios)
   still PASSes.
3. **Pattern-key infrastructure**, `_pattern_registry[
    MAX_PATTERN_KEYS]` array of `Key_def`s with name templates
    (`panel.<id>.height`, `panel.<id>.position`, `panel.<id>.visible_
    widgets`); `_instantiated_ids` map of instantiated per-id
    `Key_def`s; `_apply_pattern_value(key, value)` that parses
    `<id>` and dispatches to the matching validator; the `_value_
    valid` lookup walks flat registry first, then instantiated
    pattern entries, then the template registry (rejects unknown).
    **Instantiation timing rule**: pattern keys are instantiated
    SYNCHRONOUSLY at WRITE time (the `_apply_validated_value`
    path), never at READ time (the broadcast-generation path).
    The write path: (a) parses the `<id>` segment; (b) validates
    it against the charset `[a-z0-9_-]{1,16}` BEFORE the value
    validator runs (charset-fail returns the structured error
    immediately, the value is not touched); (c) checks
    `_instantiated_ids` for an existing entry; (d) if absent and
    `_pattern_registry` has capacity, clones the matching template
    `Key_def` into the instantiated slot (the `<id>` is the
    discriminator); (e) if absent and the registry is full,
    returns `pattern-keys-full`; (f) runs the cloned validator on
    the value. The broadcast path (`_generate_broadcast`) ONLY
    iterates the instantiated set; it never expands templates
    lazily. A read of an uninstantiated template name
    (`panel.<id>.height` where the `<id>` has never been written)
    is NOT in the broadcast; the key is only visible after a
    successful write.
4. **New flat keys**, add `background.color`, `background.image`,
    `shortcuts.bindings`, `panel.ids`. The first three get typed
    validators (`#RRGGBB` regex for color; allowlist match for
    image; structured callback for shortcuts). `panel.ids` is the
    comma-list string that bootstraps the pattern-key set (without
    it, no per-id keys are valid).
 5. **Extend `panel.visible_widgets` enum-list token set**, change
    `sponge_configd/main.cc:248-250` from `{ "clock", "launcher" }`
    (count 2) to `{ "clock", "launcher", "tasklist" }` (count 3)
    so the panel context menu's `tasklist` checkbox in W5 round-
    trips through configd. Verify with `run/sponge-configd-persist
    .run` extended: write `panel.visible_widgets=clock,tasklist`,
    assert the broadcast carries `clock,tasklist` (no silent
    fallback to `clock,launcher`). Also fix the stale comment at
    `repos/sponge/src/sponge-de/panel/panel_widget.cc:311-314`
    which currently says `the validator sponge_configd accepts
    "tasklist" as a valid token` (the validator does NOT today; the
    `panel_widget.cc:325` branch parses the token locally but a
    configd write including `tasklist` is rejected before the
    broadcast ever sees it). The corrected comment text: `Phase 16
    W2 #7 extends the validator to accept "tasklist"; the panel-
    side parse was already in place. The default theme ships
    "clock,launcher" unchanged; the tasklist is opt-in via the
    panel context menu's checkbox.`
 6. **Validator parity test**, `panel.visble_widgets` typo returns
    a structured error with `panel.visible_widgets` as the
    suggestion (mirror vct's existing validator behavior).
 7. **`sponge_configd/README.md` update**, registry table grows to
    14 rows (10 + 4 new) with the `panel.visible_widgets` enum-
    list extended to 3 tokens (row count unchanged; token count
    grows). Add a paragraph on the pattern-key mechanism. Add a
    paragraph on `shortcuts.bindings` as the documented exception
    to one-key-per-setting.
 8. **Bump `MAX_KEYS = 16` reference** in the Phase 14 plan + the
    `docs/11-environment.md` §4 preamble note.

**Pass conditions**:

- `run/sponge-configd-pattern-keys.run` PASS on base-sel4 + QEMU.
- `run/sponge-configd-shortcuts.run` PASS on base-sel4 + QEMU.
- `run/sponge-configd-badkey.run` PASS on base-sel4 + QEMU.
- `run/sponge-configd-persist.run` regression still PASSes
  (proves the MAX_KEYS bump didn't break the existing keys).
- `run/sponge-bake-firstboot.run` regression still PASSes
  (proves the bake path wasn't broken).
- `run/sponge-panel-config-sel4.run` regression still PASSes
  (proves the existing live keys are still applied).

**Commit units**:

- (test) `test(configd): add configd-pattern-keys probe + failing
  run scenario`.
- (test) `test(configd): add configd-shortcuts probe + failing
  run scenario`.
- (test) `test(configd): add configd-badkey probe + failing run
  scenario`.
- (feat) `feat(configd): bump MAX_KEYS 16 → 32 (interim; pattern
  keys live in a separate array)`.
- (feat) `feat(configd): add pattern-key registry (panel.<id>.
  {height,position,visible_widgets}) with charset validator`.
- (feat) `feat(configd): add background.color / background.image /
  shortcuts.bindings / panel.ids flat keys with typed validators`.
- (docs) `docs(configd): README registry table update + pattern
  key paragraph + shortcuts key exception note`.
- (test) each per-scenario PASS log receipt as
  `docs/evidence/task-2-phase16-configd-{pattern-keys,shortcuts,
  badkey}.log`.

#### W3, Bake wiring into product scenarios + first-boot acceptance scenario

**Goal**: every product scenario (`sponge-alpha.run`,
`sponge-desktop-disk.run`, `sponge-desktop-disk-nvme.run`,
`sponge-desktop-disk-uefi.run`, `sponge-desktop-disk-uefi-nvme.run`,
`sponge-desktop-disk-uefi-usb.run`) ships sponge_configd with
`<bake/>` + bake ROM routes + `bin/bake/*` boot modules. First boot
on the shipped media lands on the baked profile's values.

**Files touched**:

- `run/sponge-alpha.run` (line 627-638: add `<bake/>`; lines 631-637
  add bake ROM routes; lines 813-822 boot_modules list gains
  `bake/bake_manifest.json` + `bake/config.defaults` + `bake/
  theme.defaults`; remove the "today we just plant them on disk"
  comment at lines 805-811).
- `run/sponge-desktop-disk.run` (mirror the changes at line 519 +
  boot_modules list).
- `run/sponge-desktop-disk-nvme.run` (mirror).
- `run/sponge-desktop-disk-uefi.run` (mirror).
- `run/sponge-desktop-disk-uefi-nvme.run` (mirror).
- `run/sponge-desktop-disk-uefi-usb.run` (mirror).
- `run/sponge-desktop-defaults-firstboot.run` (new scenario; base-
  sel4 + QMP; first boot from baked IMG; asserts panel pixel +
  bake sentinel + theme + panel.height + visible_widgets + full
  launcher set).
- `repos/sponge/run/sponge-desktop-defaults-firstboot.run ->
  ../../../run/sponge-desktop-defaults-firstboot.run` (symlink).
- `repos/sponge/src/test/alpha_probe/main.cc` (line 240-274: extend
  the launcher-set assertion to all 7 desktop packages; per-pair
  timeout bounded by 30 s each, total 210 s ceiling).
- `docs/13-installation.md` quick-start tour: update the first-
  boot paragraph to mention the baked defaults + the `vct bake
  show` recommendation.

**Tasks**:

1. **Scenario stub FIRST**, `run/sponge-desktop-defaults-firstboot
   .run`: boot the `sponge-desktop-disk` image with
   `SPONGE_BAKE_PROFILE=desktop`; assert the alpha_probe extended
   set (7 desktop packages); assert configd broadcast carries
   `bake.profile=desktop`, `bake.version=1`, `bake.applied=yes`,
   `theme.active=default`, `panel.height=28`,
   `panel.visible_widgets=clock,launcher`, `clock.format=HH:mm`,
   `launcher.sort_by=alpha`. Stub fails on current tree with the
   named sentry marker: `defaults-firstboot-stub: FAIL (no
   bake-applied, alpha_probe extended set timed out at <pkg>)`.
   The exact `<pkg>` placeholder is the first non-hello desktop
   package the probe iterates (`terminal`, in the canonical
   iteration order `hello, terminal, textedit, files, calculator,
   pdf_view, falkon`). Root cause on the current tree: no
   `<bake/>` means `bake.applied=yes` never appears in the
   broadcast, so the structured-error sentinel
   `defaults-firstboot-stub: ...` is the gate the run script
   matches (the stub gates on the FAIL line, NOT on `alpha-probe:
   PASS`, the PASS marker is what the implementation commit
   flips).
2. **Apply the `<bake/>` + ROM routes + boot_modules changes** to
   each of the 6 product scenarios (the W3 wiring). Re-run
   `sponge-alpha.run` end-to-end; verify the alpha_probe extended
   set passes AND the configd broadcast carries every baked key.
3. **Extend alpha_probe** with the per-package launcher-set check
   (iterate the 7 desktop packages' `{name, category}` pairs;
   bounded timeout per pair; final gate `alpha-probe: PASS`).
4. **Author the new scenario's run script** with the boot image
   (use `RUN_OPT="--include image/disk"`); QEMU `-m 2G` (the
   sponge-desktop-disk default).
5. **Docs sync**, `docs/13-installation.md` quick-start tour.

**Pass conditions**:

- `run/sponge-desktop-defaults-firstboot.run` PASS on base-sel4 +
  QEMU; `alpha-probe: PASS` + the 8 configd-broadcast assertions
  byte-for-byte.
- `run/sponge-alpha.run` regression still PASSes (the bake wiring
  didn't break the existing probe).
- `run/sponge-desktop-disk.run` regression still PASSes.
- `run/sponge-desktop-disk-{nvme,uefi,uefi-nvme,uefi-usb}.run`
  regressions still PASS.
- `tool/dist --bake-profile desktop` still produces both ISO and
  IMG artifacts; the IMG size stays under the 2 GiB D15.5 budget.

**Commit units**:

- (test) `test(alpha): extend alpha_probe launcher-set assertion
  to all 7 desktop packages`, failing commit, then the
  implementation commit below.
- (feat) `feat(alpha): apply the bake wiring to all 6 product
  scenarios (sponge-alpha + 5 desktop-disk variants)`.
- (test) `test(defaults): add sponge-desktop-defaults-firstboot
  run scenario skeleton (failing)`.
- (feat) `feat(defaults): implement sponge-desktop-defaults-
  firstboot scenario assertions`.
- (docs) `docs(install): update quick-start tour first-boot
  paragraph + `vct bake show` recommendation`.

---

### Wave 2, Settings providers, panel menu, background, keyboard shortcuts

(Wave 2 starts after W1+W2+W3; the schema is the root for 9 of 12
criteria. Tasks in this wave are file-disjoint: settings shell in
`repos/sponge/src/sponge_settings/`, first-party providers in
`repos/sponge/src/settings_providers/`, panel menu in
`repos/sponge/src/sponge-de/panel/panel_widget.cc`,
background widget in `repos/sponge/src/sponge-de/background/`,
shortcuts in `repos/sponge/src/sponge-de/config/` / `sponge_de_main.cc`.)

#### W4, Settings control center + provider-owned settings architecture (criterion 2 target)

**Goal**: deliver D16.3 + D16.9 + D16.10 end-to-end: a separate
on-demand `sponge_settings` control-center shell; package-declared
settings entries validated/republished by `sponge_pkgd` (identity
`(package, settings-id)`); provider-owned settings windows with
per-label configd key ACLs; the quick-menu change lifecycle
behind the panel/background quick menus (ensure-start via pkgd on
the quick menu's own launch labels, closed domain operations over
per-provider quick-request / quick-result pairs;
`docs/05-sponge-de.md` §5.4); the first-party provider split (Panel,
Background, Theme, Shortcuts, Defaults, Display, WM, Terminal); a
third-party demo package with TWO settings entries; focus-on-second-
open; direct `vct settings list` / `open`; failure/removal
containment; and physical-hardware acceptance. The legacy monolithic
dialog module and the universal `SettingsController` writer on
`de_config_request` are removed by this split (D16.9) — not kept as
a compatibility writer.

**Files touched**:

- `repos/sponge/src/sponge_settings/` (new on-demand shell
  component: `main.cc`, `target.mk`, installed-metadata ROM
  reader, category-grouped entry view (closed enum + `other`
  fallback), launch client on the shell's own settings launch
  request/result labels). Capability surface: ROM (installed
  metadata), Report (launch request), ROM (launch result), Gui +
  Timer as needed. NO configd write channel, NO package
  File_system session, NO provider code loading, NO GL-heavy
  requirement.
- `pkg/sponge-de/` (the core first-party provider package/
  metadata — the registration source for pkgd discovery: declares
  the seven core `<settings>` entries (Panel, Background, Theme,
  Shortcuts, Defaults, Display, WM) and stages their provider
  binaries as package payloads. The seven providers remain
  SEPARATE components despite being declared by this one package —
  declaration grouping is a packaging convenience, not process
  merging).
- `repos/sponge/src/settings_providers/{panel,background,theme,shortcuts,defaults,display,wm}/`
  (seven first-party provider components; each owns its UI,
  validation, writes, lifecycle, errors; each writes shared DE
  keys on its own config request label — `panel_settings`,
  `background_settings`, `theme_settings`, `shortcuts_settings`,
  `defaults_settings`, `display_settings`, `wm_settings`; each
  serves its own quick-request / quick-result Report/ROM pair for
  the quick-menu change lifecycle (`docs/05-sponge-de.md` §5.4,
  "Quick-menu change lifecycle"): a closed operation vocabulary
  owned by the provider, with structured success/error results).
- The Terminal provider ships in the terminal package payload
  (`pkg/terminal/` declares its `<settings>` entry; Konsole or
  any arbitrary package provides its own provider the same way).
- `pkg/settings_demo/` (new third-party demo package with TWO
  `<settings>` entries, package-private store persistence, NO
  shared-DE configd write access).
- `repos/sponge/src/sponge_pkgd/main.cc` (`<settings>` entry
  validation/resolution: 0..N per package, unique `(package,
  settings-id)` stable across reinstall, title + closed category
  enum + optional icon/keywords + package-payload binary target;
  reject absolute paths, shell commands, interpreters, arbitrary
  Exec strings; republish the installed set via installed
  metadata; settings launch handling with live-instance tracking
  (second activation = focus/raise, never a refusal, never a
  duplicate; settings targets never exclusive; distinct
  start/lifecycle identity even when sharing the main binary);
  the quick menu's ensure-start requests arrive on the quick
  menu's own single-writer launch request/result labels
  (separate from the `sponge_settings` and `vct` labels) and use
  this same normal `(package, settings-id)` launch/focus path;
  pkgd starts/focuses only and never carries quick operations or
  their values).
- `repos/sponge/src/sponge_configd/main.cc` (per-label
  `config_request` ROM sessions; label-to-key-domain allowlist
  enforced on EVERY set; out-of-domain key returns the structured
  `key-outside-domain` error and never touches the store; the
  `vct` label remains the admin/manual escape hatch on every
  domain; third-party providers receive NO shared-DE configd
  label).
- `repos/sponge/src/vct/` (`settings` subcommand: `list` / `open`
  sharing the same backend (installed metadata + launch channel)
  with separate single-writer labels and `--json` support).
- `docs/12-package-format.md` (document the `<settings>` entry
  schema: 0..N per package, `(package, settings-id)` identity,
  title / closed category enum (`desktop`, `hardware`, `system`,
  `applications`, `other`; unknown/absent → `other`) / optional
  icon/keywords / package-payload binary target; the no-absolute-
  paths / no-shell / no-interpreter / no-Exec rule; settings
  targets' own start identity and never-exclusive rule).
- `repos/sponge/src/sponge-de/panel/panel_widget.cc` +
  `background/background_controller.{h,cc}` (quick-menu changes
  run the quick-menu change lifecycle, `docs/05-sponge-de.md` §5.4
  "Quick-menu change lifecycle": ensure-start of the owning
  provider via pkgd on the quick menu's own launch labels, then a
  closed domain operation over the per-provider quick-request /
  quick-result pair; sponge-de holds no config write session, and
  the quick pair is not an arbitrary key/value relay).
- `repos/sponge/src/sponge-de/settings/` and
  `repos/sponge/src/sponge-de/config/settings_controller.{h,cc}`
  (the legacy dialog module + the universal writer on
  `de_config_request` / `de_config_result` are REMOVED, retired
  with their `run/sponge-de-settings*.run` scenarios; the
  `settings/` slot is rewritten in place as the quick-menu
  change-lifecycle bridge: the ensure-start client on the quick
  menu's own launch request/result labels plus the per-provider
  quick-request / quick-result clients, NO config write session).
- `run/sponge-settings-center.run`, `run/sponge-settings-providers.run`,
  `run/sponge-settings-thirdparty.run`, `run/sponge-settings-failure.run`,
  `run/sponge-settings-vct.run` (new scenarios; stubs first) +
  the `repos/sponge/run/` symlinks.
- `run/qmp.inc` (`qmp_right_click <gx> <gy>` helper, reused from
  the earlier panel-menu work; bounded retry + `qmp_capabilities`
  handshake, mirrors `qmp_ps2_click`).

**Tasks**:

1. **Scenario stubs FIRST**, five failing scenarios (`sponge-
   settings-center`, `sponge-settings-providers`, `sponge-
   settings-thirdparty`, `sponge-settings-failure`, `sponge-
   settings-vct`), each with its named sentry marker on the
   current tree.
2. **Metadata + pkgd target lifecycle**, the `<settings>` entry
   schema (0..N per package, independent of the singular
   `<launcher>`): stable identity `(package, settings-id)` unique
   per package and stable across reinstall; title; category from
   a closed enum (`desktop`, `hardware`, `system`,
   `applications`, `other`; unknown/absent → `other`); optional
   icon/keywords; a package-payload binary target. Reject
   absolute paths, shell
   commands, interpreters, and arbitrary Exec strings at
   validation time. `sponge_pkgd` validates/resolves every entry
   and republishes the installed set via installed metadata;
   discovery in the shell refreshes on the ROM update signal,
   never polling. Settings targets get their own start/lifecycle
   identity distinct from the main app even when they share the
   main binary; settings targets are never exclusive.
3. **Launch + focus lifecycle**, the shell sends only `package` +
   `entry-id` over its own dedicated settings launch request/
   result labels; pkgd starts the resolved binary target as its
   own component. pkgd tracks live settings targets by
   `(package, settings-id)`: a second activation routes a focus/
   raise to the existing singleton window (window identity
   derives from `(package, settings-id)`), reports `focused`, and
   creates no duplicate — it is never an already-running refusal.
   Any package can join with no shell code changes and no central
   allowlist; no in-process plugins, no embedding. The quick
   menu's ensure-start runs over this same normal
   `(package, settings-id)` launch path on the quick menu's own
   single-writer launch request/result labels (separate from the
   `sponge_settings` and `vct` labels): a quick action first
   ensures the owning first-party provider is running (start or
   focus), then sends the closed domain operation over the
   per-provider quick-request / quick-result pair (quick-menu
   change lifecycle, `docs/05-sponge-de.md` §5.4). pkgd still
   only starts/focuses and never carries the operation or its
   value; if the provider exits later, the next quick action
   repeats ensure-start (no persistent-provider requirement, no
   refusal-when-closed).
4. **configd per-label key ACLs**, one `config_request` ROM
   session per writer label (`vct`, `panel_settings`,
   `background_settings`, `theme_settings`, `shortcuts_settings`,
   `defaults_settings`, `display_settings`, `wm_settings`,
   `terminal_settings`); configd's component config declares each
   label's key-domain allowlist (e.g. `panel_settings → panel.*,
   panel.<id>.*, panel.ids`) and enforces it on EVERY `set`:
   out-of-domain writes return the structured `key-outside-domain`
   error and never touch the store. `vct` keeps the admin/manual
   escape hatch (every domain). Third-party providers receive NO
   shared-DE configd label and use package-private stores.
   `sponge_pkgd` never relays config writes.
5. **`sponge_settings` shell**, the installed-metadata ROM
   reader + category-grouped entry view + launch client; Gui/
   Timer only as needed; NO configd write channel, NO package
   File_system session, NO provider code loading, NO GL-heavy
   requirement. Closing the shell reclaims its resources.
6. **First-party provider split**, the seven Sponge providers
   (Panel, Background, Theme, Shortcuts, Defaults, Display, WM)
   as separate components + the Terminal provider in the
   terminal package. Each provider owns its UI, validation,
   writes, lifecycle, and errors. The historical tab surfaces
   split among their owners (Panel → `panel.*`, Background →
   `background.*`, Theme → `theme.*`, Shortcuts → `shortcuts.*`,
   Defaults → the `bake.applied` reset path) — the tab set is
   not preserved as a monolith. Shared-DE keys stay in configd's
   closed typed registry (D16.10 tier (a)); the Defaults provider
   triggers the same `_reset_bake_keys` path vct's `bake reset`
   uses, on its own label.
7. **Remove the universal writer (D16.9)**, delete
   `repos/sponge/src/sponge-de/config/settings_controller.{h,cc}`,
   the `de_config_request` / `de_config_result` routes, and the
   legacy `repos/sponge/src/sponge-de/settings/` dialog module
   (their `run/sponge-de-settings*.run` scenarios retire with
   them). No compatibility writer is retained. The panel/
   background quick menus follow the quick-menu change lifecycle
   (`docs/05-sponge-de.md` §5.4, "Quick-menu change lifecycle"):
   ensure-start of the owning provider via pkgd on the quick
   menu's own launch labels when it is not running, then a closed
   domain operation (for example `Panel height=40`) over the
   per-provider quick-request / quick-result pair; the provider
   validates the operation and writes on its own ACL-limited
   label. This is not a replacement universal `SettingsController`
   and not an arbitrary config relay: each quick pair has a closed
   operation vocabulary owned by its provider, configd's
   label-to-key-domain ACL still enforces the final write, and
   the quick menu never gets a configd write channel. A start
   failure, a provider-unavailable condition, or a validation
   failure is surfaced without hanging the DE and leaves the
   config unchanged. sponge-de consumes domain broadcasts only.
8. **Third-party demo with TWO entries**, `pkg/settings_demo`
   declares exactly two `<settings>` entries (distinct
   `settings-id`s, e.g. `greeting` + `about`); both windows
   launch, validate, and persist in the package-private store
   (D16.10 tier (b)); persistence survives provider restart and
   one reboot. No shared-DE configd access on the demo's labels.
9. **Direct vct open**, `vct settings list` (with `--json`) reads
   the same installed metadata; `vct settings open` uses vct's
   own launch writer label and the same backend/launch channel as
   the shell; results map to the same structured errors
   (`not-installed`, `package-manager-unavailable`).
10. **Failure + removal tests**, (a) kill a provider: control
    center and sibling providers stay alive, the entry relaunches;
    (b) stop pkgd: open fails fast with `package-manager-unavailable`
    (cached discovery list may display; no hang/retry loop);
    (c) remove the demo package: its entries disappear on the
    next installed-metadata ROM update; a stale open attempt
    returns `not-installed`; (d) kill the control center: the DE
    (panel, background, windows) is unaffected; (e) quick-menu
    failure paths: a failed ensure-start (pkgd down or the
    provider unavailable) or a quick operation rejected by
    provider validation surfaces the structured error, leaves the
    config store unchanged, and the DE stays responsive.
11. **Physical-hardware acceptance — the complete Criterion 18
    matrix on physical hardware** (not a shell/provider smoke):
    the real-hardware run covers (1) two third-party entries
    discovered without reboot, (2) provider first paint / open,
    (3) package-private persistence across provider restart + one
    reboot, (4) shared-key live effect + outside-domain
    rejection, (5) second-open focus, (6) provider crash /
    relaunch, (7) pkgd-down fail-fast, (8) removal / stale open,
    (9) direct `vct settings list` / `open`, (10) control-center
    crash leaving the DE alive. Each physical receipt records
    machine, parent commit, Genode pin, image/scenario, first
    paint + an input-driven change (a click-driven provider-side
    edit whose effect is observed). Receipts land in
    `docs/evidence/phase16-settings-hardware.md` and are
    UNCHECKED until actually performed.

**Pass conditions** (target acceptance; none of these is claimed
until the scenarios actually pass):

- **Event-driven discovery without reboot**: adding/removing a
  settings entry shows up in an open control center via the
  installed-metadata ROM update signal alone — no reboot, no
  polling.
- **Two third-party entries**: both `settings_demo` entries are
  listed (category-grouped), launch, and are addressable by
  `(package, settings-id)`.
- **Provider first paint + provider-side change**: each
  first-party provider window paints on launch, and a change made
  in the provider UI takes effect (broadcast observed).
- **Cold provider start + quick-menu change**: with the owning
  provider not running, a quick-menu change (Panel *Height → 40*)
  ensures the provider starts via pkgd on the quick menu's own
  launch labels (normal `(package, settings-id)` path), then the
  closed `Panel height=40` operation lands over the panel
  quick-request / quick-result pair and the `panel.height=40`
  broadcast is observed. The W4 scenario vehicle is
  `sponge-settings-providers.run` (cold-path probe over the same
  ensure-start + quick-pair entry points the menu drives); once
  W5 lands, `run/sponge-panel-menu.run` drives the same path
  through the real menu. The physical panel context-menu
  acceptance (roadmap Extension criterion 19) continues to
  exercise this path.
- **Provider-start / validation failure leaves config unchanged
  and DE responsive**: a failed ensure-start (pkgd down, provider
  unavailable) or an operation rejected by provider validation
  surfaces a structured error, never hangs the DE, and leaves the
  config store unchanged (exercised by
  `sponge-settings-failure.run`).
- **Shared-key live effect + outside-domain write rejected**: a
  shared-key change (e.g. Panel provider height) applies live;
  a write attempted outside the provider's domain returns the
  structured `key-outside-domain` error and the store is
  unchanged.
- **Package-private persistence across provider restart and one
  reboot**: the demo package's package-private values survive
  both.
- **Second-open focus**: activating an already-open entry focuses
  and raises its existing singleton window; no duplicate window,
  no refusal.
- **Provider crash containment + relaunch**: a provider crash
  leaves the control center and siblings alive; the entry
  relaunches cleanly.
- **pkgd-down fail-fast**: with pkgd stopped, opening an entry
  fails fast with `package-manager-unavailable`; cached discovery
  is allowed but there is no hang or retry loop.
- **Package removal / stale open**: removal deletes the entries
  on the next metadata update; a stale open returns
  `not-installed`.
- **Direct vct open**: `vct settings list --json` and
  `vct settings open` work over the shared backend with separate
  writer labels.
- **Control-center crash not harming DE**: killing `sponge_settings`
  leaves the desktop (panel, background, open windows) fully
  functional.
- **Physical receipts (complete Criterion 18 matrix)**: the
  full matrix above runs on physical hardware — two third-party
  entries discovered without reboot, provider first paint/open,
  package-private persistence across provider restart + one
  reboot, shared-key live effect + outside-domain rejection,
  second-open focus, provider crash/relaunch, pkgd-down
  fail-fast, removal/stale open, direct vct list/open, and
  control-center crash leaving the DE alive. Each receipt
  records machine, parent commit, Genode pin, image/scenario,
  first paint + input-driven change; without all five fields per
  receipt the hardware acceptance is not satisfied.
- Regression anchors unchanged: `run/sponge-panel-config-sel4.run`
  and `run/sponge-de-sel4-interactive.run` still PASS (the live
  keys and the panel/launcher geometry are intact).

**Commit units**:

- (test) `test(settings): add 5 failing settings scenarios
  (center / providers / thirdparty / failure / vct) + qmp_right_click
  helper`.
- (feat) `feat(pkgd): <settings> entry validation + installed-
  metadata republish + launch/focus lifecycle ((package,
  settings-id) identity, never exclusive)`.
- (feat) `feat(configd): per-label config_request sessions +
  label-to-key-domain ACLs (key-outside-domain on violation)`.
- (feat) `feat(settings): sponge_settings on-demand shell
  (installed-metadata ROM, category view, launch client)`.
- (feat) `feat(settings-providers): first-party split (Panel /
  Background / Theme / Shortcuts / Defaults / Display / WM) +
  Terminal provider in the terminal package`.
- (feat) `feat(settings-providers): third-party settings_demo
  package with TWO entries + package-private store`.
- (feat) `feat(vct): settings list/open (--json) on the shared
  backend with separate writer labels`.
- (docs) `docs(pkg-format): document the <settings> entry schema
  (identity, category enum, binary target, safety rules)`.
- (chore) `chore(sponge-de): remove SettingsController +
  de_config_request routes + legacy settings/ dialog module
  (D16.9; no compatibility writer)`.
- (test) each per-scenario PASS log receipt as
  `docs/evidence/task-4-phase16-settings-{center,providers,
  thirdparty,failure,vct}.log`.
- (docs) `docs(sponge-de): §5.4 control-center subsection +
  §3 settings-bridge role`.

#### W5, Panel context menu + panel.position live via dual domains (U16.2 / D16.2)

**Goal**: right-click on the panel opens a `QMenu` with live
controls for height (spinbox), visible_widgets (3 checkboxes), and
position (radio group with top / bottom enabled; left / right
disabled per D16.2). The position change goes through the dual
nitpicker panel-domain topology (U16.2).

**Files touched**:

- `repos/sponge/src/sponge-de/panel/panel_widget.{h,cc}` (override
  `contextMenuEvent`; build the QMenu; each control's change runs
  the quick-menu change lifecycle, `docs/05-sponge-de.md` §5.4
  "Quick-menu change lifecycle": ensure-start of the Panel
  provider via pkgd on the quick menu's own launch labels, then a
  closed domain operation over the panel quick-request /
  quick-result pair).
- `repos/sponge/src/sponge-de/panel/panel_top_widget.{h,cc}` +
  `panel_bottom_widget.{h,cc}` (new; thin wrappers over
  `PanelWidget` with distinct object names so the show/hide
  toggle addresses them individually).
- `repos/sponge/src/sponge-de/sponge_de_main.{h,cc}` (instantiate
  the two panel widgets; route their Gui sessions to
  `label_last="Sponge Panel"` and `label_last="Sponge Panel
  Bottom"` respectively; subscribe both to `ConfigController`'s
  `panel.position` broadcast).
- `run/sponge-alpha.run:680-705` (the sponge-de start config: add
  the second Gui session route to `Sponge Panel Bottom`; add the
  nitpicker `<domain name="panel_top">` and `<domain name=
  "panel_bottom">` blocks; add the two `<policy>` rules routing
  the Gui sessions by `label_last`).
- `run/sponge-panel-menu.run` (new; base-sel4 + QMP; QMP right-
  click on the panel; assert the menu opens; click *Position →
  Top*; assert the bottom panel widget hides + the top panel
  widget shows; click *Height → 40*; assert configd carries
  `panel.height=40`). The Height step is a cold provider start +
  quick-menu change through the quick-menu change lifecycle
  (`docs/05-sponge-de.md` §5.4): the Panel provider is not
  running when the menu opens. The physical panel context-menu
  acceptance (roadmap Extension criterion 19) continues to
  exercise this path.
- `repos/sponge/run/sponge-panel-menu.run ->
  ../../../run/sponge-panel-menu.run` (symlink).

**Tasks**:

1. **Scenario stub FIRST**, `sponge-panel-menu.run` failing on
   the current tree (no `contextMenuEvent` override, the panel
   has no menu).
2. **`PanelWidget::contextMenuEvent`**, build the QMenu with
   three sections: Height (QSpinBox action, slider delegate with
   values 16..128 in 4 px steps), Visible widgets (QAction
   checkboxes for clock, launcher, tasklist), Position (QAction
   group with top / bottom / left / right; left/right disabled per
   D16.2). Each control's value-change signal runs the quick-menu
   change lifecycle (`docs/05-sponge-de.md` §5.4, "Quick-menu
   change lifecycle"): ensure-start of the Panel provider via
   pkgd on the quick menu's own launch labels when it is not
   running (start or focus; pkgd never carries the change), then
   the closed domain operation (for example `Panel height=40`)
   over the panel quick-request / quick-result pair; the provider
   validates the operation and writes `panel.*` on its own
   `panel_settings -> config_request` label (configd ACL per
   D16.1). A start failure or validation failure surfaces a
   structured error without hanging the DE and leaves the config
   unchanged. sponge-de holds no config write channel.
3. **Dual-domain panel topology**, instantiate two `PanelWidget`
   instances (`panel_top_widget`, `panel_bottom_widget`); route
   each to its own nitpicker domain; the `ConfigController`'s
   existing `panel.position` poll decides which widget is
   `QWidget::show()`'n and which is `QWidget::hide()`'n. The
   default `panel.position=bottom` keeps `panel_bottom_widget`
   visible and `panel_top_widget` hidden (no visible change from
   today's single-panel-on-top behaviour, the panel currently
   lives at `y=0..28`; the dual-domain topology means
   `panel_bottom_widget` is the visible one, rendered at `y=
   screen_h-28..screen_h`; the run scenario captures pixels at
   both locations to verify the geometry switch).
4. **nitpicker config**, `<domain name="panel_top" ypos="0"
   height="28" layer="2"/>` and `<domain name="panel_bottom"
   ypos="<screen_h - 28>" height="28" layer="2"/>`; the
   `<policy label_last="Sponge Panel" domain="panel_top"/>` +
   `<policy label_last="Sponge Panel Bottom" domain=
   "panel_bottom"/>` rules. Default screen is 1024×768
   (`run/sponge-de-sel4-interactive.run` baseline); the bottom
   domain's ypos is computed from `screen_h - 28` at scenario
   build time.
5. **`run/qmp.inc` `qmp_right_click`**, landed in W4; reused
   here.

**Pass conditions**:

- `run/sponge-panel-menu.run` PASS on base-sel4 + QMP;
  `panel-menu-probe: PASS` + the position-switch pixel assertion
  (bottom panel pixel sample at `(512, 740)` is non-background
  before switch, background after switch; top panel pixel sample
  at `(512, 14)` is the reverse).
- `run/sponge-alpha.run` regression still PASSes (the dual-domain
  topology didn't break the existing alpha_probe; the default
  `panel.position=bottom` keeps the bottom widget visible).
- `run/sponge-de-sel4-interactive.run` regression still PASSes
  (the panel + launcher geometry is unchanged at the default
  position).
- `run/sponge-panel-config-sel4.run` regression still PASSes (the
  live keys are still applied by `ConfigController`).

**Commit units**:

- (test) `test(panel): add sponge-panel-menu probe + failing run
  scenario`.
- (feat) `feat(sponge-de): add panel context menu (height spinbox,
  visible_widgets checkboxes, position radio group)`.
- (feat) `feat(sponge-de): dual nitpicker panel domains
  (panel_top / panel_bottom) for live panel.position (U16.2)`.
- (feat) `feat(run): wire dual panel domains in sponge-alpha.run
  + the 5 disk variants`.
- (docs) `docs(sponge-de): §5 panel context menu sub-section +
  dual-domain topology diagram`.

#### W6, Background widget + background context menu + background image (criteria 10, 11)

**Goal**: right-click on the uncovered background region opens a
context menu (Settings / Launch / Show desktop); a configurable
`background.color` + `background.image` paints the desktop surface
below all windows; the show-desktop toggle reuses the Phase 14 W7
tasklist state machine (U16.3).

**Files touched**:

- `repos/sponge/src/sponge-de/background/background_widget.{h,cc}`
  (new; the fullscreen frameless QWidget).
- `repos/sponge/src/sponge-de/background/background_controller.
  {h,cc}` (new; subscribes to the configd `background.color` +
  `background.image` broadcasts; owns the context menu).
- `repos/sponge/src/sponge-de/background/show_desktop.{h,cc}`
  (new; the minimize-all / restore-all state machine; piggybacks
  on the layouter-rule ROM overwrite from the tasklist).
- `repos/sponge/src/sponge-de/sponge_de_main.{h,cc}` (instantiate
  `BackgroundWidget`; subscribe to configd; route the Gui session
  to `label_last="Sponge Background"` → nitpicker `<domain
  name="default">` BELOW the window domains via `<layer>`).
- `run/sponge-alpha.run:680-705` (the sponge-de start config: add
  the third Gui session route to `Sponge Background`; the
  nitpicker `<domain name="default">` block already exists;
  ensure its `<layer>` is below `wm`'s).
- `run/sponge-de-bgmenu.run` (new; base-sel4 + QMP; QMP right-
  click at `(600, 400)`, uncovered region on 1024×768 with
  default panel position; assert the menu opens).
- `run/sponge-de-bgimage.run` (new; base-sel4 + QMP; write
  `background.image=/system/background/default.png` via the
  Background provider's settings window (its own
  `background_settings -> config_request` label); assert the
  backdrop Capture sample shows
  the Sponge-blue colour (RGB approx 24,72,144) within 30 frames
  post-write; write a second image from the allowlist; assert
  the pixel change).
- `run/sponge-de-bgimage-badpath.run` (new; F10 path-traversal
  defense; write `background.image=../../etc/passwd`; assert the
  structured error).
- `repos/sponge/run/sponge-de-bgmenu.run ->
  ../../../run/sponge-de-bgmenu.run` (symlink).
- `repos/sponge/run/sponge-de-bgimage.run ->
  ../../../run/sponge-de-bgimage.run` (symlink).
- `repos/sponge/run/sponge-de-bgimage-badpath.run ->
  ../../../run/sponge-de-bgimage-badpath.run` (symlink).
- `pkg/background/` (new package metadata; the allowlist default
  image `/system/background/default.png` staged at boot module
  time).
- `repos/sponge/src/sponge-de/sponge_de.pro` (add new sources).
- `run/sponge-alpha.run:127` + `:194-195` + `:820` (drop `app/
  backdrop` from the build list, drop the `genode_logo.png` cp,
  drop the boot module, see task 7 below).
- `run/sponge-leitzentrale.run:41` + `:101-102` (drop `app/
  backdrop` from the build list and drop the `genode_logo.png`
  cp, same change as above, the lz subsystem does not need a
  separate backdrop because the new in-DE widget paints below
  its window).
- `run/sponge-usb-boot.run:235` + `:302-303` + `:846` (drop
  `app/backdrop` from the build list, drop the `genode_logo.png`
  cp, drop the boot module, same change; USB-boot is a storage
  variant that does not need its own backdrop).

**Tasks**:

1. **Scenario stubs FIRST**, three failing scenarios as above.
2. **`BackgroundController`**, subscribes to configd's `background.
   color` + `background.image` broadcasts (the existing
   `ConfigController` signal slots; no new poll). On write
   through the Background provider to `background.image`, the
   provider validates against the allowlist
   `[ "/system/background/default.png" ]` (passed from its
   component config at boot time) before writing on its own
   `background_settings -> config_request` label.
3. **`BackgroundWidget`**, a `QWidget` with `Qt::WA_ShowWithout
   Activating` + `Qt::BypassWindowManagerHint` (frameless,
   never-focused), painted via `paintEvent` to fill the screen
   with the `background.color` solid colour or the
   `background.image` QImage. `contextMenuEvent` builds the
   three-item QMenu (Settings → opens/focuses the Background
   provider's settings window over the quick menu's own launch
   labels, the normal `(package, settings-id)` launch path of the
   quick-menu change lifecycle (`docs/05-sponge-de.md` §5.4;
   window identity `(sponge-de, background)`); Launch →
   `LauncherController::toggle_launcher()`;
   Show desktop → `ShowDesktop::toggle()`).
4. **`ShowDesktop::toggle`**, writes the layouter-rule ROM
   overwrite that moves every focused / visible window to off-
   screen `(x=-32000, y=-32000)` (the W7 tasklist minimize path);
   the second click restores the saved `(x, y, w, h)`. The
   state machine is the W7 state table from `docs/plans/wm-state-
   table.md` applied to ALL focused/visible windows at once.
5. **Allowlist + boot module**, `pkg/background/metadata.xml`
   declares a single boot module `default.png`; the scenario
   stages it via the `pkg_<name>.xml` glob in
   `run/sponge-alpha.run:830`. The allowlist is read from
   sponge-de's `<config>` `<background><allowlist>` block at
   boot time (default `[ "/system/background/default.png" ]`).
6. **nitpicker layer ordering**, `<domain name="default"
   layer="1"/>` is BELOW the wm's window domains (default
   `layer="3"`); the background widget paints BELOW windows.
7. **Remove `app/backdrop` from `sponge-alpha.run`** (build list
   at line 127, `genode_logo.png` cp at lines 194-195, boot
   modules entry at line 820). The build list loses the
   `app/backdrop` token; the cp command drops; the `genode_logo
   .png` token drops from the boot modules list. Verify
   `sponge-alpha.run` still PASSes (`alpha-probe: PASS`);
   rejection is a Phase-16 acceptance criterion (no behavior
   loss; backdrop is replaced by the in-DE widget).
8. **Remove `app/backdrop` from `sponge-leitzentrale.run`**
   (build list at line 41, `genode_logo.png` cp at lines 101-
   102). The lz subsystem does not need its own backdrop because
   the new in-DE widget paints below its window. Verify
   `sponge-leitzentrale.run` still PASSes (the Phase 12
   Leitzentrale acceptance marker; `lz-probe: PASS` or
   whatever the current marker is named).
9. **Remove `app/backdrop` from `sponge-usb-boot.run`** (build
   list at line 235, `genode_logo.png` cp at lines 302-303, boot
   modules entry at line 846). The USB-boot scenario is a
   storage variant; it does not need its own backdrop. Verify
   `sponge-usb-boot.run` still PASSes.
10. **Documentation sync**, add a paragraph to `docs/13-installation
    .md` Known Limitations: `app/backdrop` is replaced by the
    in-DE `background/` widget in Phase 16; the static
    `genode_logo.png` no longer paints on the desktop; the
    first boot from baked media shows the live widget in the
    theme's `panel_bg` colour by default.

**Pass conditions**:

- `run/sponge-de-bgmenu.run` PASS on base-sel4 + QMP;
  `bgmenu-probe: PASS` after the QMP right-click at `(600, 400)`.
- `run/sponge-de-bgimage.run` PASS on base-sel4 + QMP;
  `bgimage-probe: PASS` after both image writes.
- `run/sponge-de-bgimage-badpath.run` PASS on base-sel4 + QMP;
  the structured error appears in the configd broadcast
  (`config_request` rejects the write; the broadcast does NOT
  carry the bad path).
- `run/sponge-alpha.run` regression still PASSes (the third Gui
  session + the layer-1 default domain didn't break the existing
  probe; `app/backdrop` is removed).
- `run/sponge-leitzentrale.run` regression still PASSes
  (`app/backdrop` removed; the lz subsystem's Leitzentrale window
  is unchanged).
- `run/sponge-usb-boot.run` regression still PASSes (`app/backdrop`
  removed; the storage variant boot chain is unchanged).

**Commit units**:

- (test) `test(bg): add background_widget probe + 3 failing run
  scenarios (bgmenu / bgimage / bgimage-badpath)`.
- (feat) `feat(sponge-de): add BackgroundController +
  BackgroundWidget (color + image + context menu)`.
- (feat) `feat(sponge-de): add ShowDesktop toggle (W7 state
  machine reuse)`.
- (feat) `feat(sponge-de): wire BackgroundController into Main +
  third Gui session route to default domain layer=1`.
- (feat) `feat(pkg): add pkg/background/ allowlist default image`.
- (feat) `feat(run): wire pkg/background in sponge-alpha.run boot
  modules`.
- (chore) `chore(run): remove app/backdrop + genode_logo.png from
  sponge-alpha.run (build list + cp + boot modules)`.
- (chore) `chore(run): remove app/backdrop + genode_logo.png from
  sponge-leitzentrale.run (build list + cp)`.
- (chore) `chore(run): remove app/backdrop + genode_logo.png from
  sponge-usb-boot.run (build list + cp + boot modules)`.
- (docs) `docs(sponge-de): §6 background sub-section + the
  allowlist contract`.
- (docs) `docs(install): Known Limitations update for the
  app/backdrop → in-DE background/ widget replacement`.

#### W7, Keyboard shortcut framework (U16.4 / D16.5)

**Goal**: an extensible keyboard-shortcut framework driven by the
`shortcuts.bindings` configd key. Initial shipped bindings:
Super → launcher, Alt-Tab → focus next, Escape → dismiss.

> **Landed-state note (2026-09-19, supersedes the relay design
> below):** W7 is complete and live-verified, with two binding
> platform findings recorded in
> `docs/evidence/phase16-w7-shortcuts.md`: (1) event_filter's
> `<report>` source does not compose with `<merge>`/`<chargen>` —
> the shortcut scenarios drop chargen from their event_filter
> config; (2) report_rom ROM sessions opened before the backing
> report's first write never receive content — so the captured
> bindings are baked into the scenarios' static
> `event_filter.config` and event_filter does NOT reconfigure
> from the controller's dynamic emit. Runtime rebinding of the
> captured keys and shortcut+chargen coexistence on the product
> media are Phase 17+ items. The framework (configd validation,
> persistence, Settings UI, dispatch) is fully dynamic.

**Files touched**:

- `repos/sponge/src/sponge-de/config/shortcut_controller.{h,cc}`
  (new; subscribes to `shortcuts.bindings`; parses the structured
  value; installs the key listeners via sponge-de's existing
  event-filter chain, same path as Phase 14 W9's stability
  watchdog).
- `repos/sponge/src/sponge-de/sponge_de_main.{h,cc}` (instantiate
  the controller; connect the actions to `LauncherController::
  toggle_launcher()`, `TasklistController::cycle_focus()`, and a
  new `Dismisser::dismiss_topmost()` slot).
- `repos/sponge/src/sponge-de/launcher/launcher_controller.{h,cc}`
  (new `toggle_launcher()` slot).
- `repos/sponge/src/sponge-de/panel/tasklist/tasklist_controller.
  {h,cc}` (new `cycle_focus()` slot, extends the existing W7
  state machine with an Alt-Tab-driven focus shift).
- `repos/sponge/src/sponge-de/config/dismisser.{h,cc}` (new;
  walks the wm `window_list` ROM in reverse-stack order, finds
  the topmost popover (launcher popup / provider settings window
  / context menu), and closes it).
- `run/sponge-de-shortcuts.run` (new; base-sel4 + QMP; QMP
  `send-key super` → assert `launcher.toggle` opens; `send-key
  escape` → assert it closes; `send-key alt-tab` → assert focus
  moves to the next window).
- `run/sponge-de-shortcuts-extend.run` (new; base-sel4 + QMP;
  write `shortcuts.bindings=launcher\tSuper focus_next\tAlt-Tab
  dismiss\tEscape ctrl_alt_t\tCtrl-Alt-T` via configd; assert
  the new Ctrl-Alt-T binding fires).
- `repos/sponge/run/sponge-de-shortcuts.run ->
  ../../../run/sponge-de-shortcuts.run` (symlink).
- `repos/sponge/run/sponge-de-shortcuts-extend.run ->
  ../../../run/sponge-de-shortcuts-extend.run` (symlink).

**Tasks**:

1. **Scenario stubs FIRST**, two failing scenarios as above.
2. **`ShortcutController`**, subscribes to the `shortcuts.bindings`
   broadcast; parses the structured value into a `Genode::Vector`
   of `{action, key_sequence}` pairs; installs the key listeners
   via sponge-de's existing event-filter chain (F9 defense:
   single listener, no double-fire).
3. **Action slots**, `LauncherController::toggle_launcher()`
   (extends the existing launcher toggle from Phase 10 W7);
   `TasklistController::cycle_focus()` (extends the existing
   tasklist state machine with an Alt-Tab-driven focus shift);
   `Dismisser::dismiss_topmost()` (new; walks the wm `window_
   list` ROM in reverse-stack order, finds the topmost popover,
   closes it).
4. **GUI-thread marshaling**, every shortcut action routes
   through `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
   (Phase 11 risk #2 codification).
5. **`Dismisser`**, the dismiss action handles launcher popup,
   provider settings windows, and the panel/background context
   menus; the list is closed in priority order.

**Pass conditions**:

- `run/sponge-de-shortcuts.run` PASS on base-sel4 + QMP;
  `shortcuts-probe: PASS` after all three key sequences.
- `run/sponge-de-shortcuts-extend.run` PASS on base-sel4 + QMP;
  `shortcuts-extend-probe: PASS` after the new binding fires.
- `run/sponge-de-sel4-interactive.run` regression still PASSes
  (the event-filter chain is unchanged for the default input
  flow).
- `run/sponge-panel-menu.run` regression still PASSes (the panel
  context menu QMenu opens + closes via the existing W5 path).
- `run/sponge-de-bgmenu.run` regression still PASSes (the
  background context menu QMenu opens + closes via the existing
  W6 path).

**Commit units**:

- (test) `test(shortcuts): add shortcut_controller probe + 2
  failing run scenarios (shortcuts / shortcuts-extend)`.
- (feat) `feat(sponge-de): add ShortcutController (subscribes to
  shortcuts.bindings; parses structured value; installs event-
  filter listeners)`.
- (feat) `feat(sponge-de): add LauncherController::toggle_launcher
  + TasklistController::cycle_focus + Dismisser::dismiss_topmost
  action slots`.
- (feat) `feat(sponge-de): wire ShortcutController into Main +
  GUI-thread marshaling (QMetaObject::invokeMethod)`.
- (docs) `docs(sponge-de): §4 keyboard shortcut framework + U16.4
  shipped bindings`.

#### W8, Multi-panel generalization (U16.5 / D16.5)

**Goal**: arbitrary panel count on arbitrary edges, per-panel
config namespace `panel.<id>.{height,position,visible_widgets}`,
distinct Gui label suffix per instance (F5 defense).

**Files touched**:

- `repos/sponge/src/sponge-de/panel/panel_collection.{h,cc}` (new;
  a singleton that owns N `PanelWidget` instances; subscribes to
  the configd `panel.ids` broadcast; creates / destroys panel
  instances on `<id>` add / remove).
- `repos/sponge/src/sponge-de/sponge_de_main.{h,cc}` (replace the
  single-panel instantiation in W5 with the `PanelCollection`; the
  default `panel.ids=` empty means 0 explicit panels, the
  collection instantiates the default bottom panel at boot).
- `run/sponge-alpha.run:680-705` (add the third + fourth Gui
  session route to `Sponge Panel 1` and `Sponge Panel 2`; the
  nitpicker `<domain name="panel_1" layer="2"/>` and `<domain
  name="panel_2" layer="2"/>` blocks; the matching `<policy
  label_last="..." domain="..."/>` rules).
- `run/sponge-de-multipanel.run` (new; base-sel4 + QMP; write
  `panel.ids=alpha,beta` via the Panel provider's settings
  window (its own `panel_settings` label); write
  `panel.alpha.position=top`, `panel.alpha.height=40`,
  `panel.beta.position=bottom`, `panel.beta.visible_widgets=clock`;
  assert each panel's widget is in the right nitpicker domain via
  Capture; click on each panel independently and assert the
  click registers on the correct panel's entry point; `-m 4G`).
- `run/sponge-de-multipanel-idspace.run` (new; F5 + charset
  defense; write `panel.bogus!!id.height=40`; assert the
  structured error and the `Genode::warning`).
- `repos/sponge/run/sponge-de-multipanel.run ->
  ../../../run/sponge-de-multipanel.run` (symlink).
- `repos/sponge/run/sponge-de-multipanel-idspace.run ->
  ../../../run/sponge-de-multipanel-idspace.run` (symlink).

**Tasks**:

1. **Scenario stubs FIRST**, two failing scenarios as above.
2. **`PanelCollection`**, singleton owning N `PanelWidget`
   instances; subscribes to `panel.ids`; on every add /
   remove, creates / destroys the corresponding `PanelWidget`
   with a unique `objectName` and a unique `label_last` for the
   Gui session.
3. **nitpicker config**, `<domain>` blocks generated at scenario
   build time based on `panel.ids` (or statically declared for
   `alpha` and `beta`); the per-panel ypos / xpos is the panel's
   own `position` + `height` (default bottom).
4. **Per-panel widget clicks**, each `PanelWidget` listens on
   its own Gui session; the click registers on the correct panel
   only (F5 defense); the run scenario's QMP clicks verify this
   with an exact per-panel assertion: after writing
   `panel.ids=alpha,beta` via the multi-panel UI (W8 task 2),
   `panel.alpha.height=40` + `panel.alpha.position=top` +
   `panel.beta.height=28` + `panel.beta.position=bottom` (the
   default bottom), the run scenario dispatches `qmp_click 100 14`
   (panel-alpha's clicked-coordinate at its top-edge y=14); the
   probe asserts ONLY `panel_alpha.click_count=1` increments
   (where the new `panel_alpha` + `panel_beta` reporters carry a
   monotonically-increasing `click_count` attribute per
   PanelWidget instance, distinct from the existing `panel_click`
   reporter which collapses across instances). The probe then
   dispatches `qmp_click 800 740` (panel-beta's bottom y=740 at
   default screen 1024×768 and panel.height=28); the probe
   asserts ONLY `panel_beta.click_count=1` increments
   (`panel_alpha.click_count` stays at 1). A failure of either
   cross-panel assertion is the F5 label_prefix trap signature
   and is a HARD FAIL.
5. **RAM budget**, `-m 4G` for `sponge-de-multipanel.run`
   (4 GiB QEMU; two panel instances + wm + layouter + decorator +
   configd + themed + pkgd fit comfortably).

**Pass conditions**:

- `run/sponge-de-multipanel.run` PASS on base-sel4 + QMP at
  `-m 4G`; `multipanel-probe: PASS` + the per-panel click
  assertion.
- `run/sponge-de-multipanel-idspace.run` PASS on base-sel4 + QMP;
  the structured error appears in the configd broadcast.
- `run/sponge-alpha.run` regression still PASSes (the default
  empty `panel.ids` means the collection instantiates only the
  default bottom panel; behavior is unchanged).
- `run/sponge-panel-menu.run` regression still PASSes (the dual
  panel domains in W5 are absorbed into the collection).

**Commit units**:

- (test) `test(panel): add PanelCollection probe + 2 failing run
  scenarios (multipanel / multipanel-idspace)`.
- (feat) `feat(sponge-de): add PanelCollection (owns N
  PanelWidgets; subscribes to panel.ids; creates / destroys on
  id add / remove)`.
- (feat) `feat(run): wire per-panel Gui session routes + nitpicker
  domains in sponge-alpha.run + the 5 disk variants`.
- (docs) `docs(sponge-de): §5 multi-panel sub-section + RAM budget
  table`.

---

### Wave 3, Mouse resize + vendored patch + release-media regression

#### W9, Mouse window resize (criterion 9) + vendored themed_decorator sizer + minimizer patch (ledger row #17)

**Goal**: criterion 9 closes via two coordinated changes: (a) the
per-package metadata opt-in (`<resizeable="yes"/>`) plus the app-
side `resize_request` ROM subscriber, proven on the release-media
topology (motif decorator, which already draws sizers); (b) the
vendored themed_decorator patch that adds sizer + minimizer (one
ledger row #17, closes D14.8(d) in the same patch). Release media
stays on the motif decorator.

**Criterion-9 headline acceptance**: `run/sponge-de-release-resize
.run` (motif decorator, release-media topology) is the SINGLE gate
that closes criterion 9 end-to-end on the release-media topology
the user actually ships. `run/sponge-de-themed-chrome-resize.run` is
a SIBLING acceptance for the themed_decorator extension (proves
the vendored patch works), but it does NOT replace the release-
media gate. The criterion-completion marker is
`release-resize-probe: PASS`. W10 / W12 use this scenario in the
release-media regression sweep.

**Files touched**:

- `docs/12-package-format.md` §X: document the `<resizeable=
  "yes"/>` attribute on the `<rom>` element of a package's
  metadata (closed list of apps that opt in: textedit, files,
  calculator, terminal, NOT falkon in Phase 16 because of its
  WebEngine constraint).
- `pkg/textedit/metadata.xml`, `pkg/files/metadata.xml`,
  `pkg/calculator/metadata.xml`, `pkg/terminal/metadata.xml`
  (add `<resizeable="yes"/>`).
- `repos/sponge/src/sponge_pkgd/main.cc:_do_launch` (carry
  `resizeable="yes"` to the generated layouter `<assign>` block
  for the package's window).
- `repos/sponge/src/sponge-de/launcher/launcher_controller.{h,cc}`
  + `repos/sponge/src/sponge_files/main.cc` (or equivalent
  ResizeSubscriber shim), small subscriber that reads the
  `resize_request` ROM and applies it to the QWidget geometry.
- `genode/repos/gems/src/app/themed_decorator/theme.h:53`
  (extend the `Element_type` enum: add `SIZER_NW`, `SIZER_NE`,
  `SIZER_SW`, `SIZER_SE`, `MINIMIZER`).
- `genode/repos/gems/src/app/themed_decorator/theme.cc` (new
  `sizer_nw`, `sizer_ne`, `sizer_sw`, `sizer_se`, `minimizer`
  texture load + dispatch).
- `genode/repos/gems/src/app/themed_decorator/window.h:136-137`
  (add `_sizer_*` member Elements; mirror the maximizer Element).
- `genode/repos/gems/src/app/themed_decorator/window.cc:280-283`
  (render the sizer affordances; emit the new hover flags the
  layouter already consumes).
- `tool/decor_assets_data/pngs/` (4 new sizer PNGs + 1 minimizer
  PNG).
- `tool/decor_assets_data/metadata.txt` (add the 4 sizer +
  minimizer entries).
- `docs/patches/themed-decorator-resize-minimize.patch` (the
  durable record; re-applied on vendored re-import).
- `docs/11-environment.md` §4 (new row #17; ledger preamble note
  per W1; drop-when: "upstream Genode adds resize + minimizer to
  themed_decorator").
- `run/sponge-de-themed-chrome-resize.run` (new; base-sel4 + QMP;
  themed_decorator with the vendored patch; QMP drag on 8 hit-
  zones (4 edges + 4 corners) plus a `<minimizer/>` click;
  assert `window_layout` reports the new geometry + the
  tasklist entry dims).
- `run/sponge-de-release-resize.run` (new; base-sel4 + QMP; the
  motif decorator; QMP drag on the sizer of a `resizeable="yes"`
  package window; assert the window's geometry changes).
- `repos/sponge/run/sponge-de-themed-chrome-resize.run ->
  ../../../run/sponge-de-themed-chrome-resize.run` (symlink).
- `repos/sponge/run/sponge-de-release-resize.run ->
  ../../../run/sponge-de-release-resize.run` (symlink).

**Tasks**:

1. **Scenario stubs FIRST**, two failing scenarios as above.
2. **Per-package metadata opt-in**, apply `<resizeable="yes"/>`
   to textedit, files, calculator, terminal. Verify each per-
   package boot scenario (`sponge-textedit.run`,
   `sponge-files.run`, `sponge-calculator.run`,
   `sponge-terminal.run`) still PASSes after the metadata
   change (the generated layouter `<assign>` must carry the
   resizeable flag without breaking the existing focus /
   drag path).
3. **`sponge_pkgd` change**, the generated layouter config
   carries `resizeable="yes"`. The change is in the `<assign
   label_prefix="..." resizeable="yes" target="screen" .../>`
   block the launcher-config generator emits.
4. **App-side `resize_request` subscriber**, a small
   `ResizeSubscriber` shim in each opt-in package. **Open
   question (verify in W9):** does Qt6 QPA already resize
   content on Gui window resize? If yes, the shim is a no-op
   for Qt6 apps and only the metadata opt-in is needed. Verify
   by booting `sponge-textedit.run` with the metadata change
   and observing the QWidget geometry when the resize_request
   ROM fires.
5. **Vendored patch (ledger row #17), DOCUMENT FIRST.** The
   `docs/patches/themed-decorator-resize-minimize.patch` file
   is committed before any vendored-tree modification. The
   `docs/11-environment.md` §4 row #17 entry is committed in
   the same commit as the patch file (per W1's preamble note
   that "patches never absorbed silently"). The actual vendored-
   tree edit is applied in the next commit (the `git apply`
   step).
6. **QGenodeScreen / Qt6 QPA open question**, verify whether
   the QPA already resizes content on Gui window resize; if not,
   add a minimal Qt-side patch in `repos/sponge/src/qt6_base/`
   (Sponge-side, NOT vendored) to wire `QPlatformWindow::set
   Geometry()` to the Genode resize_request ROM.

**Pass conditions**:

- `run/sponge-de-themed-chrome-resize.run` PASS on base-sel4 +
  QMP; `themed-chrome-resize-probe: PASS` + 8 hit-zone drag
  assertions + 1 minimizer assertion (sibling acceptance for the
  vendored themed_decorator patch; NOT the criterion-9 headline).
- `run/sponge-de-release-resize.run` PASS on base-sel4 + QMP;
  `release-resize-probe: PASS` + the per-package drag assertion
  on the motif-decorator release-media topology. **This is the
  criterion-9 headline gate** (motif decorator ships on the
  release media; the regression sweep in W12 re-runs it).
- `run/sponge-de-themed-chrome.run` regression still PASSes
  (the vendored patch adds sizer + minimizer; the existing
  themed-chrome scenario still asserts the title-bar tint +
  drag).
- `run/sponge-textedit.run` / `run/sponge-files.run` /
  `run/sponge-calculator.run` / `run/sponge-terminal.run`
  regressions still PASS.
- `run/sponge-alpha.run` regression still PASSes.

**Commit units**:

- (docs) `docs(patch): add docs/patches/themed-decorator-resize-
  minimize.patch (durable record) + docs/11-environment.md §4
  row #17 entry (U16 + D16.6)`, the ledger row is committed
  BEFORE the vendored edit.
- (vendor) `vendor(genode): apply themed_decorator sizer +
  minimizer patch (ledger row #17)`, the git apply step.
- (test) `test(launcher): add release-resize probe + failing run
  scenario`.
- (feat) `feat(pkgd): carry resizeable=yes to the generated
  layouter <assign> block`.
- (feat) `feat(pkg): add <resizeable="yes"/> to textedit,
  files, calculator, terminal metadata`.
- (feat) `feat(sponge-de): add ResizeSubscriber shim (Qt6 apps
  that need it; verified open question in W9)`.
- (test) `test(themed-chrome): add themed-chrome-resize probe +
  failing run scenario`.
- (docs) `docs(pkg-format): document the <resizeable="yes"/>
  metadata attribute`.

#### W10, Release-media regression gates

**Goal**: prove every release-media-topology claim survives
Phase 16: criterion 5 (title-bar drag, regression), criterion 8
(new windows: focus / stacking / decoration, discrete initial-
state gate), criterion 6 + 7 (bundled packages, extend
alpha_probe to assert all 7 desktop packages appear with declared
categories), **criterion 9 headline** (`run/sponge-de-release-resize
.run`, motif decorator on the release-media topology). W10's job
on criterion 9 is to re-run the headline scenario as part of the
release-media regression sweep (so the criterion-9 gate is itself
the regression gate, not a sibling).

**Files touched**:

- `repos/sponge/src/test/sponge_de_probe/main.cc` (new phases:
  `_phase_panel_position` exercising the W5 dual-domain
  switch; `_phase_bgmenu` exercising the W6 context menu at
  `(600, 400)`; `_phase_settings_provider` exercising the W4
  control center's entry listing + provider launch; `_phase_shortcut_super` exercising
  the W7 Super binding).
- `repos/sponge/src/test/alpha_probe/main.cc` (extend to assert
  every desktop package's `{name, category}` pair).
- `run/sponge-de-themed-chrome.run` (regression only; no change).
- `run/sponge-wm-qmp.run` (regression only; no change).
- `run/sponge-de-release-resize.run` (criterion-9 headline gate;
  no change; this is the release-media run W10 re-runs).
- `run/sponge-alpha.run` (regression only; the W3 bake wiring is
  exercised; the W4 / W5 / W6 / W7 additions are exercised by
  their own scenarios).

**Tasks**:

1. **`alpha_probe` extended launcher-set**, the same change
   that lands in W3 commit `test(alpha): extend alpha_probe
   launcher-set assertion to all 7 desktop packages`.
2. **`sponge_de_probe` new phases**, append after the existing
   `_phase_panel_config` (line 962) per the existing
   `phases="..."` config attribute convention; each new phase is
   bounded by a per-phase `run_genode_until` timeout.
3. **`_phase_panel_position`**, QMP right-click on the panel →
   click *Position → Top* → assert pixel at `(512, 14)` is
   non-background AND pixel at `(512, 740)` is background.
4. **`_phase_bgmenu`**, QMP right-click at `(600, 400)` → assert
   the context menu opens (panel reporter `bgmenu.open=yes`).
5. **`_phase_settings_provider`**, QMP open the W4 control
   center → launch the Panel provider entry → spin the height
   spinbox in the provider window → assert configd carries
   `panel.height=<new value>`.
6. **`_phase_shortcut_super`**, QMP `send-key super` → assert
   the launcher popup opens (panel reporter
   `launcher.open=yes`); QMP `send-key escape` → assert it
   closes.

**Pass conditions**:

- `run/sponge-alpha.run` PASS on base-sel4 + QMP; the extended
  alpha_probe carries 7 of 7 desktop packages' `{name,
  category}` pairs AND the 8 configd broadcast assertions
  (`bake.profile=desktop`, `bake.version=1`, `bake.applied=yes`,
  `theme.active=default`, `panel.height=28`,
  `panel.visible_widgets=clock,launcher`, `clock.format=HH:mm`,
  `launcher.sort_by=alpha`) byte-for-byte. `alpha-probe: PASS`.
- `run/sponge-de-themed-chrome.run` regression still PASSes
  (the title-bar drag is unchanged on the themed chrome).
- `run/sponge-wm-qmp.run` regression still PASSes (the title-bar
  drag on the plain motif decorator is unchanged, criterion 5).
- `run/sponge-de-sel4-interactive.run` regression still PASSes.
- `run/sponge-launch.run` regression still PASSes (criterion 8:
  new windows open correctly via the launch path).

**Commit units**:

- (test) `test(de-probe): add _phase_panel_position +
  _phase_bgmenu + _phase_settings_provider + _phase_shortcut_super
  to sponge_de_probe`.
- (feat) `feat(alpha): the W3 extended launcher-set commit
  (already counted in W3)`.

---

### Wave 4, Paper-cut sweep + close-out

#### W11, Paper-cut disposition matrix (Phase 14/15 carryovers touching Phase 16 paths)

**Goal**: apply the 4-way classification (Resolved in 16 /
Re-scoped / Blocked / Not-a-defect) to every Phase 14/15
carryover item that touches a Phase 16 code path. Only
`Resolved in 16` items get implementation work; `Re-scoped`
items carry a target phase (17 / 18 / 19+).

**Files touched**:

- `docs/plans/phase16-daily-desktop-defaults.md` (this plan;
  new Paper-cut Disposition Appendix table).
- `docs/09-roadmap.md` §10 (the cross-reference table; flips
  each item's phase target to 16 if resolved, 17+ if re-scoped).
- Any code commits that close the `Resolved in 16` items
  (small + targeted).

**Tasks**, the appendix below lists every relevant item. Only the
`Resolved in 16` items are listed as commit units here; the rest
are disposition updates only.

1. **#6 / #14 `panel.position` boot-time-only**, `Resolved in
   16` (W5 + U16.2).
2. **D14.8(d) `<minimizer/>`**, `Resolved in 16` (W9, same
   patch as resize).
3. **#18 parsed-but-unused theme keys**, `Resolved in 16` (no
   new code needed; the Phase 14 W11 cleanup landed; Phase 16
   inherits).
4. **QTimer leak suspects #47-#50**, `Resolved in 16` (W11
   re-audit; no new QTimer introduced; the keystroke-capture is
   event-driven; the position apply uses the existing 250 ms
   broadcast poll).
5. **Phase 14 W8 step-5 hover timing**, `Re-scoped → Phase 17+`
   (no Phase 16 demand).
6. **Phase 14 W5 textedit Ctrl-C**, `Re-scoped → Phase 17+`
   (no Phase 16 demand).

**Pass conditions**:

- The Paper-cut Disposition Appendix table is committed.
- The `docs/09-roadmap.md` cross-reference table is updated.
- `run/sponge-de-stability.run` from Phase 14 W9 (not extended
  in Phase 16, no regression gate on the cycle workload, only
  on the regression sweep) still PASSes.

**Commit units**:

- (docs) `docs(roadmap): Phase 16 paper-cut disposition appendix
  + cross-reference updates`.

#### W12, Close-out

**Goal**: complete Phase 16 durably and hand off cleanly.

**Tasks**:

1. **Roadmap checkboxes**, flip every Phase 16 criterion in
   `docs/09-roadmap.md` §10 Phase 16 to `[x]` if the bound
   conditions are met; otherwise mark the specific criterion as
   `[x]` with an honest disposition note inline (e.g. criterion
   11's "ISO metadata-only" honest disclosure).
2. **README current-status update**, flip the Phase 16 items in
   the "Current Status" list to ✅.
3. **Docs evidence index**, author `docs/evidence/phase16-index
   .md` (mirroring `phase14-index.md`): per-W receipts, the
   per-criterion `criterion → scenario → exact marker → evidence`
   table, the W1 ledger row #17 record, the W3 first-boot
   acceptance receipts, the W11 disposition matrix, and the
   regression sweep table.
4. **Regression sweep**, the full Phase 16 scenario suite (the
   new scenarios + every Phase 10/11/12/13/14/15 scenario
   touched by a Phase 16 code path) re-run end-to-end on
   base-sel4 in QEMU. One scenario at a time, `make -j1`, no
   concurrent `make` in `genode/build/x86_64` (the Phase 12
   sweep's lesson). The sweep's receipts live in
   `docs/evidence/phase16-envelope-*.log` (one per scenario).
   The exact list:
   - `sponge-minimal.run` (Phase 1).
   - `sponge-de-test.run` (Phase 3).
   - `sponge-de-sel4-interactive.run` (Phase 10).
   - `sponge-wm-qmp.run` (Phase 10).
   - `sponge-launch.run` (Phase 7).
   - `sponge-wm-tasks.run` (Phase 14 W7).
   - `sponge-clipboard-qtsettext.run` (Phase 14 W5).
   - `sponge-notify.run` (Phase 14 W4).
   - `sponge-configd-persist.run` (Phase 14 W6).
   - `sponge-panel-config-sel4.run` (Phase 11).
   - `sponge-de-themed-chrome.run` (Phase 11).
   - `sponge-bake-firstboot.run` (Phase 15 W3; promoted from
     base-linux to the regression sweep alongside the new
     base-sel4 first-boot acceptance scenario).
   - `sponge-bake-reset.run` (Phase 15 W3; same promotion).
   - `sponge-alpha.run` (Phase 16 W3 + W10).
   - `sponge-desktop-defaults-firstboot.run` (Phase 16 W3).
   - `sponge-configd-pattern-keys.run` (Phase 16 W2).
   - `sponge-configd-shortcuts.run` (Phase 16 W2).
   - `sponge-configd-badkey.run` (Phase 16 W2).
   - `sponge-settings-center.run` (Phase 16 W4).
   - `sponge-settings-providers.run` (Phase 16 W4).
   - `sponge-settings-thirdparty.run` (Phase 16 W4).
   - `sponge-settings-failure.run` (Phase 16 W4).
   - `sponge-settings-vct.run` (Phase 16 W4).
   - `sponge-panel-menu.run` (Phase 16 W5).
   - `sponge-de-bgmenu.run` (Phase 16 W6).
   - `sponge-de-bgimage.run` (Phase 16 W6).
   - `sponge-de-bgimage-badpath.run` (Phase 16 W6).
   - `sponge-de-shortcuts.run` (Phase 16 W7).
   - `sponge-de-shortcuts-extend.run` (Phase 16 W7).
   - `sponge-de-multipanel.run` (Phase 16 W8).
   - `sponge-de-multipanel-idspace.run` (Phase 16 W8).
   - `sponge-de-themed-chrome-resize.run` (Phase 16 W9).
   - `sponge-de-release-resize.run` (Phase 16 W9).
   - `sponge-textedit.run` / `sponge-files.run` /
     `sponge-calculator.run` / `sponge-terminal.run`
     (Phase 16 W9 per-package metadata regression).
   - `sponge-hw-matrix.run` (Phase 15 W3; one variant).
5. **Phase 17+ handoff**, the Paper-cut Disposition Appendix's
   `Re-scoped` items each carry a target phase (17 / 18+);
   these are NOT touched in Phase 16 but the appendix's per-item
   handoff note is committed.

**Pass conditions**:

- `docs/evidence/phase16-index.md` exists and is referenced from
  `docs/09-roadmap.md` §11 current-focus paragraph.
- The regression sweep exits 0 for every scenario in the Phase
  16 suite.
- The Phase 16 completion criterion checkboxes are internally
  consistent with the evidence index.

**Commit units**:

- (docs) `docs(roadmap): flip Phase 16 checkboxes + closeout
  paragraph`.
- (docs) `docs(readme): update current status list with Phase
  16 items`.
- (docs) `docs(evidence): add phase16-index.md (per-W receipts
  + per-criterion mapping + ledger row #17 record + W11
  disposition + regression sweep table)`.
- (docs) `docs(roadmap): Phase 16 Paper-cut Disposition
  Appendix cross-references to Phase 17+ handoff notes`.
- (docs) one per-scenario regression log file:
  `docs/evidence/phase16-envelope-<scenario>.log`.

---

## Paper-cut Disposition Appendix (Phase 14/15 → Phase 16)

Every Phase 14/15 carryover item that touches a Phase 16 code path,
plus every Phase 16-introduced paper cut identified during the
baseline walk. The classification column is authoritative; only
`Resolved in 16` items get implementation work in W11.

| # | Item | Origin | Classification | Target phase / resolution |
|---|------|--------|----------------|---------------------------|
| 1 | `panel.position` boot-time-only (Phase 14 W11 row #6) | P14 | **Resolved in 16** | W5 (dual-domain topology per U16.2 + D16.2); cross-ref `docs/09-roadmap.md` |
| 2 | `panel.position` duplicate of #1 (Phase 14 W11 row #14) | P14 | **Resolved in 16** | W5 (same) |
| 3 | D14.8(d) `<minimizer/>` decorator button | P14 | **Resolved in 16** | W9 (vendored themed_decorator patch; same patch as resize; ledger row #17) |
| 4 | QGenodeScreen 1×1 race (Phase 14 W11 row #46) | P11 | Re-scoped | Phase 17+ (no Phase 16 demand) |
| 5 | Phase 14 W8 step-5 hover timing | P14 | Re-scoped | Phase 17+ (no Phase 16 demand) |
| 6 | Phase 14 W5 textedit Ctrl-C (clipboard qtsettext known limitation) | P14 | Re-scoped | Phase 17+ (no Phase 16 demand) |
| 7 | Phase 14 #18 parsed-but-unused theme keys | P14 | Resolved (historical) | Phase 14 W11 cleanup already landed; Phase 16 inherits |
| 8 | QTimer leak suspects #47-#50 | P11/P14 | **Resolved in 16** | W11 re-audit; no new QTimer introduced; the keystroke-capture is event-driven; position apply uses the existing 250 ms broadcast poll |
| 9 | Phase 15 W3 first-boot sentinel in configd (not wired into product scenarios) | P15 | **Resolved in 16** | W3 (add `<bake/>` + bake ROM routes to all 6 product scenarios; first-boot acceptance scenario) |
| 10 | Phase 15 W3 `bake_*.json` files not in boot_modules list | P15 | **Resolved in 16** | W3 (same) |
| 11 | ISO metadata-only media (falkon 509 MiB exceeds boot-module ceiling) | P7 | Not-a-defect | D16.7 honest disclosure in `docs/13-installation.md` Known Limitations |
| 12 | `app/backdrop` in `sponge-alpha.run` + `sponge-leitzentrale.run` + `sponge-usb-boot.run` build lists (no configd, no context menu; same `genode_logo.png` cp) | P7 | **Resolved in 16** | W6 #7-9 (removed from all THREE scenarios; the in-DE `background/` widget per D16.4 replaces it; install-md limit + run-script commits land together) |
| 13 | alpha_probe only checks hello/Utilities (line 240-274) | P10 | **Resolved in 16** | W3 + W10 (extend to all 7 desktop packages' `{name, category}` pairs) |
| 14 | No `contextMenuEvent` override on `PanelWidget` | P11 | **Resolved in 16** | W5 |
| 15 | No background widget (nitpicker default domain catches nothing) | P11 | **Resolved in 16** | W6 |
| 16 | No settings GUI (deferred to Phase 15+ per Phase 14 D14.7) | P14 | **Resolved in 16** | W4 (provider-owned settings control center per D16.3; the interim dialog is replaced by the provider split, D16.9) |
| 17 | No keyboard shortcut layer (only QMP path) | P11 | **Resolved in 16** | W7 (U16.4 + D16.5 framework) |
| 18 | No multi-panel (singleton PanelWidget) | P11 | **Resolved in 16** | W8 (U16.5 + D16.5 pattern keys) |
| 19 | No mouse resize affordance on themed_decorator (Phase 14 deferred) | P11 | **Resolved in 16** | W9 (vendored patch + release-media motif decorator regression) |
| 20 | `report_rom` single-writer limitation | Arch | Not-a-defect | `AGENTS.md` §1.2 (architecture boundary); Phase 16 keeps single-writer-per-label and adds configd label-to-key-domain ACLs (D16.1 per-provider labels) |

(Total rows = 20; classification `Resolved in 16` = 11, `Re-scoped`
= 3, `Not-a-defect` = 2, `Resolved (historical)` = 1, plus the 3
`Resolved in 16` items that ARE the Phase 16 net-new work
themselves. The "duplicate of" notes keep the matrix exhaustive
without losing the classification-by-row pattern.)

---

## Verification Contract

Per `AGENTS.md` §4.2 and the Phase 14 W-prefacing convention:

- **Every new feature ships a boot-verified scenario.** Every W
  item's "Pass conditions" names a specific run scenario, a
  specific PASS marker string, and a specific QEMU configuration
  (`KERNEL=sel4 BOARD=pc`). The scenario follows the
  `sponge-*.run` naming + probe pattern; builds `lib/ld` + `core
  init <component>`; passes `[build_artifacts]` to
  `build_boot_image`. Every new scenario has a committed relative
  symlink at `repos/sponge/run/sponge-<name>.run ->
  ../../../run/sponge-<name>.run`.
- **Misleading-success-output defense**: every new scenario has a
  focused assertion beyond `exit 0`. Captures pixel checks,
  structural report reads, and bounded-byte log matches (e.g.
  the 8-byte `bake.applied=yes` configd-broadcast assertion in
  W3).
- **Per-criterion traceability** (the Phase 12 contract): every
  Phase 16 criterion maps to one or more scenarios, each with an
  exact PASS marker and a `docs/evidence/phase16-*.log`
  reference. The mapping lives in
  `docs/evidence/phase16-index.md` and is referenced from
  `docs/09-roadmap.md` §10 Phase 16.
- **No vendored-tree patches are absorbed silently.** Every patch
  (the W9 themed_decorator sizer + minimizer) gets a
  `docs/11-environment.md` §4 ledger row BEFORE the patch is
  applied; the row is committed first, the patch commit second.
  The base one-patch budget (row #17) is historical, not a cap
  (D16.8): any additional Extension patch is individually
  justified, ledgered before application, independently
  revertable, and re-audited at the final Criterion 27 cleanup.
- **Atomic commits** per `AGENTS.md` §4.3. Commit units are
  listed per W item. Conventional-commits style. Test commits
  land BEFORE implementation commits (TDD orientation).
- **Final regression sweep** (W12) runs every Phase 16 scenario
  + every Phase 10/11/12/13/14/15 scenario touched by a Phase
  16 code path. Serialized (`make -j1`); receipts in
  `docs/evidence/phase16-envelope-*.log`.

---

## Commit Strategy

Atomic units are listed per W item. The summary:

- **Wave 1**: ~9 commits (W1 × 5, W2 × ~6, W3 × ~5, W2's
  registry extension is the largest single Wave-1 item).
- **Wave 2**: ~30 commits (W4 × 10, W5 × 4, W6 × 7, W7 × 4,
  W8 × 4).
- **Wave 3**: ~9 commits (W9 × 9; the vendored patch + ledger
  row + per-package metadata + 2 scenarios).
- **Wave 4**: ~3 commits (W11 × 1 disposition, W12 × ~5 docs
  + per-scenario regression logs).

Total ~51 commits, comparable to Phase 14 (~50). The "test-first"
rule means each W item's first commit is the failing scenario +
its initial log; the last commit is the source change that flips
the marker to PASS. This makes every step of the phase bisectable
(a regression in W5's panel menu does not silently invalidate
W4's settings work).

The W9 ledger row + vendored-patch sequence (per `AGENTS.md`
§5.2):

```
docs(patch): add docs/patches/themed-decorator-resize-minimize.
patch (durable record) + docs/11-environment.md §4 row #17 entry
(U16 + D16.6)

The vendored-decorator patch (sizer affordances + minimizer
button) is documented BEFORE the patch is applied. Drop when
upstream Genode adds resize affordance AND `<minimizer/>` to
themed_decorator. Refs: D16.6, U16, docs/11 §4.
```

```
vendor(genode): apply themed_decorator sizer + minimizer patch
(ledger row #17)

The git apply step. The patch file
docs/patches/themed-decorator-resize-minimize.patch is the
durable record; the patched files are
genode/repos/gems/src/app/themed_decorator/{theme.h,theme.cc,
window.h,window.cc}. Refs: D16.6, docs/11 §4 row #17.
```

---

## Open Questions (recorded, not blocking)

1. **Qt6 QPA resize_request auto-resize?**, Does the Qt6 QPA
   already resize content on Gui window resize? If yes, the W9
   `ResizeSubscriber` shim is a no-op for Qt6 apps and only the
   metadata opt-in is needed. Verify in W9 by booting
   `sponge-textedit.run` with the metadata change and observing
   the QWidget geometry when the resize_request ROM fires. If no,
   add a minimal Qt-side patch in `repos/sponge/src/qt6_base/`
   (Sponge-side, NOT vendored).
2. **ISO metadata-only honest disclosure scope (D16.7).** The
   bundled-package criterion (6 + 7) is restricted to IMG media
   by honest disclosure. If a future ISO-build pipeline wants to
   satisfy criterion 6 + 7 on ISO alone, that requires a
   smaller desktop set (drop falkon) OR a future remote-repo
   payload-fetch mechanism. Phase 16 picks (b-i) (restrict to
   IMG); revisit in Phase 18 (the GUI installer phase) if the
   installer can deliver payloads on ISO boot.
3. **panel.position right / left panels (D16.2 disabled
   disclosure).** Phase 16 ships top + bottom live; right + left
   are exposed in the configd enum + the panel context menu but
   the menu items are disabled (greyed) with a "Phase 17+"
   tooltip (the historical shipped state). When Phase 16
   Extension criterion 22 (2026-11-10 scope update) adds the
   lateral-domain support in nitpicker, the disabled items flip
   to enabled. No silent stub.
4. **`shortcuts.bindings` validator edge cases (D16.5b).** The
   initial shipped bindings are `launcher` + `focus_next` +
   `dismiss`. The structured validator accepts more action
   tokens from a closed enum (Phase 16 ships those 3; Phase 17+
   adds `settings` + `terminal_open` + `textedit_open` etc.).
   The user can write any future token; the validator rejects
   unknown tokens with a `Genode::warning`. This is the
   deliberate exception to the one-key-per-setting rule
   (documented in `sponge_configd/README.md`).
5. **Multi-panel RAM cost per-instance (U16.5).** 128 MB per
   sponge-de Qt6 instance. At `-m 4G` (the multi-panel QEMU
   budget), 2 panels + wm + layouter + decorator + configd +
   themed + pkgd fit comfortably. The Phase 15 17ZD90N real-
   hardware boot (Phase 15-3) at `-m 8G` has ~6.5 GB free after
   the desktop stack; two panels cost ~256 MB more (~4% of the
   budget). Real-hardware multi-panel regression is Phase 17+
   (not Phase 16, Phase 16 proves the topology on QEMU).
6. **Falkon in criterion 6 + 7.** Falkon is in `desktop.profile`
   and is staged on IMG media. On seL4 it hits the per-PD CSPACE
   ceiling (`run/sponge-falkon-disk.run` evidence §11); the
   `sponge_pkgd` request channel returns `not-installed`-shaped
   error when the binary fetch fails (verified in Phase 7). The
   criterion 6 + 7 honest disclosure lands in `docs/13-installation
   .md` Known Limitations: "Falkon appears in the bundled
   package set on IMG media; on seL4 without the vendored base-
   sel4 caps patch (Phase 15+ open), launching Falkon fails
   gracefully with a structured error. The remaining 6 desktop
   packages are boot-verified launchable on seL4."
7. **Background image allowlist (D16.10).** Phase 16 ships with
   `/system/background/default.png` as the only allowed image.
   Users can add more images by packaging them (Phase 13
   conventions; the package's metadata carries the image path
   on the allowlist). Future "user-supplied image upload" is
   Phase 17+ scope (requires a file picker UI + a writable
   allowlist update).
8. **Phase 14 W11 row #17 / #18 theme-aliases / parsed-but-unused
   keys cleanup.** Already landed in Phase 14 W11. Phase 16
   inherits; the new `theme.active=default` is honored by
   `sponge_themed` without regression.

---

## UX-Metrics Appendix (per `AGENTS.md` §5.1)

Per `AGENTS.md` §5.1, every convenience claim must be backed by
code, measured in clicks / keystrokes / commands. The four
canonical Phase 16 actions:

| Task | Before Phase 16 | After Phase 16 | Reduction |
|------|------------------|-----------------|-----------|
| Change panel height | (a) `vct config panel.height 40` (CLI) + reboot OR (b) hand-edit `store.xml` + reboot OR (c) the Phase 11 configd write path (reboot NOT needed), but no GUI existed | Panel-menu *Settings* raises the Panel provider window → spin the Height spinbox → 3 clicks total (right-click panel; *Settings*; spin) + 0 commands, 0 reboots, 0 files | **From 3+ commands + reboot** to **3 clicks + 0 commands + 0 reboots** |
| Change background image | Not implemented (Phase 11 only had `theme.active`; no background surface) | Background-menu *Settings* raises the Background provider window → pick from the image combobox → 3 clicks total (right-click background; *Settings*; pick image) + 0 commands, 0 reboots, 0 files | **From "not possible"** to **3 clicks + 0 commands** |
| Add a second panel | Not implemented (single-panel singleton) | Panel provider window → enter the new panel id (`beta`) into the *Add panel* field → click Add → 4 clicks total (open the Panel provider entry; focus the field; enter id; Add) + 0 commands, 0 reboots | **From "not possible"** to **4 clicks + 0 commands** |
| Open the launcher via Super | Not implemented (Phase 10 / Phase 11 used a QMP-driven click on the S toggle) | Press Super → 1 keystroke | **From 1 host QMP click + 1 host-side `qmp_click` Tcl call** to **1 keystroke** |

Every reduction is exercised in the corresponding Phase 16 run
scenario (W4 Panel provider window → height spinbox → 3 clicks +
configd broadcast byte match; W6 Background provider image
combobox → 3 clicks + image byte match; W8 multipanel → 4 clicks +
new panel's widget visible at the right nitpicker domain; W7
shortcut → 1 keystroke + launcher.open=yes). The receipts live in
`docs/evidence/phase16-index.md` §"UX metrics"; the receipts that
exist today measured the replaced dialog flow (the universal
`SettingsController` writer) and are not proof of the provider
architecture.

**Provider-flow measurements are TARGETS.** The provider-flow
rows above (change panel height, change background image, add a
second panel) are plan-time targets: their provider-architecture
numbers are exercised only once the new W4 scenarios pass
(`sponge-settings-center.run`, `sponge-settings-providers.run`
including the cold-provider-start quick-menu change,
`sponge-settings-failure.run` with the provider-start/validation
failure cases, `sponge-settings-thirdparty.run`,
`sponge-settings-vct.run`), on the quick-menu change lifecycle
(`docs/05-sponge-de.md` §5.4). Until those scenarios pass, do
not cite the existing `phase16-index.md` UX-metrics receipts as
provider-architecture evidence: they measured the dialog flow
the W4 provider split replaces. The evidence files themselves
are left untouched; this note is the plan-side correction.

**Forward-looking note** (per `AGENTS.md` §5.1, claims must be
re-verified at delivery time, not at plan time): the table above
is the plan-time commitment. **Every row must be re-verified against
the PASSed scenario's evidence at W12 close-out**, the
regression-sweep receipts in
`docs/evidence/phase16-envelope-*.log` and the per-criterion
mapping in `docs/evidence/phase16-index.md` §"UX metrics" carry
the verified numbers. If a row's actual measurement at W12
exceeds the plan's commitment (more clicks, a required command
where the plan said zero, a non-zero reboot count, etc.), the
plan is wrong, NOT the implementation: the row is re-measured in
the pass-condition step of the corresponding W and the table is
updated before the W12 commit lands. The W11 paper-cut sweep
calls out any user-visible regression vs the row's plan-time
target, and the Phase 16 close-out index documents the actual
measurements next to the planned reductions.

---

## Task Dependency Graph

| Task | Depends On | Reason |
|------|------------|--------|
| **W1** (design-doc amends + decisions ledger + roadmap reword) | None | Starting point; gates every other W |
| **W2** (configd registry re-architecture) | W1 | The schema decisions land in W1's decisions table; W2 implements them |
| **W3** (bake wiring + first-boot acceptance) | W1, W2 | The new keys (W2) make the alpha_probe extended set possible; W1's U16.6 keeps `desktop.profile` |
| **W4** (settings control center + provider split) | W1, W2 | The D16.1 per-label ACL model needs W2's registry shape; W1 locks D16.3 / D16.9 / D16.10 |
| **W5** (panel context menu + dual-domain position) | W1, W2, W4 | U16.2 decision in W1; panel.position becomes live via W2's schema; the menu's `tasklist` checkbox requires W2 #7 (validator extension, see D16.10); the menu forwards changes to the Panel provider landed in W4 |
| **W6** (background widget + context menu + image) | W1, W2, W4 | D16.4 + D16.10 decisions in W1; background.color/image keys land in W2; writes go through the Background provider landed in W4 |
| **W7** (keyboard shortcuts) | W1, W2, W4 | U16.4 + D16.5 decisions in W1; shortcuts.bindings key lands in W2; the Shortcuts provider writes it on its own ACL'd label (W4) |
| **W8** (multi-panel generalization) | W1, W2, W4, W5 | U16.5 + D16.5 decisions in W1; pattern keys land in W2; the panel collection builds on W5's dual-domain topology; panel-id writes go through the Panel provider (W4) |
| **W9** (mouse resize + vendored patch) | W1, W2 | D16.6 decision in W1 (base ledger row #17, historical per D16.8); the per-package metadata opt-in is independent of the configd schema |
| **W10** (release-media regression) | W1-W9 | Composed regression over every Wave-2 / Wave-3 output |
| **W11** (paper-cut sweep) | W1-W9 | Disposition over every carryover; only `Resolved in 16` items are committed |
| **W12** (close-out) | W1-W11 | Closes the phase durably |

---

## Parallel Execution Graph

- **Wave 1** (sequential, foundational): W1 → W2 → W3.
- **Wave 2** (after W1+W2+W3; parallel where file-disjoint):
  - W4 (settings control center + provider split),
    `sponge_settings/` + `settings_providers/` + pkgd/configd
    lifecycle + vct `settings`.
  - W5 (panel context menu + dual domains), `panel/` +
    sponge_de_main + nitpicker config in `run/`.
  - W6 (background widget), `background/` in sponge-de.
  - W7 (keyboard shortcuts), `config/shortcut_controller` +
    `launcher/`, `panel/tasklist/`, `config/dismisser` in
    sponge-de.
  - W8 (multi-panel), `panel/panel_collection` in sponge-de.
  - File-disjoint: mostly (each W adds a new module directory or a
    new QMenu entry; the quick-menu change lifecycle (per-provider
    quick request/result pairs + the quick menu's own launch
    labels) and the configd ACLs are written in W4 and consumed by
    W5 / W6 / W7 / W8, whose menu forwarding touches W4's
    provider entry points).
- **Wave 3** (after Wave 2): W9 (vendored patch + per-package
  metadata + 2 scenarios).
- **Wave 4** (serial): W10 → W11 → W12.

Critical path: **W1 → W2 → W3 → W4 → W12** (W2's registry
extension is the root for 9 of 12 criteria; W3's first-boot
acceptance is the headline acceptance for criterion 1; W4's
control center + provider split is the headline acceptance for
criterion 2; W12's
regression sweep is the close-out gate).

Estimated parallel speedup: ~35% over serial (Wave 2 is the
biggest parallel block; W5-W8 share infrastructure, the
quick-menu change lifecycle (quick request/result pairs) +
configd ACLs from W4, but their scenarios and modules
are independent).