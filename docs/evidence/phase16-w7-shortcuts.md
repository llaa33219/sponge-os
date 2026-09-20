# Phase 16 W7 — Keyboard shortcut framework: live-capture evidence and platform findings

> Status: W7 complete (2026-09-19). Both scenarios PASS on base-sel4.
> This note records the two Genode platform findings that shaped the
> final mechanism; they constrain any future runtime-rebinding work.

## Final verification (base-sel4, QEMU q35/Skylake-Client, QMP)

`run/sponge-de-shortcuts.run` — `shortcuts-probe: PASS`:

```
QMP-TARGET key super
qmp: dispatching key 'super'
[init -> sponge-de] sponge-de: shortcut hit: launcher (serial=1)
[init -> shortcuts_probe] shortcuts-probe: shortcut_hit action=launcher hit=yes observed (W7 live keypress path)
[init -> shortcuts_probe] shortcuts-probe: launcher popup open observed via launcher_state
[init -> shortcuts_probe] shortcuts-probe: shortcut_hit action=dismiss hit=yes observed (W7 live keypress path)
[init -> shortcuts_probe] shortcuts-probe: launcher popup close observed via launcher_state
[init -> shortcuts_probe] shortcuts-probe: shortcut_hit action=focus_next hit=yes observed (W7 live keypress path)
[init -> shortcuts_probe] shortcuts-probe: PASS
```

`run/sponge-de-shortcuts-extend.run` — `shortcuts-extend-probe: PASS`
(override binding `launcher <- Ctrl-Alt-Tab` written live through
configd's `de_config_request` channel and captured live).

Regression sweep (all PASS): `sponge-de-sel4-interactive`,
`sponge-panel-config-sel4`, `sponge-panel-menu`, `sponge-de-bgmenu`,
plus base-linux `sponge-de-settings`.

## Chain proven live

QMP `send-key` (qcode-object form) → emulated PS/2 keyboard → ps2
driver → event_filter `<report>` source → `<shortcut name="X">` match
→ per-action Report (`drivers -> event_filter -> <action>`) →
report_rom relay → sponge-de `ShortcutController` per-action ROM
(`shortcut_launcher` etc.) → GUI-thread dispatch
(`QMetaObject::invokeMethod`, `Qt::QueuedConnection`) →
`LauncherController::toggle_launcher()` /
`TasklistController::cycle_focus()` / `Dismisser::dismiss_topmost()`
→ consolidated `shortcut_hit` report (`<shortcut_hit action="X"
hit="yes"/>`) + `launcher_state` report (`open=yes|no`) consumed by
the probe.

## Finding 1 — event_filter `<report>` source does not compose

`genode/repos/os/src/server/event_filter/report_source.h` (added
upstream 2025-12-12) fires its `<shortcut>` reports only when the
report source is the DIRECT child of `<output>` wrapping a plain
`<input>` chain. Every compositional variant silently breaks the
publish path (events still reach the filter — verified with a
`<log>` source — but no report is generated):

| Topology | Events reach report | Shortcut publishes |
|---|---|---|
| `output → report → [log →] input` | yes | **yes** |
| `output → merge → [chargen, report]` | no | no |
| `output → merge → [report, chargen]` | no | no |
| `output → merge → [report]` (no chargen) | yes | **no** |
| `output → report → chargen` | (chargen eats LEFTMETA) | no |
| `output → report → merge → [input, chargen]` | yes | no |
| `output → chargen → merge → […, report]` | no | no |

Consequence for Sponge OS: the dedicated shortcut scenarios drop the
chargen chain from THEIR event_filter config (they never type text —
keyboard-to-app coverage stays with the terminal/textedit scenarios).
**Shortcut + chargen coexistence on the product media is a Phase 17+
item** and needs either an upstream report-source composition fix or
a ledgered vendored patch (the Phase 16 vendored budget, one row #17,
is allocated to the themed_decorator resize + minimizer work in W9).

## Finding 2 — report_rom ROM sessions do not track late-created reports

A ROM session opened through a report_rom policy BEFORE the backing
report's first write never receives the content: the reader registers
on an empty module, and report_rom's version/notify path
(`rom_service.h` `mark_as_outdated` / `notify_client`) does not bring
the late-arriving content to the already-opened ROM session. Updates
to a report that existed when the ROM session opened DO arrive.

Consequences baked into the code:

- The shortcuts scenarios bake the captured bindings into the static
  `event_filter.config` boot module; sponge-de's `ShortcutController`
  still emits the dynamic `event_filter_config` report (the probe
  verifies its content), but event_filter does not reconfigure from
  it. **Runtime rebinding of the captured keys is Phase 17+.** The
  configd framework side (validation, persistence, Settings UI,
  dispatch) is fully dynamic today.
- `LauncherController`'s `launcher_state` reporter publishes its
  initial `open="no"` eagerly at `attach_view()` so the report exists
  before any client opens the ROM (comment at
  `launcher_controller.h`).

## Component fixes landed during W7 live-capture bring-up

- `shortcut_controller.cc`: subscriber walk now checks the report's
  ROOT node (event_filter's report body root IS the `<shortcut>`
  element); fixed a dangling `QByteArray::constData()` pointer that
  corrupted the action dispatch table lookup; 250 ms poll of the
  per-action ROMs (the sigh path alone is unreliable for these
  policy-relayed ROMs on seL4).
- `launcher_controller.h`: `toggle_launcher()` moved to
  `public slots:` (QMetaObject::invokeMethod requires it).
- `dismisser`: launcher-popup close routed through
  `LauncherController::close_popup()` so `launcher_state` stays in
  sync.
- `shortcuts_probe`: launcher popup side effect observed via the
  `launcher_state` ROM (position-independent; the popup anchors above
  a bottom panel, defeating any fixed Capture rect).
