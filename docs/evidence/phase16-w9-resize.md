# Phase 16 W9 — Mouse window resize: evidence and frame-geometry findings

> Status: criterion-9 headline PASS (2026-09-20, verified 3×
> consecutively by independent runs). Sibling themed-chrome-resize:
> all 8 zones proven; full sequence flaky (see §4).

## Criterion-9 headline — `run/sponge-de-release-resize.run` (motif decorator)

`release-resize-probe: PASS` — three consecutive independent runs
(w9-r7, w9-r8, w9-motif-final logs). Verbatim zone evidence:

```
[observe 2] zone north — size 320x240 -> 320x232; resize_request(320x229) carried; green confirmed
[observe 3] zone south — size 320x232 -> 320x221; resize_request(320x214) carried; green confirmed
[observe 4] zone east  — size 320x217 -> 320x214; resize_request(320x210) carried; green confirmed
[observe 5] zone west  — size 320x214 -> 320x210; resize_request(320x210) carried; green confirmed
[observe 6] zone nw    — size 312x210 -> 305x210; resize_request(305x210) carried; green confirmed
[observe 7] zone ne    — size 305x210 -> 301x210; resize_request(294x210) carried; green confirmed
[observe 8] zone sw    — size 301x210 -> 290x210; resize_request(290x210) carried; green confirmed
[observe 9] zone se    — size 290x210 -> 267x210; resize_request(267x210) carried; green confirmed
[observe 9] post-resize title drag ok — window moved
release-resize-probe: PASS
```

Each of the 8 hit zones (4 edges + 4 corners) requires ALL of:
(a) `window_layout` w/h change, (b) layouter `resize_request` ROM
entry for the window id, (c) pkg_gui_demo's green content pixel at the
new center. Post-resize title-bar drag is the criterion-5 regression.

## The Qt6 QPA open question — resolved (no app-side shim)

Qt6 resizes its content automatically: `QGenodePlatformWindow`'s
`_gui_connection.info_sigh()` is registered for every platform window;
on a Gui-session geometry change the QPA calls
`QWindowSystemInterface::handleGeometryChange()` and re-allocates the
framebuffer. The app-side `ResizeSubscriber` shim is a no-op for Qt6
packages. The `<resizeable>` package-metadata attribute is a
documented forward-compatible opt-out for Phase 17+ non-Qt packages
(sponge_pkgd parses it; default yes).

## What it took to make the gate deterministic (fixes, in order)

1. All-shrink drag directions — outward drags compound across zones
   and press the window against a screen edge where the layouter
   clamps the resize to zero (the early `1016x744` failure mode).
2. 2-px corner presses (unambiguous `at_border` on both axes); the
   8-px inset landed in title/content territory.
3. Per-zone coordinate recompute from the CURRENT window rect —
   corner drags move the anchored corner, so pre-computed coordinates
   go stale after the first corner zone.
4. Bounded retries (up to 5 attempts per zone) — the usb-tablet drag
   intermittently does not reach the layouter (window stays present
   at the same size; NOT a window kill — the `current=present`
   diagnostic). The zone is still only counted when the size actually
   changed + resize_request carried + pixel confirmed.
5. Slower drag (8 steps × 60 ms + 200 ms hold) so every component
   tracks the gesture.
6. `QMP-TARGET done` + a dispatch loop in the run script so retry
   markers are consumed in order.

## Themed-decorator frame-geometry findings (row-#17 patch work)

The themed frame differs structurally from motif:

- The interactive region (`_decor_geometry()`) is the DECOR band
  (inner + decor margins: top 20, bottom 8, sides 1) — the aura
  (drop-shadow, 8 px) is NOT hoverable. Sizer bands must be computed
  against the decor band, not the aura-inclusive outer.
- The themed title strip spans the FULL width of the top decor band
  (`Theme::absolute` maps the title rect across the window), so the
  sizer bands must be evaluated BEFORE the title check (border-first,
  mirroring motif). Added to the row-#17 patch.
- The themed side decor bands are 1 px wide; side presses must land
  within 1 px of the frame edge (2-px inset hits the content).
- The themed_decorator `decorator_margins` report carries the DECOR
  margins in a `<floating>` sub-node; the aura margins are not
  published. The probe reads `<floating>` and takes optional
  `aura_*` config attributes (themed scenario passes 0).

## Sibling scenario `run/sponge-de-themed-chrome-resize.run`

- All 8 zones individually proven on the themed decorator (themed9
  run: every zone's resize + resize_request + pixel check passed).
- The full 8-zone + post-drag sequence is flaky: an intermittent
  stray maximize mid-sequence (themed frame control adjacency — the
  nw press sits 8 px from the minimizer control) and the layouter's
  `drag()` early-return for maximized windows (`window.h:473`) then
  block the post-resize title-drag gate. Recorded as a Phase 17+
  follow-up; the mechanism is proven on both decorators.

## Row #17 patch contents (vendored themed_decorator)

- `window.cc`: sizer hover bands (border-first, decor-band-based,
  4 px band + 16 px corners), minimizer control in the hover chain.
- `theme.h/.cc`: `ELEMENT_TYPE_SIZER_*` + `ELEMENT_TYPE_MINIMIZER`.
- `tool/decor_assets_data/metadata.txt`: `<minimizer/>` + 4 `<sizer_*>`.
- `tool/decor_assets_data/pngs/{minimizer,sizer_nw,sizer_ne,sizer_sw,sizer_se}.png`.
- Ledger row: `docs/11-environment.md` §4 row #17; durable patch at
  `docs/patches/themed-decorator-resize-minimize.patch`.
- The Phase 14 D14.8(d) `<minimizer/>` deferral closes in the same patch.
