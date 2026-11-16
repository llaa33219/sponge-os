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

> **Full text moved to
> [`docs/de/theme-and-notifications.md`](de/theme-and-notifications.md)
> (§4).** Direction: visual elements are not hardcoded in the
> source; they live in theme files (INI-style key-value format,
> `docs/10-theme-format.md`), with two storage layers (user
> settings directory over system default), applied at Sponge DE
> start and reapplied at runtime by `vct theme apply` through the
> `sponge_themed` backend. User-customized themes take priority
> over the system default, and upgrades do not silently overwrite
> user changes.

---

## 4.5 Notification System (Phase 14 — Implemented)

> **Full text moved to
> [`docs/de/theme-and-notifications.md`](de/theme-and-notifications.md)
> (§4.5).** The Sponge-native `sponge_notifier` daemon (decision
> D14.1) over the Report/ROM pattern, the themed panel popover,
> per-entry `kind` / `title` / `body` / `id` / `ttl_ms` with a
> 500 ms TTL sweep and FIFO `max_live` cap, per-component opt-in,
> the absent-daemon warning, and the D14.1 capability surface.

---

## 4.6 Window Management (Phase 14 D14.3 — Implemented)

> **Full text moved to
> [`docs/de/window-management.md`](de/window-management.md)
> (§4.6).** Real minimize + restore (per U3): the four-state
> machine (`docs/plans/wm-state-table.md`), the panel tasklist as
> the deterministic restore path, the `window_list` /
> `window_layout` / `focus_request` / `rules` architecture, and
> the **Protocol v1 report/ROM contract (frozen 2026-11-12)**.
> That child holds the frozen WM protocol contract; the living
> user scenarios moved to `docs/de/user-scenarios.md`.

### 4.6.1 Package-control failure surface (Phase 16 P2.5)

> **Full text moved to
> [`docs/de/window-management.md`](de/window-management.md)
> (§4.6.1).** Bounded, non-blocking launch-result polling
> (100 ms x 60); one persistent last-failure-wins `de_op_error`
> report per package-scoped failure (`op="launch"` only); panel
> messages keep their 10 s TTL.

### 4.7 Background Module (Phase 16 W6 — Implemented)

> **Full text moved to
> [`docs/de/window-management.md`](de/window-management.md)
> (§4.7).** The in-DE `background/` module: `BackgroundWidget`,
> `BackgroundController`, and the `ShowDesktop` (U16.3)
> minimize-all / restore-all machine, behind the
> `<background source="controller"/>` activation gate.

---

## 5. User Scenarios

> **Full text moved to
> [`docs/de/user-scenarios.md`](de/user-scenarios.md) (§5).**
> Living user scenarios, kept separate from the frozen WM protocol
> contract of §4.6. The numbered subsections keep their original
> numbers and names.

### 5.1 Right After Boot

> Moved to
> [`docs/de/user-scenarios.md` §5.1](de/user-scenarios.md#51-right-after-boot).
> Clean wallpaper and a minimal panel (launcher, tasklist, clock),
> no Genode terminology visible, no forced first-time guide.

### 5.2 Launching an App

> Moved to
> [`docs/de/user-scenarios.md` §5.2](de/user-scenarios.md#52-launching-an-app).
> Launcher icon or `Meta`; the app list is generated from
> `vct install` packages; one click runs the app via
> `sponge_launcher` -> `sponge_pkgd` -> `init`.

### 5.3 Panel context menu (right-click on the panel)

> Moved to
> [`docs/de/user-scenarios.md` §5.3](de/user-scenarios.md#53-panel-context-menu-right-click-on-the-panel).
> The `QMenu` with Height / Visible widgets / Position / Settings
> live controls, the quick-menu change lifecycle, and the dual
> nitpicker panel domains (`panel_top` / `panel_bottom`, U16.2 /
> D16.2).

### 5.4 Changing Settings (Settings control center)

> Moved to
> [`docs/de/user-scenarios.md` §5.4](de/user-scenarios.md#54-changing-settings-settings-control-center).
> Provider-owned settings windows behind the on-demand
> `sponge_settings` control center (D16.3): discovery from
> installed metadata, `(package, settings-id)` launch, focus on
> second open, per-label configd key ACLs, the quick-menu change
> lifecycle (ensure-start plus closed domain operations), and the
> three session opt-in gates. `vct config` stays the intentional
> admin / manual escape hatch.

### 5.5 Control for Advanced Users

> Moved to
> [`docs/de/user-scenarios.md` §5.5](de/user-scenarios.md#55-control-for-advanced-users).
> `vct leitzentrale` is currently the only way to open
> Leitzentrale; a panel menu entry is deferred to a later release.

### 5.6 Phase 16 — Daily-Usable Desktop Defaults & Configuration

> Moved to
> [`docs/de/user-scenarios.md` §5.6](de/user-scenarios.md#56-phase-16--daily-usable-desktop-defaults--configuration).
> The eight Phase 16 module / component changes (settings control
> center + providers, `background/` module, panel context menu +
> dual-domain topology, keyboard shortcuts, multi-panel, mouse
> resize hookup, registry re-architecture, bake wiring) with their
> acceptance scenarios, the vendored budget note, and the
> 2026-11-10 scope update blockquote.

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
  by default). The legacy `repos/sponge-de/src/sponge-de/settings/`
  module and the universal writer (`config/settings_controller.*`
  on `de_config_request`) are removed by the landed provider split
  (D16.9); they are not a second live design. Implementation and
  QEMU verification: `docs/evidence/criterion18-settings.md`
  (physical-hardware acceptance still open). Detailed design:
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
  (Scope update 2026-11-12: "settled" here describes the upstream
  baseline, which stays the fallback path (D16W.7). The custom
  Sponge WM (Phase 16 Extension criterion 16) is delivered and
  verified at commit `191222130e` with the native product scenarios
  passing with the upstream trio absent; receipts in
  `docs/evidence/phase16-wm-index.md`.)
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
