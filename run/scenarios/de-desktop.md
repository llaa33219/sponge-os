# Sponge DE, window management, and panel/theme config

Breadcrumb: [`run/README.md`](../README.md) → Sponge DE, window management, and panel/theme config

Sponge DE demos and probes, the upstream window-management stack, live
configd-driven panel keys, themed window chrome, and the configd store.
Full catalog entries follow verbatim (cautions and limitations
included); the parent index carries one-line summaries.

## Scenarios
- `sponge-de.run` — Phase 3: interactive Sponge DE demo (panel + themed
  demo window) on base-linux with fb_sdl + nitpicker. Opens an SDL
  window on the host display and accepts host mouse/keyboard. Ends on
  the `sponge-de: panel and window shown` marker; replace the final
  `run_genode_until` with `run_genode_until forever` for interactive
  viewing.

- `sponge-de-test.run` — Phase 3: automated, headless GUI verification.
  No framebuffer driver and no host display: the `sponge_de_probe`
  component reads nitpicker's composited pixels via a Capture session
  (window rendering check) and injects a synthetic click via the Event
  service (input round-trip check), then logs `sponge-de-probe: PASS`.
  Kernel-agnostic.

- `sponge-wm.run` — Sponge DE on the upstream window-management stack
  (wm + window_layouter + decorator). The panel bypasses the wm to a
  fixed nitpicker domain; app windows route through the wm and get
  decorated, movable frames. `wm_probe` verifies the decoration
  pipeline and injects a synthetic title-bar drag, confirming the
  window actually moves (`wm-probe: PASS`). Headless.

- `sponge-panel-config.run` — **Phase 11 criterion 1** (base-linux):
  the four new configd keys applied live to the panel. sponge-de runs
  with `<config source="configd"/>` (the W2 ConfigController);
  `sponge_de_probe`'s `panel-config` phase drives configd through the
  real `config_request`/`config_result` channel and asserts 7
  subphases (P1/P2 launcher-toggle height tracks `panel.height`
  28↔64, P3–P5 `panel.visible_widgets` hides/shows clock and toggle,
  P6 `clock.format` HH:mm→HH:mm:ss glyph growth, P7 a validator-
  rejected write leaves the broadcast unchanged). Gates on
  `sponge-de-probe: phase panel-config PASS`.

- `sponge-panel-config-sel4.run` — Phase 11 criterion 1 on the
  production kernel: the same 7-subphase panel-config flow on
  base-sel4 with the interactive driver stack. Configd-write driven
  (no QMP choreography). Gates on
  `sponge-de-probe: phase panel-config PASS`.

- `sponge-de-themed-chrome.run` — **Phase 11 criterion 3**
  (base-sel4 + QMP): the upstream `themed_decorator` drop-in replaces
  the stock decorator in the WM stack (child named `decorator`,
  binary `themed_decorator`, so the Phase-10 report_rom/wm policies
  apply verbatim). The theme tar comes from `./tool/decor_assets`
  (upstream geometry metadata + byte-vendored font.tff); the
  decorator's whole config (libc + vfs + policy color from the active
  theme's panel_bg) is delivered live by `sponge_decorator_bridge`
  via report_rom — there is no inline `<config>` (init reserves that
  ROM label and would shadow the route). Gates: nonzero
  `decorator_margins` (20/8/1/1), the title bar verifiably tinted by
  the theme palette (wm_probe observe-3b, measured RGB(91,91,100) vs
  untinted (180,180,191)), a usb-tablet QMP drag moving pkg_gui_demo
  through the themed chrome, and `wm-probe: PASS`. The Phase-10
  `sponge-wm-qmp.run` drag regression must stay green alongside.

- `sponge-configd-persist.run` — **Phase 14 W1 skeleton** (W6
  deliverable scenario; only the build list + config + route + gated
  run_genode_until land in W1). Boots `sponge_configd` with the
  report_rom wire contract for `config_request` / `config_result` /
  `config` broadcast (mirrors `run/sponge-config-probe.run`); the W6
  implementation commit will add `test/configd_persist_probe/`, drive
  the three-write sequence (`panel.height=64`, `panel.visible_widgets=
  clock`, `panel.height=28`), restart the daemon (or re-read the on-
  disk store), and assert the broadcast carries `panel.height=28` after
  restart. The W1 skeleton times out at the gated `configd-persist-
  probe: PASS` marker — that is the EXPECTED FAIL until W6 lands.
