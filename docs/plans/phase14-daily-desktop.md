# Phase 14 — Sponge DE as a Daily-Usable Desktop (Work Plan)

> Status: active. Created 2026-08-11.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 14* (lines 652–668).
> Prior phase plan: `docs/plans/phase13-package-ecosystem.md`.
> Input evidence: two thorough exploration passes on 2026-08-11
> (a) vendored Genode 26.05 clipboard/notifications survey +
> `docs/11-environment.md` §4 patch-ledger audit, (b) sponge-de
> source module + run-scenario inventory + Phase 10–13 carryover
> walk. The two immediate operational inputs are
> `docs/evidence/phase10-index.md` (QMP-driven input stack — the
> proving ground for every Phase 14 input scenario) and
> `docs/evidence/phase11-index.md` (live-theme + themed-chrome —
> the foundation the window-management work builds on).

---

## Goal Restatement (`docs/09-roadmap.md` §10, Phase 14, verbatim)

> Sponge DE can serve as a primary working environment for everyday
> tasks — not a demo, but a desktop one can actually sit down and use.

Four completion criteria (verbatim):

1. **Session stability**: the desktop runs extended interactive sessions
   without component crashes or resource exhaustion.
2. **Core desktop services complete**: notifications, clipboard,
   sensible window management defaults (focus model, raising,
   minimizing).
3. **Everyday workflow proven end-to-end in scenario**: boot → launch
   terminal/editor/files/browser → do real work → shut down cleanly.
4. **Remaining paper cuts from Phases 10–13 resolved**.

---

## Recorded Scope Rulings (U1–U6, decided by the user up-front)

These six rulings constrain the plan; they are not reopened here.
The plan is consistent with each one and explicitly cites the ruling
where it applies.

- **U1 — Browser.** ATTEMPT a Falkon rescue on seL4 within a documented
  effort bound (D14.5). If the rescue fails inside the bound, explicitly
  AMEND the Phase 14 criterion wording and record why; no silent
  substitution (the everyday-workflow scenario may use the existing
  files/textedit/terminal/calculator/pdf_view set if the browser stays
  blocked).
- **U2 — Clipboard is cross-component.** A capability-based clipboard
  service; the workflow scenario must prove copy in one separately-
  launched component, paste in another (separate address spaces).
  Qt-local intra-process clipboard does not count.
- **U3 — Window management is real minimize+restore.** Real minimize +
  restore semantics, including a deterministic restoration path (the
  panel task list). Plus focus / raise / close / maximize defaults. A
  decorative minimize button with no restore is forbidden.
- **U4 — Stability is a workload loop with a time floor.** Repeated
  launch / focus / clipboard / notification / theme-reload cycles for
  a defined duration (suggested 30 min wall-clock on QEMU) plus a
  documented bounded-growth threshold. The scenario must also detect
  child-application crashes, not only sponge-de exit.
- **U5 — Paper-cut disposition matrix is FULL.** A 4-way classification
  (Resolved in 14 / Re-scoped with target phase / Blocked with evidence
  / Not-a-defect + doc fix) over every Phase 10–13 carryover item.
  Only Phase-14-relevant items are implemented; Phase 15+ items must
  NOT be absorbed.
- **U6 — Settings GUI / system tray / Advanced Mode menu are NOT in
  Phase 14.** The plan amends `docs/05-sponge-de.md` §5.1 / §5.4 and
  `docs/07-leitzentrale.md` §3.2 to defer these promises explicitly
  (D14.7).

---

## Baseline (2026-08-11)

### Components and code

- **`sponge-de` is a single Qt6 component** at
  `repos/sponge/src/sponge-de/` with internal modules `panel/`,
  `launcher/`, `theme/`, `config/` plus the demo window
  `sponge_de_main.{h,cc}`. There is **no** `notifications/` directory;
  the entire notifications implementation is one `Genode::warning` at
  `main.cc:109`. There is **no** clipboard code anywhere under
  `repos/sponge/`. The decorator controls enabled in
  `run/sponge-wm.run:170-174` are `maximizer` + `title` only (no
  `closer`, no `minimizer`).
- **Window stack** is upstream Genode's `wm` + `window_layouter` +
  `themed_decorator` (Phase 11) + `sponge_decorator_bridge`. Per
  `AGENTS.md` §5.2 the stack is reused, not re-implemented; any
  vendored-tree patch is ledgered in `docs/11-environment.md` §4
  (current rows: 9 entries; the Phase 11 themed-chrome ledger row is
  the only Phase-10–13-derived patch).
- **Backends in production**: `sponge_pkgd` (Phase 4/7; Report/ROM;
  shared `_do_launch` between vct and launcher — `AGENTS.md` §3.3.5),
  `sponge_configd` (in-memory key-value, 7 live keys applied live by
  the in-sponge-de `ConfigController`), `sponge_themed` (resolves
  theme names; live theme reload). Established pattern: a Sponge-owned
  daemon + `report_rom` + a single ROM module per service.

### Vendored Genode 26.05 — clipboard exists, notifications do not

- **Clipboard server**: `genode/repos/os/src/server/clipboard/`
  (server, `Component::construct` entry point). Implements the
  canonical Genode Report (writer) + ROM (reader) bus with a single
  ROM module named `"clipboard"`. The server reads nitpicker's
  `focus` ROM and gates writes (`match_labels` mode) to the focused
  domain; it enforces an `<flow from=... to=.../>` policy for reads.
  Test component at `genode/repos/os/src/test/clipboard/`. **Phase 14
  reuses this server as-is — no re-implementation, per `AGENTS.md`
  §5.2.**
- **Notification service**: confirmed absent. A recursive grep across
  `genode/repos/os/include/` and `genode/repos/gems/` finds no
  `notification_session/` directory; only the five
  `file_system_session`-adjacent references in unrelated components
  match the literal substring. **Phase 14 builds a Sponge-native
  daemon**, modeled on the established `sponge_pkgd` /
  `sponge_configd` / `sponge_themed` pattern (D14.1).

### Run scenarios already boot-verified

- `run/sponge-alpha.run` (umbrella, seL4 + interactive-PC drivers +
  WM stack + `alpha_probe: PASS`).
- `run/sponge-de-sel4-interactive.run` (base-sel4 + QMP-driven input;
  three bounded phases input/panel/launch).
- `run/sponge-wm-qmp.run` (real-pointer drag, `wm-probe: PASS`).
- `run/sponge-terminal-qmp.run` and `run/sponge-textedit-qmp.run`
  (QMP keyboard to focused apps; `*-probe: PASS`).
- `run/sponge-terminal.run` / `sponge-textedit.run` /
  `sponge-files.run` / `sponge-calculator.run` / `sponge-pdf-view.run`
  (Phase 7/13 packages, base-sel4 boot-verified).
- `run/sponge-power.run` (the proven `vct shutdown` → acpica S5 path;
  the workflow scenario's clean-shutdown gate reuses this protocol).

### Known carryover inventory (Phase 10–13)

Full ~50-item matrix in **Paper-cut Disposition Appendix** below.
Highlights relevant to the design decisions:

- **base-sel4 Qt6 staging bug** blocks `sponge-de-test.run` and
  `sponge-launcher.run` on seL4 (Phase 10 §11.2 item 3). Scenario-side
  fix: move the `cp` calls BEFORE `build_boot_image`. Carried over.
- **Residual launch-click nondeterminism** on seL4 (Phase 11 §11.3
  item 1; the W3b usb-tablet recipe); carried forward as a regression
  that the W6 stability workload must detect, not silently pass.
- **`sponge_configd` is in-memory only**; settings revert on reboot
  (Phase 4 follow-up #2 added `<vfs>` activation; the Alpha media
  does not currently wire it). W5 closes this carryover.
- **calculator / pdf_view not wired into Alpha media** (Phase 13 open
  item). W6 + D14.10 close this carryover.
- **`pkg_import` broader coverage deferred** (D13.5; Phase 13 open
  item) — remains deferred; no Phase 14 demand.
- **Theme-key cleanup**: deprecated error/success/warning aliases kept
  for backward compat; parsed-but-unused keys `title_family`,
  `icon_size`, `popup_width`, `popup_entry_min_height` left in the
  parser. W11 (paper-cut sweep) closes.
- **Four vendored-patch candidates** identified during Phase 10–13
  review: (a) QPA absolute-motion misrouting; (b) QGenodeScreen 1×1
  race; (c) nitpicker pointer ROM from REL (Phase 11 patch #9 only
  fixes part of it; the report-ROM gap remains); (d)
  `themed_decorator` live asset cache invalidation. Disposition in
  D14.8.
- **`vct` live resource stats not implemented**
  (`commands.cc:158` placeholder). Carried into W11.
- **`report_rom` single-writer limitation** is an architecture
  boundary (not a defect — see `AGENTS.md` §1.2). Documented, not
  fixed.
- **QTimer leak suspects** identified in the stability review:
  panel clock 1 s, theme 250 ms, configd 250 ms, launcher 1.5 s
  poll, launcher `qApp` eventFilter. W11 sweeps.

### Items NOT in Phase 14 scope (per U6 + carryover classification)

- Hardware-specific items (real-hardware boot, virtio_nic, aarch64,
  UEFI, i2c_hid, physical USB boot) are Phase 15+; not absorbed.
- Settings GUI, system tray, Advanced-Mode panel menu are Phase 15+;
  not absorbed (U6 + D14.7).
- Sponge IME (Phase 16) and GUI installer (Phase 17) are out.

---

## Binding Decisions

| # | Decision | Rationale |
|---|---|---|
| **D14.1** | **Notification backend = Sponge-native daemon `sponge_notifier`, modeled on `sponge_pkgd` / `sponge_configd` / `sponge_themed`.** No equivalent service exists in the vendored Genode 26.05 tree (verified by grep over `genode/repos/os/include/` and `genode/repos/gems/`; no `notification_session/` directory). It settles `docs/05-sponge-de.md` §7 ODQ "notification backend". Wire shape: a single Report session for clients to post (`<notif_request/>` policy label) and a single ROM module `notifications` for the panel widget to read. The XML payload is a `<notifications>` list with `<notification id ts source kind ttl_ms><title/><body/></notification>` entries (kind ∈ {info, warn, error}; ttl_ms ≤ 30 s default). Capability boundary: the daemon provides Report + ROM only; it requests Timer + ROM (config) + Report (re-broadcast). Notification **ownership**: in Phase 14 only sponge-de (system events: theme applied, config applied, package install completed) and vct (audit events: shutdown requested, install succeeded) post; no third-party package gets the API yet — the `<notif_request/>` session is granted by `init`'s per-label routing to known clients only. Cross-component posting is wired (sponge-de and vct are separately-launched components), but the client surface is intentionally narrow for Phase 14. | Re-using the existing daemon pattern keeps the design consistent (`AGENTS.md` §1.2: Genode services are capability-minimal, single-writer per label). Building on a Report/ROM bus makes it auditable, replayable, and observable — the same property that makes `sponge_pkgd` testable. A Sponge-native daemon is the correct Phase-14 answer; an upstream-shaped service is not available to reuse. |
| **D14.2** | **Clipboard service = REUSE the upstream Genode `os/src/server/clipboard` as-is.** Cross-component proof is mandated by U2. The Sponge work is **boundary**, not re-implementation: (a) stage the upstream binary in our scenarios (build list addition, no source change); (b) add a `clipboard` start node + per-label route in `init`; (c) integrate Qt on the sponge-de side by checking whether the upstream qt6_base QPA exposes a clipboard backend — if it does, bind it to the existing Genode clipboard service via a small bridge in `qgenodeplatformintegration`; if it does not, add a minimal `QPlatformClipboard` subclass that bridges Qt's `QMimeData` to a pair of Report (write) + ROM (read) sessions against the upstream server (default mime = `text/plain`; image / html are Phase 15+); (d) make ONE demo package (textedit) participate as a separately-launched component: it opens its own clipboard `Report` + `ROM` session pair and round-trips a sentinel byte string against the upstream server; (e) author `run/sponge-clipboard.run` that proves copy-from-sponge-de + paste-in-textedit across the address-space boundary. Default flow config in the clipboard server: `<flow from="default" to="default"/>` + `match_labels="no"` (single-domain permissive for Phase 14; the focused-domain gating is exercised by `match_labels="yes"` in a second, focused sub-scenario). Sponge-de MUST use the bus; intra-process Qt clipboard does not count for the U2 proof. | The upstream server already implements the canonical Genode Report/ROM bus + per-domain flow policy + focus-aware write gating. Re-implementing it would violate `AGENTS.md` §5.2. The Qt-side gap (whether the qt6_base QPA has a clipboard backend) is the only Sponge-owned piece; the upstream binary is untouched. |
| **D14.3** | **Minimize/restore semantics + restoration path = themed_decorator minimizer+closer controls + panel tasklist module.** The restoration path is the panel tasklist (deterministic, automation-friendly); Alt-Tab is a secondary shortcut but the tasklist is the **required** path per U3. Decorator controls: extend the `<controls>` block of themed_decorator to add `<closer/>` and `<minimizer/>` (the existing `<maximizer/>` and `<title/>` stay; the four are emitted by the bridge with restyle-aware geometry). Each control emits a Genode report to a new label: `decorator -> close_request` and `decorator -> minimize_request`. The panel tasklist (new `repos/sponge/src/sponge-de/panel/tasklist/` module) subscribes to wm's `window_list` report (already in `sponge-alpha.run:290`) and tracks per-window `(x, y, w, h, visible, focused)` locally. State-transition table (mandatory reference for W4): <br>**Window state machine** (states: `Normal-Visible`, `Normal-Visible-Focused`, `Maximized`, `Minimized`; action column lists the trigger, side-effects column lists what the system does): <br>• `(init) → Normal-Visible` on `window_created` — added to tasklist, default placement. <br>• `Normal-Visible → Normal-Visible-Focused` on `title click` — wm sends `focus_request` (existing). <br>• `Normal-Visible-Focused → Minimized` on `minimizer click` — layouter moves window to off-screen `(x=-32000, y=-32000)` via a layouter `<assign>` overwrite; tasklist marks the entry as minimized. <br>• `Normal-Visible → Minimized` (same as above; off-focus windows can minimize). <br>• `Minimized → Normal-Visible-Focused` on `tasklist button click` — tasklist writes its saved `(x,y,w,h)` to a layouter-rule ROM update + wm `focus_request`. <br>• `Normal-Visible → Maximized` on `maximizer click` — layouter assigns full-screen. <br>• `Maximized → Normal-Visible` on `maximizer click` (toggle) — restored. <br>• `Normal-Visible → (destroyed)` on `closer click` — wm closes the window; tasklist removes the entry. <br>• `Normal-Visible-Focused → (next-window-Focused)` on `Alt-Tab` — round-robin through the tasklist's focused-window counter. <br>The tasklist entry MUST be visible whenever a window is `Normal-Visible` or `Maximized`; entry dimmed when `Minimized`; entry removed on `(destroyed)`. The "decorative minimize with no restore" anti-pattern is explicitly forbidden by U3; the tasklist is the audit-able artifact. | U3 forbids decorative minimize. The tasklist is deterministic (host-driven QMP can click the same screen pixel every time) which is essential for the W6 stability workload and the workflow scenario. Restoring the saved geometry requires per-window state in sponge-de; a single ROM over `window_list` is sufficient. |
| **D14.4** | **Stability thresholds = 30 min wall-clock + ≥ 100 cycles + bounded resource growth + child-app crash detection.** Duration: 30 minutes wall-clock on QEMU (the user's suggested floor; `sponge-de-sel4-interactive.run` already passes in ~180 s, so 30 min is roughly 10× the proven upper bound — generous headroom for leaks). Cycles: minimum 100, OR 30 min, whichever finishes last. One cycle = (install-or-already-installed) → focus-click on the panel launcher entry → focused window first paint (Capture pixel check) → `Ctrl-A Ctrl-C` (clipboard copy sentinel) → Alt-Tab to the next window → paste sentinel → minimize current window → restore via tasklist → close window → `vct theme apply dark` (theme-reload) → `vct theme apply default` (theme-reload back). Resource snapshots at `t=0`, `t=10 min`, `t=20 min`, `t=30 min`: sponge-de RSS + cap count, pkg_runtime RSS, and the system-wide Genode `ram_used` are logged to a `stability-summary.xml` line the run script asserts. Growth tolerance: `ΔRAM < 50 MB` and `Δcaps < 100` over the 30 min window are required; any unbounded growth (Δ > tolerance at any snapshot, OR a regression-to-bigger-than-startup) is a HARD FAIL. Crash signatures (any one = FAIL): (i) `[init -> sponge-de] child exited with non-zero status`, (ii) a child app's marker missing after its expected boot (e.g. `pkg_gui_demo: window shown` not seen), (iii) `sponge-de: not responding` (a 5 s UI-event watchdog added by W6), (iv) any `[init -> ... ] denied` log line, (v) log flood (serial rate > 100 KB/s for > 5 s). The probe logs `stability-probe: PASS` plus a single `stability-summary.xml` line; the run script gates on both. QMP-driven cycles use the proven `run/qmp.inc` machinery; each cycle is bounded (`run_genode_until {stability-probe: cycle N PASS} 60` per cycle). | U4 requires a workload loop with a time floor and a bounded-growth threshold. The numbers here are concrete and consistent with `sponge-de-sel4-interactive.run`'s proven cycle time (~10–30 s on softpipe Mesa). 50 MB / 100 cap growth is conservative vs the Phase 11/12 evidence (sponge-de boots at ~120 MB RSS; the cap ceiling has plenty of headroom at `caps=1000`). The watchdog + child-app marker is the U4 "child-application crashes" requirement. |
| **D14.5** | **Falkon rescue attempt bounded by 2 days of focused effort; on failure, AMEND the Phase 14 criterion wording explicitly.** The current seL4 blocker is the per-PD CSPACE ceiling (Phase 8 P4 evidence; patches #6 and #7 from `docs/11-environment.md` §4 partially mitigate but did not fully close the WebEngine gap — re-confirmed 2026-08-11). The rescue attempt is W7 (below). If W7 fails within the bound, the amendment is: replace criterion 3's "browser" with "browser OR equivalent file/WebKit consumer" and record the evidence pointer in `docs/evidence/phase14-index.md` + update `docs/09-roadmap.md` §10 Phase 14 + the README current-status list. The amendment is documented in the commit message of the criterion-amendment commit. No silent substitution: the workflow scenario uses the proven files/textedit/terminal/calculator/pdf_view set if Falkon stays blocked, and the daily-workflow claim is honest about it. | U1 makes the bound + amendment explicit. The amendment is recorded in the durable record (`docs/09-roadmap.md` + evidence index) so the Phase 14 completion remains auditable. The default workflow scenario stays useful even if the browser stays blocked. |
| **D14.6** | **Paper-cut disposition = the 4-way classification in the appendix.** Every Phase 10–13 carryover item is classified `Resolved in 14 / Re-scoped / Blocked / Not-a-defect`. Only `Resolved in 14` items get implementation work in W11. `Re-scoped` items carry a target phase (15+ for hardware, 16 for IME, 17 for GUI installer). `Blocked` items carry evidence pointers. `Not-a-defect` items carry a doc-fix pointer. | U5 mandates the full matrix. Carrying an explicit classification prevents the silent-absorption failure mode and gives the Phase-15+ planner a clean handoff. |
| **D14.7** | **Settings GUI / system tray / Advanced Mode menu deferred to Phase 15+.** Doc amends (W3): `docs/05-sponge-de.md` §5.1 (panel currently contains clock + launcher; add a sentence: "A system tray and additional applets arrive in a later release; the panel today is intentionally minimal.") and §5.4 (replace the "Advanced Mode menu" mention with: "In this release the only way to open Leitzentrale is the `vct leitzentrale` CLI command; a panel menu entry is deferred to a later release.") — `docs/07-leitzentrale.md` §3.2 ("Planned" → "Deferred to Phase 15+"). No code is written for these. | U6 is binding; the amend is the durable record. The deferral does NOT block the Phase 14 criteria — none of them require a settings GUI, system tray, or Advanced-Mode menu. |
| **D14.8** | **Vendored-patch policy for the 4 Phase 10–13 candidates.** Of the 4 candidates identified in the baseline: <br>(a) QPA absolute-motion misrouting — IN-SCOPE if W6's stability workload exposes it (cycle-count or crash-signature reproduction); one-line vendored patch with a docs/11 §4 row. <br>(b) QGenodeScreen 1×1 race — IN-SCOPE if a focused W1 reproducer (deliberately cold-boot + immediate QMP click) shows the race in < 3 of 10 trials; otherwise `Re-scoped` to Phase 15+ (the Phase 11 evidence didn't pin it). <br>(c) Nitpicker pointer ROM from REL — `Re-scoped`, upstream-shaped, already partially patched (#9); the report-ROM gap remains an upstream Genode question per Phase 11 §11.2 item 2. <br>(d) themed_decorator live asset cache — IN-SCOPE if W4 (which adds minimizer+closer) cannot deliver a clean re-skin on theme change without it; otherwise documented in the work item as a known follow-up. Each patch goes into docs/11 §4 with a one-line "drop when" note (per `AGENTS.md` §5.2). Maximum 2 vendored-tree patches are absorbed in Phase 14 — more requires an explicit Phase-15 budget. | `AGENTS.md` §5.2 allows vendored patches; the constraint is "keep Sponge-specific changes to the vendored tree minimal". The 4-candidate list was the Phase 10–13 review's surface; the policy here is the prioritization, not the right to patch arbitrarily. |
| **D14.9** | **Single-monitor baseline.** Phase 14 ships single-monitor only. Nitpicker is configured for one screen (the existing default). Multi-monitor remains a Phase-15+ open question per `docs/05-sponge-de.md` §7. | Criterion 1–3 do not require multi-monitor. The stability scenario is single-monitor. Multi-monitor is a real workload addition that changes the resource model. |
| **D14.10** | **Supported application envelope = terminal, textedit, files, calculator, pdf_view (boot-verified from Phases 7–13).** Plus the Falkon browser subject to D14.5. All five Phase-13 packages are added to the Alpha media's pre-staged set (this closes the Phase 13 carryover "calculator/pdf_view not wired into Alpha media"). The Alpha media now pre-stages: `terminal` (nested sub-init, bash + UNIX toolset), `textedit` (qt6_textedit), `files` (Qt6 file manager), `calculator` (qt6_calculatorform, source-built), `pdf_view` (mupdf, source-built with bundled sample.pdf). Launcher category mapping: terminal → `System`, textedit → `Office`, files → `System`, calculator → `Utilities`, pdf_view → `Office`. The daily workflow scenario boots against this set. Versioning: same as shipped; no rebuild in Phase 14 (any version bumps follow D14.5 for Falkon only). | The five packages are boot-verified on base-sel4 (the `sponge-{terminal,textedit,files,calculator,pdf-view}.run` suite); adding them to the Alpha media closes the Phase 13 carryover and gives the W6 workflow scenario a concrete real-world set. Falkon remains opt-in via the rescue. |

---

### Decision closure notes (updated during execution)

- **D14.2 closure (2026-08-11).** Sub-design settled: the vendored
  qt6_base QPA already ships `QGenodeClipboard` (REUSE verdict — no
  Sponge `QPlatformClipboard` subclass was written; the
  `repos/sponge/src/sponge-de/clipboard/` path is dead). Delivered
  proofs: `run/sponge-clipboard.run` (cross-component bus write → Qt
  paste), `run/sponge-clipboard-qtsettext.run` (Qt→server write path
  healthy via a programmatic `QGuiApplication::clipboard()->setText()`
  harness — decisive verdict per Oracle consultation),
  `run/sponge-clipboard-focus.run` (`match_labels="yes"` gating).
  **Known limitation:** keyboard Ctrl-C inside the `qt6_textedit`
  example does not fire `setMimeData` on base-sel4 (evidence:
  `docs/evidence/phase14-w5-qtwrite-failure.md`); the failure is
  downstream of the bridge in the shortcut/input layer and is a
  Phase-15+ item (disposition matrix row, `Blocked`-with-evidence).
  **Consequence for W8:** the workflow scenario's copy step uses the
  `clipboard_qtsettext` harness as the writer (separate address space;
  U2 still holds), not textedit Ctrl-C.

---

## Work Items

### TDD convention (applies to every work item)

Every W item follows **test-first**: a focused run scenario stub is
added to `run/` BEFORE any source change; the stub fails on the
current tree (no PASS marker, or an explicit "expected FAIL" sentry).
The implementation commits then make the scenario pass. The final
commit of every W item flips the scenario's PASS marker; the W item
is not "done" until that marker is reproduced on base-sel4 in QEMU
(`KERNEL=sel4 BOARD=pc`).

### Wave 1 — Infrastructure, design, vendored-patch policy

#### W1 — Scenario staging fix + carryover infrastructure

**Goal**: unblock the seL4 regression sweep so every downstream
scenario can build + boot.

**Carries**: Phase 10 §11.2 item 3 (base-sel4 Qt6 staging blocks
`sponge-de-test.run` + `sponge-launcher.run`); the dev-environment
boundary (`AGENTS.md` §1.2: every scenario must build `lib/ld` + `core
init <component>` and pass `[build_artifacts]` to `build_boot_image`).

**Tasks**:

1. Apply the scenario-side fix to `sponge-de-test.run` and
   `sponge-launcher.run`: move every `cp` of a Qt6 `.lib.so` /
   `qt6_*.tar` file BEFORE `build_boot_image` (and BEFORE the manual
   staging loop that follows it). Add a code comment citing
   `docs/11-environment.md` §10.4 (base-sel4 boot image is a single
   packed `image.elf`; modules added after `build_boot_image` never
   reach the guest).
2. Build the two scenarios end-to-end on base-sel4 and gate on the
   existing PASS markers. If a previously blocked scenario still
   fails for a different reason, file the new failure as a paper-cut
   row (not silently fixed here).
3. Add `sponge_configd` `<vfs>` activation scaffolding to one
   headless scenario (`sponge-configd-persist.run`, see W5) — no
   implementation yet, just the scenario skeleton (build list,
   config, route, gated `run_genode_until`).

**Pass conditions**:

- `run/sponge-de-test.run` and `run/sponge-launcher.run` PASS on
  base-sel4 (QEMU).
- `run/sponge-configd-persist.run` exists, boots, and is currently
  FAIL with the expected sentry (no `configd-persist-probe: PASS`).

**Commit units**: (i) `fix(scenario): stage Qt6 libs before
build_boot_image on base-sel4` (covers both files); (ii) each
regression log receipt as `docs/evidence/task-0-phase14-baseline.log`.

---

#### W2 — Vendored-patch policy + design-decision docs

**Goal**: lock down the design before any UI work, per the Metis
"design decisions first" directive.

**Tasks**:

1. Update `docs/11-environment.md` §4 patch ledger with the D14.8
   policy as a preamble (does not add new patch rows yet — that
   happens in W3 or W4 if any patch is actually applied).
2. Amend `docs/05-sponge-de.md`:
   - §5.1 — add the system-tray/applets deferral sentence (D14.7).
   - §5.4 — replace the "Advanced Mode menu" mention with the
     `vct leitzentrale` CLI deferral (D14.7).
   - §7 ODQ — strike "Notification backend design" (settled by
     D14.1) and add a cross-reference to D14.2 for clipboard (the
     clipboard ODQ had been left open in Phase 5); add a note that
     multi-monitor + panel placement remain Phase-15+ open.
3. Amend `docs/07-leitzentrale.md` §3.2 — relabel "Planned" as
   "Deferred to Phase 15+" (D14.7).
4. Author `docs/plans/wm-state-table.md` (new, ≤ 80 lines) — the
   W4 state-transition table from D14.3 with the column headers
   and a worked example for each transition. This file is the
   implementation reference for W4; it must exist and be reviewed
   BEFORE W4 begins.
5. Update `docs/13-installation.md` "Known limitations" — reflect
   that system tray / Advanced-Mode menu are deferred, that
   clipboard is now feature-complete (subject to W3), and that the
   Alpha media pre-stages the five Phase-13 packages (D14.10).

**Pass conditions**:

- Doc files saved; `git diff --stat` matches the expected files
  only (no spurious edits).
- `wm-state-table.md` reviewed and checked in BEFORE W4 starts
  (commit gating).
- `./tool/build docs` (or equivalent lint) exits 0 if such a lint
  exists; otherwise `grep` checks confirm the doc anchors are
  reachable from the README doc-map.

**Commit units**: one commit per file; the wm-state-table lands
separately and gates W4.

---

#### W3 — Vendored-patch investigation (D14.8 triage)

**Goal**: turn the D14.8 policy into a concrete ledger update (or
the evidence that no patch is needed).

**Tasks**:

1. **Candidate (b) QGenodeScreen 1×1 race** — author a focused
   reproducer (`run/sponge-de-qscreen-race-probe.run`): boot
   `sponge-de`, immediately issue a QMP click (no warm-up), observe
   whether the first capture sample has the `1×1`-stale frame
   reported by the Phase 11 review. Three trials; record the result.
2. **Candidate (c) nitpicker pointer ROM from REL** — `Re-scoped`
   per D14.8 (no work). Add a row to `docs/09-roadmap.md` §11
   known-issue follow-ups confirming the deferral.
3. **Candidate (a) QPA ABS misrouting** — wait for W6 stability
   workload evidence. If W6 detects it, the patch lands in W3b
   (below); otherwise `Re-scoped`.
4. **Candidate (d) themed_decorator asset cache** — wait for W4
   evidence; the patch (or upstream restart) lands in W4 or W11.

**Pass conditions**:

- W3 reproducer commits the per-trial result to
  `docs/evidence/task-3-phase14-qscreen-race.log`.
- The docs/09 follow-up note is committed.
- No vendored patch is applied in W3 (this is an investigation
  workstream, not a fix workstream).

**Commit units**: (i) reproducer scenario + log; (ii) docs/09
follow-up.

---

### Wave 2 — Notification daemon, clipboard, settings persistence

(These three items can run in parallel after W1+W2.)

#### W4 — Notification daemon (`sponge_notifier`)

**Goal**: deliver `D14.1` end-to-end: a Sponge-native daemon +
panel widget + a boot-verified scenario that posts from one
component and renders in sponge-de.

**Tasks**:

1. Author `repos/sponge/src/sponge_notifier/` (new component):
   `target.mk` + `main.cc` + `README.md`. API follows the
   established pattern (`sponge_pkgd` / `sponge_configd` /
   `sponge_themed`):
   - Provides `Report` (clients post `<notif_request/>` payloads)
     and `ROM` (single module `notifications`).
   - Capability boundary: takes `Timer` + `ROM` (config) +
     `Report` (re-broadcast). No `PD`, no `RM`, no `GUI`.
   - Validates each incoming report (id, kind, ttl_ms), assigns
     a monotonic id, stores the active list, emits the single
     `notifications` ROM, schedules a Timer-driven expiry that
     re-emits.
   - **Default config** (new `notifier.config`): `max_live=8`,
     `default_ttl_ms=5000`. Empty `config` is acceptable
     (defaults applied).
2. Add `notifier_widget.{h,cc}` to
   `repos/sponge/src/sponge-de/panel/` — a small themed popover
   that subscribes to the `notifications` ROM, renders the top-N
   entries, and dismisses on click or ttl expiry.
3. Wire sponge-de to consume the new daemon:
   - Add a `<notif_request/>` route label + a `notifications` ROM
     route in `sponge-de`'s run config (mirroring `theme`/`config`
     wiring).
   - The ConfigController or a new `NotifyController` posts
     notifications on theme apply, config change, and package
     install completion (the latter requires a one-way listener
     to sponge_pkgd's `installed` broadcast).
4. Wire vct: when `vct install` / `vct remove` / `vct shutdown`
   / `vct reboot` completes, post a notification. Add an
   `enable_notifications="yes"` attribute to vct's `<config>`
   (default on); when the daemon is absent, vct logs the
   notification once (`Genode::warning("notifier unavailable,
   dropping: <title>")`) — **never a silent drop, never a
   crash**.
5. Author `run/sponge-notify.run` (base-sel4 + QMP):
   - Boots sponge-de + sponge_notifier + a tiny `notify_probe`.
   - `notify_probe` posts a sentinel
     (`<notification kind="info" ttl_ms="3000"><title>Sponge
     Phase 14 notification sentinel</title><body>W4 probe</body>
     </notification>`).
   - Sponge-de renders the popover; the probe Capture-polls the
     popover rect for non-background fraction rising above
     threshold (popover open) and then dropping (ttl expiry).
   - Final gate: `notify-probe: PASS` + `Run script execution
     successful.`.

**Pass conditions**:

- `run/sponge-notify.run` PASS on base-sel4 + QEMU.
- The notifier daemon's `notifications` ROM is reachable from a
  separately-launched probe (cross-component proof, U2-shaped —
  the daemon is one component, the widget is in sponge-de, the
  probe is a third).
- vct's `notifier unavailable` warning fires cleanly when the
  daemon is omitted (a `--without-notifier` scenario variant
  proves the no-crash contract).

**Commit units** (atomic, conventional-commits):

- (test) `test(notify): add notify_probe skeleton + failing run
  scenario`.
- (feat) `feat(notify): add sponge_notifier daemon (Report/ROM
  bus, capability-minimal)`.
- (feat) `feat(sponge-de): add notifier_widget panel popover`.
- (feat) `feat(sponge-de): wire notifications route + controller`.
- (feat) `feat(vct): post notifications on install/remove/
  shutdown`.
- (docs) `docs(notify): update docs/05 §4 / §7 (ODQ settled)` and
  `docs/15` row.

---

#### W5 — Clipboard service integration

**Goal**: deliver `D14.2` end-to-end: the upstream
`os/src/server/clipboard` binary is staged, the Qt side is wired
(in sponge-de + one separately-launched demo app), and the
cross-component copy/paste proof is scenario-verified.

**Tasks**:

1. **Sub-design decision** — investigate whether the qt6_base QPA
   ships a clipboard backend. Read
   `genode/repos/libports/src/qt6/base/.../qgenodeplatformintegration.{h,cc}`
   (or the closest equivalent in the vendored 26.05 qt6_base). If
   yes, bind it to the Genode clipboard service via the existing
   API. If no, add a minimal `QPlatformClipboard` subclass at
   `repos/sponge/src/sponge-de/clipboard/qgenode_clipboard.{h,cc}`
   (one source file, one header) that bridges `QMimeData` (text
   only) to a pair of Report (write) + ROM (read) sessions
   against the upstream server. Decision points:
   (a) reuse-or-add recorded in commit message;
   (b) mime scope = `text/plain` only for Phase 14
   (image / html deferred);
   (c) per-QMimeData byte cap = 16 KiB (matches the
   `sponge_themed` transport cap of 8 KiB doubled for UTF-8 headroom).
2. Add `clipboard` start node + routes in `run/sponge-alpha.run`
   and the new `run/sponge-clipboard.run` (mirroring the
   `sponge_configd` / `sponge_themed` / `sponge_pkgd` route style):
   - Server provides Report + ROM.
   - Default flow config: `<flow from="default" to="default"/>`
     + `match_labels="no"` (permissive, single-domain).
   - Each client opens a labeled Report + ROM session pair
     (label `-> clipboard`).
3. Add `clipboard_request` + `clipboard` routes to sponge-de's
   config and pkg_runtime's start nodes (so a launched package
   also gets a clipboard session if its metadata declares
   `<session label="clipboard"/>`).
4. Wire textedit as the separately-launched cross-component
   actor. Update `pkg/textedit/metadata.xml` to declare the
   clipboard session; verify `sponge-textedit.run` still PASSes
   after the metadata change (the session must be granted without
   breaking the existing keyboard probe).
5. Author `run/sponge-clipboard.run` (base-sel4 + QMP):
   - Boots sponge-de + sponge_configd + sponge_themed + clipboard
     + sponge_pkgd + textedit (installed via the request channel).
   - The probe focuses sponge-de's main window, sends a sentinel
     `"Sponge Phase 14 clipboard sentinel"` via QMP `send-key`
     into the demo (a `QLineEdit`-like input or just a `QWidget`
     with copy-on-focus; design point decided in W5 sub-design).
   - Probe sends `Ctrl-C` (focused-domain write to clipboard
     server).
   - Probe focuses textedit (a QMP click on the textedit
     domain), sends `Ctrl-V`; the probe captures the textedit
     domain via Capture and asserts the sentinel string is
     present (pixel + structural probe — textedit exposes a
     `textedit_content` report; pixel sampling alone is not
     enough, misleading_success_output defense).
   - Final gate: `clipboard-probe: PASS` + `Run script execution
     successful.`.

**Pass conditions**:

- `run/sponge-clipboard.run` PASS on base-sel4 + QEMU.
- The sentinel string `Sponge Phase 14 clipboard sentinel` is
  asserted byte-for-byte in the textedit `textedit_content`
  report AND pixel-detected in the textedit domain capture.
- A second sub-scenario (`match_labels="yes"` + nitpicker focus
  assertion) confirms the focus-aware write gating works.
- `sponge-textedit.run` regression still PASSes.

**Commit units**:

- (test) `test(clipboard): add clipboard_probe skeleton + failing
  run scenario`.
- (feat) `feat(clipboard): stage upstream clipboard server in
  run scenarios`.
- (feat) `feat(sponge-de): bridge Qt clipboard to Genode
  clipboard bus (reused or added QPlatformClipboard)`.
- (feat) `feat(textedit): declare + use clipboard session for
  cross-component copy/paste`.
- (docs) `docs(clipboard): update docs/05 §4 / §7` + Phase 14
  ODQ closure note.

---

#### W6 — `sponge_configd` persistence activation (carryover)

**Goal**: close the Phase 4 / Phase 13 "settings revert on reboot"
carryover for the Alpha media.

**Tasks**:

1. Author `repos/sponge/src/sponge_configd/main.cc` change:
   when `<config>` carries `<vfs><ram .../></vfs>`, persist the
   active key-value set to a vfs-backed file
   (`/var/sponge_configd/store.xml`) on every change; reload on
   construct. The existing Phase 4 follow-up #2 code is the
   foundation; the change is to wire it on by default for the
   Alpha media (a `<vfs>` node in the run config) AND add
   crash-consistency by writing to `store.xml.tmp` and renaming
   (Phase 4 §13.2 already documents the single-writer contract).
2. Author `run/sponge-configd-persist.run` (the W1 stub, now
   fully implemented):
   - Boots sponge_configd with `<vfs>` wired to a writable RAM
     vfs.
   - Probe writes `panel.height=64`, then `panel.visible_
     widgets=clock`, then `panel.height=28` — three writes.
   - Probe restarts sponge_configd (or reads the on-disk store
     from the booted scenario — depends on the persistence
     transport choice).
   - Asserts the broadcast carries `panel.height=28` after
     restart (the most-recent value).
3. Update `run/sponge-alpha.run` to carry the `<vfs>` node in
   sponge_configd's start config (with the SPONGE-DATA
   partition path on disk-backed scenarios; with the in-memory
   ram path on the ISO scenarios).

**Pass conditions**:

- `run/sponge-configd-persist.run` PASS on base-sel4 + QEMU.
- `run/sponge-alpha.run` regression still PASSes.
- A `corrupt_store.xml` failure scenario proves the daemon
  detects a torn write and recovers (logs the warning, never
  crashes — same contract as Phase 4 §13.2).

**Commit units**: (i) `feat(configd): activate <vfs> persistence
on Alpha media`; (ii) `test(configd): add persist run scenario`;
(iii) `fix(configd): atomic store write (write-tmp + rename)`.

---

### Wave 3 — Window management

(After W4; W4's notification popover reuses the same panel-tasklist
geometry code and the same `decorator` `<controls>` plumbing.)

#### W7 — Decorator controls + panel tasklist + window state machine

**Goal**: deliver `D14.3` end-to-end: themed_decorator exposes
`closer` + `minimizer` + `maximizer` + `title`, the panel tasklist
is the deterministic restoration path, the state machine in
`docs/plans/wm-state-table.md` (W2) is implemented, and the
window-management scenario is boot-verified.

**Tasks**:

1. **themed_decorator extension** (not a re-implementation — extend
   the existing themed_decorator recipe):
   - Add `<closer/>` and `<minimizer/>` to the `controls` block
     emitted by `sponge_decorator_bridge` (the Phase 11 bridge
     currently emits `maximizer` + `title`; the geometry JSON
     needs two new button specs sourced from the active theme's
     `decorator.json`).
   - Each control emits a Genode report to a new label:
     `decorator -> close_request` (with the window label) and
     `decorator -> minimize_request` (same).
   - If candidate (d) — themed_decorator live asset cache — blocks
     a clean re-skin on theme change (verifiable: re-skin
     produces a stale frame in the title bar texture), apply the
     ledgered vendored patch OR document the need for a child
     restart on theme change (the cleaner Phase 15 option).
2. **Panel tasklist module** (new `repos/sponge/src/sponge-de/panel/tasklist/`):
   - `tasklist_widget.{h,cc}` — a horizontal widget below the
     existing launcher S toggle (or at the right edge of the
     panel; design point decided in W7 sub-design based on panel
     width budget). Renders one button per running app; entry
     dimmed if minimized, highlighted if focused.
   - `tasklist_controller.{h,cc}` — subscribes to wm's
     `window_list` report (already in `sponge-alpha.run:290`);
     tracks per-window `(x, y, w, h, visible, focused)`;
     writes layouter-rule updates and `focus_request` reports on
     click.
3. **sponge_de_main** change — instantiate the tasklist widget in
   the panel layout (after the launcher S toggle), and wire its
   controller to the existing ConfigController and theme reload
   paths so the entry colors track the active theme.
4. Author `run/sponge-wm-tasks.run` (base-sel4 + QMP):
   - Boots the WM stack + sponge-de + textedit + pkg_gui_demo
     + alpha_probe-equivalent `wm_tasks_probe`.
   - Probe launches pkg_gui_demo (already a proven path).
   - Probe minimizes pkg_gui_demo via a QMP click on the
     decorator's minimizer button (compute the button pixel
     from `decorator_margins` ROM; the same source-of-truth as
     Phase 11).
   - Probe asserts the tasklist entry dims (visual sentry).
   - Probe clicks the tasklist entry to restore; asserts the
     window re-paints at the saved `(x, y)` and the tasklist
     entry highlights.
   - Probe launches textedit; focuses pkg_gui_demo via
     `Alt-Tab`; focuses textedit via `Alt-Tab` again. Asserts
     focus order matches the tasklist's tracked order.
   - Probe closes pkg_gui_demo via the decorator's closer button;
     asserts the tasklist entry is removed.
   - Final gate: `wm-tasks-probe: PASS` + `Run script execution
     successful.`.

**Pass conditions**:

- `run/sponge-wm-tasks.run` PASS on base-sel4 + QEMU.
- The state-transition table from `docs/plans/wm-state-table.md`
  is exercised end-to-end (every row in the table has a probe
  sub-step).
- `sponge-alpha.run`, `sponge-de-themed-chrome.run`,
  `sponge-wm-qmp.run` regressions still PASS.

**Commit units**: (i) `feat(decorator): extend themed_decorator
controls (closer + minimizer) via sponge_decorator_bridge`;
(ii) `feat(sponge-de): add panel tasklist module (tasklist_widget
+ tasklist_controller)`; (iii) `feat(sponge-de): wire tasklist
into Main + theme reload`; (iv) `test(wm): add sponge-wm-tasks
run scenario`; (v) `docs(wm): implement state-transition table
from docs/plans/wm-state-table.md`.

---

### Wave 4 — Stability workload + Workflow + Falkon rescue

(After W4+W5+W7; this is the biggest wave in terms of runtime and
the most likely to surface paper cuts.)

#### W8 — Workflow scenario (criterion 3)

**Goal**: prove criterion 3 end-to-end: boot → launch terminal /
editor / files / (browser subject to D14.5) → real work → clipboard
→ minimize / restore → clean shutdown via `vct shutdown` →
acpica S5.

**Tasks**:

1. Author `run/sponge-de-workflow.run` (base-sel4 + QMP):
   - Boots the proven stack from `sponge-alpha.run` + the new
     `sponge_notifier` (W4) + the clipboard server (W5) + the
     panel tasklist (W7) + the persisted `sponge_configd` (W6).
   - Pre-stages all five Phase-13 packages (D14.10).
   - `workflow_probe` drives the 7-step sequence:
     1. Boot to `panel and window shown` (existing marker).
     2. Launch terminal via launcher; type `echo Sponge Phase 14
        workflow sentinel`; assert glyph growth (existing
        terminal_probe pattern, with the new sentinel string).
     3. Launch textedit; type `Sponge Phase 14 workflow
        clipboard sentinel`; assert content delta (existing
        textedit_probe pattern).
     4. Copy from textedit (`Ctrl-A Ctrl-C`); paste into
        terminal (`Ctrl-V`); assert the sentinel appears in the
        terminal glyph sample.
     5. Minimize textedit via decorator minimizer; restore via
        tasklist click; assert re-paint at saved position.
     6. Launch calculator; assert the rendered window is
        non-blank (existing calculator_probe pattern).
     7. `vct shutdown`; assert the acpica S5 path (existing
        `sponge-power.run` pattern: `vct: shutdown: requesting
        poweroff` followed by QEMU `eof`).
   - Each step bounded; final gate `workflow-probe: PASS` +
     `Run script execution successful.`.

**Pass conditions**:

- `run/sponge-de-workflow.run` PASS on base-sel4 + QEMU.
- The 7 steps complete in ≤ 900 s wall-clock.
- The clean-shutdown gate proves the S5 path end-to-end
  (the proven `sponge-power.run` contract is reused verbatim).

**Commit units**: (i) `test(workflow): add sponge-de-workflow run
scenario skeleton (failing)`; (ii) `feat(workflow): wire probe
sequencer for the 7-step protocol`; (iii) `docs(workflow):
update README current-status + docs/13 quick-start tour`.

---

#### W9 — Stability workload (criterion 1)

**Goal**: prove criterion 1 end-to-end: 30 min wall-clock + ≥ 100
cycles + bounded resource growth + child-app crash detection, per
`D14.4`.

**Tasks**:

1. Author `repos/sponge/src/test/stability_probe/main.cc`
   (new probe; the long-running component):
   - Drives a `cycle` counter and emits `stability-probe: cycle
     N PASS` every cycle (each cycle = the 10-step protocol in
     D14.4).
   - Snapshots RSS + cap counts at `t=0`, `t=10m`, `t=20m`,
     `t=30m`; emits a single `stability-summary.xml` line at
     end-of-run.
   - Installs a 5 s UI-event watchdog: if no `input` report
     arrives on `sponge-de -> input` for 5 s while the desktop
     is otherwise responsive, log `sponge-de: not responding`
     and FAIL the cycle.
   - Listens for child-app exit (any of pkg_runtime's children
     with `running="no"` flipped while the cycle expected
     `running="yes"`) and logs `[stability-probe] child
     <pkg> exited unexpectedly` — also a FAIL.
2. Author `run/sponge-de-stability.run` (base-sel4 + QMP):
   - Boots the proven stack + the stability probe.
   - `run_genode_until {stability-probe: PASS}` with a 1800 s
     wall-clock timeout (30 min + 5 s slop).
   - Asserts the `stability-summary.xml` line is present;
     asserts `ΔRAM < 50 MB` AND `Δcaps < 100` via a Tcl `expr`
     on the parsed values.
   - On FAIL: the scenario exits non-zero (fail-loud).

**Pass conditions**:

- `run/sponge-de-stability.run` PASS on base-sel4 + QEMU.
- Final `stability-summary.xml` parsed by the run script
  matches the bounded-growth thresholds.
- The probe's watchdog log line is included in the FAIL path
  (proves the watchdog is wired).
- A `sponge-de-stability-fastfail.run` variant (5 cycles,
  aggressive child-app `kill -9` injection at cycle 3)
  proves the crash-detection path fires.

**Commit units**: (i) `test(stability): add stability_probe + run
scenario skeleton (failing)`; (ii) `feat(stability): cycle loop
+ resource snapshot + watchdog + crash detection`;
(iii) `test(stability): add fastfail variant for crash-detection
proof`; (iv) `docs(stability): update docs/09 Phase 14 stability
criterion traceability`.

---

#### W10 — Falkon rescue attempt + criterion-amendment decision

**Goal**: attempt the D14.5 rescue within the 2-day effort bound;
if it fails, amend the Phase 14 criterion wording explicitly.

**Tasks**:

1. Reproduce the failure on the current tree:
   `run/sponge-falkon.run` (already boot-verified) + the
   seL4 alpha-media variant (verify whether Falkon still fails
   at first paint on the current vendored tree with patches #6
   + #7 already applied). Record the failure as a focused log
   in `docs/evidence/task-10-phase14-falkon-rescue.log`.
2. **Attempt 1 — disk-served topology** (`run/sponge-falkon-rescue.run`):
   - Reuse the `sponge-falkon-disk.run` pattern from Phase 8
     (disk-served payload via `cached_fs_rom`).
   - If it reaches first paint on the current tree: ship
     (D14.5 satisfied). Commit `feat(falkon): disk-served
     first-paint proof on current tree`.
3. **Attempt 2 — vendored cap-ceiling patch** (only if attempt 1
   fails): apply the smallest possible vendored patch to
   `genode/repos/base-sel4/src/core/spec/x86_64/platform_pd.cc`
   to bump the per-PD CNode ceiling. Ledger the patch in
   `docs/11-environment.md` §4 (the 10th row). If the patch
   reaches first paint: ship.
4. **Amendment path** (only if both attempts fail within the
   bound): amend `docs/09-roadmap.md` §10 Phase 14 criterion
   wording (per D14.5), update `README.md` current-status, and
   record the evidence pointer in
   `docs/evidence/phase14-index.md`. The workflow scenario
   uses files / textedit / terminal / calculator / pdf_view;
   the daily-workflow criterion is honest about the browser
   absence.

**Pass conditions**:

- One of: (a) `run/sponge-falkon-rescue.run` PASS on base-sel4;
  (b) vendored patch applied + scenario PASS; (c) Phase 14
  criterion wording amended + evidence pointer committed.
- The chosen path is committed atomically with the scenario
  log + the docs amendment.
- `sponge-alpha.run` regression still PASSes regardless of
  the chosen path.

**Commit units**: (i) the chosen path (attempt 1, attempt 2,
or amendment); (ii) the docs/09 amendment if path (c).

---

### Wave 5 — Paper-cut sweep + Close-out

#### W11 — Paper-cut implementations (carryover items classified
`Resolved in 14`)

**Goal**: resolve every `Resolved in 14` item from the Paper-cut
Disposition Appendix.

**Tasks** (the list below is the implementation work for the
items classified `Resolved in 14` in the appendix):

1. **#3 base-sel4 Qt6 staging fix** — done in W1.
2. **#5 launch-click nondeterminism detection** — the W9
   stability workload's crash-watchdog already detects this
   class; add a focused assertion in
   `sponge-de-stability.run` cycle log that the first-cycle
   launch click lands within ±10 px (the Phase 11 §11.3
   item 1 budget).
3. **#7 partial drag delta on themed chrome** — add a focused
   drag-delta assertion in `sponge-wm-tasks.run` (the
   W7 scenario's themed-chrome drag phase must move the
   window by the dispatched delta within a 50% tolerance).
4. **#13 themed_decorator live asset re-skin** — either the
   vendored patch from candidate (d) or a documented
   "decorator restart required on theme change" follow-up,
   whichever the W7 evidence shows is required.
5. **#15 decorator controls minimizer + closer** — done in W7.
6. **#17 deprecated theme aliases removed** —
   `repos/sponge/src/sponge-de/theme/theme_loader.cc`: remove
   `error_bg` / `error_text` / `success_bg` / `success_text` /
   `warning_bg` / `warning_text` aliases (keep the
   `*_bg` / `*_text` canonical names from Phase 11); remove the
   parsed-but-unused `title_family`, `icon_size`,
   `popup_width`, `popup_entry_min_height` keys (with a
   deprecation warning during one release cycle? — NO, just
   remove; the keys were never documented).
7. **#33 duplicated noux build list** — extract the shared
   `run/terminal_runtime.inc` (the bash + UNIX toolset vfs
   block shared by `sponge-terminal.run`, the workflow
   scenario, and any future terminal-hosting scenarios).
8. **#34 docs/13 app-set drift** — done in W2.
9. **#39 notifications implementation** — done in W4.
10. **#40 clipboard implementation** — done in W5.
11. **#41 decorator closer + minimizer** — done in W7.
12. **#42 settings revert on reboot** — done in W6.
13. **#43 vct live resource stats** — implement
    `vct status --resources` (reads `ram_used` / `cap_used`
    from the init state report; `commands.cc:158` placeholder
    is replaced). Boot-verified by extending
    `sponge-vct-status.run` with the `--resources` assertion.
14. **#47–#50 QTimer leaks** — leak audit: each candidate timer
    is checked for cancellation in the destructor path. The
    panel clock, theme controller, config controller, and
    launcher poll are audited; missing cancellations are fixed
    and a leak-audit scenario (`sponge-de-leak-audit.run`)
    proves the fix via 200 cycles + resource snapshot.
15. **#29 calculator / pdf_view wired into Alpha media** —
    done via D14.10 in W8 (the workflow scenario boots
    against the pre-staged set).

**Pass conditions**:

- Every `Resolved in 14` item's PASS condition is documented
  inline in the commit that resolves it.
- `sponge-de-leak-audit.run` PASS on base-sel4 (the leak
  audit's regression gate).
- `sponge-de-stability.run` from W9 still PASSes (proves
  no regression from the cleanup).

**Commit units**: one commit per item (smaller items bundled if
the diff is trivial, per `AGENTS.md` §4.3 "one logical change
equals one commit").

---

#### W12 — Close-out

**Goal**: complete Phase 14 durably and hand off cleanly.

**Tasks**:

1. **Roadmap checkboxes** — flip the four Phase 14 criteria in
   `docs/09-roadmap.md` §10 Phase 14 to `[x]` if the bound
   conditions are met; otherwise mark the specific criterion
   as `[x]` with the D14.5 amendment note inline. The
   `docs/09-roadmap.md` §11 "Current Focus" section gets a
   short Phase 14 closeout paragraph.
2. **README current-status update** — flip the Phase 14 items
   in the "Current Status" list to ✅ (or note the D14.5
   amendment for the browser criterion).
3. **Docs evidence index** — author
   `docs/evidence/phase14-index.md` (mirroring
   `phase13-index.md`): per-W receipts, the per-criterion
   `criterion → scenario → exact marker → evidence` table,
   the D14.5 decision record, the leak-audit result, and
   the regression sweep table.
4. **Regression sweep** — the full Phase 14 scenario suite
   (the new scenarios + every existing Phase 7/10/11/12/13
   scenario touched by a Phase-14 code path) re-run end-to-end
   on base-sel4 in QEMU. One scenario at a time, `make -j1`,
   no concurrent `make` in `genode/build/x86_64` (the Phase 12
   sweep's lesson). The sweep's receipts live in
   `docs/evidence/phase14-envelope-*.log` (one per scenario).
5. **Phase 15+ handoff** — the
   Paper-cut Disposition Appendix's `Re-scoped` items each
   carry a target phase; these are NOT touched in Phase 14
   but the appendix's per-item handoff note is committed
   (verifies the U5 boundary was held).

**Pass conditions**:

- `docs/evidence/phase14-index.md` exists and is referenced
  from the docs/09 §11 current-focus paragraph.
- The regression sweep exits 0 for every scenario in the
  Phase 14 suite.
- The Phase 14 completion criterion checkboxes are
  internally consistent with the evidence index.

**Commit units**: (i) `docs(roadmap): flip Phase 14 checkboxes
+ closeout paragraph`; (ii) `docs(readme): update current
status`; (iii) `docs(evidence): add phase14-index.md`;
(iv) `docs(roadmap): amend Phase 14 criterion per D14.5`
(if applicable); (v) the per-scenario regression log files.

---

## Paper-cut Disposition Appendix

Every Phase 10–13 carryover item + every Sponge-code paper cut
identified in the baseline walk. The classification column is
authoritative; only `Resolved in 14` items get implementation work
in W11.

| # | Item | Origin | Classification | Target phase / resolution |
|---|------|--------|----------------|---------------------------|
| 1 | QPA misroutes tablet absolute-motion under multi-domain Qt (Phase 10 §11.2 item 1) | P10 | Re-scoped | Phase 15+ — upstream QPA improvement; W6 stability workload detects if it reproduces |
| 2 | Nitpicker pointer ROM only updates on `absolute_motion` (Phase 11 §11.2 item 2) | P10/P11 | Re-scoped | Already partially fixed by patch #9; report-ROM gap is upstream-shaped |
| 3 | base-sel4 Qt6 staging blocks `sponge-de-test.run` + `sponge-launcher.run` (Phase 10 §11.2 item 3) | P10 | **Resolved in 14** | W1 (scenario-side `cp` move) |
| 4 | `QTimer + QCursor::pos()` popup auto-close (Phase 10 commit `3727eaf2d2`) | P10 | Resolved (historical) | Already shipped in Phase 10 |
| 5 | Residual launch-click nondeterminism on seL4 (Phase 11 §11.3 item 1) | P10/P11 | **Resolved in 14** | W11 (stability workload's launch-click sentry) |
| 6 | `panel.position` is boot-time-only (Phase 11 §11.3 item 3) | P11 | Re-scoped | Phase 15+ — dual-domain visibility toggle OR WM-managed panel placement |
| 7 | Partial drag delta on themed chrome (Phase 11 §11.3 item 4) | P11 | **Resolved in 14** | W11 (drag-delta assertion in `sponge-wm-tasks.run`) |
| 8 | PS/2 REL click lands ~10 px below button on the launcher entry (Phase 11 §11.3 item 1) | P11 | Resolved (historical) | Phase 12 W3b usb-tablet recipe |
| 9 | Headless QEMU / SDL cursor escape hatch | P10/P11 | Not-a-defect | `run_genode_until forever` + `-display sdl` is the documented control escape hatch |
| 10 | Panel widgets: only clock + launcher S (no system tray, no applet, no tasklist) | P11 | **Resolved in 14** | W7 (tasklist module) + D14.7 (system tray deferred) |
| 11 | Theme active re-paint only partial on some widgets | P11 | **Resolved in 14** | W9 stability workload's theme-reload cycle exposes it; W11 fixes as needed |
| 12 | Cursor invisible under PS/2-only input (Phase 11 patch #9 partial fix) | P11 | Re-scoped | Report-ROM gap is upstream-shaped; patch #9 covers pointer-side |
| 13 | themed_decorator live asset re-skin (Phase 11 §11.3 item 2) | P11 | **Resolved in 14** | W7 (vendored patch per D14.8 candidate (d) OR decorator-restart follow-up) |
| 14 | `panel.position` persistence (duplicate of #6) | P11 | Re-scoped | Same as #6 |
| 15 | decorator controls missing `closer` + `minimizer` (Phase 11 §11.3 item 2) | P11 | **Resolved in 14** | W7 (themed_decorator extension) |
| 16 | Unknown-theme fallback hardened (commit shipped) | P11 | Resolved (historical) | Phase 11 |
| 17 | Deprecated theme aliases not removed (`error_bg` / `error_text` etc.) | P11 | **Resolved in 14** | W11 (`theme_loader.cc` cleanup) |
| 18 | Parsed-but-unused theme keys (`title_family`, `icon_size`, `popup_width`, `popup_entry_min_height`) | P11 | **Resolved in 14** | W11 (`theme_loader.cc` cleanup) |
| 19 | Theme transport cap raised 2048→8192 (commit shipped) | P11 | Resolved (historical) | Phase 11 |
| 20 | Decorator policy color live via `sponge_decorator_bridge` | P11 | Resolved (historical) | Phase 11 |
| 21 | Boot module q35+Skylake-Client pin (Phase 12 W1) | P12 | Resolved (historical) | Phase 12 |
| 22 | AHCI default + NVMe opt-in (`tool/dist --storage {ahci,nvme}`) | P12 | Resolved (historical) | Phase 12 |
| 23 | i440fx IDE smoke-only (not a real i440fx) | P12 | Not-a-defect | docs/15 row honest-claim text |
| 24 | Multi-namespace NVMe gap | P12 | Re-scoped | Phase 15+ — multi-namespace needs NVMe 1.2+ driver changes |
| 25 | `pc_nic` only `e1000` verified (rtl8169/Wi-Fi/USB-Ethernet untested) | P12 | Re-scoped | Phase 15+ — additional NIC driver bring-up |
| 26 | USB HID keyboard glyph-delta gap (probe-focus ROM quirk) | P12 | Re-scoped | Phase 12 already documented; PS/2 send-key path covers |
| 27 | rtl8169/Wi-Fi/USB-Ethernet not QEMU-tested | P12 | Re-scoped | Phase 15+ |
| 28 | `i2c_hid` not implemented | P12 | Re-scoped | Phase 15+ |
| 29 | calculator / pdf_view not wired into Alpha media | P13 | **Resolved in 14** | W8 (workflow scenario pre-stages both) + D14.10 |
| 30 | `pkg_import` broader coverage deferred (D13.5) | P13 | Re-scoped | No Phase 14 demand; revisit Phase 15+ if a third Qt6 source-built package is needed |
| 31 | Latent `<env>` format bug (Phase 13 fix in `pkg/terminal/metadata.xml`) | P13 | Resolved (historical) | Phase 13 |
| 32 | Prompt-detection race (Phase 13 fix in `terminal_probe`) | P13 | Resolved (historical) | Phase 13 |
| 33 | Three terminal-hosting scenarios share duplicated noux build list | P13 | **Resolved in 14** | W11 (`run/terminal_runtime.inc`) |
| 34 | docs/13 app-set drift (Alpha app set description) | P13 | **Resolved in 14** | W2 (docs/13 amendment) |
| 35 | Authoring guide `docs/16-package-authoring.md` (three paths delivered) | P13 | Resolved (historical) | Phase 13 |
| 36 | Terminal toolset tars not in pre-staged list (decided to leave as `vct install terminal`) | P13 | Re-scoped | Decision stands — explicit install keeps the boot image small |
| 37 | Falkon not in daily scenario on seL4 | P13 | **Resolved in 14** | W10 (rescue attempt OR D14.5 amendment) |
| 38 | falkon QPA disabled on seL4 | P13 | **Resolved in 14** | W10 (same — D14.5) |
| 39 | Notifications not implemented (Phase 5 placeholder `Genode::warning`) | P5 | **Resolved in 14** | W4 (`sponge_notifier` + panel widget) |
| 40 | Clipboard not implemented | P5/P6 | **Resolved in 14** | W5 (upstream server reuse + Qt bridge) |
| 41 | Window controls (closer, minimizer) missing | P11 | **Resolved in 14** | W7 (duplicate of #15) |
| 42 | Settings revert on reboot (`sponge_configd` in-memory only) | P4 | **Resolved in 14** | W6 (`<vfs>` activation) |
| 43 | `vct` live resource stats not implemented (`commands.cc:158`) | P7 | **Resolved in 14** | W11 (`vct status --resources`) |
| 44 | `report_rom` single-writer limitation | Arch | Not-a-defect | Architecture boundary (`AGENTS.md` §1.2); single-writer is by design |
| 45 | QPA misroutes ABS (duplicate of #1) | P10 | Re-scoped | Same as #1 |
| 46 | QGenodeScreen 1×1 race | P11 | Blocked | W3 reproducer; if not reproduced in < 3/10 trials, `Re-scoped` to Phase 15+ |
| 47 | Panel clock QTimer 1 s leak suspect | P11 | **Resolved in 14** | W11 (leak audit) |
| 48 | Theme reload QTimer 250 ms leak suspect | P11 | **Resolved in 14** | W11 (leak audit) |
| 49 | Config reload QTimer 250 ms leak suspect | P11 | **Resolved in 14** | W11 (leak audit) |
| 51 | Launcher popup `qApp` eventFilter lifetime (commit `3727eaf2d2`) | P10 | Resolved (historical) | Phase 10 |

(Total rows = 50; row #50 is the seL4 EGL hang fix from
`docs/09-roadmap.md` §11.1 — recorded as resolved historical;
collapsed out of the implementation list. The "duplicate of"
notes keep the matrix exhaustive without losing the
classification-by-row pattern.)

---

## Verification Contract

Per `AGENTS.md` §4.2:

- **Every new feature ships a boot-verified scenario.** Every W
  item's "Pass conditions" names a specific run scenario, a
  specific PASS marker string, and a specific QEMU configuration
  (`KERNEL=sel4 BOARD=pc`). The scenario follows the
  `sponge-*.run` naming + probe pattern; builds `lib/ld` + `core
  init <component>`; passes `[build_artifacts]` to
  `build_boot_image`.
- **Misleading-success-output defense**: every new scenario has a
  focused assertion beyond `exit 0`. Captures pixel checks,
  structural report reads, and bounded-byte log matches (e.g.
  the `Sponge Phase 14 clipboard sentinel` byte match in W5).
- **Per-criterion traceability** (the Phase 12 contract): every
  Phase 14 criterion maps to one or more scenarios, each with an
  exact PASS marker and a `docs/evidence/phase14-*.log`
  reference. The mapping lives in
  `docs/evidence/phase14-index.md` and is referenced from
  `docs/09-roadmap.md` §10 Phase 14.
- **No vendored-tree patches are absorbed silently.** Every patch
  (D14.8 candidates) gets a `docs/11-environment.md` §4 ledger
  row BEFORE the patch is applied; the row is committed first,
  the patch commit second.
- **Atomic commits** per `AGENTS.md` §4.3. Commit units are
  listed per W item. Conventional-commits style. Test commits
  land BEFORE implementation commits (TDD orientation).
- **Final regression sweep** (W12) runs every Phase 14 scenario
  + every Phase 7/10/11/12/13 scenario touched by a Phase 14
  code path. Serialized (`make -j1`); receipts in
  `docs/evidence/phase14-envelope-*.log`.

---

## Commit Strategy

Atomic units are listed per W item. The summary:

- **Wave 1**: 4 commits (W1 fix + W1 stub, W2 docs, W2
  wm-state-table, W3 reproducer + docs/09 follow-up).
- **Wave 2**: ~12 commits (W4 × 6, W5 × 5, W6 × 3 — includes
  the sub-design decision in W5 #1).
- **Wave 3**: ~10 commits (W7 × 5).
- **Wave 4**: ~6 commits (W8 × 3, W9 × 4 with the fastfail
  variant, W10 × 1–2 depending on path).
- **Wave 5**: ~20 commits (W11 × 15 items + W12 × 5).

Total ≈ 50 commits. The "test-first" rule means each W item's
first commit is the failing scenario + its initial log; the
last commit is the source change that flips the marker to PASS.
This makes every step of the phase bisectable (a regression in
W7's tasklist does not silently invalidate W4's notifier work).

The D14.5 amendment path is its own commit with a body like:

```
docs(roadmap): amend Phase 14 criterion 3 (browser) per D14.5

The seL4 cap-ceiling rescue attempt (W10) exhausted the 2-day
effort bound without reaching first paint; the upstream Genode
clipboard server (reused in W5) is unaffected. This commit
amends the Phase 14 criterion 3 wording to "browser OR
equivalent file/WebKit consumer" and records the evidence in
docs/evidence/phase14-index.md. The daily workflow scenario
uses files/textedit/terminal/calculator/pdf_view; the daily-
workflow criterion is honest about the browser absence.

Refs: D14.5, docs/evidence/task-10-phase14-falkon-rescue.log,
docs/evidence/phase14-index.md.
```

---

## Open Questions (recorded, not blocking)

1. **Qt clipboard backend in qt6_base** — does the vendored
   Genode 26.05 qt6_base QPA expose a clipboard backend that
   can be bound to the upstream `os/src/server/clipboard`? If
   not, the minimal QPlatformClipboard subclass is Sponge-owned
   work (W5 sub-design). Resolved by reading
   `genode/repos/libports/src/qt6/base/.../qgenodeplatformintegration.{h,cc}`
   in W5 #1.
2. **Layout choice for the panel tasklist** — below the launcher
   S toggle (horizontal strip) vs the right edge of the panel
   (vertical column). The horizontal strip is easier for
   deterministic QMP clicking (Phase 14 default); the vertical
   column scales better for many windows (Phase 15+ default).
   Resolved in W7 sub-design based on the panel width budget.
3. **Candidate (b) QGenodeScreen 1×1 race** — if W3's
   reproducer doesn't pin it, the item moves from `Blocked` to
   `Re-scoped`. The classification flip is committed in W3.
4. **Falkon cap-ceiling rescue depth** — patches #6 + #7 from
   `docs/11-environment.md` §4 partially mitigate. Whether the
   remaining gap is closable with a small additional patch (the
   "10th ledger row" path) or requires a deeper architectural
   change (the "deferred to Phase 15" path) is decided in W10.
5. **The `stability-summary.xml` format** — not yet designed;
   W9 specifies it (RSS, cap counts, cycle count, sentinel
   pass rate, watchdog triggers, child-exit events). The
   format is documented in W9's commit message.
6. **`vct status --resources` semantics** — what the command
   actually reports (RSS, cap count, per-component breakdown?)
   is decided in W11 #13. The default is "RSS + cap count for
   the init tree", matching the upstream `init` state report
   shape.
7. **Whether the workflow scenario should attempt a real `make
   build` from inside the booted terminal** — the criterion 3
   "do real work" claim is honest about it either way; the
   build-in-terminal path would prove the toolset wiring more
   strongly. W8 default = "type and read a result"; the
   build-in-terminal variant is a Phase-15 hardening.

---

## Execution Guidance (Task Dependency Graph, Parallel Waves, TODO List)

### Task Dependency Graph

| Task | Depends On | Reason |
|------|------------|--------|
| **W1** (staging fix + W5/W6 stubs) | None | Starting point; unblocks every other seL4 scenario |
| **W2** (design-doc amends + wm-state-table) | None | Independent doc work; gates W7 |
| **W3** (QGenodeScreen race triage + docs/09 follow-up) | None | Pure investigation; can run alongside W1+W2 |
| **W4** (notification daemon + panel widget + vct hook) | W1 | Needs the W1 staging fix on seL4 |
| **W5** (clipboard service integration) | W1 | Needs the W1 staging fix on seL4 |
| **W6** (configd persistence activation) | W1 | Needs the W1 staging fix on seL4 |
| **W7** (decorator controls + tasklist + state machine) | W2, W4 | The wm-state-table is the implementation reference; the notifier daemon's panel widget reuses tasklist geometry code |
| **W8** (workflow scenario) | W4, W5, W6, W7 | The workflow integrates all upstream services |
| **W9** (stability workload) | W8 | Stability runs against the workflow's component stack |
| **W10** (Falkon rescue + criterion amendment) | W8, W9 | The rescue uses the proven workflow topology |
| **W11** (paper-cut implementations) | W1, W4–W9 | Implements items that were discovered/landed during the upstream W items |
| **W12** (close-out: roadmap + evidence index + sweep) | W1–W11 | Closes the phase durably |

### Parallel Execution Graph

- **Wave 1** (start immediately; no dependencies):
  - W1 — staging fix + W5/W6 scenario stubs
  - W2 — design-doc amends + `wm-state-table.md`
  - W3 — QGenodeScreen race triage + docs/09 follow-up
- **Wave 2** (after W1; can run in parallel):
  - W4 — notification daemon
  - W5 — clipboard service integration
  - W6 — `sponge_configd` persistence activation
- **Wave 3** (after W2 + W4):
  - W7 — decorator + tasklist + window state machine
- **Wave 4** (after W4, W5, W6, W7):
  - W8 — workflow scenario
  - W9 — stability workload
- **Wave 5** (after W8 + W9):
  - W10 — Falkon rescue + criterion amendment
- **Wave 6** (after W1–W10):
  - W11 — paper-cut implementations
  - W12 — close-out

Critical path: **W1 → W4 → W7 → W8 → W9 → W10 → W12** (notification
is the heaviest single-feature W item; workflow depends on it;
stability depends on the workflow; close-out depends on all).

Estimated parallel speedup: ~35% over serial (Wave 2 is the
biggest parallel block; W5 and W6 share infrastructure but their
scenarios are independent).

### Delegation Recommendations (per task)

| Task | Category | Skills to load | Reason |
|------|----------|----------------|--------|
| **W1** | `unspecified-low` | `git-master` | Mechanical scenario-side `cp` move; one-file-per-scenario edit |
| **W2** | `writing` | (none) | Pure doc amends + the wm-state-table markdown; the table is a markdown artifact, not code |
| **W3** | `unspecified-low` | `ast-grep` | Focused reproducer + log inspection; `ast-grep` helps scan the qt6_base sources for the QGenodeScreen class |
| **W4** | `visual-engineering` + `unspecified-high` | `git-master`, `programming`, `frontend`, `visual-qa` | Daemon + panel popover UI; the UI half is `visual-engineering`, the daemon half is `unspecified-high` |
| **W5** | `unspecified-high` | `git-master`, `programming`, `debugging` | Cross-component Qt ↔ Genode clipboard bus; `debugging` for the qt6_base QPA sub-design decision |
| **W6** | `unspecified-low` | `git-master`, `programming` | One-file configd change + a focused scenario; mechanical |
| **W7** | `visual-engineering` + `unspecified-high` | `git-master`, `programming`, `frontend`, `visual-qa` | Tasklist UI + decorator extension + state machine; the UI half is `visual-engineering`, the decorator + state machine half is `unspecified-high` |
| **W8** | `ultrabrain` | `git-master`, `programming`, `debugging` | 7-step sequencer + cross-component integration; the cleanest cross-cutting scenario in the phase |
| **W9** | `ultrabrain` | `git-master`, `programming`, `debugging` | Stability workload + watchdog + crash detection; the highest-risk item in the phase |
| **W10** | `deep` | `git-master`, `programming`, `debugging` | Bounded Falkon rescue — one focused goal, autonomous problem-solving within the 2-day bound |
| **W11** | `unspecified-low` + `quick` | `git-master`, `programming` | Per-item small commits; each row in the appendix is a small targeted fix |
| **W12** | `writing` + `review-work` | `git-master`, `review-work` | Docs + evidence index + regression sweep; `review-work` orchestrates the post-implementation review |

**Skill inclusions / omissions**:

- `git-master` INCLUDED on every W item that produces a commit —
  per its trigger description ("MUST USE for ANY git operations").
- `programming` INCLUDED on every W item that touches
  `repos/sponge/src/**` (`.cc` / `.h`) — per its trigger.
- `mojo-syntax` OMITTED — none of the W items touch `tool/*.mojo`.
- `frontend` INCLUDED for the panel tasklist + notifier widget
  work — per its trigger ("MUST USE for frontend ... UI/UX
  work").
- `visual-qa` INCLUDED for the UI W items — per its trigger
  ("MUST USE after building/changing any UI").
- `debugging` INCLUDED for the cross-component integration items
  (W5, W8, W9, W10) — per its trigger ("MUST USE for any real
  runtime debugging").
- `ast-grep` INCLUDED for W3 — per its trigger ("structural code
  matching ... qt6_base QPA scan").
- `review-work` INCLUDED for W12 — per its trigger ("post-
  implementation review orchestrator").
- `mojo-gpu-fundamentals`, `mojo-python-interop`,
  `new-modular-project` OMITTED — none of the W items touch Mojo
  GPU code, Mojo-Python interop, or new Mojo project setup.
- `data-scientist`, `lsp-setup`, `ultimate-browsing`,
  `ulw-research`, `security-research`, `security-review`,
  `customize-opencode` OMITTED — none of the W items touch data
  analysis, LSP setup, blocked-web browsing, ulw-research,
  security research/review, or opencode configuration.
- `refactor` OMITTED — no W item is a refactor; the W items are
  net-new features and small bug fixes.
- `remove-ai-slops` OMITTED — the user is the planning agent;
  this skill is for post-implementation cleanup, not planning.

### TODO List (ADD THESE)

#### Wave 1 (start immediately; no dependencies)

- [ ] **1. W1 — Staging fix + W5/W6 stubs**
  - What: move Qt6 `cp` calls in `sponge-de-test.run` and
    `sponge-launcher.run` BEFORE `build_boot_image`; boot-verify
    both PASS on base-sel4; author `sponge-configd-persist.run`
    scenario skeleton (failing).
  - Depends: None
  - Blocks: W4, W5, W6
  - Category: `unspecified-low`
  - Skills: `git-master`
  - QA: `make -C genode/build/x86_64 run/sponge-de-test` and
    `run/sponge-launcher` both exit 0 on base-sel4; the
    `sponge-configd-persist.run` skeleton boots and fails at the
    expected sentry.

- [ ] **2. W2 — Design-doc amends + wm-state-table**
  - What: amend `docs/05-sponge-de.md` §5.1, §5.4, §7; amend
    `docs/07-leitzentrale.md` §3.2; author
    `docs/plans/wm-state-table.md`; update `docs/13-installation.md`
    Known Limitations.
  - Depends: None
  - Blocks: W7 (the state-table is the implementation reference)
  - Category: `writing`
  - Skills: (none)
  - QA: `git diff --stat` matches the expected file list; the
    wm-state-table is reviewed before W7 starts.

- [ ] **3. W3 — QGenodeScreen race triage + docs/09 follow-up**
  - What: author `run/sponge-de-qscreen-race-probe.run` (10 cold-
    boot + immediate QMP click trials); record the result; add
    the nitpicker-pointer-ROM deferral note to `docs/09-roadmap.md`
    §11 known-issue follow-ups.
  - Depends: None
  - Blocks: nothing (the only vendored patch this might force is
    gated by the W3 result)
  - Category: `unspecified-low`
  - Skills: `ast-grep`
  - QA: 10 trial log in
    `docs/evidence/task-3-phase14-qscreen-race.log`; the
    classification flip (if any) is committed.

#### Wave 2 (after W1; parallel)

- [ ] **4. W4 — Notification daemon (`sponge_notifier`)**
  - What: new component `repos/sponge/src/sponge_notifier/`;
    panel `notifier_widget`; vct hook; `run/sponge-notify.run`
    (cross-component post + render proof); the
    `--without-notifier` no-crash variant.
  - Depends: W1
  - Blocks: W7 (panel widget reuse), W8 (workflow uses notifications)
  - Category: `visual-engineering` + `unspecified-high`
  - Skills: `git-master`, `programming`, `frontend`, `visual-qa`
  - QA: `make -C genode/build/x86_64 run/sponge-notify` PASS on
    base-sel4 + QMP; the `--without-notifier` variant exits 0
    with the vct warning line.

- [ ] **5. W5 — Clipboard service integration**
  - What: stage upstream `clipboard` binary; qt6_base QPA
    sub-design decision (reuse-or-add QPlatformClipboard);
    `run/sponge-clipboard.run` (cross-component copy + paste
    proof with the `Sponge Phase 14 clipboard sentinel` byte
    match); the `match_labels="yes"` focus-gating sub-scenario.
  - Depends: W1
  - Blocks: W8 (workflow uses clipboard)
  - Category: `unspecified-high`
  - Skills: `git-master`, `programming`, `debugging`
  - QA: `make -C genode/build/x86_64 run/sponge-clipboard` PASS
    on base-sel4 + QMP; the byte-match assertion is reproduced;
    `sponge-textedit.run` regression still PASSes.

- [ ] **6. W6 — `sponge_configd` persistence activation**
  - What: `<vfs>`-backed store (`store.xml.tmp` + rename) in
    `sponge_configd`; `run/sponge-configd-persist.run` (three
    writes, restart, most-recent value asserted); the
    `corrupt_store.xml` recovery variant; `<vfs>` node wired into
    `sponge-alpha.run`.
  - Depends: W1
  - Blocks: W8 (workflow boots with persisted settings)
  - Category: `unspecified-low`
  - Skills: `git-master`, `programming`
  - QA: `make -C genode/build/x86_64 run/sponge-configd-persist`
    PASS on base-sel4; the corrupt-store variant logs the warning
    and exits 0; `sponge-alpha.run` regression still PASSes.

#### Wave 3 (after W2 + W4)

- [ ] **7. W7 — Decorator controls + panel tasklist + window state machine**
  - What: extend `themed_decorator` `<controls>` with `closer` +
    `minimizer` via `sponge_decorator_bridge`; new
    `repos/sponge/src/sponge-de/panel/tasklist/` module
    (widget + controller subscribing to wm `window_list`);
    implement every row of `docs/plans/wm-state-table.md`;
    `run/sponge-wm-tasks.run` (minimize → dim → tasklist-click
    restore → Alt-Tab order → closer removal).
  - Depends: W2 (state table), W4 (panel widget geometry reuse)
  - Blocks: W8 (workflow uses minimize/restore)
  - Category: `visual-engineering` + `unspecified-high`
  - Skills: `git-master`, `programming`, `frontend`, `visual-qa`
  - QA: `make -C genode/build/x86_64 run/sponge-wm-tasks` PASS on
    base-sel4 + QMP; every state-table row has a probe sub-step;
    `sponge-alpha.run`, `sponge-de-themed-chrome.run`,
    `sponge-wm-qmp.run` regressions still PASS.

#### Wave 4 (after W4 + W5 + W6 + W7)

- [ ] **8. W8 — Workflow scenario (criterion 3)**
  - What: `run/sponge-de-workflow.run` driving the 7-step
    protocol (boot → terminal type → textedit type → cross-
    component copy/paste → minimize/restore → calculator →
    `vct shutdown` → acpica S5) against the pre-staged five-
    package set (D14.10).
  - Depends: W4, W5, W6, W7
  - Blocks: W9 (stability runs on this stack), W10
  - Category: `ultrabrain`
  - Skills: `git-master`, `programming`, `debugging`
  - QA: `make -C genode/build/x86_64 run/sponge-de-workflow` PASS
    on base-sel4 + QMP within 900 s; the S5 shutdown gate matches
    the `sponge-power.run` contract.

- [ ] **9. W9 — Stability workload (criterion 1)**
  - What: `repos/sponge/src/test/stability_probe/` (cycle loop,
    resource snapshots, 5 s UI watchdog, child-exit detection);
    `run/sponge-de-stability.run` (30 min, ≥ 100 cycles, ΔRAM <
    50 MB, Δcaps < 100); the `sponge-de-stability-fastfail.run`
    crash-injection variant.
  - Depends: W8
  - Blocks: W10, W12
  - Category: `ultrabrain`
  - Skills: `git-master`, `programming`, `debugging`
  - QA: `make -C genode/build/x86_64 run/sponge-de-stability`
    PASS on base-sel4 with the parsed `stability-summary.xml`
    inside thresholds; the fastfail variant proves the watchdog
    fires.

#### Wave 5 (after W8 + W9)

- [ ] **10. W10 — Falkon rescue attempt + criterion-amendment decision**
  - What: reproduce the seL4 first-paint failure; attempt 1 =
    disk-served topology (`sponge-falkon-rescue.run`); attempt 2 =
    vendored cap-ceiling patch (ledgered as docs/11 §4 row 10);
    on failure inside the 2-day bound, amend criterion 3 wording
    per D14.5 with the evidence pointer.
  - Depends: W8, W9
  - Blocks: W12
  - Category: `deep`
  - Skills: `git-master`, `programming`, `debugging`
  - QA: exactly one of the three D14.5 paths is committed
    atomically with its scenario log; `sponge-alpha.run`
    regression still PASSes.

#### Wave 6 (after W1–W10)

- [ ] **11. W11 — Paper-cut implementations (`Resolved in 14` rows)**
  - What: implement every `Resolved in 14` row of the Paper-cut
    Disposition Appendix not already closed by W1–W10: theme
    loader cleanup (#17, #18), `run/terminal_runtime.inc` (#33),
    `vct status --resources` (#43), the QTimer leak audit +
    `sponge-de-leak-audit.run` (#47–#50), plus the focused
    assertions for #5 and #7.
  - Depends: W1, W4–W9
  - Blocks: W12
  - Category: `unspecified-low` + `quick`
  - Skills: `git-master`, `programming`
  - QA: `make -C genode/build/x86_64 run/sponge-de-leak-audit`
    PASS on base-sel4; each resolved row's commit body names the
    row number and its PASS evidence; `sponge-de-stability.run`
    still PASSes afterward.

- [ ] **12. W12 — Close-out**
  - What: flip the four Phase 14 roadmap checkboxes (with the
    D14.5 amendment note if taken); README current-status update;
    author `docs/evidence/phase14-index.md`; full serialized
    regression sweep (`make -j1`, receipts in
    `docs/evidence/phase14-envelope-*.log`); Phase 15+ handoff
    notes for every `Re-scoped` row.
  - Depends: W1–W11
  - Blocks: nothing (phase complete)
  - Category: `writing` + `review-work`
  - Skills: `git-master`, `review-work`
  - QA: the regression sweep exits 0 for every scenario in the
    Phase 14 suite; the roadmap checkboxes are internally
    consistent with `docs/evidence/phase14-index.md`.

