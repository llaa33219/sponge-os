# Launch-stall root cause: the tasklist static-rules publication dropped every rule beyond the first (2026-10-03)

The original real-hardware bug report: on the LG gram 17ZD90N, launcher
clicks fill the panel's R: bookkeeping (N/C/U increment — pkgd
processes the launch, pkg_runtime spawns the child, its state report
updates) but the app window never appears (W frozen at 1). Reproduced
in QEMU and root-caused to a one-line serialization bug in sponge-de's
tasklist rules publication.

## Root cause

`main.cc` collected the `<tasklist_static_rules>` from the config into
a FLAT concatenation of `<assign/>` elements with no wrapper root:

    <assign label="..."/> <assign label_prefix="pkg_runtime" .../>

`TasklistController::_emit_static_rules` distinguishes only two
document shapes:

    if (fragment.has_type("assign"))
        emit_assign(fragment);                        // single-assign doc
    else
        fragment.for_each_sub_node("assign", ...);    // wrapper + children

A flat concatenation parses with the FIRST element as the root, so
`has_type("assign")` is true and only the FIRST static rule was ever
published. On the product topology the second rule —
`<assign label_prefix="pkg_runtime" ...>` — is the only rule that can
match launched-app windows (their exact labels are unknown ahead of
time). With it dropped:

1. A newly launched app window matches no layouter rule.
2. rules="rom" mode assigns no geometry — the wm's real view stays at
   the empty initial rect — the window is invisible.
3. The window never enters the tasklist's tracked set (tracking
   requires a window_layout entry), so W stays frozen: a deadlock
   between placement and tracking.

Every other layer is healthy — proven by instrumenting each in QEMU on
the reproduction (an autostarted calculator on the dist topology,
whose panel matched the real machine exactly, C:3 U:3 W:1 K:11 P:1
Q:0 D:4096b):

| Layer | Instrumentation | Verdict |
|---|---|---|
| Session transport | Gui routed direct to nitpicker (same double nesting) | window APPEARS — transport innocent |
| wm capacity | wm quota ×10 (caps 20000, ram 64M) | still frozen — not capacity |
| wm session/view | session-open log (label+quota), view() result log, failure-branch logs | all sessions open (depth-2: ram≈18.7K caps 4), every view() returns OK, windows registered, reports submitted |
| report_rom relay | update()/dataspace()/submit() logs | submits grow 14→137→278→408 bytes; both readers (layouter, sponge-de) re-fetch on every growth |
| window_list content | verbose report_rom dump | HID entries complete (id/label/title/width/height) |
| **published rules** | verbose dump of the rules report | **one assign short — the only broken artifact** |

## Fix

Commit `aeeed63a13` — wrap the collected `<assign>` elements in a
`<tasklist_static_rules>` root in main.cc so the wrapper path
publishes them all. (The tracked-window exact assigns still precede
the static fallbacks — the layouter keeps the FIRST match per window,
preserving the minimize/restore semantics.)

Verified on the reproduction image: both rules published, the layouter
assigns the calculator `xpos: 50 | ypos: 328 | width: 400 | height:
300` (the rule plus the panel offset), and the window renders —
screenshot-verified "Input 1"/"Input 2"/"Output" controls at the
assigned position.

## Scenario verification after the fix

- `sponge-desktop-disk-uefi-usb` (the product scenario, two static
  rules — the fix's target): PASS.
- `sponge-wm-tasks`: FAIL at the minimize transition — analyzed and
  attributed to the documented W12 pre-existing minimize-transition
  flake, not this fix: that scenario has exactly ONE static rule, for
  which the fix is a no-op (a single assign publishes identically
  through either code path). The failure signature matches the
  documented sibling flakiness (see the row-20 ledger entry and
  `docs/evidence/task-7-phase14-wm-tasks-regression.md`).

## Field instrumentation prerequisite

The diagnosis was only possible after the panel instrument was itself
repaired — see `docs/evidence/field-instrument-hid-discovery.md` (the
dead-instrument regression and the HID serialization discovery). The
real-hardware panel reading (K:11 P:1 Q:0 D:4096b, click → N↑ C↑ U↑ W
frozen) that localized the failure to window creation was the pivot
point of this investigation.

## Hardware path

The fix is in sponge-de (a /system/bin component) — the corrected
image must be re-flashed to the test media for the real machine.
Expected real-hardware result after re-flash: launcher clicks open
app windows (W increments from 1).
