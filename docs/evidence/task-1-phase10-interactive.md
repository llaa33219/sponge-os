# Phase 10 / W1 — Real QMP-driven input proof (criterion 1)

## Header

- **Date:** 2026-08-04
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W1)
- **Scenario:** `run/sponge-de-sel4-interactive.run` (extended in place)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **Command:**
  ```bash
  cd /home/luke/sponge-os
  timeout 1500 ./tool/build run sponge-de-sel4-interactive \
      > docs/evidence/task-1-phase10-interactive.raw.log 2>&1
  ```
  (`make` exit code 0; scenario prints `Run script execution successful.`)
- **Raw captured log:** `docs/evidence/task-1-phase10-interactive.raw.log`

---

## Verdict — GREEN

Criterion 1 (real-input end-to-end proof) is satisfied: the
sponge-de-sel4-interactive scenario now boots seL4 + the full
interactive-PC driver set, and sponge_de_probe's gate-3 PASS is caused
solely by a host-side QMP `input-send-event` usb-tablet click
dispatched at the demo window — the synthetic Event-session injection
path is bypassed via the probe's `inject=no` observe mode. The full
hardware input chain
QMP → usb-tablet → pc_usb_host → usb_hid → event_filter → nitpicker
→ sponge-de is exercised end-to-end.

| Gate | Marker | Verdict |
|------|--------|---------|
| 1 | `[init -> drivers -> fb] using 1024x768 (1024x768)` | **PASS** |
| 2 | `[init -> drivers -> usb_hid] Connected device: input0 (QEMU QEMU USB Tablet at usb-usbbus-0/input0) POINTER` | **PASS** |
| 3 | `[init -> sponge_de_probe] sponge-de-probe: PASS` (cause: QMP click → sponge-de input report) | **PASS** |

Final marker: `Run script execution successful.`

---

## Step 0 — Config-delivery quirk diagnosis (TDD red → green)

### The W0 finding (recap)

Gate 3 in W0 PASSES via the probe's synthetic Event-session injection
because the run script's `<config | inject: no>` shorthand never
reaches the probe's config ROM — the probe ran with `inject=true` (its
default) and logged `injecting click at (512,460)`. W0's most-likely
diagnosis was "the HID shorthand doesn't round-trip the `inject="no"`
attribute".

### The real root cause (verified empirically in W1)

The shorthand DOES round-trip — verified by adding a temporary debug
print to the probe and re-running:

```
sponge-de-probe: DEBUG config.valid=1 size=4096
sponge-de-probe: DEBUG config first 200 bytes:
'config | inject: no | render_iters: 1800 | generate_xml: yes
```

The config ROM IS delivered with the right attributes — **but in HID
format, not XML**. Genode 26.05's sandbox
(`Inline_config_rom_service::produce_content` in
`genode/repos/os/src/lib/sandbox/child.h`) emits child configs via
`Generator::generate()`, which dispatches on the dynamic linker's
`_generate_xml()` flag (`genode/repos/base/src/lib/ldso/main.cc:795`).
The flag defaults to `false`, so the sandbox delivers HID by default.

The probe reads its config via `_config.xml().attribute_value("inject",
true)`. The `Xml_node` constructor throws `Invalid_syntax` on HID
input, the catch returns `<empty/>`, and `attribute_value("inject",
true)` returns the default `true`. Observe mode never engages.

### The W1 fix (probe-side, run-script-side still satisfies the plan)

Two layered fixes were attempted:

1. **Run-script-side attempt that DID NOT work:** add
   `generate_xml="yes"` to the probe's inline `<config>`. The linker
   reads this attribute from the binary's config at startup and sets
   the flag, BUT the sandbox's Inline_config_rom_service has already
   produced the first content delivery (in HID) by the time the flag
   is set, and the probe's subsequent reads return cached HID content.
   Empirically verified — the debug print still showed HID format with
   this attribute set. Reverted.

2. **Probe-side fix that DID work:** migrate `_config.xml()` →
   `_config.node()`. The `Node` API (`genode/repos/base/include/base/node.h`)
   auto-detects HID vs XML at parse time and works with both. This is
   the modern Genode API (since the framework default switched to HID
   delivery) and is purely a robustness improvement — for an invalid
   or absent config it short-circuits to the same defaults as the old
   `.xml()` call.

With the migration, observe mode engages correctly: the probe logs
`observe mode (inject=no) -- awaiting external click via the real
input driver path (usb-tablet/ps2 -> event_filter)`, and after ~90s
of no input arrives, logs `FAIL external click did not reach sponge-de
(usb-tablet injection missing or input driver path not wired)`. This
is the TRUE RED baseline the plan predicted. The inject=yes default
behavior used by `run/sponge-de-test.run` is byte-identical (no config
delivered → `_config.valid()` is false → short-circuit on the first
clause of both `(!_config.valid()) ||` and `(!_config.valid()) ?`,
never reaching the API migration sites).

### Why the plan's run-script-side suggestion wasn't taken

The plan said "fix it run-script-side (e.g. element form or correct
attribute syntax — whatever makes the generated XML contain
`inject="no"`)". The element form `<inject>no</inject>` would NOT have
fixed the underlying issue — the problem isn't the shorthand vs
element syntax, it's the HID-vs-XML delivery format. The `generate_xml`
attribute (the documented switch for the format) doesn't actually work
for this configuration of sandbox+linker either. The cleanest correct
fix is to use the format-agnostic `Node` API in the probe — a 2-line
change to two attribute reads. The run-script-side config now carries
a long comment explaining the format quirk and pointing at the
probe-side fix.

---

## Step 1-3 — What was built

### `run/qmp.inc` — shared Tcl QMP helper

New file, sourced by `run/sponge-de-sel4-interactive.run` (and future
W2-W5 scenarios) via:
```tcl
source [file join [file dirname [file normalize [info script]]] qmp.inc]
```
A symlink `repos/sponge/run/qmp.inc → ../../../run/qmp.inc` mirrors the
existing pattern for `.run` files so `[info script]` resolves through
the symlink chain used by the Genode build tool. The helper provides:

- `qmp_pick_port` — kernel-assigned ephemeral TCP port via
  `socket -server ... 0` (avoids TIME_WAIT collisions between
  sequential QEMU respawns within one scenario).
- `qmp_connect port` — bounded TCP connect retry, QMP greeting read,
  `qmp_capabilities` handshake.
- `qmp_cmd chan json` — send one JSON command, skip async events, die
  loud on `{"error":...}`.
- `qmp_abs x y` — coordinate scaling (see calibration section below).
- `qmp_pointer_move`, `qmp_button`, `qmp_click`, `qmp_drag`,
  `qmp_send_key`, `qmp_type` (char→QEMU-keyname map).
- `qmp_exec_target chan timeout_s` — bounded `expect` on global
  `qemu_spawn_id` for `QMP-TARGET (click|drag|type) ...` markers
  emitted by probes, dispatches to the matching proc, FAIL+exit on
  timeout.
- `qmp_disconnect chan`.

Tcl 8.x compatible, no external dependencies (only `socket` and
`expect`, both already used by the run tool).

### `repos/sponge/src/test/sponge_de_probe/main.cc` — observe-mode marker

In observe mode (`inject=no`), when the probe finishes its render poll
and switches to awaiting an external click, it now logs:

```
QMP-TARGET click 512 412
```

with the demo-window center in global screen coords
(`DEMO_X + DEMO_W/2, DEMO_Y + DEMO_H/2` = `192 + 320, 172 + 240` =
`512, 412`). The PASS path now also logs the observed press coordinates
(`press=<x>,<y>`) read back from sponge-de's input report, for
calibration. The inject=yes branch (used by sponge-de-test.run) is
byte-identical.

The probe's config reads were also migrated from the format-strict
`.xml()` accessor to the format-agnostic `.node()` accessor (see the
Step 0 diagnosis above for why this is necessary).

### `run/sponge-de-sel4-interactive.run` — QMP wiring + comment fixes

- Sources `run/qmp.inc`.
- Appends `-qmp tcp:127.0.0.1:${qmp_port},server=on,wait=off` to
  qemu_args ONLY before gate 3 (gates 1 and 2 spawn a plain QEMU and
  don't need it). `qmp_port` is a fresh kernel-assigned port per
  scenario run via `qmp_pick_port`.
- Gate 3 choreography (single QEMU spawn, single boot):
  1. `run_genode_until {.*observe mode.*} 300` — gate-3 QEMU spawned,
     probe finishes render poll and enters observe mode.
  2. `qmp_connect $qmp_port` — Tcl socket to QEMU's QMP server.
  3. `qmp_exec_target $qmp_chan 60` — bounded expect on `qemu_spawn_id`
     for the `QMP-TARGET click 512 412` marker, dispatches
     `input-send-event` abs-axis + button events through QMP.
  4. `run_genode_until {.*sponge-de-probe: PASS.*} 120 $qemu_spawn_id`
     — bounded wait on the existing spawn for the PASS marker.
- Header/footer comments rewritten to describe the now-implemented
  real-QMP-click mechanism (previously described as a "Remaining
  follow-up"). The stale "(inject=no) ... injected click -> PASS"
  phrasing in the start-node summary is corrected to "... real
  QMP-driven usb-tablet click dispatched from the host".

---

## Step 4 — Calibration

### Calibration matrix

| Formula divisor pair | Sent abs (x,y) for guest (512,412) | Observed widget-relative press | Observed screen press (widget + (192,172)) | Screen delta vs (512,412) |
|---|---|---|---|---|
| `/1024, /768` (framebuffer size) | (16384, 17578) | (319, 211) | (511, 383) | (-1, -29) |
| `/1023, /767` (framebuffer size - 1) | (16400, 17601) | (319, 211) | (511, 383) | (-1, -29) |
| `/1023, /714` (empirical) | (16400, 18908) | (319, 211) | (511, 383) | (-1, -29) |

**Key finding:** ALL formula variants produce the same observed screen
position. The abs-axis value sent does not change where the click
lands. The 29 px y drift is downstream of our abs value — it lives in
QEMU's usb-tablet→screen translation under `-nographic` mode.

### Root cause of the 29 px y drift

With `-nographic`, QEMU disables the graphical display. The usb-tablet
device is created but is **not bound to a QemuConsole** — verified by
attempting to send events with an explicit `device:"tablet"` argument,
which produces the error `Device tablet (head 0) is not bound to a
QemuConsole`. Without an explicit device argument, QEMU routes
input-send-event through a fallback path that still delivers the event
to the guest (usb_hid receives and processes it) but the abs-axis
translation uses a different calibration than the graphical-console
path, producing the consistent 29 px y offset.

The drift is **consistent** across runs and **does not affect the
criterion-1 proof**:
- The click reliably lands inside the demo domain (640x480 starting at
  (192,172)) — observed widget y=211 out of 0..480 is near vertical
  center.
- The click triggers sponge-de's `mousePressEvent`, which reports the
  press through the `input` report.
- The probe confirms the press and logs PASS.

### Final formula

`/1024, /768` (framebuffer-size divisors) with round-to-nearest:

```tcl
ax = (x * 32767 + 512) / 1024
ay = (y * 32767 + 384) / 768
```

This is the principled formula; calibration tweaks to the divisors
have no effect given the downstream offset. Perfect calibration is
Phase 11+ scope (likely requires either a real graphical display or a
vendored QEMU patch to bind the usb-tablet to a virtual console under
`-nographic`).

---

## Step 5 — Regression + commits

### `run/sponge-de-test.run` regression (code inspection)

`sponge-de-test.run` could not be re-run cleanly in this environment
due to a pre-existing build-environment issue unrelated to this
workstream: the run script copies Qt6 shared libraries into
`[run_dir]/genode/` AFTER `build_boot_image`, but on base-sel4 the
boot_dir/sel4 module calls `remove_genode_dir`
(`genode/tool/run/boot_dir/sel4:59`) post-build, so the target
directory doesn't exist. This is a `sponge-de-test.run` ↔ base-sel4
interoperability issue independent of any Phase 10 code.

Regression verified by code inspection instead: the probe's
`inject=yes` default path is byte-identical to the pre-W1 code. The
`xml()` → `node()` migration sites are both guarded by
`(!_config.valid()) ||` / `(!_config.valid()) ?` which short-circuit
BEFORE the API call when no config ROM is delivered — exactly the
sponge-de-test.run case (no `<config>` on the probe's start node).
Zero behavioral impact.

### Commits landed

Per the plan's Commit Strategy items 1-3 (conventional commits, repo
style):

1. `test(sponge_de_probe): emit QMP-TARGET click marker in observe mode`
   - Adds the QMP-TARGET marker emission in observe mode.
   - Adds observed-press coordinate logging in the PASS path.
   - Migrates `_config.xml()` → `_config.node()` (HID-format config
     delivery compatibility; required for `inject=no` to take effect).

2. `feat(run): add run/qmp.inc — QMP helper for host-driven guest input`
   - The shared Tcl helper.
   - The `repos/sponge/run/qmp.inc` symlink.

3. `feat(run): drive sponge-de-sel4-interactive input via QMP usb-tablet`
   - QMP wiring in the run script (qmp_port, qemu_args, gate-3
     choreography).
   - Header/footer comment fixes (no more "synthetic injected click"
     / "Remaining follow-up" claims).
   - Closes the §11.1 follow-up.

### Evidence artifacts

- `docs/evidence/task-1-phase10-interactive.log` (this file)
- `docs/evidence/task-1-phase10-interactive.raw.log` (full boot
  transcript of the green run, ~6.2k lines)

---

## Reproducibility

- The scenario is deterministic on this base-sel4 / QEMU + Mesa
  softpipe stack. Three consecutive runs (the first GREEN run, two
  calibration runs, and the final GREEN run with the principled
  formula) all produced the same `sponge-de-probe: PASS` caused by the
  QMP click landing at observed widget (319, 211).
- The `error: can't find command 'loadfont'.` line in the boot log is
  a pre-existing GRUB bootloader warning (present in W0 too); it does
  not affect the scenario outcome.
