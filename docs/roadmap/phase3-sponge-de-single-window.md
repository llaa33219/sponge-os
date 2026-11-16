> Part of [09 - Roadmap](../09-roadmap.md) §5.

## 5. Phase 3: Sponge DE Single Window

### Goal

Verify that Sponge DE opens a **single window** on top of Genode's
`nitpicker` (the window compositor). This phase only confirms the bare
fact that "one window is drawn by Qt"; there is no panel, launcher, or
notification yet.

### Completion Criteria

- [x] Qt builds and links inside Genode (using the existing Genode Qt
  port). Verified: sponge-de binary compiles and links against Qt6 6.8.3
  (libQt6Core, libQt6Gui, libQt6Widgets) on Genode 26.05.
- [x] Sponge DE opens a `Gui` session and displays a Qt widget.
  Verified by `run/sponge-de-test.run`: the `sponge_de_probe` component
  reads nitpicker's composited screen through a `Capture` session and
  positively matches the demo window's themed `window_bg` color
  (`#313244`) at the demo-domain center.
- [x] Keyboard and mouse input are received through the `Input`
  session. Verified by the probe injecting an absolute-motion +
  BTN_LEFT click into nitpicker's `Event` service: sponge-de's `input`
  report (relayed via `report_rom`) confirms the press, and the demo
  button's `clicked` signal fires
  (`sponge-de: input event received (button clicked)`).
- [x] A run scenario automatically verifies that the window appears:
  `run/sponge-de-test.run` is headless (no framebuffer driver, no host
  display) and kernel-agnostic; it passes when the probe logs
  `sponge-de-probe: PASS` (`run_genode_until` match). The interactive
  escape hatch is `run/sponge-de.run` (fb_sdl on the host display).

### Explicitly **Excluded** From This Phase

- DE modules such as the panel, launcher, and notifications (Phase 5).
- Theme system (Phase 5).
- Multi-monitor support.
