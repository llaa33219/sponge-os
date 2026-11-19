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

- `run/compositor.inc` — shared Tcl include: the Tier-0 selection seam
  for the G1 compositor scenarios (reused by later G3/G4 product
  integration). `compositor::enabled` maps `SPONGE_COMPOSITOR`
  (unset/`upstream` → 0, `native` → 1; any other value fails loudly)
  and `compositor::tier0_start_node` returns the Tier-0 start node:
  upstream the full nitpicker node (capture + event, full-screen
  client content, `label: no`, background `#1e1e2e`); native the
  `sponge_compositor` node (width=1024 height=768, same background).
  Upstream nitpicker is the default and the native node is never
  emitted without the `SPONGE_COMPOSITOR=native` opt-in
  (docs/plans/phase16-sponge-compositor.md, D16C.8).

- `sponge-compositor-opaque.run` — **Phase 16 G1**: headless opaque
  golden smoke (no framebuffer driver, no host display, like
  `sponge-de-test.run`) booting the **selected** Tier-0 with the two G1
  view clients (A (100,80,400,300) fill `#3366cc`, white border,
  front; B (300,200,400,300) fill `#cc3333`, black border, back; the
  overlap shows A — the clipping check) and `compositor-capture-probe`
  in golden mode, which verifies the composed screen through a Capture
  session and logs the bounded marker `g1-capture: PASS` (timeout is
  the FAIL path). Tier-0 selection: unset/`SPONGE_COMPOSITOR=upstream`
  runs upstream nitpicker (the default), `SPONGE_COMPOSITOR=native`
  opts in to `sponge_compositor`, invalid values fail loudly. No WM/DE
  components. **seL4 QEMU is the receipt** — base-linux is a dev path
  only and never counts as proof
  (docs/plans/phase16-sponge-compositor.md section 8.7). The build
  list names the G1 component contract of `repos/sponge-compositor`
  (`sponge_compositor`, `compositor-view-client`,
  `compositor-capture-probe`). The probe asserts
  `expect_checksum=0x670d6b5d9fc0ed7b` after a byte-exact comparison;
  view configs use the `client` node's numeric `color`/`border` and
  `stack="front|back"` attributes.

- `sponge-compositor-diff.run` — **Phase 16 G1** differential oracle:
  boots the native (`sponge_compositor`) and upstream (`nitpicker`)
  Tier-0s side by side with identical A/B view clients on each (same
  scene as `sponge-compositor-opaque.run`) and runs
  `compositor-capture-probe` in diff mode with Capture labels `native`
  and `upstream`; byte-exact equal captures log the bounded marker
  `g1-diff: MATCH` (timeout is the FAIL path), with the same checksum
  assertion `0x670d6b5d9fc0ed7b`. Requires
  `SPONGE_COMPOSITOR=native` and fails loudly otherwise: the native
  path is a mandatory explicit opt-in even here (D16C.8); the upstream
  node is only the comparison reference. No WM/DE components. **seL4
  QEMU is the receipt** (same plan, section 8.7).

- `sponge-compositor-input.run` — **Phase 16 G2**: real QMP input battery
  with `compositor-input-probe`. ps2/event_filter carries clicks through
  the selected Tier-0, an alpha/input-mask hole falls through to the lower
  view, focus changes preserve leave-before-enter order, deterministic
  Event batches are delivered to the hit sessions, and the pointer report
  is validated through `report_rom`. Final marker `g2-input: PASS`; each
  phase has its own bounded failure marker. Native is enabled only with
  `SPONGE_COMPOSITOR=native`; upstream nitpicker remains the fallback
  comparison path. No WM/DE components. **seL4 QEMU is the receipt**
  (D16C.8/D16C.14). Host clicks steer the pointer through closed-loop
  `absmove`/`move` confirmed against the `pointer` ROM (framebuffer and
  USB-tablet readiness rendezvous first), each ending in one `press`;
  unscripted buttons or off-count totals fail loudly. The exact
  Event-injection differential `g2-input: trace-inject` matches across
  both Tier-0s; the full-trace digest `g2-input: trace` is diagnostic and
  diverges by the documented attach-time hover-establishment records
  (plan section 8.2). The optional `trace_dump="yes"` probe attribute is
  off by default and disabled in normal scenario runs; enable it only to
  log each canonical record as `g2-input: record ...` for a line-by-line
  native-vs-upstream diff. It is bounded by the probe's MAX_RECORDS and
  never affects the digests or the phase assertions.
