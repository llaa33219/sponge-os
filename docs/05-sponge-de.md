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
├── settings/     # settings bridge: quick-menu change lifecycle
│                 # (ensure-start + per-provider quick pairs)
├── background/   # desktop backdrop surface (paint + right-click menu)
└── theme/        # theme loading and application
```

> `sponge_launcher` has its own component directory ready, but it will
> start out integrated into Sponge DE; splitting it off is considered
> after Sponge DE stabilizes. See `AGENTS.md` §3.4.

> **Settings are not a sponge-de module.** The Settings control
> center is the separate on-demand `sponge_settings` component
> (D16.3 in `docs/plans/phase16-daily-desktop-defaults.md`), and
> the settings windows it launches are package-declared provider
> components. The `settings/` entry above is only the bridge that
> runs the panel/background quick-menu change lifecycle (§5.4,
> "Quick-menu change lifecycle"): ensure-start of the owning
> provider via pkgd, then a closed domain operation over the
> per-provider quick-request / quick-result pair. sponge-de holds
> no config write channel. See §5.4.

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

### 4.6.1 Package-control failure surface (Phase 16 P2.5)

Launch requests already use bounded, non-blocking result polling (100 ms ×
60, about 6 s), so a dead or wedged `sponge_pkgd` cannot freeze the Qt event
loop. Every package-scoped failure now also writes one persistent
`de_op_error` report with the shape:

```xml
<op_error op="launch" pkg="hello" reason="no answer from the package manager"/>
```

The report is enabled in `LauncherController`'s constructor before
`app.exec`, and is a single last-failure-wins slot. Recovery and later
successes do not clear it; it remains until an explicit acknowledgement
mechanism is added. Channel-level failures with no package identity are not
recorded, so they cannot overwrite a package-specific error.

Panel messages intentionally keep their 10 s TTL rather than becoming
sticky: one label is shared with health warnings, and a permanent error
message would mask later `unresponsive(N)` or `exited(N)` states. Health
warnings remain derived from `system_state` and clear when the provider
recovers. The persistent machine-readable error and the transient human
message therefore have independent lifetimes. The DE has no install/remove
writer; only `op="launch"` is emitted.

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
  (configd `background.image` round-trip over the interim
  universal write path; the W4 provider split moves the write
  to the Background provider's own label, which configd's
  label-to-key-domain ACL limits to `background.*` — §5.4).
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
discoverability path for panel configuration; its *Settings*
entry opens (or focuses, if already open) the Panel provider's
settings window (§5.4) — the same provider that owns the
`panel.*` keys. The quick controls themselves change settings
live through the **quick-menu change lifecycle** (§5.4,
"Quick-menu change lifecycle"; W4 in
`docs/plans/phase16-daily-desktop-defaults.md`): each control's
value-change signal first ensures the Panel provider is running
via pkgd on the quick menu's own launch labels (the normal
`(package, settings-id)` path), then sends a closed domain
operation (for example `Panel height=40`, never an arbitrary
config key/value) over the panel quick-request / quick-result
pair. The provider validates the operation and performs the
write on its own config request label, and configd's
label-to-key-domain ACL limits that label to the `panel.*`
domain (D16.1 / D16.10). sponge-de itself holds no config write
channel, and the quick pair is not a config relay: its
vocabulary is closed and owned by the provider.

The menu is implemented in `PanelWidget::contextMenuEvent`
(`repos/sponge/src/sponge-de/panel/panel_widget.cc`); the menu
is anchored to the `QContextMenuEvent`'s global position (the
Genode QPA returns `(0,0)` from `QCursor::pos()` — known
Phase 10+ lesson — so the event-local position is the only
valid anchor). The forwarding runs through
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` (Phase 11
risk #2 codification), so the GUI-thread marshal is enforced at
compile time via the `QWidget::` setter signatures.

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
  `panel.height` `sponge_configd` key (Phase 11); the menu sends
  the change through the quick-menu change lifecycle (§5.4):
  ensure-start of the Panel provider, then the closed
  `Panel height=<n>` operation over the panel quick pair. The
  provider writes the key on its own `panel_settings ->
  config_request` label (per-label key ACL per D16.1 in
  `docs/plans/phase16-daily-desktop-defaults.md`; sponge-de
  holds no config write channel).
- **Visible widgets**, three checkboxes for `clock`, `launcher`,
  and `tasklist`. Sourced from the `panel.visible_widgets` enum-
  list key; Phase 16 W2 extends the configd validator to accept
  the `tasklist` token (the panel-widget has parsed it locally
  since Phase 14 W7, but the configd side rejected writes that
  included it; D16.10).
- **Position**, a radio group with `Top` / `Bottom` enabled and
  `Left` / `Right` displayed-but-disabled with a "Phase 17+"
  tooltip (present implementation state; the 2026-11-10 scope
  update moved their target to Phase 16 Extension criterion 22,
  tooltip text and disabled state unchanged until then). The
  enabled pair maps to the live `panel.position`
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
- **Settings**, opens (or focuses, if already open) the Panel
  provider's settings window over the quick menu's own launch
  labels (the normal `(package, settings-id)` launch path via
  pkgd; §5.4 quick-menu change lifecycle; window identity
  `(sponge-de, panel)`).

All menu actions route through `QMetaObject::invokeMethod(...,
Qt::QueuedConnection)` (Phase 11 risk #2 codification), so the
GUI-thread marshal is enforced at compile time via the
`QWidget::` setter signatures.

### 5.4 Changing Settings (Settings control center)

Settings live in provider-owned windows, not in a universal
dialog. The shell that lists and launches them is the separate
on-demand `sponge_settings` component (decision D16.3 in
`docs/plans/phase16-daily-desktop-defaults.md`); the window that
changes a setting belongs to the provider that owns the key or
store. `vct config` on the CLI stays the intentional admin /
manual escape hatch (`AGENTS.md` §1.1 control door).

**Discovery.** Packages declare `0..N` settings entries in their
metadata (separate from the singular `<launcher>`; a package may
ship a launcher, settings entries, both, or neither). Stable
identity is `(package, settings-id)`, unique per package and
stable across reinstall. Each entry declares a title, a category
from a closed enum (`desktop`, `hardware`, `system`,
`applications`, `other`; unknown/absent → `other`), optional
icon/keywords, and a package-payload binary target. Entries must
not contain absolute
paths, shell commands, interpreters, or arbitrary Exec strings.
`sponge_pkgd` validates and resolves every entry and republishes
the installed set via installed metadata; `sponge_settings` holds
that ROM and refreshes on the ROM update signal (never polling),
grouping entries by category.

**Launch.** Clicking an entry sends only `package` + `settings-id`
over the shell's dedicated settings launch request/result labels;
`sponge_pkgd` starts the declared binary target as its own
component with its own start/lifecycle identity (settings targets
are never exclusive, even when they share the main app's binary).
Any package can join with no shell code changes and no central
allowlist; there is no in-process plugin or embedding path.

**Focus.** Window identity derives from `(package, settings-id)`.
A second activation of an already-open target focuses and raises
the existing singleton window; it is not an already-running
refusal and creates no duplicate.

**Provider-owned change.** The provider owns its UI, validation,
writes, lifecycle, and errors. First-party shared DE keys stay in
configd's closed typed registry, but every first-party provider
has a distinct config request label, and configd enforces a
label-to-key-domain allowlist on every set (the Panel provider
cannot write `background.*`; the rejection is a structured error
and the store is untouched). Required first-party providers:
Panel, Background, Theme, Shortcuts, Defaults, Display, WM,
Terminal. The historical tab surfaces split among their owners —
panel settings to the Panel provider, theme to Theme, background
to Background, shortcuts to Shortcuts, reset-to-defaults to
Defaults. Terminal settings belong to the terminal package;
Konsole or any other package ships its own provider the same way.
Third-party providers never receive shared-DE configd write
access; they validate and persist in a package-private store.
`sponge_pkgd` never relays config writes. sponge-de consumes
domain broadcasts but is not a settings writer: the universal
`de_config_request` / `SettingsController` writer is removed when
the W4 provider split lands and is not kept as a compatibility
path (D16.9).

**Quick-menu change lifecycle (cold start and change request).**
The panel and background quick menus change live settings
through their owning first-party providers, never through a
configd write channel of their own. This lifecycle is binding
for every quick-menu change, in the cold case (the provider is
not running) exactly as in the warm case:

1. **Ensure-start.** A quick-menu action first ensures the
   owning first-party settings target is running via
   `sponge_pkgd`, over the normal `(package, settings-id)`
   launch path. The quick menu has its own single-writer launch
   labels, separate from `sponge_settings`'s and `vct`'s. pkgd
   only starts (or focuses) the provider; it never carries the
   operation or its value.
2. **Closed domain operation.** Once the provider is live,
   sponge-de sends a closed domain operation (for example
   `Panel height=40`, not an arbitrary config key/value) over a
   dedicated per-provider quick-request / quick-result
   Report/ROM pair. Each pair's vocabulary is closed and owned
   by its provider. This is not a universal `SettingsController`
   and not an arbitrary config relay.
3. **Provider-side validation and write.** The provider
   validates the operation and performs the configd write on its
   own ACL-limited label (for example `panel_settings ->
   config_request`); configd's label-to-key-domain ACL still
   enforces the final write. The quick menu never gets a configd
   write channel. The provider answers on the quick-result label
   with a structured success or error. A start failure, a
   provider that is unavailable, or a validation failure is
   surfaced without hanging the DE and leaves the config store
   unchanged.
4. **Warm path and re-start.** If the provider is already
   running, the operation goes directly over its quick pair. If
   it has exited, step 1 repeats. Settings-target singleton /
   focus semantics are unchanged. Third-party packages get no
   quick-control channel: the quick pairs exist only for the
   named first-party domains (Panel, Background, and the other
   required providers).

Providers are not kept persistent just to serve quick edits, and
the menu never refuses a change because the provider is closed:
ensure-start is the designed cold path (W4 in
`docs/plans/phase16-daily-desktop-defaults.md`).

**Direct launch.** `vct settings list` / `vct settings open` share
the same backend (installed metadata + launch channel) with
separate single-writer labels and `--json` output. The panel and
background context-menu *Settings* entries open or raise their
owning provider over the quick menu's own launch labels, the
same normal `(package, settings-id)` launch path as the
quick-menu change lifecycle above (the *Settings* entry is the
ensure-start of that lifecycle with the window raised instead of
an operation sent).

**Privileges and failure containment.** `sponge_settings` holds
only the installed-metadata ROM, its own launch request/result
labels, and Gui/Timer sessions as needed. It has no configd
write channel, no package File_system session, no provider code
loading, and no GL-heavy requirement; closing it reclaims the
resources. A provider crash leaves the control center and sibling
providers alive, and the entry can be relaunched. With pkgd down,
opening a target fails fast with `package-manager-unavailable`
(a cached discovery list may still be shown; there is no hang or
retry loop). Removing a package deletes its entries on the next
metadata update; a stale open attempt returns `not-installed`.
Changes apply immediately, or the provider says clearly when a
restart is required.

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

1. **Settings control center + providers (W4 target; replaces the
   interim dialog).** The settings surface is the separate
   on-demand `sponge_settings` control-center component plus
   package-declared provider windows (identity `(package,
   settings-id)`), with `sponge_pkgd` validating and republishing
   entries, discovery on the installed-metadata ROM update signal
   (no polling), focus-on-second-open (window identity derives
   from `(package, settings-id)`), per-label configd key ACLs for
   shared DE keys, package-private stores for third-party
   providers, direct `vct settings list` / `vct settings open`
   (`--json`), the quick-menu change lifecycle of §5.4
   (ensure-start via pkgd + closed per-provider quick
   operations), and the failure/removal contract of §5.4. The
   required first-party providers are Panel, Background, Theme,
   Shortcuts, Defaults, Display, WM, and Terminal (Terminal
   settings belong to the terminal package; Konsole or any
   package provides its own). Acceptance (target, unchecked):
   `run/sponge-settings-center.run`,
   `run/sponge-settings-providers.run`,
   `run/sponge-settings-thirdparty.run`,
   `run/sponge-settings-failure.run`,
   `run/sponge-settings-vct.run`, plus physical-hardware receipts
   (machine, parent commit, Genode pin, image/scenario, first
   paint + input-driven change). Interim status: the shipped tree
   still contains the legacy dialog module
   `repos/sponge/src/sponge-de/settings/` and the universal
   writer `config/settings_controller.*`; they remain until the
   W4 provider split lands and are then removed (D16.9).
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
   "Phase 17+" tooltip per D16.2 (unchanged; target moved to Phase
   16 Extension criterion 22 per the 2026-11-10 scope update);
   Settings entry). The height / widgets / position edits travel
   the quick-menu change lifecycle (§5.4). Position
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

The base Phase 16 vendored budget (**one new row #17**, themed
decorator sizer + minimizer; `docs/11-environment.md` §4) is
historical, not a cap on Extension work (D16.8). No vendored
patch is absorbed silently: any additional patch required by
Extension work must be individually justified, recorded in the
ledger before application, independently revertable, and
re-audited at the final Criterion 27 cleanup; Sponge-owned
components and public interfaces (especially the custom WM) are
preferred over vendored changes. No specific new patch is claimed
required or approved. The
honest limitations register (Phase 17+ carry-overs) lives in
`docs/evidence/phase16-index.md` §6.

> **Scope update (2026-11-10, user-directed; revised 2026-11-11).**
> The Phase 16 Extension gained twelve further completion criteria
> (`docs/09-roadmap.md` §10, Criteria 16-27). "Settled" statements
> below describe the current upstream baseline: the custom Sponge
> WM is now an unchecked Phase 16 Extension criterion (16), and the
> left/right panel positions' target moved to Phase 16 Extension
> (criterion 22). Nothing in the current UI has changed yet: the
> left/right entries stay disabled with their "Phase 17+" tooltip
> until that criterion is implemented.

---

## 6. Lightweight Strategy

How Sponge DE stays light under Genode's resource constraints:

1. **Minimal Qt module linking**: link only what is needed.
2. **Lazy loading / on-demand components**: modules that are not
   in use load on demand, and the settings control center
   (`sponge_settings`) runs as an on-demand component whose
   resources are reclaimed when it closes.
3. **Minimal static assets**: the default theme ships only the bare
   minimum.
4. **Simple rendering**: avoid complex shaders and animations at first.
5. **Apply component separation gradually**: keep the single-component
   memory advantage at first, and split later when needed.

---

## 7. Open Design Questions

- Priority and timing of multi-monitor support. Remains open for
  Phase 15+.
- **Settings GUI backend — settled by Phase 16 D16.3, revised to
  the provider-owned control center.** The settings surface is a
  separate on-demand `sponge_settings` control-center component
  plus package-declared provider windows: discovery from installed
  metadata (ROM update signal, no polling), launch by
  `(package, settings-id)` over a dedicated launch channel,
  focus-on-second-open (window identity `(package, settings-id)`),
  provider-owned UI/validation/writes/errors (per-label configd
  key ACLs for shared DE keys, package-private stores for
  third-party providers), direct `vct settings list` / `open`
  (`--json`), and the failure/removal contract of §5.4. The
  control center holds no configd write channel, no package
  File_system session, no provider code loading, and no GL-heavy
  requirement; closing it reclaims its resources (resource quotas
  are measured and declared per component — shell and provider
  components do not inherit sponge-de's 128 MB Qt6 + Mesa quota
  by default). The legacy `repos/sponge/src/sponge-de/settings/`
  module and the universal writer (`config/settings_controller.*`
  on `de_config_request`) are removed by the provider split
  (D16.9) — they are not a second live design. Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.1 + D16.3 +
  D16.9 + D16.10 + W4. (Strikes the previous `Re-scoped → Phase
  15+` disposition and the interim in-component dialog decision.)
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
  "Phase 17+" tooltip (honest disclosure; AGENTS.md §1.1; target
  moved to Phase 16 Extension criterion 22 per the 2026-11-10
  scope update, tooltip unchanged).
  Detailed design:
  `docs/plans/phase16-daily-desktop-defaults.md` D16.2 + W5.
- **`de_op_error` acknowledgement.** The persistent launch-error record is
  currently last-failure-wins per boot and is never cleared by recovery or
  later success. A future acknowledgement path (an explicit UI or report
  control) must be deliberate; silently clearing it on the next successful
  launch would violate the current persistence contract.
- Window management is settled: Phase 14 ships the panel tasklist
  as the deterministic minimize+restore path (D14.3), with the
  decorator's `<closer/>` and `<maximizer/>` buttons covering the
  remaining state transitions. The `<minimizer/>` button on the
  decorator is closed by Phase 16 W9 (the same vendored patch
  that adds the resize sizers to `themed_decorator`; ledger row
  #17 lands in `docs/11-environment.md` §4). See
  `docs/plans/phase16-daily-desktop-defaults.md` D16.6.
  (Scope update 2026-11-10: "settled" here describes the current
  upstream baseline only. The custom Sponge WM is now an unchecked
  Phase 16 Extension criterion 16; until it lands, this baseline
  remains the operative window-management stack.)
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