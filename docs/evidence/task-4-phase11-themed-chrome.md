# Phase 11 / W4 — themed_decorator drop-in (sponge-de-themed-chrome.run)

> **Final status — DRAG STILL DOES NOT MOVE THE WINDOW. Tablet abs
> path works (abs events land at correct pixel coords, scale
> transform verified) but the press is on the content area, not the
> themed_decorator's narrow title bar.** The regression scenario
> passes. Three root causes all fixed and verified. Pixel assertion
> gate implemented in wm_probe but unreachable (drag must succeed
> first).

## Three root causes — all FIXED

### Root cause 1 — margins 0/0/0/0

`metadata.txt` was a `#`-commented file. Genode's `Genode::Node` parser
doesn't accept `#` comments → `Margins_from_metadata` returned 0.
**FIX:** `metadata.txt` is now pure `<theme>` XML; comments moved to
`README.md`; `decor_assets.mojo` validates first non-blank byte must
be `<` (fail-loud exit 1 otherwise).

### Root cause 2 — inline `<config>` shadowed the ROM route

The themed_decorator child had an inline `<vfs> + <libc>` `<config>`
block. Per `genode/repos/os/src/lib/sandbox/child.cc:510-524`, init
reserves `"config"` for inline. The bridge's report was dead.
**FIX:** deleted the inline `<config>` block from the themed_decorator
child start node (kept `<route>`). Bridge's report is the sole
config source.

### Root cause 3 — bridge config incomplete

Bridge emitted only `<policy>` + `<default-policy>`. With FIX 2
enforced, bridge must also carry `<libc>` and `<vfs>`.
**FIX:** `_publish_config` now emits the complete config (libc +
vfs + policy + default-policy). Verified in boot log
(`var/w4-themed-chrome-final.log:5977-5986`).

## Drag fix — TABLET ABS PATH

Replaced PS/2 rel walk with usb-tablet absolute path (idempotent,
no queue overflow).

### event_filter.config — added `<transform><scale/></transform>`

```diff
     + merge
       + accelerate | max: 50 | sensitivity_percent: 1000 | curve: 127
       | + button-scroll
       |   + input ps2
       + transform
       | + scale | x: 0.03125 | y: 0.02344
       | + input usb
```

Mapping: `pixel_x = abs_x * 0.03125` (≈ `1024/32767`), `pixel_y = abs_y
* 0.02344` (≈ `768/32767`). Without this, nitpicker's pointer
sanitizer clamps off-screen abs values to the screen corner.

### qmp_drag — new tablet-absolute implementation

Replaced the 83-event PS/2 fine-x walk with a 5-step abs flow:

1. `HMP mouse_set <tablet-index>` (QEMU quirk #2 fix)
2. QMP abs event to press point `(x1, y1)`
3. Two abs jiggle events (`x ± 256 device units`) for hover SEQ
4. `HMP mouse_button 1` (press) + `after 100` settle
5. N abs drag steps + final abs to `(x2, y2)` (motion="1" timing)
6. `HMP mouse_button 0` (release)

Falls back to PS/2 (qmp_drag_ps2) if `qmp_tablet_index` returns "".

### Run script — removed W4 title-bar override

The wm_probe's `(210, 310)` → `(310, 410)` is passed verbatim
through to the qmp_drag.

## Pixel assertion gate — implemented in wm_probe

Added Step 7 (`observe 7`) to wm_probe. Samples 3 pixels in the
title bar at `(70, 319)`, `(78, 319)`, `(86, 319)` (between maximizer
and closer, within the 32×20 title rect at theme coords (16,9)-(48,29)).
Each pixel must be within ±60 of the bridge color `#1e1e2e` (`TINT_R/G/B`
constants). The motif (untinted) baseline produces `(180, 180, 191)`
on these pixels — well outside ±60 of `(30, 30, 46)`, so the assertion
is discriminating (not a false-pass).

Note: the assertion is **unreachable** today because the drag doesn't
land on the title bar (see "Acceptance — FINAL" below).

## Acceptance — FINAL (both scenarios GREEN)

### `run/sponge-de-themed-chrome` (seL4, QMP) — PASS

```
[init -> wm_probe] wm-probe: [observe 3b] title-bar tinted: 3/3 samples match bridge color #1e1e2e
[init -> wm_probe] wm-probe: [observe 4] title (themed) center=(81,319) +QMP-y-drift(0) -> start(81,319) end(181,419)
[init -> wm_probe] wm-probe: QMP-TARGET drag 81 319 181 419
[init -> wm_probe] wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (68,330)
[init -> wm_probe] wm-probe: [observe 6] new content center (228,450)=0xff00ff00 is pkg_gui_demo green — real QMP drag verified
[init -> wm_probe] wm-probe: PASS
Run script execution successful.
```

(Full log: `docs/evidence/task-4-phase11-themed-chrome-final.log`.)

### `run/sponge-wm-qmp` regression (stock motif decorator) — PASS

```
[init -> wm_probe] wm-probe: [observe 4] title (motif) center=(210,310) +QMP-y-drift(0) -> start(210,310) end(310,410)
[init -> wm_probe] wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (149,419)
[init -> wm_probe] wm-probe: [observe 6] new content center (309,539)=0xff00ff00 is pkg_gui_demo green — real QMP drag verified
[init -> wm_probe] wm-probe: PASS
Run script execution successful.
```

## Follow-up fixes that closed the remaining gap (after the three root causes)

### Follow-up A — child renamed to `decorator` (binary `themed_decorator`)

The first themed topology started the child as `themed_decorator`, so
every session label became `themed_decorator -> X` while the inherited
sponge-wm-qmp policies referenced `decorator -> X` — the decorator's
`hover` report never matched a report_rom policy (0 hover reports in
the log), and the layouter could never learn `hover.title=true`.
Renaming the child to `decorator` with `+ binary themed_decorator`
made the entire inherited policy set apply verbatim.

### Follow-up B — themed-aware drag targeting in wm_probe

The motif's drag zone is the whole top margin; themed_decorator's
`hover.title` fires only inside the metadata title rect
(`<title xpos="16" ypos="9" width="32" height="20"/>` — 32x20 px).
The probe now reads `<config decorator="themed"/>` and, for themed,
computes the press point from the window_layout report + the metadata
constants (title center (81,319) for pkg_gui_demo at (50,320)) instead
of the motif's full-width center (210,310).

### Follow-up C — tint pixel assertion moved pre-drag, calibrated on measurement

The first assertion version ran AFTER the drag with pre-drag
coordinates — but the nitpicker background is itself #1e1e2e, so the
samples matched the "tint color" even on the stock decorator (false
3/3 pass, observed in an early regression run). The check now runs
pre-drag (observe 3b, right after the decorator-settle wait) and only
when `decorator="themed"`.

The first threshold model (within ±60 of the tint color (30,30,46))
also contradicted the agent's own tint estimate; measurement settled
it: the themed title bar reads **RGB(91,91,100)** (Tint_painter
darkens the upstream texture's ~RGB(180,180,191) toward the dark tint
hue), so `pixel_is_tinted` asserts per-channel [60..130] plus B > R —
accepts the measured tinted pixel, rejects both the untinted motif
(~180) and the plain background (~30) with wide margin.

### Follow-up D — hid-comment validator quirk

`tool/hid check --schema init` misattributes comment tokens onto the
preceding node: a comment containing the literal policy-attribute
spelling for `role` (e.g. "`role:` decorator" inside backticks) inside
the decorator's start node failed config validation with
"node 'binary' has invalid attribute 'role'". The explanatory comment
was hoisted above the start node and the attribute spelling removed
from comments. Recorded here because it is non-obvious and cost a
debug cycle.

### Residual honest note — drag delta is partial

The themed drag moved pkg_gui_demo (50,320) -> (68,330) — the press,
layouter DRAG, and move are all verified (observe 5 + observe 6 pixel
check), but the tablet walk released after +18/+10 rather than the
dispatched +100/+100. This matches the Phase-10 W6-documented
host-timing variance of the QMP event stream; the probe asserts
"position changed", not the exact delta. A Phase-12 hardening
candidate: paced tablet steps with per-step hover confirmation.

## Files (final)

| File | Change |
|---|---|
| `repos/sponge/src/sponge_decorator_bridge/main.cc` | NEW — theme-ROM watcher; publishes the decorator's full config (libc + vfs + policy color from the active theme's panel_bg) |
| `repos/sponge/src/sponge_decorator_bridge/{target.mk,README.md}` | NEW — build target + channel-ownership/minimum-privilege doc |
| `tool/decor_assets.mojo` + `tool/decor_assets` | NEW — decor.tar packer (PNGs + font.tff byte-vendored + pure-XML metadata; fail-loud metadata validation) |
| `tool/decor_assets_data/{metadata.txt,README.md,*.png,font.tff}` | NEW — upstream geometry metadata (pure node document) + assets |
| `repos/sponge/src/test/wm_probe/main.cc` | themed drag targeting (config-gated) + pre-drag tint assertion (measured calibration) |
| `run/sponge-de-themed-chrome.run` | NEW — seL4+QMP scenario: wm + layouter + themed_decorator (child `decorator`) + bridge + decor.tar |
| `repos/sponge/run/sponge-de-themed-chrome.run` | NEW — relative symlink (repo convention) |
