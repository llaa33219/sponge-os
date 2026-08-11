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

## 5. User Scenarios

How Sponge DE should present itself to an everyday user:

### 5.1 Right After Boot

- A clean wallpaper and a minimal panel.
- Genode terminology (`init`, `nitpicker`, and the like) is not visible.
- The panel contains the launcher and clock — nothing more.
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

- How to split roles exactly between `nitpicker` and Sponge DE
  (who is responsible for window placement?). Remains open for
  Phase 15+.
- Priority and timing of multi-monitor support. Remains open for
  Phase 15+.
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