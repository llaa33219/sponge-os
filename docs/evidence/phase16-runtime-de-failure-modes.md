# Phase 16 P0.4 — sponge-de failure modes under package-control failure

Task: **P0.4 DE failure-mode audit** (`docs/plans/phase16-runtime-overhaul.md`).
Scope: sections 1–6 are the code audit; section 7 is the verified P2.1
runtime RED receipt. Date: 2026-10-28. No real-hardware claim is made or
implied (D16R.12).

Purpose: enumerate what sponge-de does today when package control
(`sponge_pkgd`) hangs, dies, times out, goes stale, fails an operation, or
is starved by storage, and record the **testable target degraded-state
contract** that P2.1 (red test `run/sponge-de-pkgd-failure.run`,
marker `de-survival-probe: PASS`) and P2.5 (DE hardening) must implement
per D16R.7: *the DE stays interactive and labeled; package-control
failures are contained to the operation.* The goal is **independent
liveness, not hiding the launcher failure** — the degraded state must
remain visible, never masked.

---

## 1. Dependency map: direct DE→pkgd vs shared substrate

### 1.1 Direct DE→pkgd dependencies (the surface P2.5 owns)

| Dependency | Transport | DE-side entry point |
|---|---|---|
| Installed-set feed | pkgd `installed` report → report_rom → ROM `installed` | `LauncherController::_installed_rom`, polled at 1500 ms (`repos/sponge/src/sponge-de/launcher/launcher_controller.cc:138`, `:158-164`, `:168-181`) |
| Launch request | Report `launcher_request` (`op="launch"` written at `launcher_controller.cc:413-416`) | `LauncherController::request_launch` (`:383-423`) |
| Launch result | pkgd `launcher_result` report → ROM `launcher_result` | non-blocking poll `_poll_launch_result` (`launcher_controller.cc:498-545`) |
| Liveness surface | pkgd heartbeat skips in the `system` sandbox state report → ROM `system_state` | health watcher `health_refresh` (`repos/sponge/src/sponge-de/main.cc:625-691`) |

pkgd-side counterparts (for context): result channels `result` /
`launcher_result` / `tty_result` / `probe_result`
(`repos/sponge/src/sponge_pkgd/main.cc:227/244/254/264`, routing `:841-849`);
launch outcomes `not-installed`, `already-running`, exclusive conflict,
`binary-missing`, `insufficient-resources`, `ok`
(`sponge_pkgd/main.cc:1837-1932`, result shape `_report_launch_ok`
`:2580-2599`); duplicate-request replay so a polling client always gets an
answer while pkgd is alive (`sponge_pkgd/main.cc:397-428`, comment at
`:408-409`, `_replay_last_result` `:2683-2756`).

### 1.2 Shared substrate (NOT a DE→pkgd dependency — out of P2.5 scope)

| Substrate | Why it is shared | Failure signature |
|---|---|---|
| System entrypoint / heartbeat machinery | init's heartbeat restarts re-create any wedged child; pre-`restart_budget` this cycled forever and froze the whole desktop | 2026-10-17 field freeze: `boot -> desktop -> sponge_pkgd unresponsive(1) -> total freeze, no console takeover` (`docs/11-environment.md` ledger row #24, `:219`); bounded now by `restart_after_skipped: 3 | restart_budget: 3` (`run/sponge-desktop-disk.run:662`, `:684`, `:717`, `:735`) |
| Forwarded create chain | a parked session create blocks the container entrypoint that also serves heartbeats — everything freezes, DE included | `docs/evidence/forwarded-create-starvation.md`; "the isolation model held, but liveness did not" (`docs/17-runtime-component-topology.md:9-11`) |
| report_rom relay | `installed`, `launcher_result`, `system_state` all cross report_rom (`run/sponge-desktop-disk.run:829`, `:1092`, `:1365`); a denied ROM session **stops** a component (`repos/sponge/src/sponge-de/rom/optional_rom_dataspace.h:6-13`) | component death at session request, or frozen feed when the relay/writer stalls |
| Storage chain | pkgd does package-store I/O; log/package-store providers share paths (FD4) | pkgd entrypoint blocks → heartbeat skips → looks exactly like mode A/F below (`sponge_pkgd/main.cc:558-565` documents the slow-storage heartbeat effect) |

A frozen `system` state report (shared substrate) freezes the health label
at its last text: `health_refresh` returns early on an invalid/empty ROM
and never marks staleness (`main.cc:636-641`). The red test must not
conflate "label stuck" with "DE alive".

---

## 2. Existing bounded machinery and exclusions (red-test constraints)

### 2.1 Bounded polling that already exists — the red test may rely on it

| Mechanism | Bound | Reference |
|---|---|---|
| Installed-ROM poll | 1500 ms QTimer, non-blocking | `launcher_controller.cc:158-164` |
| Launch-result poll | 100 ms singleShot × `LAUNCH_POLL_MAX` 60 = **~6 s**, then `launch_problem` | `launcher_controller.cc:498-545`, `launcher_controller.h:242` |
| Launch-channel open retry | at most once per `LAUNCH_CHANNEL_RETRY_MS` 5000 ms | `launcher_controller.cc:329-380`, `launcher_controller.h:222` |
| Health watcher | 1500 ms QTimer over `system_state` | `main.cc:686-690` |
| Diag readouts | 1000 ms QTimer | `main.cc:738-740` |
| Settings write (configd, not pkgd) | synchronous but bounded ~6 s (200 ms settle + 60×100 ms), then structured failure | `repos/sponge/include/sponge/backend_client.h:26-31`, `repos/sponge/src/sponge-de/config/settings_controller.cc:152-168` |
| Backend `ReportRomClient::request` (install/remove/list) | same ~6 s bound; **vct-side only**, sponge-de must not call it on the GUI thread | `backend_client.h:25-31`, `:93-100` |
| Supervisor fault scan | 250 ms tick, `fault_skipped` 3 | `repos/sponge/src/sponge_supervisor/main.cc:203-204`, `:581-587`, `:863`; `run/supervisor.inc:131-136` |

### 2.2 Health-watcher exclusions — the red test must not assert past them

- The DE health watcher suppresses **clean exits of `alpha_probe` and
  `cfg_probe`** (boot verifiers): `if (code != 0 || !probe)` before adding
  `exited(N)` (`main.cc:652-665`). Non-zero exits always warn; any other
  child's clean exit (`exited(0)`) **does** warn.
- The supervisor mirrors the pair via `ignore_exited "alpha_probe,cfg_probe"`
  (`run/supervisor.inc:110`, parsed `sponge_supervisor/main.cc:341-344`,
  matcher `:485-521`, applied `:570`). The two lists must stay in agreement
  (`sponge_supervisor/main.cc:212-218`, `supervisor.inc:95-109`).
- Consequence for P2.1: a new probe child whose clean exit appears in the
  `system_state` report will render as `exited(0)` on the panel health
  label (warnings joined with ` · `, `main.cc:659-660` and `:671-672`).
  Either the probe
  is added to **both** exclusion lists or the label assertions must match
  substrings (e.g. `sponge_pkgd unresponsive(`), never whole-label text.
- The label format is composed in exactly one place:
  `<name> unresponsive(<skipped_heartbeats>)` at `main.cc:669-677` (the
  `(` at `:675`), rendered `⚠ <text>` by `PanelWidget`
  (`panel/panel_widget.cc:463-473`).
- The health watcher only runs when `system_state` is routed
  (`Optional_rom_dataspace` tolerant open, `main.cc:614-627`; routing
  exists only in the product scenarios, e.g.
  `run/sponge-desktop-disk.run:1092`/`:1365`). The failure scenario must
  route `system_state` or contract item C3 is unobservable.

### 2.3 Mechanisms the red test must NOT assert (impossible or out of scope)

1. **DE-side install/remove results** — the DE's only pkgd writer op today
   is `launch` (`launcher_controller.cc:413-416`). `install`/`remove` go
   through vct's `ReportRomClient::request` (`backend_client.h:93-100`).
   Assert C4 on the launch op (or on an op P2.5 actually adds), never on
   an install the DE cannot issue.
2. **Zero GUI-thread blocking globally** — the settings (configd) write is
   synchronous-by-design on the GUI thread (`settings_controller.cc:152-168`);
   the pkgd paths (ROM polls, report write + timer poll) are the non-blocking
   ones. Scope any "never blocks" assertion to the pkgd package-control path.
3. **Storage recovery** — containing storage stalls is P3 (FD4,
   `provider-isolation-probe`); P2.5 owns only the DE's reaction to the
   pkgd-level symptoms.
4. **Survival when the shared substrate freezes** — a parked system
   entrypoint (forwarded create) or a dead report_rom freezes the panel and
   input regardless of DE code; that is FD1/P3 territory. The wedge
   mechanism must isolate the DE→pkgd dependency (wedge/kill the pkgd child
   only, with the heartbeat restart machinery accounted for) or the P2.5
   gate is unflippable.
5. **Staleness detection before it exists** — asserting "stale feed is
   labeled" is part of the *red*; today nothing detects staleness (§3 D).
   The red receipt must show it failing, not assume it.

---

## 3. Failure-mode table — current vs target

Current-behavior references are exact file:line. "Target" is the §4
contract item(s) P2.5 implements.

| # | Mode | Current DE behavior (evidence) | Target (contract) |
|---|---|---|---|
| A | **pkgd hang** (entrypoint wedged, e.g. blocked on storage mid-operation) | The DE event loop does not block on pkgd: feed is a ROM poll (`launcher_controller.cc:158-181`), launch is report + timer poll (`:383-423`, `:498-545`). Health label `⚠ sponge_pkgd unresponsive(N)` from `system_state` `skipped_heartbeats` (`main.cc:634-677`), refreshed 1500 ms (`:686-690`). `installed` freezes at last content; launcher list stays **stale with no staleness marker** (`launcher_controller.cc:175-192`). A click during the hang times out at ~6 s → `launch_problem("no answer from the package manager")` (`:508-509`, `:540-541`) → panel `⚠` message for 10 s (`main.cc:703-723`, TTL `panel_widget.h:256`) then back to the health text. Substrate caveat: heartbeat restarts churn the child (row #24 mechanism); with `restart_budget` exhausted pkgd is left stopped and the escalation hand-off is the config owner's (ledger row #24). | C1 C2 C3 C4 C5 |
| B | **pkgd crash / respawn** | Non-clean exit surfaces as `<name> exited(code)` on the health label (`main.cc:655-665`). During the gap `installed` keeps its last content and the menu keeps the last list (`launcher_controller.cc:175-192`); no respawn-specific notice. In-flight launch results die with the process (the replay recipe lives inside the process, `sponge_pkgd/main.cc:397-428`) → client ~6 s timeout path. After republication the next ≤1500 ms poll re-parses; changed set → `appsChanged` + menu repopulate + `package installed` notification for new names (`launcher_controller.cc:252-273`); health label clears once heartbeats resume. | C3 C4 C5 |
| C | **Launcher result timeout** | Fully bounded and non-blocking today: 100 ms × 60 ≈ 6 s (`launcher_controller.h:242`, `cc:498-545`), then `launch_problem` per failure status: `not-installed` (`:559-563`), `binary-missing` (`:564-568`), `already-running` (`:569-573`), `insufficient-resources` with need/avail RAM+caps numbers (`:574-604`), raw status fallback (`:605-608`), timeout `"no answer from the package manager"` (`:508-509`, `:540-541`), channel failure `"package manager unreachable"` (`:362-363`, `:371-372`). Surfaced on the panel (`main.cc:703-723`) and logged `sponge-de: launch problem surfaced on the panel: ...` (`main.cc:720`). Gap: the structured error is transient (10 s TTL) and log-only as a machine witness; no persistent operation-error channel. | C4 (generalize + make machine-checkable) |
| D | **`installed` ROM absence / staleness** | Opt-in gate `<launcher source="pkgd"/>` (`launcher_controller.cc:25-62`); absent → launcher disabled gracefully, panel reports "not attached" (`:130-136`). **Opted in but route missing → component death**: `_installed_rom.construct(_env, "installed")` is a plain `Attached_rom_dataspace` (`:138`), and a denied session stops the component (`optional_rom_dataspace.h:6-13`) — the health watcher already avoids exactly this with `Optional_rom_dataspace` (`main.cc:431-437`, `:625-627`). Staleness: no freshness check; invalid ROM → previous list kept (`launcher_controller.cc:175-192`); `config_writes` counter is a bring-up readout only (`launcher_controller.cc:184-187`, `main.cc:454-466`). | C3 (labeled feed state), C5, absence non-fatal |
| E | **Package operation failure** (structured error, not hang) | pkgd emits `<result status op pkg .../>` (`sponge_pkgd/main.cc:2580-2599`) incl. `_report_error` paths; the DE maps launch statuses to structured panel errors with resource numbers (`launcher_controller.cc:547-608`) and configd's `status="error"` to `request_failed` → `QMessageBox` (`settings_controller.cc:174-210`, `settings/settings_dialog.cc:84-89`). The DE has **no install/remove writer** (vct owns those, `backend_client.h:93-100`); install outcomes are observed only via the `installed` delta + notification (`launcher_controller.cc:264-273`). | C4 (every DE package-control op surfaces a structured error; never silent) |
| F | **Storage-driven unresponsiveness** (shared substrate manifesting as A) | pkgd moved storage work off its constructor so slow storage stops skipping heartbeats — when it still blocks, "the sandbox heartbeat counts skips, and pkgd is abandoned and respawned - repeatedly" (`sponge_pkgd/main.cc:558-565`); that cycle is the field freeze (ledger row #24, `docs/11-environment.md:219`), now bounded by `restart_budget: 3`. DE-side manifestation is identical to mode A. The DE cannot and should not detect storage directly. | C1-C5 at the pkgd-symptom level; storage containment is P3 |

---

## 4. Target degraded-state contract (P2.1 red / P2.5 green)

Applies while `sponge_pkgd` is wedged or dead **and the shared substrate
(§1.2) is healthy**. Each item is written as a scenario-assertable
observable; where today's code cannot yet witness it, the gap is stated so
the P2.1 RED receipt lands on the real missing mechanism.

- **C1 — Panel renders (and keeps rendering).** The panel (and launcher
  popup) reach first paint and continue repainting through the degraded
  window. Witness: Capture/pixel check, per D16R.11 (first paint plus one
  real input event — not a boot marker).
- **C2 — QMP input reaches the DE.** A QMP-injected click or keystroke
  during the degraded window produces a measurable state change (e.g. the
  `launcher_state` report flips `open="no" -> "yes"`,
  `launcher_controller.cc:437-455`, opt-in `<launcher_state source="report"/>`
  `:57-100`; or the demo `input` report flips,
  `sponge_de_main.cc:63-72`). Harness pattern exists:
  `run/sponge-de-sel4-interactive.run` (QMP input path + `input` report
  relay `:157-159`, pixel check).
- **C3 — Health label appears.** `⚠ sponge_pkgd unresponsive(N)` (format
  `main.cc:669-677`) shows on the panel while heartbeat skips accumulate;
  `exited(code)` replaces/preceded it across a crash (mode B). Requires
  `system_state` routing (§2.2). Assert by substring, and tolerate sibling
  segments joined with ` · ` (`main.cc:659-660`) — especially the probe's
  own `exited(0)` unless it is excluded in both lists.
- **C4 — Structured operation error.** Every package-control operation the
  DE performs during the degraded window terminates in a bounded (~6 s) ,
  structured error — never a hang and never silence. Machine witness (the
  gap): today only the log line `sponge-de: launch problem surfaced on the
  panel: ...` (`main.cc:720`) plus the transient panel message; P2.5 must
  expose the error as a stable, probe-readable artifact (report or
  persistent panel state) so `de-survival-probe` can assert it without
  pixel-scraping, and must cover install/remove errors for any op it adds
  (mirroring the `status="error"` pattern,
  `settings_controller.cc:189-210`).
- **C5 — Recovery on `installed` republication.** When pkgd revives and
  republishes `installed`, the launcher feed is live again within ≤ 2 poll
  periods (≤ 3 s at the 1500 ms cadence): `sponge-de: launcher list updated
  (...)` (`launcher_controller.cc:259`), menu repopulated (`:252-273`), the
  health label clears when heartbeats resume, and a subsequent launch
  reaches `sponge-de: launched '...'` (`launcher_controller.cc:558`) or a
  structured non-`ok` result. Recovery must be the *feed's* recovery — the
  failed operation's error stays visible until acknowledged (independent
  liveness ≠ hiding the failure).

Expected red/green split under a **substrate-clean** pkgd wedge (guidance
for the P2.1 receipt): C1/C2 are believed already-green (the pkgd paths are
non-blocking); C3 is green only where `system_state` is routed and the label
is asserted by substring; C4 is partial (launch-only, transient, log-only
witness); C5 is implicit-only (no explicit degraded/recovered state, no
machine witness). The scenario is red if **any** of C3-C5 fails; the red
receipt must state which item failed.

---

## 5. Positive and negative controls for `run/sponge-de-pkgd-failure.run`

### 5.1 Positive controls (harness must demonstrably work)

| ID | Control | Observable |
|---|---|---|
| P1 | Pre-wedge launch click over the real input path | `sponge-de: launched '<pkg>'` (`launcher_controller.cc:558`) or pkgd `launch ... -> ok`; proves QMP click → request → result before the wedge |
| P2 | Pre-wedge feed is live | `sponge-de: launcher list updated (N apps)` (`launcher_controller.cc:259`) |
| P3 | Pre-wedge health label is absent | panel health label hidden while `_health_text` is empty (`panel_widget.cc:468-470`) — so a later label appearance is wedge-induced |
| P4 | Post-recovery launch click | `ok` again, within the C5 window — the recovery half of the contract |
| P5 | QMP input witness during the degraded window | `launcher_state` flip or `input` report activity (C2), harness per `run/sponge-de-sel4-interactive.run` |

### 5.2 Negative controls (must NOT fire — false-green guards)

| ID | Control | Why |
|---|---|---|
| N1 | `de-survival-probe: PASS` absent before P2.5 | the P2.1 red gate (plan §P2.1) |
| N2 | The wedge touches only pkgd — `system_state` keeps updating and the panel keeps compositing | a frozen substrate (§2.3 item 4) or frozen state report (§1.2) must not masquerade as "label appeared" or "label stuck"; witness: state-report cadence markers or a bystander child still emitting |
| N3 | No `exited(0)`/`unresponsive` warnings asserted for `alpha_probe`/`cfg_probe` | excluded pairs (`main.cc:652-665`, `supervisor.inc:110`) |
| N4 | No install/remove result asserted on the DE's `launcher_request` channel | the DE only writes `op="launch"` (`launcher_controller.cc:413-416`); install/remove belong to vct/probe channels (`backend_client.h:93-100`) |
| N5 | No assertion requires pkgd to answer while wedged | the ~6 s timeout + structured error **is** the designed degraded outcome (§2.1) |
| N6 | No whole-label exact-text matches on the health surface | label is a ` · `-joined aggregate (`main.cc:659-660`) polluted by the probe's own `exited(0)` (§2.2) |

Run discipline for the future scenario (recorded here so P2.1 inherits it):
serial runner only (`./tool/build run`, D16R.13); `lib/ld` +
`build_boot_image [build_artifacts]` + bounded `run_genode_until` per
`docs/08-development.md` §4; QEMU-labeled receipts only (D16R.12);
first-paint-plus-input acceptance (D16R.11).

---

## 6. Open questions

- **Q1 (probe exclusion).** Should the `de-survival` probe join the
  `alpha_probe`/`cfg_probe` exclusion pair (touching both
  `main.cc:652-654` and `supervisor.inc:110`), or should the health
  assertions tolerate its `exited(0)` segment? P2.1 decides; until then
  N6 applies.
- **Q2 (state after `restart_budget` exhaustion).** Ledger row #24 leaves
  the child "stopped, escalation is up to whoever owns the config". The
  exact `system_state` shape in that state (does the node keep `exited`,
  disappear, or freeze?) determines whether C3 shows `exited(N)`,
  `unresponsive(N)`, or nothing after exhaustion. Must be settled by
  observation in the P2.1 red run, not assumed here.
- **Q3 (operation-error channel).** C4's machine-readable witness (report
  label vs persistent panel state) is a P2.5 design choice; P2.1's probe
  should assert against the log marker first and switch when the channel
  lands, so the red receipt stays comparable.

---

## 7. P2.1 runtime RED receipt (2026-10-28)

`run/sponge-de-pkgd-failure.run` was executed serially on base-sel4/QEMU
with the stock QEMU spawn path. The host's 8 MiB memlock limit required a
non-committed io_uring queue-depth clamp for QEMU 11.1.2; no shim or other
host workaround is part of the scenario. This is QEMU evidence only.

The probe completed launcher launch → first paint → real QMP input in both
healthy and degraded windows and emitted `de-survival-probe: verdict RED
(c4 c5)`. The scenario exited nonzero intentionally and printed the P2.1
receipt; `de-survival-probe: PASS` was absent (N1).

- **Green:** C1 first paint and continued compositing, C2 degraded-window
  input, C3 health-label substring witness, and all positive controls
  P1–P5. The receipt counted two pkgd launch results, two `pkg_gui_demo`
  first paints, and five launcher-feed updates.
- **Intentional RED:** C4 lacks the stable, probe-readable operation-error
  artifact, and C5 lacks error persistence until acknowledged. The bounded
  human-readable `launch 'hello' failed: no answer from the package
  manager` error did surface, so the RED is limited to the documented
  missing mechanisms rather than a frozen DE or a broken harness.
- **Q2 observed:** restart churn settled; the pkgd node disappeared after
  the restart budget was exhausted (about 16.7 s in the final run).
- **Q3 remains open for P2.5:** `de_op_error` is still empty, which is the
  explicit C4/C5 RED witness the hardening change must flip.

---

## 8. P2.5 runtime GREEN receipt (2026-10-30)

`run/sponge-de-pkgd-failure.run` now reaches `de-survival-probe: PASS` and
`scenario GREEN (P2.5 mechanisms present)` on base-sel4/QEMU with no
scenario or probe edit. Serial log: `/tmp/opencode/pkgd-failure-run2.log`.

- **C4 machine witness:** the report_rom log contains
  `<op_error op="launch" pkg="hello" reason="no answer from the package manager"/>`,
  followed by `de-survival-probe: [c4] ok - operation-error machine witness
  present (de_op_error populated)`.
- **C5 persistence:** the record remains after provider recovery and the
  post-recovery launch, producing `de-survival-probe: [c5] ok - operation
  error still visible after recovery (persists until acknowledged)`.
- **Health recovery:** the independently derived health warnings clear on
  revival while the error record persists, satisfying both halves of C5.
- **Regression:** `sponge-de-test` reports `sponge-de-probe: PASS` and
  `sponge-launch` reports `launch-probe: PASS`, each with
  `Run script execution successful.`
- **Harness note:** an earlier attempt stopped at P1 when the pre-existing
  USB-tablet enumeration gate consumed the entry marker before dispatch.
  The following run completed without that harness race; no product or
  scenario behavior was changed to bypass it.
