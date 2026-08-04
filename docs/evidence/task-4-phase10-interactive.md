# Phase 10 / W4 — QMP `send-key` to a focused terminal (criterion 5a)

## Header

- **Date:** 2026-08-05
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W4)
- **Scenario:** `run/sponge-terminal-qmp.run` (new)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **Command:**
  ```bash
  cd /home/luke/sponge-os
  flock /tmp/sponge-os-qemu.lock -c \
    'timeout 1800 make -C genode/build/x86_64 run/sponge-terminal-qmp \
       KERNEL=sel4 BOARD=pc' \
    > docs/evidence/task-4-phase10-interactive.log 2>&1
  ```
  (make exit code 0; scenario prints `Run script execution successful.`)
- **Raw captured log:** `docs/evidence/task-4-phase10-interactive.log`
  (the green run, single boot)
- **Host QEMU:** system-x86_64 **11.0.2** (not 7.2 as W1's evidence
  assumed — several input-routing behaviors differ; see below)

---

## Verdict — GREEN (2 consecutive runs)

Criterion 5a is satisfied: the scenario prints `terminal-probe: PASS`
with the bash echo round-trip caused solely by host-driven QMP
`send-key` events through the real chain

```
QMP send-key -> emulated PS/2 keyboard -> ps2 driver
  -> event_filter (en_us.chargen) -> nitpicker
  -> focused terminal Gui session (pkg_runtime -> terminal sub-init)
  -> gems terminal read buffer -> /dev/terminal (vfs <terminal/>)
  -> noux bash echo -> terminal re-render -> probe glyph check
```

| Gate | Marker | Verdict |
|------|--------|---------|
| DISPLAY | `[init -> drivers -> fb] using 1024x768 (1024x768)` | **PASS** |
| INPUT | `[init -> drivers -> usb_hid] Connected device: ... QEMU QEMU USB Tablet ... POINTER` | **PASS** |
| RENDER | `terminal-probe: [4] terminal window detected (98 glyph pixels)` | **PASS** |
| FOCUS CLICK | `QMP-TARGET click 528 396` -> host: `mouse_set 3` + abs move + HMP `mouse_button 1/0` | **PASS** |
| TYPE | `QMP-TARGET type echo ok` -> host: 7x `send-key` (qcode objects) + `send-key ret` | **PASS** |
| ECHO | `terminal-probe: keystroke echo confirmed (glyphs 98 -> 155)` | **PASS** |

Final marker: `terminal-probe: PASS` + `Run script execution successful.`

**Regression:** `run/sponge-terminal.run` (synthetic, unchanged probe
default) re-run after all probe changes: `terminal-probe: PASS`,
`Run script execution successful.` The probe's default (no `qmp`
attribute) path is byte-identical: the original scan band, the original
synthetic focus click + `Press_char` injection, no focus/hover ROM
sessions (lazily constructed in qmp mode only).

---

## Focus-click coordinates chosen

`(528, 396)` — the terminal-window center, and the y-drift story is
more subtle than W1's flat ~29px:

1. **The window is NOT at the domain origin here.** With vesa_fb
   present, the terminal's Gui session sees TWO nitpicker capture
   clients (probe + fb). Genode's client-side `Gui::window()` returns
   the single capture client's bounding box when `count == 1`
   (the synthetic scenario: `(0,0,1024,768)`) but the info ROM's
   top-level rect — the domain rect `(64,48,800,600)` — when
   `count != 1`. The gems terminal places its view at that rect in
   domain-relative coordinates, so the window lands at
   **(128, 96, 800, 600)** (a double domain offset). Root-caused in
   `genode/repos/os/include/gui_session/connection.h` (`window()`) and
   confirmed by a QMP `screendump`: the black terminal rect measures
   exactly 800x600 at (128,96). (528,396) is its center and also lies
   inside the pre-wipe `(64,48,800,600)` position, so the target is
   correct whether or not the fb capture client has registered when
   the probe emits the marker.
2. **QEMU 11's `-nographic` pointer path is broken differently than
   W1's QEMU 7.2** (details below), so the pointer is positioned with
   a tablet abs event scaled back to pixels by a staged event_filter
   `<transform>` — the residual translation error of W1's calibration
   does not apply here; the transform is exact (verified:
   `ABS_MOTION +528+396` in the event_filter log).

---

## Root causes found (the long version)

W4 needed eight distinct fixes beyond the plan's choreography. All are
in the scenario/probe; no vendored-tree changes.

### 1. Outer `parent-provides` lacked `service RM`
The drivers sub-init (copied into a sponge-terminal-based config whose
outer list came from `run/sponge-terminal.run`) died at
`acpi: parent denied RM-session`. Added `+ service RM` to the outer
`parent-provides` (as in the interactive scenario).

### 2. Probe/driver bring-up race -> rendezvous instead of sequential gates
The probe's Capture-based render check needs no drivers, so it emitted
both QMP-TARGET markers while acpi/xHCI/usb_hid were still initializing
(observed serial order: render -> click marker -> fb -> type marker ->
POINTER). The plan's sequential gates (fb -> POINTER -> render ->
`qmp_exec_target` x2) deadlock in that order: earlier expects consume
the markers, and dispatch must wait for the usb-tablet bind anyway. The
scenario performs ONE bounded (360s) rendezvous expect collecting all
four markers (fb, POINTER, click, type) via a single **alternation**
pattern, then connects QMP and dispatches. Two expect pitfalls were
hit and fixed: (a) separately-listed patterns match out of buffer
order and silently eat earlier markers (fb pattern consumed the click
line); an alternation always matches the earliest marker in the
buffer; (b) QEMU's `-nographic` serial emits **CR CR LF** line endings
(verified by `od -c`), so the EOL anchor must be `\r*\n`, not `\r?\n`
(the click capture never matched with `\r?`).

### 3. `send-key` JSON schema (latent W1 bug in run/qmp.inc)
QEMU rejects qmp.inc's `{"keys":["e"]}` string form:
`Invalid parameter type for 'keys[0]', expected: object`. The scenario
uses local `term_qmp_*` helpers emitting
`{"keys":[{"type":"qcode","data":"e"}]}`. **Flagged for W6:**
`qmp.inc::qmp_send_key/qmp_type` have this bug; W5's textedit scenario
will hit it.

### 4. QEMU 11 tablet reports device units; nitpicker reads raw pixels
`usb_hid` forwards the tablet's raw absolute values (0..32767/axis)
and nitpicker's `user_state` treats `Absolute_motion` as PIXELS, so
every abs move clamped to the (1023,767) corner. Fixed by staging a
generated `event_filter.config` with a `<transform><scale
x="0.0312517" y="0.0234382"/>` (1024/32767, 768/32767) on the usb
input. Verified in the event_filter log: `ABS_MOTION +528+396` for abs
(16896,16895).

### 5. QEMU 11 broadcasts untargeted input events; PS/2 mouse wins
Untargeted abs events reach BOTH mice; QEMU's PS/2 mouse translates
abs to accelerated rel deltas that slam the pointer into a corner.
`query-mice` shows the PS/2 mouse is `"current":true` by default.
Device-targeting the tablet is impossible (no QOM id; `device:`
errors `DeviceNotFound`, and with `id=tablet`, "not bound to a
QemuConsole" under `-nographic`; `-vnc` crashes with the run tool's
fixed `-serial mon:stdio`). Fix: HMP `mouse_set <tablet-index>` makes
the tablet current so abs events take the tablet path.

### 6. input-send-event BUTTON events reach no guest device at all
With event_filter's `log` filter staged as a diagnostic, untargeted
btn events (and `send-key btn-left` — not a qcode on QEMU 11)
produced ZERO guest-side events. HMP `mouse_button 1/0` delivers
BTN_LEFT through the current (tablet) device — verified by
`usblog ... PRESS BTN_LEFT` and by nitpicker's focus ROM flipping to
`focus | label: pkg_runtime -> terminal -> terminal -> | domain: term
| active: yes`.

### 7. The terminal's first render is unreliable with vesa_fb present
(a) When fb's capture session registers, the terminal's `window()`
changes (see coordinates section) and the gems terminal reallocates
and CLEARS its text surface; dumb bash never repaints (documented
upstream: "clients are expected to respond to a terminal-size change
with a redraw"). (b) In some boots the prompt renders only at the
repositioned window, below the probe's original scan band. Probe-side
fixes (qmp mode only; defaults untouched): a wider scan region
covering both window positions, a longer prompt wait (2400 vs 700
polls; TCG driver bring-up competes with the noux first boot), a
one-shot recovery "poke" (synthetic focus click + Return at poll 300
to force bash to print a fresh prompt — SETUP ONLY, pre-marker, so the
PASS-gating glyph increase remains caused solely by QMP events), and
re-baselining when the wipe lands after the baseline sample.

### 8. Serial backpressure freezes the guest during host-side sleeps
During `after`-based waits the expect side stops draining the spawn;
QEMU's stdio buffer fills and guest serial writes block, freezing any
component that logs (the probe went silent mid-poll). The scenario
never uses bare `after` for long waits (the diag variant's `diag_wait`
drains via a pattern-less bounded expect). Noted for W6's qmp.inc
usage guidance.

---

## Evidence details

- Focus-click target `(528,396)`; pre-click pointer `(511,383)`
  (hover ROM: `hover | label: pkg_runtime -> terminal -> terminal ->
  | domain: term | xpos: 511 | ypos: 383` — already resting over the
  terminal window).
- Focus switch proven in the diagnostic run (log filter +
  focus/hover ROMs): after the HMP click,
  `focus | label: pkg_runtime -> terminal -> terminal -> | domain:
  term | active: yes`.
- Echo: glyphs 98 -> 155 within a few typed characters; bash echoes
  each character as it arrives (PS1 prompt is 98 glyph pixels at the
  repositioned window).
- The typed string is within qmp_type's mapped subset (lowercase +
  space); Return is sent explicitly as `send-key ret` so bash executes
  `echo ok`.
- All waits bounded: boot gate 120s, rendezvous 360s (fail-loud with a
  marker-presence dump), PASS gate 120s.
- The scratch diagnostic scenario (`run/sponge-terminal-qmp-diag.run`,
  event_filter log filters + QMP matrix) was deleted after the green
  run; its insights are captured here.
- Two consecutive green runs of `run/sponge-terminal-qmp.run` plus a
  green `run/sponge-terminal.run` regression.

## Follow-ups for W6 (not in W4 scope)

1. Fix `run/qmp.inc` `qmp_send_key`/`qmp_type` to the qcode-object
   form (W5 will hit the same QEMU error).
2. `qmp.inc`'s untargeted abs pointer model is invalid on QEMU 11
   (`-nographic` broadcast + PS/2 translation + device-unit/pixel
   mismatch); the working recipe (mouse_set + transform-scaled tablet
   abs + HMP `mouse_button`) should be centralized. W3 is concurrently
   reworking qmp.inc's pointer delivery (PS/2 relative); reconcile.
3. Document the CR CR LF serial line-ending (`\r*\n` anchors) and the
   serial-backpressure rule (no bare `after` while the guest logs).
