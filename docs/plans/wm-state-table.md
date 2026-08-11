# Window State-Transition Table (Phase 14)

> Implementation reference for W7 (decorator controls + panel tasklist).
> Authoritative source: decision D14.3 in
> `docs/plans/phase14-daily-desktop.md`. This file gates W7 — it is
> reviewed and checked in BEFORE W7 begins.

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

## Out of Scope

- Window drag (title-bar drag) updates `(x,y)` while staying in
  `Normal-Visible(-Focused)` or `Maximized` — not a state change.
- Alt-Tab focus lands at `Normal-Visible-Focused` (row 2).
- Theme reload preserves the current state; tasklist colors update
  in place.
