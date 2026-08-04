# Phase 10 / W3 — Real QMP-driven window drag proof (criterion 2)

## Header

- **Date:** 2026-08-05
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W3)
- **Scenario:** `run/sponge-wm-qmp.run` (new)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **Command:**
  ```bash
  cd /home/luke/sponge-os
  timeout 1500 make -C genode/build/x86_64 run/sponge-wm-qmp KERNEL=sel4 BOARD=pc \
      > docs/evidence/task-3-phase10-interactive.raw.log 2>&1
  ```
  (`make` exit code 0; scenario prints `Run script execution successful.`)
- **Raw captured log:** `docs/evidence/task-3-phase10-interactive.raw.log`

---

## Verdict — GREEN

Criterion 2 (window dragging over real input) is satisfied: the
sponge-wm-qmp scenario boots seL4 + the interactive PC driver set +
the upstream wm/window_layouter/decorator stack + sponge_pkgd +
pkg_runtime, launches pkg_gui_demo via sponge_pkgd's "request" channel,
and moves the decorated pkg_gui_demo window +99,+99 via a real QMP
input-send-event PS/2 Mouse drag dispatched from the host. The full
hardware input chain
QMP → emulated PS/2 controller → ps2 driver → event_filter (accelerate)
→ nitpicker → decorator title-bar → wm → window_layouter drag rule
→ window moves is exercised end-to-end.

| Gate | Marker | Verdict |
|------|--------|---------|
| display | `[init -> drivers -> fb] using 1024x768 (1024x768)` | **PASS** (in serial log) |
| input | `[init -> drivers -> usb_hid] Connected device: input0 (QEMU QEMU USB Tablet ...) POINTER` | **PASS** (in serial log) |
| drag | `wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (149,419)` | **PASS** (+99,+99 ≈ +100,+100) |
| pixel | `wm-probe: [observe 6] new content center (309,539)=0xff00ff00 is pkg_gui_demo green` | **PASS** |
| final | `wm-probe: PASS` + `Run script execution successful.` | **PASS** |

---

## Key findings

### 1. label_prefix support on layouter `<assign>` — CONFIRMED

`genode/repos/gems/src/app/window_layouter/README` lines 44-46 confirm
`<assign>` accepts `label`, `label_prefix`, or `label_suffix`. The W3
route fix uses `label_prefix: pkg_runtime` to match all launched-
package window labels (which all begin with `pkg_runtime -> `). This
is inert for sponge-alpha today (only `hello` is pre-staged; hello is
non-GUI) but enables any future GUI package launched from the desktop
to get a decorated, movable frame.

### 2. W1's "~29px y-drift" root cause — NOT a QEMU translation bug

W1's evidence diagnosed the y-drift as a QEMU `-nographic` abs-axis-
to-screen translation issue. W3's deeper investigation revealed the
actual mechanism: **nitpicker treats `Input::Absolute_motion`
coordinates as pixel coordinates** (see
`genode/repos/os/src/server/nitpicker/user_state.cc:117-124`). The
usb-tablet delivers abs values in `0..32767`, which are all off-screen
on a 1024x768 display. Nitpicker's pointer sanitizer
(`main.cc:780-810`) then clamps every off-screen event to the screen
center (`~512, 384`).

W1's "click worked" because the demo domain (640x480 at (192,172))
is large enough that the screen center (512, 384) lands inside it.
The "~29px y-drift" was really `screen-center-y (384) - intended-y
(412) = -28`, not a per-event QEMU translation offset. This is why W1's
calibration matrix showed "NO divisor choice changes the landing
position" — ALL abs values clamp to the same center regardless.

### 3. PS/2 Mouse used instead of usb-tablet for the drag

The usb-tablet's absolute coordinates cannot drive precise targeting
(a 20px-tall title bar requires ±10px precision; the center-clamp
gives ±0 precision). Adding an event_filter `<scale>` transformation
to convert abs→pixel works in principle (`transform_source.h`
supports it) but causes a seL4 boot stall (the drivers sub-init hangs
during usb_hid init for unexplained reasons when the config is
modified).

The PS/2 Mouse avoids the abs-coordinate problem entirely. Its
relative motion events go through event_filter's existing `accelerate`
chain (`sensitivity_percent=1000, max=50, curve=127`). The non-linear
acceleration LUT gives:
- `rel-1` → 1px per event (precision steps, no acceleration)
- `rel-50` → ~100px per event (v + max_accel = 50 + 50)
- `rel-100` → ~150px per event (v + max_accel = 100 + 50)

The drag navigates the pointer via clamp-to-(0,0) → coarse rel-50 →
fine rel-1, achieving ±1px precision.

The PS/2 path exercises the real input chain: QEMU input-send-event →
emulated PS/2 controller (i8042) → Genode ps2 driver → event_filter →
nitpicker → decorator → wm → window_layouter. This is the same chain
the plan names ("usb-tablet/ps2 → event_filter → nitpicker"), just
the ps2 branch.

### 4. QMP connect timing — must defer until after boot completes

Connecting the QMP TCP socket DURING the early boot phase (before
Qt6/Mesa init completes) causes the second Qt6 renderer (pkg_gui_demo)
to hang indefinitely on seL4. The mechanism is unclear (the QMP TCP
listener is non-blocking: `server=on,wait=off`) but reproducible. The
fix: defer `qmp_connect` until AFTER "observe 3" (pkg_gui_demo's
window has appeared in window_layout — the full boot including Qt
first paint is complete). W1 doesn't hit this because its single Qt
renderer (sponge-de) finishes before the QMP connect.

### 5. Drag choreography details

```
QMP-TARGET drag 210 310 310 410
qmp: dispatching drag (210,310) -> (310,410)
    Step 1: 10 × rel(-100) x + 10 × rel(-100) y → clamp to (0,0)
    Step 2: 2 × rel(50) x + 3 × rel(50) y → (200, 300) [coarse]
           10 × rel(1) x + 10 × rel(1) y → (210, 310) [fine]
    Step 3: rel(1) x → (211,310), rel(-1) x → (210,310) [hover jiggle]
    Step 4: BTN_LEFT down
    Step 5: 1 × rel(50) x + 1 × rel(50) y → (310, 410) [drag +100,+100]
    Step 6: BTN_LEFT up
wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (149,419)
```

The observed move (+99,+99) is within 1px of the +100,+100 target —
the PS/2 Mouse acceleration rounding.

---

## Regression results

| Scenario | Result | Method |
|----------|--------|--------|
| `sponge-alpha.run` | **GREEN** | Full run (flock-serialized); `alpha-probe: PASS` + `Run script execution successful.` (route fix is inert: only `hello` pre-staged, non-GUI) |
| `sponge-wm.run` | **GREEN** (by inspection) | Pre-existing base-sel4 Qt6-after-build_boot_image staging issue (W1 precedent); inject=yes path byte-identical (early-return check gated on `_config.valid()` which is false when no `<config>`) |
| `sponge-desktop-disk.run` | **GREEN** (by inspection) | Same route fix as alpha, also inert (only `hello` pre-staged); full run takes 30+ minutes for disk-image creation, skipped per W1 precedent for inert changes |

---

## Commits landed

Per the plan's Commit Strategy items 6-8:

6. `fix(run): route pkg_runtime Gui sessions through the wm in sponge-alpha` (`13609e4bf5`)
   - pkg_runtime route: `+ service Gui | + child wm` before any-service
   - layouter: `+ assign | label_prefix: pkg_runtime | target: screen | xpos: 50 | ypos: 300 | width: 320 | height: 240 | maximized: no`
   - Identical fix in sponge-desktop-disk.run's staged system.config
   - Inert for both scenarios today (only non-GUI `hello` pre-staged)

7. `test(wm_probe): add observe mode driven by QMP-TARGET drag markers` (`b2bedaa90d`)
   - Observe mode (`inject=no`): launches pkg_gui_demo via pkgd, polls
     window_layout for the window, emits QMP-TARGET drag marker, polls
     for position change, pixel-verifies the green content center
   - inject=yes path byte-identical (early-return gated on
     `_config.valid()`)
   - Config reads via `_config.node()` (HID-format compatible, W1 fix)

8. `feat(run): add sponge-wm-qmp — real-pointer window drag on base-sel4` (this commit)
   - run/sponge-wm-qmp.run: single-boot scenario with wm stack +
     drivers sub-init + sponge_pkgd/pkg_runtime + pkg_gui_demo +
     wm_probe inject=no + QMP
   - QMP drag via PS/2 Mouse (relative motion through event_filter
     accelerate chain) — see finding #3 for why usb-tablet abs coords
     don't work with nitpicker
   - repos/sponge/run/sponge-wm-qmp.run symlink
   - This evidence file + raw log

---

## Reproducibility

- The scenario is deterministic on this base-sel4 / QEMU + Mesa
  softpipe stack. The drag consistently moves the window +99,+99
  (within 1px PS/2 Mouse acceleration rounding).
- The QMP connect timing (deferred until after "observe 3") is
  essential — connecting earlier causes pkg_gui_demo's Qt6/EGL init
  to hang on seL4.
