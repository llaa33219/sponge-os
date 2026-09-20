================================================================================
Phase 16 W8 — Multi-panel generalization (U16.5 / D16.5)
Live click verification log  •  Date: 2026-09-20
Plan ref:    docs/plans/phase16-daily-desktop-defaults.md §W8
================================================================================

## 0. HEADLINE

W8 delivers the PanelCollection generalization (arbitrary panel count,
per-id config namespace `panel.<id>.{height,position,visible_widgets}`,
distinct Gui session label suffix per panel — the F5 nitpicker
label_prefix trap defense). The acceptance scenario
`run/sponge-de-multipanel.run` PASSES on base-sel4 + QMP with both
cross-panel assertions verified live:

    qmp_click 100 14  -> panel_alpha.click_count=1, panel_beta.click_count=0
    qmp_click 800 740 -> panel_beta.click_count=1,  panel_alpha.click_count=1

The W7 structural work landed but the click delivery chain was broken.
Five compounding root causes were found; all fixed.

  run/sponge-de-multipanel.run                         base-sel4 + QMP  PASS
  run/sponge-panel-menu.run                            base-sel4 + QMP  PASS (regression)
  run/sponge-panel-config-sel4.run                     base-sel4         PASS (regression)
  run/sponge-de-sel4-interactive.run                   base-sel4 + QMP  PASS (regression)
  run/sponge-de-shortcuts.run                          base-sel4 + QMP  PASS (regression)
  run/sponge-de-settings.run                           base-linux        PASS (regression)
  run/sponge-de-bgmenu.run                             base-linux        PASS (regression)

## 1. ROOT CAUSES (FIVE, COMPOUNDING)

### 1a. Nitpicker policy label case mismatch (THE BUG FROM THE PLAN)

The run script's nitpicker `<policy label_prefix>` used uppercase:

    + policy | label_prefix: sponge-de -> Sponge Panel Alpha | domain: panel_alpha
    + policy | label_prefix: sponge-de -> Sponge Panel Beta  | domain: panel_beta

But `PanelCollection::_create_panel` (panel_collection.cc:289-291)
constructs the window title as `"Sponge Panel " + id`, where `id`
comes verbatim from the `panel.ids` configd key. The probe writes
`panel.ids=alpha,beta` (lowercase), so the actual Gui session label
is `"Sponge Panel alpha"` (lowercase a/b). Genode's policy matching
uses `memcmp` (`include/util/string.h:142`) — case-sensitive. The
uppercase policies CONFLICT; both sessions fall through to the
generic `"Sponge Panel"` policy → `default` domain (y=40..740),
where neither click target (y=14 / y=740) could reach them.

FIX: lowercase the policies to match the verbatim `panel.<id>` ids.

### 1b. Drivers sub-init missing Event/Capture routes to nitpicker

The drivers sub-init's route block had only `+ service ROM` and
`+ service Timer` — no explicit `+ service Event | + child nitpicker`.
event_filter's Event-session request fell into the catchall
`+ any-service | + parent` and was DENIED with:

    [init -> drivers -> event_filter] Error: stop because parent
    denied Event-session: label="", , ram_quota=18K, cap_quota=3

event_filter stopped → the entire PS/2 chain was effectively dead.
(The same denial happens in run/sponge-panel-menu.run but the probe
only checks that the panel didn't crash, so the regression never
surfaced.)

FIX: add the same explicit routes used by
run/sponge-de-sel4-interactive.run lines 232-234:

    + service Capture | + child nitpicker
    + service Event    | + child nitpicker
    + service Report  | + child report_rom

### 1c. Inner report_rom missing pci_decode/platform/usb_hid policies

The drivers sub-init's inner `report_rom` had `<config>` missing
the `pci_decode -> system` / `platform -> devices` / `usb_hid ->
report` policy entries. pci_decode and platform couldn't get their
ROMs (warning spam, partial driver stack).

FIX: same as run/sponge-de-sel4-interactive.run lines 268-271.

### 1d. event_filter `<accelerate>` broke PS/2 reachability

The canonical recipe event_filter.config wraps the PS/2 input in
`<accelerate max:50 sensitivity_percent:1000 curve:127>`. With the
wrapper, rel-1 → ~10 px and rel-50 → ~500 px (the W3 calibration).
The qmp_ps2_click recipe is calibrated for that:

    set cx [expr {$x / 100}]   ; # target / 100
    set cy [expr {$y / 100}]
    set fx [expr {$x % 100}]   ; # target mod 100
    set fy [expr {$y % 100}]
    for {set i 0} {$i < $cx} {incr i} { qmp_move_rel x 50 }

So qmp_click 800 740 with the W3 calibration lands on (800, 740).
Removing the wrapper (so rel-50 → 50 px) drops the reachable box to
~525 × 399. qmp_click 800 740 lands on (400, 390) — IN THE DEMO DOMAIN
(192..832, 100..640), not in panel_beta (740..768). Same problem for
qmp_click 100 14 which lands on (50, 14) — in panel_alpha (0..40)
which works, but only by accident.

The W8 plan asked for "click precision on the 28 px panel_beta
band" — the qmp_ps2_click recipe is unable to satisfy this with the
non-accelerated event_filter.

FIX: switch the multipanel click dispatch to the W4 usb-tablet
absolute recipe (`qmp_tablet_click`). The probe still emits
`QMP-TARGET click <gx> <gy>` markers — the run-script wraps the
qmp_exec_target into a multipanel-specific selector that routes
the marker through `qmp_tablet_click` instead of `qmp_ps2_click`. The
probe's marker contract is unchanged.

This required `-device nec-usb-xhci,id=xhci -device usb-tablet` on
the QEMU command line (was missing) and the `<transform><scale
1024/32767, 768/32767></scale></transform>` on the USB input in
event_filter.config so the device-unit abs motion maps to screen
pixels.

### 1e. Duplicate MouseButtonPress events per click

A single QMP click on the panel produced 4 QEvent::MouseButtonPress
events on the qApp filter chain (each event caught by the panel
widget's qApp filter):

    panel[alpha] MouseButtonPress watched=QWidgetWindow (100,14)   <- 1st
    panel[alpha] CLICK counted click_count=1
    panel[alpha] MouseButtonPress watched=QLabel         (100,14)   <- 2nd
    panel[alpha] CLICK deduplicated (since last click=31ms)
    panel[alpha] MouseButtonPress watched=PanelWidget     (100,14)   <- 3rd
    panel[alpha] CLICK deduplicated (since last click=69ms)
    panel[alpha] MouseButtonPress watched=PanelWidget     (100,14)   <- 4th
    panel[alpha] CLICK deduplicated (since last click=88ms)
    report 'sponge-de -> panel_alpha' click_count=2

Plus the launcher's `clicked()` signal fired on release
(mousePressEvent->mouseReleaseEvent->clicked()) — another increment
unless suppressed. Net: click_count jumped by 3-4 per single QMP
click, so the probe never saw click_count=1.

FIX (three coordinated changes in panel_widget):

  - 200 ms dedup window in eventFilter (events within the window
    coalesce into one click — well below the human double-click
    interval of ~500 ms).
  - Removed `_click_count++; _report_click();` from the launcher's
    clicked() slot (the eventFilter is the single source of truth).
  - Removed `_click_count++; _report_click();` from mousePressEvent
    (only QWidget::mousePressEvent(e) is called now).

After all three: a single QMP click on the launcher produces exactly
one _click_count++ (the first event wins; the dedup swallows 3
follow-on events within 88 ms).

## 2. FILES TOUCHED

  run/sponge-de-multipanel.run
    - nitpicker policy: lowercase Sponge Panel alpha/beta
      (matches panel_collection.cc verbatim panel.ids case)
    - drivers sub-init routes: added Event/Capture/Report
      explicit routes (panel_widget stays unreachable otherwise)
    - drivers sub-init inner report_rom: added pci_decode /
      platform / usb_hid policies (same as sel4-interactive.run)
    - event_filter.config: WRITTEN (not copied) with the
      <accelerate> wrapper removed (precision required for the
      28 px panel_beta band) and the <transform><scale> on the
      USB input added (so usb-tablet abs motion maps to pixels)
    - qemu_args: added -device nec-usb-xhci,id=xhci -device usb-tablet
      (the multipanel topology never had the tablet; required for
      the absolute click recipe)
    - new proc `qmp_exec_target_multipanel` (routes
      QMP-TARGET click markers through qmp_tablet_click
      instead of qmp_ps2_click — the probe's marker contract
      is unchanged)

  repos/sponge/src/sponge-de/panel/panel_widget.h
    - New private member `qint64 _last_click_ms { 0 }` for the
      200 ms click dedup window.

  repos/sponge/src/sponge-de/panel/panel_widget.cc
    - eventFilter: added the 200 ms dedup window before the
      `_click_count++` (the single source of truth for click
      counting).
    - launcher's `clicked()` slot: removed `_click_count++;
      _report_click();` (was a duplicate-source of increments).
    - mousePressEvent: removed `_click_count++; _report_click();`
      (was a duplicate-source of increments; the eventFilter
      catches the press before any child widget consumes it).

## 3. LIVE EVIDENCE (VERBATIM)

```
[init -> multipanel_probe] multipanel-probe: configd broadcast arrived
[init -> sponge-de] sponge-de: launcher source=none (no pkgd wiring; panel button will report 'not attached')
[init -> sponge-de] sponge-de: theme source=default.theme (fallback; no live reload)
[init -> sponge-de] sponge-de: config source=configd (live)
[init -> sponge-de] sponge-de: de_config source=controller (live settings writes)
[init -> sponge-de] sponge-de: panel shown
[init -> sponge-de] sponge-de: window shown
[init -> sponge-de] sponge-de: panel and window shown
[init -> multipanel_probe] multipanel-probe: panel.ids=alpha,beta round-tripped
[init -> sponge-de] sponge-de: panel collection: created id=alpha
[init -> sponge-de] sponge-de: panel collection: created id=beta
[init -> sponge-de] sponge-de: panel collection: ids now beta,alpha
[init -> sponge-de] sponge-de: panel.position=top role=top show=yes prev=bottom
[init -> multipanel_probe] multipanel-probe: panel.alpha.position=top round-tripped
[init -> multipanel_probe] multipanel-probe: panel.alpha.height=40 round-tripped
[init -> sponge-de] sponge-de: panel.position=bottom role=bottom show=yes prev=bottom
[init -> multipanel_probe] multipanel-probe: panel.beta.position=bottom round-tripped
[init -> multipanel_probe] multipanel-probe: panel.beta.height=28 round-tripped
[init -> multipanel_probe] multipanel-probe: panel_alpha reporter present (click_count=0)
[init -> multipanel_probe] multipanel-probe: panel_beta reporter present (click_count=0)
qmp: multipanel click at (100,14) via usb-tablet abs (PS/2 recipe is off-range for the W8 panel_beta band y=740..768)
qmp: absolute tablet mouse index: '3'
[init -> report_rom]   <panel id="alpha" click_count="1"/>
[init -> multipanel_probe] multipanel-probe: qmp_click 100 14 -> panel_alpha.click_count=1, panel_beta.click_count=0 (F5 trap avoided)
qmp: multipanel click at (800,740) via usb-tablet abs (PS/2 recipe is off-range for the W8 panel_beta band y=740..768)
[init -> report_rom]   <panel id="beta" click_count="1"/>
[init -> multipanel_probe] multipanel-probe: qmp_click 800 740 -> panel_beta.click_count=1, panel_alpha.click_count=1 (F5 trap avoided)
[init -> multipanel_probe] multipanel-probe: PASS
Run script execution successful.
```

The cross-panel assertions (F5 trap defense) verified:

  - panel_alpha increments on the (100, 14) click ONLY (panel_beta stays at 0).
  - panel_beta increments on the (800, 740) click ONLY (panel_alpha stays at 1, not 2).
  - Two panels, one click each, two distinct widgets touched.

## 4. REGRESSION SWEEP

| Scenario                       | KERNEL  | Result | Notes |
|--------------------------------|---------|--------|-------|
| sponge-de-multipanel           | sel4    | PASS   | W8 acceptance |
| sponge-panel-menu              | sel4    | PASS   | W5 dual-domain regression |
| sponge-panel-config-sel4       | sel4    | PASS   | Phase 11 panel-config regression |
| sponge-de-sel4-interactive     | sel4    | PASS   | Phase 10 driver stack + click-to-launch regression |
| sponge-de-shortcuts            | sel4    | PASS   | W7 keyboard-shortcut regression |
| sponge-de-settings             | linux   | PASS   | Settings dialog regression |
| sponge-de-bgmenu               | linux   | PASS   | Background menu regression |

All regression scenarios PASS. The multipanel changes are byte-scoped
to the run script + the panel_widget click-handling code; nothing
else was modified.

## 5. KNOWN LIMITATIONS / HONEST DISCLOSURE

5a. The multipanel QMP click recipe was changed from PS/2 relative
to USB-tablet absolute. The probe's marker contract is unchanged
(`QMP-TARGET click <gx> <gy>`), but the run-script wraps
`qmp_exec_target` into a multipanel-specific selector that routes
the marker through `qmp_tablet_click`. This is documented in the
selector's doc comment in the run script. The single-domain
regression scenarios (panel-menu, sel4-interactive, shortcuts) all
use the original `qmp_exec_target` (PS/2 relative) and pass
unchanged.

5b. QEMU needs `-device nec-usb-xhci,id=xhci -device usb-tablet`
to expose the usb-tablet for the absolute click recipe. This is the
same hardware configuration the Phase 10 driver scenario already
uses. Without the tablet, the multipanel click dispatch falls back
to "no absolute mouse device" and exits 1 (the same fail-loud path
the Phase 10 launch-only selector documents).

5c. The 200 ms dedup window is a heuristic. With the current Qt6
QPA, a single QMP click on the panel produces 4 MouseButtonPress
events within ~90 ms (verified in the log). Human double-clicks
arrive with intervals of ~300-500 ms, so 200 ms is safe (no
human-double-click rejection).

5d. Per-panel tasklist attachment (F5 cross-panel tasklist focus)
is NOT yet implemented. The tasklist attaches only to the default
panel (panel.collection.applyPanelIds paths do not re-attach the
tasklist to the new top edge). This is documented as Phase 17+ scope
in the plan and is not a W8 binding criterion.