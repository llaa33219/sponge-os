# Window State-Transition Table (Phase 14)

> Implementation reference for W7 (decorator controls + panel tasklist).
> Authoritative source: decision D14.3 in
> `docs/plans/phase14-daily-desktop.md`. This file gates W7 — it is
> reviewed and checked in BEFORE W7 begins.
>
> Extended 2026-11-12: the native Sponge WM scope (Criterion 16,
> `docs/plans/phase16-sponge-wm.md`) adopts this table as its state
> reference and adds the "Native WM Extension" section below. The
> D14.3 baseline rows are unchanged and remain the truth for the
> upstream `wm` + `window_layouter` + `decorator` path and for the
> `TasklistController` (protocol v1 freeze).

## Scope

States: `Normal-Visible`, `Normal-Visible-Focused`, `Maximized`,
`Minimized`. Each window is in exactly one state at a time. The
panel tasklist tracks `(x, y, w, h, visible, focused)` per window;
the tasklist button-click is the deterministic restore path per U3.

Off-screen parking coordinates on minimize: `(x=-32000, y=-32000)`.
Nitpicker's view space is int32; −32000 is well outside any
plausible screen, and the layouter re-paints the window at the
saved coordinates on the next restore.

## Transitions

| From | Trigger | Side-effects | To |
|---|---|---|---|
| (init) | `window_created` | Added to tasklist at default placement; no focus steal. | `Normal-Visible` |
| `Normal-Visible` | Title click | wm emits `focus_request` (existing path). | `Normal-Visible-Focused` |
| `Normal-Visible-Focused` | Minimizer click | Layouter `<assign>` overwrites to `(x=-32000, y=-32000)`; tasklist dims the entry. | `Minimized` |
| `Normal-Visible` | Minimizer click | Same as above (minimize does not require focus). | `Minimized` |
| `Minimized` | Tasklist button click | Tasklist writes its saved `(x,y,w,h)` to a layouter-rule ROM update + wm `focus_request`. | `Normal-Visible-Focused` |
| `Normal-Visible` | Maximizer click | Layouter assigns full-screen. | `Maximized` |
| `Normal-Visible-Focused` | Maximizer click | Same as above (focus preserved across maximize). | `Maximized` |
| `Maximized` | Maximizer click (toggle) | Layouter restores the saved pre-maximize `(x,y,w,h)`. | `Normal-Visible` |
| `Normal-Visible` | Closer click | wm closes the window; tasklist removes the entry. | (destroyed) |
| `Normal-Visible-Focused` | Closer click | Same as above (focus is irrelevant — window is going away). | (destroyed) |
| `Maximized` | Closer click | Same as above (wm close is independent of state). | (destroyed) |
| `Minimized` | Closer click | Tasklist removes the entry; wm closes the window. | (destroyed) |

## Worked Examples

1. **`(init) → Normal-Visible`.** `textedit` is launched via the
   launcher; `window_created` arrives; the tasklist appends one
   entry at the default placement; window is visible, not focused.
2. **`Normal-Visible → Normal-Visible-Focused`.** User clicks the
   title bar; wm emits `focus_request`; the tasklist marks the
   entry as focused (highlight color from the active theme).
3. **`Normal-Visible-Focused → Minimized`.** User clicks the
   minimizer; tasklist saves `(x,y,w,h)`; layouter `<assign>` writes
   `(x=-32000, y=-32000, ...)`; window re-paints off-screen;
   tasklist dims the entry.
4. **`Normal-Visible → Minimized`.** Same as #3; the minimizer does
   not require focus. Alt-Tab to a focused window then minimize.
5. **`Minimized → Normal-Visible-Focused`.** User clicks the
   tasklist entry; tasklist writes its saved `(x,y,w,h)` back to
   the layouter-rule ROM and emits `focus_request`; window
   re-paints at the old position AND takes focus in the same step.
6. **`Normal-Visible → Maximized`.** User clicks the maximizer;
   layouter assigns full-screen.
7. **`Maximized → Normal-Visible`.** User clicks the maximizer
   again; layouter restores the saved pre-maximize `(x,y,w,h)`;
   focus is unchanged.
8. **`Normal-Visible → (destroyed)`.** User clicks the closer; wm
   closes the window; tasklist removes the entry.

## Focus-after-Restore Semantics

Restore from `Minimized` lands in `Normal-Visible-Focused`, not
`Normal-Visible`. The restore step is the tasklist click, and the
focus grant is part of the same step. This is the deterministic
restoration path required by U3 — a decorative minimize button
with no restore is forbidden.

## Native WM Extension (delivered; `docs/plans/phase16-sponge-wm.md`)

> Honest status: this section defines the transitions for the native
> Sponge WM (Criterion 16), delivered and verified at commit
> `191222130e` (receipts: `docs/evidence/phase16-wm-index.md`). The
> rows below are the delivered truth for the native path. The
> baseline table above stays the delivered truth for the upstream
> path and for the `TasklistController`, whose contract is
> frozen at protocol v1 (`window_list`, `window_layout`,
> `focus_request`, `rules`).

Protocol note (2026-11-12): baseline row 2's "wm emits
`focus_request`" wording predates the W7 sole-writer contract. Per
the protocol v1 freeze in `docs/05-sponge-de.md` §4.6, the
`focus_request` report's only writer is `TasklistController`;
title-click focus in the upstream path runs through the
input/nitpicker chain and writes no report. The delivered D14.3 rows
are left as written; this note is the correction of record.

### Mode, Placement, and State Rules

- **Layout mode.** One global key `wm.layout.mode` (`floating` |
  `tiling`, default `floating`). Floating is user-directed placement
  and 8-zone resize. Tiling is the Master-Stack arrangement below.
  The four baseline states stay the per-window state machine;
  placement (master, stack, floating, parked, maximized) is derived
  from the mode plus the state, never an extra state.
- **Master-Stack placement.** Eligible windows are non-minimized,
  non-maximized, and resizeable. The first eligible window in
  creation order takes the left half of the work area as master; the
  remaining eligible windows split the right half into equal stack
  bands top to bottom in creation order. Non-resizeable windows
  always float and never enter the arrangement.
- **Reflow triggers (tiling mode).** The arrangement is recomputed on
  every window open and close, on minimize and restore, on maximize
  and maximize-restore, and on a mode switch. Leaving tiling mode
  keeps each window's current geometry as its floating geometry (no
  hidden pre-tile history is kept). A master that closes, minimizes,
  or maximizes passes the master slot to the next eligible window in
  creation order.
- **Maximized fills the work area.** For the native path, `Maximized`
  fills the work area (screen minus panel), not the raw screen, and
  the toggle restores the saved pre-maximize `(x,y,w,h)`. The
  baseline rows' "full-screen" wording describes the upstream
  layouter; the native chrome's fullscreen button maps to this same
  work-area `Maximized` state in the initial version (honest v1 note
  in `docs/plans/phase16-sponge-wm.md`).
- **Minimized reporting.** Minimized windows are parked and reported
  at the existing off-screen convention: the established value is
  `(x=-32000, y=-32000)` and a window whose reported x and y are
  both at or below -32000 reads as parked (the delivered
  `TasklistController` rule). The native layouter reports the same
  convention so the `TasklistController` interprets reports without
  change.

### Native Transitions (superset of the baseline table)

| From | Trigger | Side-effects | To |
|---|---|---|---|
| (init) | `window_created`, floating mode | Default placement (baseline row 1). | `Normal-Visible` |
| (init) | `window_created`, tiling mode, resizeable | Inserted into the Master-Stack arrangement (master if first eligible, else stack band in creation order); reflow. | `Normal-Visible` |
| (init) | `window_created`, non-resizeable | Floats at default placement in either mode (never enters the arrangement). | `Normal-Visible` |
| `Normal-Visible(-Focused)` | Title drag / 8-zone resize, floating mode | Geometry update only (baseline out-of-scope note applies). | unchanged |
| `Normal-Visible(-Focused)` | Title drag / 8-zone resize, tiling mode | None in v1: tiled geometry is layouter-owned. | unchanged |
| `Normal-Visible(-Focused)` | Minimizer click, tiling mode | Parked at `(-32000,-32000)`; leaves the arrangement; reflow. | `Minimized` |
| `Minimized` | Tasklist button click, tiling mode | Re-enters the arrangement in creation order; reflow; `focus_request`. | `Normal-Visible-Focused` |
| `Normal-Visible(-Focused)` | Maximizer/fullscreen click, tiling mode | Fills the work area (leaves the arrangement); reflow; focus preserved. | `Maximized` |
| `Maximized` | Maximizer toggle, tiling mode | Restores the saved geometry and its arrangement slot; reflow. | `Normal-Visible` |
| any | `wm.layout.mode` `floating -> tiling` | Eligible windows ordered by creation become master + stack bands; non-resizeable windows stay floating; reflow. Per-window states unchanged. | unchanged |
| any | `wm.layout.mode` `tiling -> floating` | Every window keeps its current geometry as floating geometry. Per-window states unchanged. | unchanged |
| arrangement | Any window destroyed, tiling mode | Remaining eligible windows reflow (next in creation order takes master if needed). | (destroyed) |

### Native Worked Examples

9. **Tiling open.** With `wm.layout.mode=tiling` and one resizeable
   window shown, a second resizeable window opens: the first stays
   master on the left half, the second becomes the single stack band
   on the right half; both report their tiled geometry in
   `window_layout`.
10. **Tiling close.** The master closes; the remaining window becomes
    master and reflows to the left half; its state machine row is the
    baseline destroy row.

## Out of Scope

- Window drag (title-bar drag) updates `(x,y)` while staying in
  `Normal-Visible(-Focused)` or `Maximized` — not a state change.
- Alt-Tab focus lands at `Normal-Visible-Focused` (row 2).
- Theme reload preserves the current state; tasklist colors update
  in place.
- Native non-goals (per `docs/plans/phase16-sponge-wm.md`):
  workspaces, window gaps, snap layouts, animations, per-window
  rules, master-ratio configuration, and per-window float-out by
  dragging (tiled geometry is layouter-owned in v1).
