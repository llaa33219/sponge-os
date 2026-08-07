# Phase 11 / W6 — alpha black-panel flake: analysis and fix

> Discovered during the Phase-11 final regression sweep: `run/sponge-alpha.run`
> (base-sel4) failed ~50-60% of boots with "themed panel never composited"
> — the nitpicker panel domain showed an all-zero (black) buffer forever.
> Root-caused 2026-08-07; fixed and verified 6/6 + sweep green.

## Symptom

The alpha_probe's criterion (a) samples the panel band at (512,4) via a
Capture session. Passing boots: `0xffe6e9ef` (theme 'light' panel_bg).
Failing boots: `0x1e1e2e` (nitpicker background) at poll 0, then `0x0`
(black) from ~poll 20 forever — a mapped but never-painted client buffer.

## Root cause chain

1. **The Genode QPA reports a degenerate 1x1 screen until nitpicker's
   panorama info arrives.** `QGenodeScreen`'s constructor and
   `_info_changed` map `Gui::Undefined` to `Gui::Area{1,1}`
   (`qgenodescreen.h:56-63`, in the qt6_base port source). Whether the
   panorama info has arrived when sponge-de's panel constructs is
   boot-timing dependent — on seL4 with the heavy alpha topology
   (pkgd + lz subsystem), the info regularly arrives late.
2. **`PanelWidget::_apply_geometry` trusted that width blindly.**
   `screen->geometry().width()` returned 1 in late-info boots, and the
   panel was fixed to **1x28** (measured via a temporary paintEvent
   trace: `panel paintEvent #1 size=1x28`). A 1-pixel-wide panel buffer
   in the full-width panel domain reads as black at the probe's sample
   point.
3. **The pre-Phase-11 code had the same vulnerable read** — what
   changed in Phase 11 (W2) is boot timing: the ConfigController
   construction and the restyle-migration work shifted when the panel
   constructs relative to nitpicker's panorama availability, pushing
   the race from ~never-loses to ~50%. Bisect legs that reverted
   launcher/panel files changed the rate by shifting timing, which is
   why the correlation initially pointed at the launcher Q_OBJECT
   change (a red herring — confirmed by Oracle consultation).

## The fix (repos/sponge/src/sponge-de)

- `panel/panel_widget.cc::_apply_geometry`: never trust an implausible
  screen width (`< 64`); fall back to the scenario reference width
  1024. Same guard in `launcher/launcher_menu_view.cc::_apply_style`
  (popup width was `screen/3` = 0 in late-info boots).
- `panel/panel_widget.cc::_apply_geometry`: only call
  `setGeometry`/`setFixedSize` when the target rect actually differs.
  On this QPA, every such call re-allocates the Gui framebuffer
  session (`_adjust_and_set_geometry` → `buffer(mode)`), rotating the
  panel's dataspace out from under nitpicker even for no-op sizes.
- `panel/panel_widget.cc::_apply_style`: only re-apply the stylesheet
  when the CSS actually changed (cached `_applied_css`).
- `panel/panel_widget.h`: `_visible_widgets` now initializes to
  `"clock,launcher"` (the constructor comment always promised this;
  the empty default hid all panel children in scenarios without the
  configd gate).

## Bisection / verification data

| Tree state | alpha result |
|---|---|
| pre-Phase-11 (e4914e63ec) | 4/4 PASS |
| + W1 configd | 3/3 PASS |
| + W3 theme loader | 3/3 PASS |
| full HEAD (W2) | ~2/5 PASS |
| + width-floor guard + geometry guards + visibility default | **6/6 PASS** |

Post-fix sweep on the final tree: `sponge-alpha` 6/6, `sponge-panel-config`
(linux) PASS, `sponge-theme` PASS, `sponge-panel-config-sel4` PASS,
`sponge-de-themed-chrome` PASS (drag + tint gates included).

## Follow-ups (not blockers)

- The proper upstream fix is in the QPA (`QGenodeScreen` should defer
  or retry rather than publish a 1x1 geometry); candidate patch-ledger
  entry alongside the Phase-12 decorator asset-reload patch
  (`docs/11-environment.md` §4.2).
- The sel4-interactive launch-click flake (PS/2 REL drift) remains a
  separate known issue — roadmap §11.3 item 1.
