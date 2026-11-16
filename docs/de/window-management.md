# Sponge DE Design: Window Management

> **Breadcrumb:** [05 - Sponge DE Design](../05-sponge-de.md) / Window Management
>
> Full text of `docs/05-sponge-de.md` §4.6 (including the frozen
> protocol v1 contract), §4.6.1, and §4.7. Section numbers are
> kept unchanged; the parent keeps numbered stubs so existing
> `docs/05-sponge-de.md §4.6` style references still resolve.

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

### Protocol v1 report/ROM contract (frozen 2026-11-12)

The four labels in the Architecture diagram above form the frozen
`TasklistController`-facing protocol (**protocol v1**, Criterion 16;
`docs/plans/phase16-sponge-wm.md` D16W.6). Everything in this
subsection is **current upstream truth** read from the delivered
consumers and producers. "Frozen" means: the native producers must
reproduce the consumed shapes exactly, and no consumed attribute
changes meaning without a protocol version bump. Required means the
delivered `TasklistController` reads it; pass-through means the field
exists in the report but the controller ignores it (upstream-only
detail). Nothing here is invented: every attribute below appears in
the shipped code.

The frozen shapes are Node-level contracts, not a mandated wire
encoding. Upstream `Generator` reports may serialize them as HID,
whereas Sponge's `Expanding_reporter::generate_xml` writes XML;
`Genode::Node`, used by the delivered controller, reads both
encodings transparently. Native Sponge producers publish the XML
rendering through the same reporter convention. Compatibility means
identical node and attribute semantics in either encoding, not identical
payload bytes across different encodings.

Native Sponge XML producers escape `&`, `"`, and `<` in string
attributes; the shared protocol parser decodes those entities once into
semantic text, and generators re-escape on output. `>` remains literal,
preserving the canonical `->` session-label separator. This changes no
node shape or attribute meaning. The layouter composes the decoded
`label + " " + title` identity before escaping it once, so the
TasklistController's raw-wire comparison remains aligned without a Qt
change. A malformed `window_list` document is not a valid empty snapshot:
the native layouter retains its last good state and warns at a bounded
rate.

Producers per topology (report label equals ROM label in every row;
the run scenarios route `wm -> window_list`, `layouter -> window_layout`,
`sponge-de -> focus_request`, `sponge-de -> rules` through `report_rom`,
one writer per label per AGENTS.md §1.2):

| Label | Root node | Upstream producer (current truth) | Native producer (target) | Writers |
|---|---|---|---|---|
| `window_list` | `<window_list>` | `server/wm` (`Window_registry`) | `sponge_wm` | exactly one |
| `window_layout` | `<window_layout>` | `app/window_layouter` | `sponge_layouter` | exactly one |
| `focus_request` | `<focus_request>` | `TasklistController` | `TasklistController` (unchanged) | sole writer: tasklist |
| `rules` | `<rules>` | `TasklistController` | `TasklistController` (unchanged) | sole writer: tasklist |

#### `window_list` (wm core -> TasklistController)

`<window>` sub-nodes. Windows without a defined size are omitted by
the upstream wm until first sized; an empty `label` is ignored.

| Attribute | Status | Semantics |
|---|---|---|
| `label` | **required** | wm session label (contains spaces and `->`, e.g. `pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo`); the window identity used in `rules` and `focus_request` |
| `title` | **required** (identity) | client title; the controller matches `window_layout`'s `title` against `label + " " + title`, never by splitting on spaces |
| `has_alpha` | consumed | boolean (`yes` form); drives the tasklist accent strip |
| `hidden` | consumed | boolean; window hidden by the wm |
| `resizeable` | consumed | boolean; window advertises a resize affordance |
| `id`, `width`, `height` | pass-through | upstream registry id and client size; geometry comes from `window_layout` instead |

#### `window_layout` (layouter -> TasklistController)

`<boundary>` sub-nodes (one per target; the shipped topology has
exactly one target, `screen`), each containing `<window>` sub-nodes.

Boundary attributes: `name` is pass-through (target name); `xpos`,
`ypos`, `width`, `height` are **consumed**: the target rect whose
origin is the coordinate-origin reference (below) and whose size
drives the maximize inference.

| Window attribute | Status | Semantics |
|---|---|---|
| `title` | **required** | concatenation `label + " " + client title` (bare `label` when the title is empty); matched against `window_list` identity pairs |
| `xpos`, `ypos` | **required** | screen-absolute inner-geometry origin |
| `width`, `height` | **required** | inner-geometry size |
| `focused` | consumed | `focused="yes"` when the window holds focus |
| `id` | pass-through | upstream window id |
| `has_alpha`, `maximizer`, `closer` | pass-through | upstream repeats `has_alpha` here and advertises decorator affordances (`maximizer`/`closer` on resizeable windows); these are affordances, not window state, and the controller reads `has_alpha`/`resizeable` from `window_list` |
| `<highlight>` sub-node | pass-through | upstream hover/drag element state (`pressed="yes"` while dragging) |

#### `focus_request` (TasklistController -> layouter)

Root `<focus_request>` with attributes `id` (monotonic counter) and
`label` (the full `window_list` label). The layouter deduplicates on
`id`, raises/focuses the named window, and forwards the change
through its own `focus` report to the wm's `focus` ROM.
`TasklistController` is the **sole writer** of this report. A
title-bar or content click in the upstream path focuses through the
input/nitpicker chain and writes no report.

#### `rules` (TasklistController -> layouter, `rules="rom"` mode)

Root `<rules>`, replaced wholesale on every publication (last
publication wins). Child order is semantic:

1. `<screen name="screen"/>` (the layouter expects at least one
   `<screen>` node per rules document).
2. One `<assign>` per tracked window, frontmost first. The layouter
   applies the first matching `assign` per window and stacks by
   assign order (earliest assign is frontmost; window_layouter
   README), so the target window is emitted first. Attributes:
   `label` (exact wm label), `target="screen"`, `xpos`, `ypos`
   (target-relative: the controller subtracts the boundary origin;
   feeding absolute values back drifts placement by that origin on
   every republish), `width`, `height`, `maximized` (`yes`/`no`).
3. Static `<assign>` fragments copied verbatim from the
   `tasklist_static_rules` property (the scenario-supplied original
   layouter rules; may use `label_prefix` matching). They follow the
   tracked assigns so the exact per-window assignment wins.
4. `<press key="BTN_LEFT" action="drag"/>` and
   `<release key="BTN_LEFT" action="drop"/>` (layouter
   key-sequence-tracker nodes; kept in every publication).

Parking and coordinate-origin rules: a reported window whose `xpos`
and `ypos` are **both** <= -32000 reads as parked/minimized (the
established write value is `xpos=ypos=-32000`); the controller then
keeps the last on-screen geometry as the restore target and emits the
parking sentinel in the assign. `window_layout` coordinates are
screen-absolute; assign coordinates are target-relative (subtract the
boundary `xpos`/`ypos`). The layouter's assign vocabulary also
accepts `label_prefix`/`label_suffix`, `visible`, and
`xpos`/`ypos="any"` (upstream capabilities the tasklist emits only
through static fragments).

#### Maximized is inferred, not reported

Neither upstream report carries a `maximized` window state. The
controller infers `Maximized` when a non-parked window's inner
geometry fills the boundary area within a 64 px decorator-margin
tolerance, preserves the flag across refreshes, and publishes it as
`<assign maximized="yes">`. Native producers must keep this
geometry-only truth working (v1 adds no maximized attribute; doing so
would be a protocol bump).

> **Correction of record (2026-11-12, `ccfef79cf1`).** The clause
> "preserves the flag across refreshes" is superseded. The delivered
> inference is two-way once a maximized geometry has been observed: a
> full-area geometry sets the flag, and a later non-full, non-parked
> geometry clears it (published after one stable poll). Only a pending
> maximize request that the layouter has not applied yet is carried
> across refreshes, so the request is not lost to a geometry
> intermediate. This corrects the behavioral note only: the frozen
> wire shapes are unchanged (v1 still carries no `maximized`
> attribute and the inference stays geometry-only), so no protocol
> bump is involved.

#### Native target versus current truth

For the native topology (`docs/plans/phase16-sponge-wm.md`,
`run/sponge-wm-native*.run`) only the producers change, per the table
above. The `TasklistController` contract, the required attributes,
the parking and origin rules, assign ordering, and the maximize
inference stay binding. Pass-through fields are upstream-only detail:
native producers may omit them (the controller ignores them) but must
never alter a consumed attribute. The native `sponge_decorator` is
associated through the Sponge-owned core contract (D16W.3) and is not
a consumer of this upstream `window_layout` shape; in the upstream
topology the decorator is a second, pass-through consumer of
`window_layout`.

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

