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

### 5.3 Changing Settings

- Settings can be changed from Sponge DE's settings GUI or from
  `vct config` on the CLI. Both paths use the same
  `sponge_configd` backend, so they stay consistent.
- Changes apply immediately, or the user is told clearly when a restart
  is required.

### 5.4 Control for Advanced Users

- `vct leitzentrale` opens the Leitzentrale window for direct
  manipulation of the system component tree.
- In this release the only way to open Leitzentrale is the
  `vct leitzentrale` CLI command; a panel menu entry is deferred to a
  later release.

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
- Window management is settled: Phase 14 ships the panel tasklist
  as the deterministic minimize+restore path (D14.3), with the
  decorator's `<closer/>` and `<maximizer/>` buttons covering the
  remaining state transitions. The `<minimizer/>` button on the
  decorator is deferred to Phase 15+ as a follow-up
  (D14.8(d) candidate — see §4.6).
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