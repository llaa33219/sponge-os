# Phase 16 P2.6 — Browser acceptance: falkon placement rule + basic typed input

Date: 2026-10-31. QEMU/seL4 (`KERNEL=sel4 BOARD=pc`) receipts only.
No commit was made. This document closes the two open items of
`phase16-p26-browser-staging.md` (§7, input calibration pending) and
`phase16-p27-qtwebengine-shm-port.md` §6.4 (the `grouped_ui_probe`
main.cc:805 placement failure): the falkon frame geometry is recovered
from window_list/window_layout runtime evidence, a falkon-specific
assign rule sized to that frame lands before the generic
`pkg_runtime` rule on all five product scenarios, and the browser
leg's focus/typing geometry is runtime-calibrated from the evidence
run instead of assuming a URL-bar position.

## 1. Outcome summary

| item | status |
|---|---|
| falkon decorated frame recovered from window_list/window_layout | **787x569 client** (both reports agree; §2) |
| falkon-specific assign rule before the generic `pkg_runtime` rule | **done** — `xpos: 5 | ypos: 80 | width: 787 | height: 569`, 5 scenarios x 2 rule sites (§4) |
| textedit 512x493 rule + all existing behavior preserved | **done** — unchanged, verified in the acceptance run (§3) |
| grouped BIOS: browser first paint + class placement check (c) | **PASS** — `rect=(5,108 787x569)`, center (398,392) inside WIN_REG (§3) |
| grouped BIOS: browser basic typed input | **PASS** — `changed=9845 baseline=0 typed="sponge"` at the calibrated URL-bar geometry (§3) |
| P2.4 regression (default BIOS path) | **GREEN** (§5; one documented boot-flake receipt) |
| P2.5 regression (`sponge-de-pkgd-failure`) | **GREEN**, 7 `qmp: dispatching` lines (§5; two harness-race receipts, root-caused) |

## 2. Exact geometry record (the evidence, not a guess)

Evidence run: `/tmp/opencode/p29-runE2.log` (the probe carried
temporary registration/pmap diagnostics that were removed afterwards;
the run's own PASS marker is therefore not an acceptance marker — the
acceptance run is §3). The probe dumped each window's window_list
`width`/`height` and its window_layout rect at registration:

| window | window_list size | window_layout rect (screen) | verdict |
|---|---|---|---|
| textedit (id=2) | 512x493 | (50,108) 512x493 | sanity: matches the 512x493 rule (rule y=80 + the 28px target origin) |
| terminal (id=3) | 800x600 -> 320x240 | (50,328) 800x600 at registration | transient: the gems terminal honors the generic rule's 320x240 resize after mapping |
| **falkon (id=4)** | **787x569** | **(50,328) 787x569** | **stable** — unchanged through the typed-input minutes later |

Falkon's 787x569 is its *natural* client size: under the generic
rule's 320x240 resize request the client stayed 787x569 from first
sight through typing (the typed-sample bounding box spanned the full
787px width), i.e. QtWebEngine's main window clamps every smaller
request up to 787x569. window_list and window_layout agree byte-wise
(wm's `Window::Attr::area` and the layouter's `_client_size` are the
same client size), so the rule size 787x569 makes the assign request
equal the client size — no resize round-trip at placement time.

Report-rect semantics (verified against the textedit/terminal pairs):
the window_layout `xpos/ypos` are screen-absolute and equal
`rule_pos + target_origin`, where the `screen` target origin is
(0,28) — the 28px panel band (decorator margins are separate:
the stock decorator's floating margins are top 20 / bottom 4 /
left 4 / right 4, `window.h` `_border_size=4`, `_title_height=16`).

Rendered chrome layout (luminance map of the typed state, evidence
run; client-relative rows): tab bar rows 0..28 (tab-title text at
rows 18..24), navigation bar rows 28..60 (URL field light-gray band
around x=264..784 with the text row at 36..48), bookmarks bar rows
60..88 (default bookmark labels at rows 66..78), web view below
(white). The typed input's changed-sample bbox was (50,356)..(834,556)
= client rows 28..228 full width = the URL-bar row plus the
completer-popup zone.

## 3. Acceptance run (the receipts)

Run V: `timeout 1800s env SPONGE_GROUPED_UI=on
LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so make -j1 -C
genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc
RUN_OPT="--include power_on/qemu --include log/qemu --include
boot_dir/sel4 --include image/disk"` → `/tmp/opencode/p29-runV.log`
(6217 lines, `make` exit 0).

- simple-class (textedit): `window id=2 rect=(50,108 512x493)
  changed=3769` — first paint, then `typed delta (changed=133
  baseline=0 typed="hello")`. The 512x493 rule is untouched.
- terminal-class: `window id=3 rect=(50,328 320x240) changed=4515` —
  the generic rule's placement unchanged; `typed delta (changed=17
  baseline=0 typed="echo grouped-ok")` + `execution delta` green.
- browser-class (falkon): `window id=4 rect=(5,108 787x569)
  changed=2279` — **first paint with the class placement check (c)
  active** (center (398,392) inside WIN_REG (40,280) 360x280 —
  margins 2px horizontally, 168px vertically). Then the
  runtime-calibrated geometry log `url-bar geometry (click=398,145
  client=(393,37) band=(5,136 787x200))` and `typed delta (changed=9845
  baseline=0 click_changed=990 typed="sponge")`.
- Gate: `grouped-ui-probe: PASS` + `sponge-desktop-disk: GROUPED UI
  PASS` + `Run script execution successful.`

The rule's prediction matched the observed placement exactly: rule
(xpos 5, ypos 80, 787x569) -> report rect (5,108) 787x569 ->
center (398,392) -> inside WIN_REG; decorated frame (1,88) 795x593
fully on the 1024x768 screen (bottom 681 < 768).

## 4. Files changed (this task's delta)

| file | change |
|---|---|
| `repos/sponge/src/test/grouped_ui_probe/main.cc` | browser leg: `URL_CLICK_DX/DY = 393/37` + `URL_BAND_DY/DH = 28/200` client-pixel calibration (replacing the 400x300-normalized (200,20) assumption), `url_bar` mode in `_run_class`, geometry evidence log line; temporary registration/pmap/bypass diagnostics were added for the evidence run and fully removed afterwards (`git grep TEMP-DIAG` = 0 hits) |
| `run/sponge-desktop-disk.run` | layouter `<rules>` + sponge-de `tasklist_static_rules`: falkon assign `label_prefix: pkg_runtime_browser -> falkon | xpos: 5 | ypos: 80 | width: 787 | height: 569` between the textedit rule and the generic `pkg_runtime` rule (both sites) |
| `run/sponge-desktop-disk-{nvme,uefi,uefi-nvme,uefi-usb}.run` | the same two rule insertions per file (cross-media consistency; structurally checked via the p26 tclsh harness: `assertions + HID schema OK` x4) |
| `docs/evidence/phase16-p26-browser-acceptance.md` | this record |
| `docs/evidence/phase16-p26-browser-staging.md`, `phase16-p27-qtwebengine-shm-port.md` | stale BLOCKED/next-blocker status lines updated to point here |

Rule-ordering contract kept: the layouter retains the FIRST matching
assign per window and the tasklist's composed rules emit per-window
exact assigns first, then `tasklist_static_rules` — so the falkon
rule must precede the generic `pkg_runtime` rule in BOTH homes (as
the textedit rule does). The legacy-needle assertion
`label_prefix: pkg_runtime -> falkon` (runtime-grouped.inc
assert_wiring, the old rom_pkg payload rule) is not a substring of
the new rule's `pkg_runtime_browser -> falkon`, so the structural
assertions stay green.

## 5. Regression receipts

**P2.4 (default BIOS path, `SPONGE_GROUPED_UI` off).** Command:
`timeout 600s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so make
-j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4
BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include
boot_dir/sel4 --include image/disk"`. First attempt
(`/tmp/opencode/p29-runP24.log`) hit the documented pre-existing boot
flake (`sponge-de: stop because parent denied ROM-session:
label="theme"` -> `create_thread: invalid PD argument` storm, 594
null-cap invocations, `alpha-probe: FAIL themed panel never
composited` — the p27 §7 H1/H2 signature exactly). Re-run
`/tmp/opencode/p29-runP24b.log`: **GREEN** —
`alpha-probe: (a) panel band rendered at (512,4) = 0xff1e1e2e`
(identical to the p27 baseline pixel), `alpha-probe: PASS`,
`Run script execution successful.`

**P2.5 (`sponge-de-pkgd-failure`, ISO media path).** Command:
`timeout 420s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so make
-j1 -C genode/build/x86_64 run/sponge-de-pkgd-failure KERNEL=sel4
BOARD=pc`. Attempts 1 and 2 (`/tmp/opencode/p29-runP25.log`,
`p29-runP25b.log`) failed `de-survival-probe: FAIL popup did not open
after the S click` with **0** `qmp: dispatching` lines. Root cause,
from the marker/spawn timeline of both logs vs the green p27 baseline:
the scenario's long-lived boot runs a `run_genode_until {... Connected
device ... POINTER}` gate (before `qmp_connect`, default match
window) whose expect scan races the probe's `[p1] QMP-TARGET entry`
marker. When usb_hid's `Connected device ... POINTER` line arrives
AFTER the marker (green baseline: POINTER at :11945, before
`[p2]`/`[c1]`/marker; red runs: marker :11954 then POINTER :11955),
the scan consumes the marker, `dispatch_sequence` never dispatches,
and the probe's 20 s popup wait fails. This is the pre-existing
host-timing harness race of the P2.5 scenario (p26 needed four
attempts at it: F1-F3 red, F4 green); the P2.5 ISO shares no file
with this task's delta. Attempt 3
(`/tmp/opencode/p29-runP25c.log`): **GREEN** —
`de-survival-probe: scenario GREEN (P2.5 mechanisms present)` with 7
`qmp: dispatching` lines (the documented green-baseline count).

## 6. Honest boundaries

- QEMU/seL4 receipts only; no page-load / DHCP / HTTP assertion (criterion 15 follow-up stays open).
- The four non-BIOS desktop-disk variants carry the same rule insertions but were verified **structurally** (tclsh pipeline + HID schema), not booted here.
- The terminal 5-second-flush flake (p26 §7) remains in the recipe, arms untouched; the acceptance run passed it on the first try.
- The P2.5 POINTER/marker harness race (§5) remains in that scenario; fixing it is a P2.5-lane change (out of scope here), and its receipts pattern (re-run until green) is unchanged.
- The run tool's expect teardown truncates a gate-matched line at the marker boundary (visible as `first paint (la` / `FAIL windo` tails in the logs); the geometry-bearing lines (`window id= ... rect=`, `url-bar geometry`, `typed delta`) are complete in every receipt.

## 7. Artifacts

- `/tmp/opencode/p29-runE.log` — evidence attempt 1, died at the documented terminal flake (`changed=30 baseline=17`)
- `/tmp/opencode/p29-runE2.log` — **evidence run** (temp diagnostics; falkon 787x569 + chrome map + typed bbox)
- `/tmp/opencode/p29-runV.log` — **acceptance run** (§3)
- `/tmp/opencode/p29-runP24.log` / `p29-runP24b.log` — P2.4 flake receipt / GREEN
- `/tmp/opencode/p29-runP25.log` / `p25b` / `p25c` — P2.5 two race receipts / GREEN

## 8. Independent visual QA

- Capture: `docs/evidence/phase16-p26-browser-acceptance/falkon.png`
  (1024x768 RGB), produced after the browser first-paint and typed-input
  gates in `/tmp/opencode/falkon-final-run.log`. The image shows the
  complete native Falkon window with no black, missing, or torn regions.
- Both independent visual reviewers returned **PASS / HIGH confidence**
  with no blocking findings: real Qt/WebEngine component tree, coherent
  title/tab/navigation/URL/webview rendering, and coherent input evidence.
- The transient typed text is not required to remain in the URL bar;
  falkon restores its packaged startup URL before the capture. The blank
  web view remains the declared page-load follow-up, not a paint defect.
