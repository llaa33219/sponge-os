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

## Follow-up (2026-10-03 night): the post-fix real-hardware reports

After the 20:43 image, the user reported: windows now open (the
primary fix works), but (1) opening a new window resets existing
windows' positions, (2) two instances of the same program cannot be
opened, (3) terminal and browser open no window, (4) some situations
crash with a frozen display.

1. **Position reset — root-caused and fixed (`7b529a807a`).** The
   window_layouter repositions EVERY assigned window on each
   window_list change (`_update_window_layout`: dissolve + re-assign +
   `outer_geometry` per the rules' xpos/ypos). The tasklist published
   the rules ROM only once at boot, so any new window snapped all
   windows back to their stale rule positions. Fix: republish the
   rules whenever the tracked geometry signature changes and has been
   stable for one extra poll (drag-safe debounce). Coordinate-frame
   invariant: window_layout is screen-absolute, <assign> is
   target-relative (the screen boundary starts at the panel, y=28);
   the republished assigns subtract the boundary origin. The first
   iteration without the conversion ran away (+28px per cycle, 2653
   rules submissions per boot); with it, 3 bounded submissions and a
   lossless round-trip (rules 50,300 <-> layout 50,328) — verified
   on the autostart-repro image.

2. **Terminal opens no window — root-caused (integration gap, fix is
   a work item).** Reproduced in QEMU on the product topology via
   the autostart harness: `rom_sys: /terminal not found` + 27
   retries of `terminal: environment ROM session denied` (the
   sandbox child's term for the binary-ROM session). The terminal
   package is a sub-init (binary `init`) whose nested starts
   (`terminal`, `vfs`, `vfs_rom`→`cached_fs_rom`) carry no `bin/`
   prefix; pkgd's `binary_prefix` rewriting applies only to the
   top-level start. The flat scenarios worked because pkg_runtime's
   ROM space IS the flat boot-module namespace (unprefixed names
   resolve); on the product media, ROM resolves via rom_sys
   chrooted at `/system` — `/system/terminal` does not exist (the
   binary is at `/system/bin/terminal`; `vfs` and `cached_fs_rom`
   are not in `/system/bin` at all — they are Tier-0 boot modules
   only). Additionally the terminal payload (the noux tars,
   VeraMono.ttf) is not staged on the product image. The `/bin/bash`
   child resolves via the sub-init's `any-service → any-child →
   vfs_rom` fallback (the tars), which itself needs vfs_rom to
   start first. The fix: stage the sub-init children binaries and
   the payload on the product media + teach pkgd's generator to
   prefix nested starts' binaries when `binary_prefix` is set.

3. **Browser (falkon) opens no window — the documented seL4
   limitation**, not a new bug: base-sel4 gives every child PD a
   fixed 8192-slot capability CNode; falkon's WebEngine runtime
   exceeds it and is stopped (`docs/13-installation.md` §6 known
   limitations; the falkon-disk scenario evidence, "Remaining
   blocker"). The falkon launch attempt is also the leading suspect
   for the crash-and-freeze report (4) — the documented freeze
   class.

4. **Two instances of one program — by design**: pkgd's launch
   idempotency ("already-running", the Phase 7 lifecycle scenario's
   verified behavior). A per-instance model would be a feature
   request, not a defect.
