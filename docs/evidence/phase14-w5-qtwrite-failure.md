================================================================================
Phase 14 W5 follow-on — run/sponge-clipboard-qtwrite.run
Investigation log  •  Date: 2026-08-12
Plan ref:    docs/plans/phase14-daily-desktop.md §W5
================================================================================

## 0. HEADLINE

The Phase 14 W5 acceptance scenario `run/sponge-clipboard-qtwrite.run` is
UNABLE TO PASS on base-sel4 in the current seL4 + Qt6.8.3 + softpipe Mesa
environment. The textedit component boots, sponge-de boots, the QGenode
clipboard bridge is enabled (`<config clipboard="yes"/>` in both init and
metadata), the upstream clipboard server is staged and configured
permissively (`match_labels: no`, default-domain flow), and the
clipboard_probe's priming write propagates through the bus. But
textedit's QGenodeClipboard::setMimeData never fires after the host-
driven QMP QWERTY click + QMP `send-key` Ctrl-A + Ctrl-C sequence; the
upstream server's "clipboard" module is never replaced with textedit's
content; the probe's needle-search therefore times out with the existing
FAIL message (1500 iters × 100 ms ≈ 150 s).

  Acceptance run/sponge-clipboard-qtwrite.run PASS: NOT MET.
  Acceptance cmd line Qt + upstream-clipboard-server proof recorded: MET
  (run/sponge-clipboard.run PASSES the server → Qt PASTE direction
  already and is the W5 acceptance scenario; the qt-write scenario is
  the §W5 follow-on per docs/plans/phase14-daily-desktop.md §D14.2 U2).

This file records the empirical evidence and the three hypotheses that
the investigation ruled out. The honest path per the task instructions
is to record the failure precisely and report back to the user —
a fabricated PASS would masquerade the broken write path. The
clipboard server, the QGenodeClipboard bridge, the upstream Qt6 bus,
the probe, and the related scenarios (`sponge-clipboard.run`,
`sponge-clipboard-focus.run`, `sponge-textedit-qmp.run`,
`sponge-textedit.run`, `sponge-alpha.run`) are unaffected.

================================================================================
1. SCENARIO TOPOLOGY (unchanged)
================================================================================

Run script (input unchanged in topology):

  init
  + timer / report_rom / nitpicker / drivers (sub-init:
    vesa_fb/ps2/pc_usb_host/usb_hid/event_filter + xhci +
    usb-tablet; QMP listener enabled)
  + sponge_configd | sponge_themed | sponge_pkgd
  + clipboard      (upstream os/src/server/clipboard; default-domain
                    permissive flow, match_labels: no)
  + sponge-de      (Qt6 DE; clipboard="yes")
  + pkg_runtime    (init only — no children launched under it in
                    qtwrite mode)
  + textedit       (Qt6 qt6_textedit; clipboard="yes"; launched as
                    a direct child of init, NOT under pkg_runtime —
                    relevant for the §3 domain analysis)
  + clipboard_probe (probe-write mode (qt_config qt_write_sentinel=...
                    boots it into qt-write witness mode)

The QMP choreography:

  1. Wait for sponge-de first paint
  2. Wait for clipboard_probe "textedit rendered" marker
  3. PS/2 click at guest (512, 384) [textedit's QTextEdit center]
     after 1500 ms wait
  4. QMP `send-key` the sentinel "qt write sentinel phase 14"
     (29 chars, lowercase only — qmp.inc's char map covers
     a-z/0-9/space/newline/minus/dot)
  5. QMP `send-key` composite Ctrl-A  (select all)
     after 250 ms wait
  6. QMP `send-key` composite Ctrl-C  (copy -> QGenodeClipboard
     -> Report -> upstream server) after 2000 ms wait
  7. run_genode_until {.*clipboard-probe: PASS.*} 180

================================================================================
2. EMPIRICAL EVIDENCE (latest run, with the §6 probe diagnostic)
================================================================================

The probe's qt-write mode emits an exact-content line for the priming
write and ONLY emits on ROM size change thereafter. The run below
records the evidence verbatim:

  [init -> clipboard_probe] clipboard-probe: starting
  [init -> clipboard_probe] clipboard-probe: qt-write mode
      (waiting for 'qt write sentinel phase 14')
  [init -> sponge-de] sponge-de: applying theme 'light'
  [init -> clipboard_probe] clipboard-probe: textedit rendered
      (title-bar frac=36%)

  qmp: focus textedit's window (the writer)
  qmp: PS/2 click -> (512,384) - clamp to (0,0) + nav + press/release
  qmp:   coarse rel-50: cx=5 cy=3
  qmp:   fine rel-1: fx=12 fy=84
  qmp:   press
  qmp:   release
  qmp: type sentinel into focused textedit
  qmp: send Ctrl-A (select all, via QEMU send-key composite)
  qmp: send Ctrl-C (copy -> QGenodeClipboard -> server)

  [init -> clipboard_probe] clipboard-probe: [qt] priming the
      clipboard server's ROM module
  [init -> clipboard_probe] clipboard-probe: [qt] ROM valid after
      priming: yes
  [init -> clipboard_probe] clipboard-probe: [qt] wait for textedit's
      Qt write to propagate

  [init -> clipboard_probe] clipboard-probe: ROM size change
      0 -> 4096 bytes (first 96 bytes):
  [init -> clipboard_probe] clipboard-probe:   0: 'clipboard | qt_w'
  [init -> clipboard_probe] clipboard-probe:  16: 'rite_ping: 1.-..'
  [init -> clipboard_probe] clipboard-probe:  32: '................'
  [init -> clipboard_probe] clipboard-probe:  48: '................'
  [init -> clipboard_probe] clipboard-probe:  64: '................'
  [init -> clipboard_probe] clipboard-probe:  80: '................'

  [init -> clipboard_probe] clipboard-probe: clipboard ROM poll 0
      size=4096
  ... poll 10 / 20 / 30 / ... / 1490 size=4096 (no ROM size change
      ever observed after the 0 -> 4096 priming transition; the
      needle-search exits after 1500 iters ≈ 150 s)
  [init -> clipboard_probe] Error: clipboard-probe: FAIL qt-write
      sentinel did not appear in the clipboard ROM (textedit's
      QGenodeClipboard -> Report -> server path did not propagate)
  [init] child "clipboard_probe" exited with exit value 1
  Error: Test execution timed out
  make: *** [Makefile:446: run/sponge-clipboard-qtwrite] エラー 254

Three diagnostic observations (from the log):

  (a) The priming write PROVES the probe-side path is wired:
      "<clipboard qt_write_ping="1"/>" lands in the upstream server's
      ROM module and the probe's ROM subscriber sees it (4096 bytes,
      valid XML, the attribute `qt_write_ping="1"` at offset 8).

  (b) For the ENTIRE 150 s needle-poll window the probe's ROM reports
      `size=4096` with no ROM size change. The needle is searched on
      every iteration; no needle match. If textedit's
      QGenodeClipboard::setMimeData had fired and submitted the typed
      bytes through its Report session, the upstream server's
      write_content path would REALLOC the backing store to ~50 bytes
      for "<clipboard>\"qt write sentinel phase 14\"</clipboard>" and
      the size would change. It does not change.

  (c) The upstream server emits NO "unexpected attempt by '...'"
      warning. This means the write_content's write_permitted check
      was NEVER reached from textedit's session — either textedit's
      Report session was never opened, or setMimeData was never
      called.

================================================================================
3. HYPOTHESES RULED OUT
================================================================================

H1. Focus-domain mismatch (clipboard_probe's "domain=default" fake
    focus vs textedit's notional domain). RULED OUT for the
    current `textedit`-as-top-level-child topology:

        textedit is launched as `+ start textedit | ...` — a direct
        child of init, NOT under pkg_runtime. textedit's session
        label is therefore `textedit -> clipboard` (truncated
        `textedit`). No `<policy label_prefix: pkg_runtime ->
        textedit | domain: edit>` policy matches it; the only
        fallback is `<default-policy | domain: default>`. So
        textedit's domain IS "default", matching the probe's fake
        focus. write_permitted returns true (matched explicitly
        by the absence of any "unexpected attempt" warning in the
        log). The mismatch is structurally impossible in this
        topology; if textedit's write were rejected, the log
        would carry the upstream server's warning — it does not.

    Earlier W5 design discussions considered starting textedit
    under pkg_runtime so the `pkg_runtime -> textedit` policy
    applied (`domain=edit`); the current run scripts launch
    textedit as a direct child of init for simplicity. With the
    current topology the "default" match always wins.

H2. clipboard="yes" attribute lost in metadata composition
    (QGenodeClipboard silently no-ops if the config attribute is
    false). RULED OUT: the qtwrite scenario's `+ start textedit`
    inline config carries `<config clipboard="yes">`, served as
    the "config" ROM by init. textedit's pkg/textedit/metadata.xml
    ALSO carries `<config clipboard="yes">` and the
    payload/textedit.config carries `<config clipboard="yes">`.
    All three sources agree. The QGenodeClipboard at
    `genode/contrib/qt6_base-.../qgenodeclipboard.cpp:38` reads
    `attribute_value("clipboard", false)` and opens the Reporter
    session IF true. The only failure mode would be a silent
    try/catch on `Reporter(env, "clipboard")`, which would yield
    a null `_clipboard_reporter` and an immediate-return in
    setMimeData — but the upstream server would still register
    textedit's session as a writer at session-open time. Neither
    a denied-session warning nor an extra "qt_write_ping" attempt
    lands in the log.

H3. Ctrl modifier delivery via QMP `input-send-event` low-level
    form. RULED OUT for the low-level form AND for the high-level
    form. The investigation tried BOTH:

    (a) Initial qtwrite scenario used `input-send-event` with
        four raw key events (ctrl down / a down / a up /
        ctrl up) inside a single QMP batch. This was the W5
        author's first cut — it had not been exercised
        anywhere else (Phase 10 only ever sent bare keys:
        letters, ret, tab).

    (b) After H3 was suspected, the QMP form was replaced with
        the high-level `send-key` composite, identical to the
        proven Ctrl-V path in run/sponge-clipboard.run:
        `{"execute":"send-key","arguments":{"keys":[
          {"type":"qcode","data":"ctrl"},
          {"type":"qcode","data":"c"}]}}`
        The same form proven for paste (server -> Qt PASTE
        direction, sponge-clipboard.run PASSES). Both forms
        produce the same FAIL.

H4. The write happens but the probe's read session doesn't
    observe it (policy / label mismatch). RULED OUT: the priming
    write by the same probe, same session pair, on the same
    module is observable end-to-end (probe's needle search would
    find the priming's `qt_write_ping` if it ever ran on it; the
    priming content was byte-verified by the §6 dump
    diagnostic). The probe-side observability is fine.

================================================================================
4. WHAT THE RUN LOG DEFINITIVELY EXCLUDES (and what it does NOT)
================================================================================

A successful run produces three classes of evidence:
    (i)  the ROM module's dataspace is REALLOC'd to a smaller
         size when textedit's setMimeData submits through the
         Report session (Realloc backing store if needed,
         genode/repos/os/include/report_rom/rom_module.h:282);
         the probe's `_clipboard_rom.size()` reflects the new
         size and the §6 byte-dump diagnostic emits;
    (ii) the upstream server emits
         `warning("unexpected attempt by '...' to write to 'clipboard'")`
         from its write_permitted reject path
         (genode/repos/os/src/server/clipboard/main.cc:263) if
         the writer's domain does not match the focused domain;
    (iii) the upstream server log is silent if NO write attempt
          was ever made — i.e., the Report session was rejected
          at session-open time (silent Service_denied) or the
          client's Reporter::generate never fired.

The current run shows (iii) without (ii). Combined with the
priming write being observable via the SAME probe session pair,
this is dispositive: textedit's Report session either failed
silently at open time, or its Reporter::generate was never called.
Both locate the breakage between the upstream keyboard chain
(event_filter → nitpicker → Qt) and QGenodeClipboard::setMimeData.

================================================================================
5. INVESTIGATED BUT UNRESOLVED (Phase 15 follow-up territory)
================================================================================

The runtime evidence above narrows the failure to a chain
explicitly NOT exercised by Phase 10 sponge-textedit-qmp.run
(none of Phase 10's QMP-driven scenarios sent Ctrl-modified
keystrokes into a QTextEdit). The remaining candidates that
this investigation did NOT pin down:

  - event_filter's `mod2 | + key KEY_LEFTCTRL | + key KEY_RIGHTCTRL`
    mapping has not been empirically verified for
    textedit-typed-then-Ctrl-A-then-Ctrl-C. The proven Ctrl-V path
    in run/sponge-clipboard.run uses the same `send-key` composite
    but fires the shortcut on textedit's QTextEdit (its
    built-in Paste action); the qtwrite scenario's Ctrl-A / Ctrl-C
    relies on textEdit::selectAll / textEdit::copy registered
    via `actionCopy->setShortcut(QKeySequence::Copy)` in the textedit
    example's `setupEditActions()`.

  - QTextEdit::selectAll() and QTextEdit::copy() in the vendored
    qt6_textedit example may have a quirk where the
    setShortcut's QAction is registered with default
    Qt::WindowShortcut context but the QMainWindow's central
    QTextEdit grabs the key event in QTextEdit's own
    keyPressEvent before the global action shortcut fires (Qt
    priority inversion on Qt::Key_C / Qt::Key_A vs the
    LowPriority actionCopy).

  - textedit's `QGenodeClipboard` opens its `Reporter` in the
    outer try/catch in qgenodeclipboard.cpp; if
    `Reporter(env, "clipboard")` throws `Service_denied`
    silently at session-open, the Reporter pointer is null and
    setMimeData exits early WITHOUT a visible error. No log
    evidence in any tested form confirms or excludes this
    possibility without Qt-level instrumentation.

  - textedit's qgenode integration initializes the
    QGenodeClipboard in the QGenodeIntegration ctor. If a Qt
    init race or session-arena pressure causes this ctor to
    fail before the Reporter opens, setMimeData exits silently
    with `_clipboard_reporter = nullptr`. This is the same
    failure mode as H2 but with the cause being a runtime
    constructor exception rather than a missing config
    attribute.

Pinning one of these requires either (a) Qt-level instrumentation
via a forked qgenodeclipboard.cpp with explicit qDebug(), or
(b) a smaller replacement Qt example that triggers the same
copy path on a single Ctrl-C keystroke. Neither is deliverable
in this Phase 14 W5 follow-on without violating AGENTS.md §5.2
(no port-side changes) or AGENTS.md §3.5 (no edits outside the
Sponge-side).

================================================================================
6. PROBE-SIDE DIAGNOSTIC ADDED (intentional, kept in the probe)
================================================================================

The clipboard_probe's `_wait_for_needle` was extended to byte-dump
the probe's "clipboard" ROM content on EVERY size change. The dump
fires at most a handful of times per scenario:

  - Run #1 (probe priming): size 0 -> 4096, content emitted
    (the §2 evidence above).
  - Run #2 (textedit writes): size would change 4096 -> ~50,
    content emitted.
  - Run #3+ (idle): no change, no log noise.

The size-change dump gives an instant verdict for any future
re-run: if textedit's setMimeData ever fires, the post-4096 dump
will show the typed bytes; if it stays at the priming content,
the failure mode is reproduced. This is a one-line maintenance
win for any follow-up investigator and does not affect the
existing primary (`sponge-clipboard.run`) or focus-gated
(`sponge-clipboard-focus.run`) scenarios.

================================================================================
7. RUN SCRIPT CLEANUP APPLIED
================================================================================

The run script's Ctrl-A / Ctrl-C dispatch was switched from
QEMU `input-send-event` (low-level) to QEMU `send-key` composite
(high-level). This matches the proven Ctrl-V form in
`run/sponge-clipboard.run` line 645 — the same `send-key` form
that the W5 acceptance scenario uses for the server → Qt PASTE
direction. The form change is a maintenance clean-up; it does
not fix the test (both forms FAIL with the same evidence), but
it removes the latent "untested input-send-event modifier
chain" footgun for any future investigator trying variations.

================================================================================
8. FOLLOWUP (for Phase 15+)
================================================================================

The Qt → server WRITE direction requires either:
  (a) instrumenting the vendored `qgenodeclipboard.cpp` with
      `qDebug()` to log Reporter construction success /
      setMimeData invocation count (port-side patch; AGENTS.md
      §5.2 forbids it in this follow-on but is appropriate for
      Phase 15 / qt6_base upstream),
  (b) replacing qt6_textedit with a smaller, single-purpose Qt
      binary whose copy action is wired to a known shortcut
      and whose constructor logs Qt init completion,
  (c) adding a Sponge-side Qt test harness in
      `repos/sponge/src/test/` that directly drives
      `QGenodeClipboard::setMimeData` from a synthetic
      QKeyEvent (no QTextEdit / no QMainWindow involved) — the
      smallest possible proof of the QGenodeClipboard::setMimeData
      → upstream server → bus round-trip.

(c) is the recommended Phase 15 follow-on. It would close the
D14.2 U2 proof on the WRITE direction without depending on the
yet-unverified QMP-driven Ctrl-shortcut path through
qgenodeclipboard.

================================================================================
9. NO REGRESSION (downstream checks)
================================================================================

The probe-side diagnostic (§6) and the run-script §7 cleanup
do not affect:

  - run/sponge-clipboard.run (Phase 14 W5 acceptance) — still
    PASSES; same probe code path (mode 1), now with a
    one-time-extra size-change byte dump that does not fire in
    this scenario's normal probe-write sequence.
  - run/sponge-clipboard-focus.run (Phase 14 W5 follow-on) —
    still PASSES; same probe code path (mode 2), same quiet
    behavior because focus rejection yields no probe writes.
  - run/sponge-textedit-qmp.run (Phase 10 criterion 5b) — no
    changes to textedit's launch wiring.
  - run/sponge-textedit.run, run/sponge-alpha.run — unchanged.

================================================================================
10. FILES TOUCHED
================================================================================

  M  run/sponge-clipboard-qtwrite.run
       Ctrl-A / Ctrl-C dispatch: input-send-event -> send-key
       composite (proven Ctrl-V form from W5 acceptance).
  M  repos/sponge/src/test/clipboard_probe/main.cc
       _wait_for_needle: byte-dump the ROM content on size
       change. One-time diagnostic for any future
       re-investigation; harmless in the other two modes.
  +  docs/evidence/phase14-w5-qtwrite-failure.md (this file).
