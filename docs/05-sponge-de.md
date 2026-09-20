# 05 - Sponge DE Design

> This document describes the design direction and principles of Sponge
> DE, the default desktop environment of Sponge OS. It is not a concrete
> implementation checklist; it defines direction.

---

## 1. Design Goals

Sponge DE pursues these goals at the same time:

1. **Lightweight** — runs fast in Genode's constrained resource
   environment.
2. **Intuitive** — usable by everyday users without Genode knowledge.
3. **Customizable** — easy for users to compose their own environment.
4. **Gradually separable** — starts as a single component, can be split
   into modules over time.

These goals are consistent with the three philosophies in `AGENTS.md`.

---

## 2. Technology Stack

- **Framework**: Qt (using the Qt port that runs on Genode).
- **Rendering**: Qt's paint system initially, with GPU acceleration as
  a future option.
- **Input**: through Genode's `Input` session, translated into Qt
  events.
- **Window management**: runs on top of Genode's `nitpicker` (the
  window compositor).

> **Minimize Qt module dependencies** — link only the modules that are
> actually needed (Qt Widgets, QtCore, QtGui, ...), not the whole of
> Qt (see `AGENTS.md` §3.4).
>
> ✅ **UI toolkit choice (locked):** Sponge DE uses **Qt Widgets** for
> the initial releases. Qt Quick may be revisited later if a use case
> calls for it.

---

## 3. Module Layout (Inside the Initial Single Component)

The initial Sponge DE starts as a single Genode component, but internally
keeps the following modules loosely separated. This sets things up to
split each module into its own component later.

```
sponge-de (single component)
├── panel/        # top or bottom panel
├── launcher/     # app-launch entry point (a simple menu at first)
├── notifications/# notification display
├── windows/      # window management helpers (nitpicker glue)
├── settings/     # user settings (GUI version of vct config)
├── background/   # desktop backdrop surface (paint + right-click menu)
└── theme/        # theme loading and application
```

> `sponge_launcher` has its own component directory ready, but it will
> start out integrated into Sponge DE; splitting it off is considered
> after Sponge DE stabilizes. See `AGENTS.md` §3.4.

---

## 4. Theme System

Visual elements are not hardcoded in the source. They live in theme
files (see `AGENTS.md` §3.4). Initial design direction:

- **Theme format**: simple INI-style key-value text (see
  `docs/10-theme-format.md` for the concrete specification). JSON or YAML
  may be reconsidered as complexity grows.
- **Storage location**: two layers — the user settings directory
  (`~/.config/sponge/theme` or its Genode VFS counterpart) and the
  system default (`repos/sponge/src/sponge-de/themes/default.theme`).
- **Application**: loaded at Sponge DE start. `vct theme apply` can
  reapply the theme at runtime (through the `sponge_themed` backend).
- **Scope**: colors, fonts, spacing, icon set, panel position and size.
- **Parser skeleton**: `repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}`
  implements the INI parser and data model for Phase 5. It is not yet
  wired into the build or applied to Qt widgets; integration is pending.

User-customized themes take priority over the system default, but
upgrades do not silently overwrite user changes
(see `docs/02-philosophy.md` §3.4).

---

## 4.5 Notification System (Phase 14 — Implemented)

Notifications are delivered via a Sponge-native daemon
`sponge_notifier` (decision D14.1 in
`docs/plans/phase14-daily-desktop.md`). The bus uses the same
Report/ROM pattern as the other Phase-4/5 daemons:

```
client(s) --[Report "notif_request"]--> report_rom
            --[ROM "notif_request"]-->  sponge_notifier

sponge_notifier --[Report "notifications"]--> report_rom
                  --[ROM "notifications"]-->  client(s)
```

The panel renders the active list as a themed popover. The popover is
drawn into the panel domain (under the panel bar) so it docks
automatically; the geometry is fixed at `(700, 36, 300, 60)` and is
the contract with the W4 acceptance probe's Capture-pixel check.

Each entry has a `kind` (`info` / `warn` / `error`), a `title`, an
optional `body`, a monotonic `id`, and a `ttl_ms` that the daemon
enforces via a 500 ms periodic Timer sweep. The active list is FIFO
(capped at `max_live`, default 8); the oldest entry is dropped silently
when the list is full.

In Phase 14 only Sponge DE and `vct` post notifications (system
events and audit events). The opt-in is per-component:

- `vct` advertises `<config enable_notifications="yes">` (default ON).
  Posts on `install` / `remove` / `shutdown` / `reboot` completions.
- `sponge-de` advertises `<notifier source="daemon"/>`. Posts on
  theme apply, config change, and package install completion.

The absent-daemon warning (D14.1): when the daemon is absent from
the topology, the poster's `post()` emits a single
`Genode::warning("notifier unavailable, dropping: <title>")` per
unique title. The acceptance run
`run/sponge-notify-without-notifier.run` proves vct boots cleanly
without the daemon in the topology; the warning path is exercised by
the main `run/sponge-notify.run` scenario on the same build.

> **Capability surface (the D14.1 boundary, AGENTS.md §1.2)** — the
> daemon PROVIDES `Report` + `ROM` for the `notifications` channel and
> REQUESTS `Timer` + `ROM` (notif_request input) + `ROM` (optional
> config). No `PD`, no `RM`, no `GUI`. The `notifier_source` config
> gate keeps scenarios without the daemon from being killed by the
> parent when the Report session is denied.

---

## 4.6 Window Management (Phase 14 D14.3 — Implemented)

Window management is **real minimize + restore** (per U3, Phases 11 +
14). No decorative-only minimize button is shipped; every minimize is
paired with a deterministic restore path.

### State machine

The four states a window can be in (`docs/plans/wm-state-table.md`):

| State | Description |
|---|---|
| `Normal-Visible` | Window is placed and visible, not focused |
| `Normal-Visible-Focused` | Window is placed, visible, and focused |
| `Maximized` | Window is placed and full-screen |
| `Minimized` | Window is parked off-screen at `(x=-32000, y=-32000)` |

The state transitions are driven by:

- **Tasklist click** (the panel tasklist — see below) — minimize,
  restore, focus.
- **Decorator close button** (the upstream themed_decorator's
  `<closer/>` element, the existing Phase 11 path) — destroys the
  window.
- **Decorator maximizer button** (the upstream themed_decorator's
  `<maximizer/>` element) — toggles between Normal-Visible and
  Maximized.

Off-screen parking coordinates `(x=-32000, y=-32000)` are well
outside `nitpicker`'s int32 view space; the saved geometry is the
last-known visible position.

### The panel tasklist (the deterministic restore path)

Sponge-DE renders a horizontal tasklist inside the panel's
QHBoxLayout, between the title label and the stretch zone. Each entry
is a fixed-width (96 px) button that renders one window known to `wm`.
Click behavior:

- Click on a `Normal-Visible(-Focused)` entry → minimize (park off-screen).
- Click on a `Minimized` entry → restore to the saved geometry + grant
  focus (focus-after-restore per U3).
- Double-click → toggle maximizer.

The tasklist is the **required** restore path for the window stack
(U3). The tasklist is NOT decorative: each click is bound to a
state-machine transition that round-trips through the layouter.

### Architecture

```
wm ──[Report "window_list"]──> report_rom ──[ROM "window_list"]──> TasklistController
wm ──[Report "window_list"]──> report_rom ──[ROM "window_list"]──> wm_tasks_probe (read-only)
layouter ──[Report "window_layout"]──> report_rom ──[ROM "window_layout"]──> TasklistController
sponge-de ──[Report "focus_request"]──> report_rom ──[ROM "focus_request"]──> layouter
sponge-de ──[Report "rules"]──> report_rom ──[ROM "rules"]──> layouter (rules="rom" mode)
```

The `TasklistController` (sources/sponge-de/panel/tasklist_controller.{h,cc}):

- Subscribes to the wm `window_list` report and the layouter
  `window_layout` report (the two together give the controller
  per-window identity + geometry).
- Tracks `(x, y, w, h, focused, minimized, has_alpha)` per window.
- On user click, writes the `rules` ROM (the layouter's
  `rules="rom"` mode) and emits a `focus_request` report.
- Is the **SOLE writer** of the `focus_request` and `rules` reports
  (AGENTS.md §1.2: report_rom is single-writer per label).

The `TasklistWidget` (sources/sponge-de/panel/tasklist_widget.{h,cc}):

- Horizontal Qt widget inside the panel's QHBoxLayout.
- Three visual states per entry: `Normal-Visible` (default bg),
  `Normal-Visible-Focused` (accent bg), `Minimized` (separator bg).
- 2 px accent strip on the left edge for `has_alpha` windows.
- Re-styles on every theme reload via the standard `restyle()` path.

### Acceptance probe

`run/sponge-wm-tasks.run` (base-sel4 + QMP) is the W7 acceptance
scenario. The probe (`test/wm_tasks_probe/`) drives the state machine
end-to-end:

```
wm-tasks-probe: [step 1] install pkg_gui_demo
wm-tasks-probe: [step 2] launch pkg_gui_demo
wm-tasks-probe: [step 3] window_layout: pkg_gui_demo at (50,320,320,240) \
                  [row 1: (init) -> Normal-Visible]
wm-tasks-probe: [step 4] window_layout: pkg_gui_demo parked at (-32000,-32000) \
                  [row 3: Normal-Visible-Focused -> Minimized]
wm-tasks-probe: [step 5] window_layout: pkg_gui_demo restored at (50,320,320,240) \
                  [row 5: Minimized -> Normal-Visible-Focused]
wm-tasks-probe: [step 6] focus_request label='pkg_runtime -> pkg_gui_demo' \
                  [focus-after-restore per U3]
wm-tasks-probe: PASS
```

The probe reads the layouter's `window_layout` and the tasklist
controller's `focus_request` / `rules` reports. The run script
dispatches the QMP clicks on the tasklist button between probes.

### D14.8(d) — the `<minimizer/>` button on the decorator

The Phase 11 themed_decorator ships only `<closer/>` and
`<maximizer/>` buttons. Adding a `<minimizer/>` button requires a
vendored patch to `theme.h`, `theme.cc`, and `window.h` in
`genode/repos/gems/src/app/themed_decorator/`. Per the D14.8(d)
patch policy, this is **deferred to Phase 15+** as a follow-up:
the tasklist is the deterministic minimize path, and the
`<closer/>` / `<maximizer/>` buttons cover the remaining state
transitions through the existing upstream action pipeline. The
deferred work is tracked in `docs/11-environment.md` §4.2 + the
Wave-5 paper-cut sweep.

### 4.7 Background Module (Phase 16 W6 — Implemented)

The desktop background surface is owned by the in-DE
`background/` module (`repos/sponge/src/sponge-de/background/`):

- **`BackgroundWidget`** — a fullscreen frameless `QWidget`
  (`Qt::Window | Qt::FramelessWindowHint`, no `BypassWindowManagerHint`).
  `paintEvent` fills the screen from either the solid
  `background.color` or the `QImage` decoded from the
  `background.image` allowlist-validated path
  (`/system/background/default.png` by default).
- **`BackgroundController`** — bridges `ConfigController`'s
  `background_color_changed` / `background_image_changed`
  signals (the same 250 ms poll + ROM sigh funnel the
  panel uses) to the widget via the GUI-thread marshaling
  pattern. The widget applies via `applyBackgroundColor()`
  / `applyBackgroundImage()` slots that call `update()`
  (no manual screen repaint — Qt's paint system schedules
  the next frame).
- **`ShowDesktop`** — the minimize-all / restore-all state
  machine (U16.3). Reuses the Phase 14 W7 tasklist
  layouter-rule ROM overwrite from
  `tasklist_controller.cc:500-516` (the off-screen
  `(-32000, -32000)` parking) but applied to every
  focused/visible window at once. The `bgmenu.open=yes`
  toggle drives the wiring; the minimize-all layouter
  ROM overwrite is the Phase 16 W7 follow-up.

Activation gate: `<background source="controller"/>` in the
component config. Absent the gate, no widget, no third Gui
session, no extra ROM session — the no-bg topologies boot
unchanged.

The widget's Gui session routes to a dedicated nitpicker
`<domain name="default">` at `layer=1` (BELOW the wm window
domains at `layer=3`, BELOW the panel at `layer=2`). The
upstream `app/backdrop` + `genode_logo.png` are removed from
all three scenarios that previously built it
(`run/sponge-alpha.run`, `run/sponge-leitzentrale.run`,
`run/sponge-usb-boot.run`); the in-DE widget is the single
live surface. The right-click `contextMenuEvent` builds a
3-entry `QMenu` (Settings / Launch / Show desktop) anchored
to the event-local position (the Genode QPA returns `(0,0)`
from `QCursor::pos()` — Phase 10+ lesson; the same
event-local anchor pattern PanelWidget uses).

The default background image is generated at scenario build
time by `run/gen_png.py` (hand-encoded 32x32 RGB PNG via
`zlib` + stdlib only; no ImageMagick / PIL dependency) and
staged into `bin/default.png`. The corresponding
`pkg/background/metadata.xml` declares the ROM module
without the standard launcher surface (no `<binary>` /
`<autostart/>` / `<quota>`) — the package is a pure
boot-module staging target.

**Acceptance scenarios** (`base-sel4` + QMP choreography
where applicable):

- `run/sponge-de-bgmenu.run` — `bgmenu-probe: PASS` (right-click
  on the uncovered background region opens the context
  menu; the bgmenu_open reporter publishes the structural
  acceptance).
- `run/sponge-de-bgimage.run` — `bgimage-probe: PASS`
  (configd `background.image` round-trip via
  `de_config_request`).
- `run/sponge-de-bgimage-badpath.run` — `bgimage-badpath-probe:
  PASS` (allowlist rejection of `../../etc/passwd` is
  atomic — the broadcast still carries the default).

---

## 5. User Scenarios

How Sponge DE should present itself to an everyday user:

### 5.1 Right After Boot

- A clean wallpaper and a minimal panel.
- Genode terminology (`init`, `nitpicker`, and the like) is not visible.
- The panel contains the launcher, the tasklist (one button per
  running app, dimmed when minimized, highlighted when focused),
  and the clock — nothing more.
- A system tray and additional applets arrive in a later release; the
  panel today is intentionally minimal.
- A "first-time user guide" is optional and not forced.

### 5.2 Launching an App

- Click the launcher icon on the panel, or press `Meta` to open the
  launcher.
- The app list is generated automatically from packages installed
  through `vct install`.
- One click runs the app. Internally:
  - Sponge DE asks `sponge_launcher` (or the integrated module).
  - The launcher asks `sponge_pkgd` for the app's component
    information.
  - `sponge_pkgd` asks `init` to start the component.

### 5.3 Panel context menu (right-click on the panel)

A right-click on the panel (anywhere outside the launcher's
existing popover) opens a `QMenu` with the live controls that
the everyday user actually touches. The menu is the
discoverability path for panel configuration; the first-party
Settings app (§5.4 below) is the same surface in dialog form.

The menu is implemented in `PanelWidget::contextMenuEvent`
(`repos/sponge/src/sponge-de/panel/panel_widget.cc`); the menu
is anchored to the `QContextMenuEvent`'s global position (the
Genode QPA returns `(0,0)` from `QCursor::pos()` — known
Phase 10+ lesson — so the event-local position is the only
valid anchor). Each control's value-change signal routes
through `SettingsController::request_set` via
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` (Phase 11
risk #2 codification), which keeps the wire contract identical
to vct's `vct config` path (D16.9 validator parity).

The dual nitpicker panel-domain topology (U16.2 / D16.2) is the
runtime backing for the Position item: two pre-built
`PanelWidget` instances (`panel_top`, `panel_bottom`) are routed
to `<domain name="panel_top">` and `<domain name="panel_bottom">`
by their distinct `label_last` Gui session suffixes ("Sponge
Panel" / "Sponge Panel Bottom"). The run scenario declares
both domains with non-overlapping `ypos` / `height` ranges
(`panel_top ypos=0 height=28`, `panel_bottom ypos=screen_h-28
height=28`). The `ConfigController`'s existing `panel.position`
poll drives the visibility toggle (no new timer) — the
matching-role widget `show()`'s itself, the other `hide()`'s.
The previously-Phase-14-W11-row-#6 paper-cut ("panel.position
is boot-time-only") is closed here.

The two pre-built widgets are gated on `<panel_bottom
source="dual"/>` in the sponge-de component config. Regression
scenarios that do not wire the second Gui session (sponge-de-
sel4-interactive, sponge-panel-config, sponge-panel-config-sel4)
omit this gate and continue to construct a single PanelWidget
in singleton mode (applyPosition no-op, panel always shows) —
the dual-domain code path is gated on the sibling pointer so the
regression scenarios remain byte-compatible.

- **Height**, a spin box slider in 4 px steps, range `16..128`,
  default `28`. Sourced from the existing live
  `panel.height` `sponge_configd` key (Phase 11); the menu
  writes through `sponge-de`'s `SettingsController` on the
  dedicated `de_config_request` label (decision D16.1 in
  `docs/plans/phase16-daily-desktop-defaults.md`).
- **Visible widgets**, three checkboxes for `clock`, `launcher`,
  and `tasklist`. Sourced from the `panel.visible_widgets` enum-
  list key; Phase 16 W2 extends the configd validator to accept
  the `tasklist` token (the panel-widget has parsed it locally
  since Phase 14 W7, but the configd side rejected writes that
  included it; D16.10).
- **Position**, a radio group with `Top` / `Bottom` enabled and
  `Left` / `Right` displayed-but-disabled with a "Phase 17+"
  tooltip. The enabled pair maps to the live `panel.position`
  configd key and materialises through the **dual nitpicker panel
  domains** topology (D16.2 / U16.2):
  `<domain name="panel_top">` and `<domain name="panel_bottom">`,
  pre-declared in the run script with non-overlapping `ypos` /
  height ranges, each routed by `label_last` to the matching
  sponge-de `Gui` session (`Sponge Panel` / `Sponge Panel
  Bottom`). The `PanelWidget` toggles visibility of the two
  pre-built `QWidget`s on the existing 250 ms configd poll (no
  new timer). The previously-Phase-14-W11-row-#6 paper-cut
  ("panel.position is boot-time-only") is closed in Phase 16 W5.
- **Settings**, opens the §5.4 dialog (lazily constructed on
  first click, cached afterward).

All menu actions route through `QMetaObject::invokeMethod(...,
Qt::QueuedConnection)` (Phase 11 risk #2 codification), so the
GUI-thread marshal is enforced at compile time via the
`QWidget::` setter signatures.

### 5.4 Changing Settings

- Settings can be changed from Sponge DE's settings GUI or from
  `vct config` on the CLI. Both paths use the same
  `sponge_configd` backend, so they stay consistent.
- Changes apply immediately, or the user is told clearly when a restart
  is required.

### 5.5 Control for Advanced Users

- `vct leitzentrale` opens the Leitzentrale window for direct
  manipulation of the system component tree.
- In this release the only way to open Leitzentrale is the
  `vct leitzentrale` CLI command; a panel menu entry is deferred to a
  later release.

### 5.6 Phase 16 — Daily-Usable Desktop Defaults & Configuration

Phase 16 (2026-09-20 close-out, 10 of 12 criteria delivered; 2
delivered with honest disclosure) adds the everyday configuration
surface that turns the desktop into a daily driver. See
`docs/evidence/phase16-index.md` §2 for the per-criterion
verification table; the §4 regression sweep there records 34 / 36
fully PASS + 2 / 36 PARTIAL (the W6 deviation 1 + the W9 sibling
flakiness) + 0 FAIL.

The eight Phase 16 module / component changes:

1. **`settings/` module (W4).** A lazy-loaded `QDialog` with five
   tabs (Panel / Theme / Background / Shortcuts / Defaults) lives
   INSIDE `sponge-de` per D16.3 — no separate `sponge_settings`
   component (would double the Qt6 + Mesa footprint per §3.4).
   Every write goes through `SettingsController` on the dedicated
   `de_config_request` label per D16.1 + `AGENTS.md` §1.2
   single-writer rule. Acceptance scenario:
   `run/sponge-de-settings.run` →
   `settings-probe: PASS` + the per-tab configd-broadcast
   assertions; regression
   `run/sponge-de-settings-regression.run` opens the dialog via
   panel-menu *Settings* and asserts `panel.height=40` round-trips.
2. **`background/` module (W6).** A fullscreen frameless
   `QWidget` painted from `background.color` (hex-validated) or
   `background.image` (allowlist-validated; default
   `/system/background/default.png`). `contextMenuEvent` opens
   a three-item `QMenu` (Settings / Launch / Show desktop); the
   show-desktop toggle reuses the Phase 14 W7 panel tasklist
   state machine (U16.3). Acceptance scenario:
   `run/sponge-de-bgmenu.run` →
   `bgmenu-probe: PASS` (structural `open="ready"`; the
   per-event `open="yes"` is timing-sensitive on the Genode QPA
   — see the W6 follow-up note in
   `docs/evidence/phase16-w6-bgmenu-followup.md` + the
   Phase 17+ minimal fix recipe).
3. **Panel context menu + dual-domain topology (W5).** Right-click
   on the panel opens the QMenu (Height spinbox 16..128 step 4;
   Visible widgets checkboxes clock/launcher/tasklist; Position
   radio group top/bottom enabled, left/right disabled with
   "Phase 17+" tooltip per D16.2; Settings entry). Position
   becomes LIVE via dual nitpicker panel domains
   `panel_top` / `panel_bottom` per U16.2 + D16.2; closes the
   Phase 14 W11 row #6 / #14 paper-cut (`panel.position` was
   boot-time-only). Acceptance scenario:
   `run/sponge-panel-menu.run` →
   `panel-menu-probe: PASS` (position-switch pixel assertion:
   bottom (512, 740) vs top (512, 14)).
4. **Keyboard shortcut framework (W7).** Extensible via the
   `shortcuts.bindings` configd key (structured
   `action<keycode-list>` value; closed `launcher` /
   `focus_next` / `dismiss` action enum). Initial shipped
   bindings: Super → launcher, Alt-Tab → focus cycle forward,
   Escape → dismiss topmost popover. Acceptance scenarios:
   `run/sponge-de-shortcuts.run` (`shortcuts-probe: PASS`) +
   `run/sponge-de-shortcuts-extend.run`
   (`shortcuts-extend-probe: PASS`).
5. **Multi-panel (W8).** Arbitrary panel count on arbitrary edges
   via `PanelCollection`; per-panel `panel.<id>.{height,position,
   visible_widgets}` pattern keys (the W2 pattern-key registry
   infrastructure). Distinct Gui session label suffix per instance
   (F5 nitpicker `label_prefix` trap defense). Acceptance
   scenario: `run/sponge-de-multipanel.run` →
   `multipanel-probe: PASS` (cross-panel click assertions live)
   + `run/sponge-de-multipanel-idspace.run`
   (`pattern-keys-probe: idspace = PASS`).
6. **Mouse resize hookup (W9).** `<resizeable="yes"/>` package-
   metadata opt-in (textedit, files, calculator, terminal —
   NOT falkon in Phase 16). `sponge_pkgd` carries the flag to
   the layouter `<assign>` block. Qt6 QPA already resizes on
   Gui window resize (verified; no app-side shim). Sibling
   acceptance: `run/sponge-de-themed-chrome-resize.run` (each
   of 8 zones individually proven on the vendored themed
   decorator + minimizer patch — the same patch closes the
   Phase 14 D14.8(d) `<minimizer/>` deferral; ledger row #17).
   Criterion-9 headline: `run/sponge-de-release-resize.run` on
   the motif decorator (release-media topology) →
   `release-resize-probe: PASS` (3x consecutive independent
   runs).
7. **Registry re-architecture (W2).** `sponge_configd` closed
   registry widened: `MAX_KEYS = 16 → 32` (interim; pattern
   keys in a separate array), `MAX_PATTERN_KEYS = 32` array
   for `panel.<id>.{height,position,visible_widgets}` with
   charset validator `[a-z0-9_-]{1,16}`, four new flat keys
   (`background.color` / `background.image` /
   `shortcuts.bindings` / `panel.ids`), and the
   `panel.visible_widgets` enum-list extended
   `{clock, launcher} → {clock, launcher, tasklist}`. Acceptance
   scenarios: `run/sponge-configd-pattern-keys.run` +
   `run/sponge-configd-shortcuts.run` +
   `run/sponge-configd-badkey.run` (validator parity).
8. **Bake wiring (W3).** All six product scenarios
   (`sponge-alpha.run` + the 5 desktop-disk variants) ship with
   `<bake/>` + bake ROM routes + the `bin/bake/*` boot modules;
   `run/sponge-desktop-defaults-firstboot.run` is the first-boot
   acceptance scenario on the disk-desktop image. The
   `alpha_probe` (`repos/sponge/src/test/alpha_probe/main.cc`)
   extended the launcher-set assertion to all 7 desktop
   packages' `{name, category}` pairs and added the 8 baked-
   key configd-broadcast assertions.

The Phase 16 vendored budget is **one new row #17** (themed
decorator sizer + minimizer; `docs/11-environment.md` §4);
no other vendored-tree patches are absorbed per D16.8. The
honest limitations register (Phase 17+ carry-overs) lives in
`docs/evidence/phase16-index.md` §6.

---

## 6. Lightweight Strategy

How Sponge DE stays light under Genode's resource constraints:

1. **Minimal Qt module linking**: link only what is needed.
2. **Lazy loading**: modules that are not in use (for example, the
   settings screen) load on demand.
3. **Minimal static assets**: the default theme ships only the bare
   minimum.
4. **Simple rendering**: avoid complex shaders and animations at first.
5. **Apply component separation gradually**: keep the single-component
   memory advantage at first, and split later when needed.

---

## 7. Open Design Questions

- Priority and timing of multi-monitor support. Remains open for
  Phase 15+.
- **Settings GUI backend — settled by Phase 16 D16.3.** A first-party
  settings dialog lives INSIDE `sponge-de` as a new `settings/`
  module (a lazy-loaded `QDialog` with `Panel` / `Theme` /
  `Background` / `Shortcuts` / `Defaults` tabs); all writes go
  through a `SettingsController` on the dedicated
  `de_config_request` label (D16.1). `AGENTS.md` §3.4 favors
  minimising Qt module dependencies, so a separate `sponge_settings`
  component (which would pull a second Qt6 + Mesa softpipe
  instance at the `sponge-de` `128M` cap quoted in
  `run/sponge-alpha.run:680`) is rejected; the in-DE dialog stays
  the canonical implementation. Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.3 + D16.9 +
  W4. (Strikes the previous `Re-scoped → Phase 15+` disposition.)
- **Background surface — settled by Phase 16 D16.4.** A new
  in-DE `background/` module owns the desktop surface: a
  fullscreen frameless `QWidget` painted via `paintEvent` with
  solid color (sourced from the `background.color` configd key)
  or a `QImage` (sourced from the `background.image` allowlist-
  validated key, defaulting to `/system/background/default.png`).
  A right-click opens a three-item `QMenu` (Settings / Launch
  / Show desktop). The vendored upstream `app/backdrop` is
  removed from the three scenarios that previously built it
  (`sponge-alpha.run`, `sponge-leitzentrale.run`,
  `sponge-usb-boot.run`) so the in-DE widget is the single live
  surface. Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.4 + W6.
- **Keyboard shortcut framework — settled by Phase 16 U16.4 +
  D16.5.** An extensible framework driven by the single
  `shortcuts.bindings` configd key (a structured
  `action<keycode-list>` value with a typed validator; the
  closed `lancer` / `focus_next` / `dismiss` action enum
  expands in Phase 17+). Initial shipped bindings: `Super` →
  launcher popup, `Alt-Tab` → forward focus cycle, `Escape` →
  close topmost popover. Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.5 + W7.
- **Multi-panel feasibility — settled by Phase 16 U16.5 +
  D16.5.** Arbitrary panel count on arbitrary edges. Per-panel
  config namespace `panel.<id>.{height,position,visible_widgets}`
  with a `panel.ids` comma-list bootstrap key. Closed registry
  extended to pattern keys + `MAX_KEYS 16 → 32` (interim) +
  `MAX_PATTERN_KEYS = 32`. Nitpicker label-trap defense: every
  panel instance uses a distinct `label_last` suffix
  (`Sponge Panel 1` / `Sponge Panel 2` / `Sponge Panel Bottom 2`
  etc.); the multi-panel scenario runs at `-m 4G`.
  Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.5 + W8.
- **`panel.position` live — settled by Phase 16 D16.2 + U16.2.**
  Click-to-apply via dual nitpicker panel domains
  (`panel_top` / `panel_bottom`) with visibility toggling
  driven by the `panel.position` configd key. The
  `panel_widget.cc:159-194` constructor-only initial-render
  trap is avoided by the `PanelWidget::applyPosition()`
  explicit refresh slot. `Right` / `Left` are registered in the
  configd enum but the menu entries are disabled with a
  "Phase 17+" tooltip (honest disclosure; AGENTS.md §1.1).
  Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.2 + W5.
- Window management is settled: Phase 14 ships the panel tasklist
  as the deterministic minimize+restore path (D14.3), with the
  decorator's `<closer/>` and `<maximizer/>` buttons covering the
  remaining state transitions. The `<minimizer/>` button on the
  decorator is closed by Phase 16 W9 (the same vendored patch
  that adds the resize sizers to `themed_decorator`; ledger row
  #17 lands in `docs/11-environment.md` §4). See
  `docs/plans/phase16-daily-desktop-defaults.md` D16.6.
- Clipboard is settled: Phase 14 reuses the upstream Genode
  `os/src/server/clipboard` binary as-is, per decision D14.2 in
  `docs/plans/phase14-daily-desktop.md`. The Qt6 side is bridged by
  the vendored `qgenodeclipboard.cpp` (in `qt6_base`); the
  cross-component write/paste proof lives in
  `run/sponge-clipboard.run` (sentinel byte-for-byte in the
  server-side ROM, plus a best-effort visual paste check) and the
  focus-aware write gating is exercised by
  `run/sponge-clipboard-focus.run` (`match_labels="yes"` sub-scenario,
  server denies cross-domain writes).

Notification backend is settled: Phase 14 builds the Sponge-native
`sponge_notifier` daemon, per decision D14.1 in
`docs/plans/phase14-daily-desktop.md`. (Removed from this list.)