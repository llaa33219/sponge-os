# Phase 16, Custom Sponge WM (Work Plan)

> Status: docs-first scope registration, checked in 2026-11-12 and
> approved by the user the same day (verbatim approval: "승인한다.
> 시작하라", "approved, begin"). No native WM code and no native
> scenario exist yet; this plan is the decision-complete spec an
> implementation executes without re-interview.
> Relationship: Extension Criterion 16 of the reopened Phase 16
> (`docs/09-roadmap.md` §10, scope expansion 2026-11-10, criterion
> text re-registered 2026-11-12), alongside the daily-desktop-defaults
> plan (`docs/plans/phase16-daily-desktop-defaults.md`, D16.x) and the
> runtime overhaul plan (`docs/plans/phase16-runtime-overhaul.md`,
> D16R.x). Decision IDs here are **D16W.x**.
> State machine: `docs/plans/wm-state-table.md` (D14.3 baseline plus
> the native extension landed together with this plan). Frozen
> protocol surface: `docs/05-sponge-de.md` §4.6. Patch ledger:
> `docs/11-environment.md` §4 (currently 38 rows; must not grow).

---

## 1. Goal

Deliver Criterion 16 in full: a Sponge-native window manager with
**user-directed floating plus Master-Stack tiling** and **native
modern window chrome**, built as Sponge components under
`repos/sponge/src/` on public Genode interfaces only. The upstream
`wm` + `window_layouter` + `decorator` trio stays untouched as the
fallback until native parity. The delivered unit is one vertical
slice: native core, native layout policy, native chrome. Criterion 16
is worked to completion first, then Criterion 18 (D16W.9), and this
plan marks nothing complete.

## 2. Recorded User Approvals (2026-11-12, binding)

The user approved the inline plan ("승인한다. 시작하라"). Recorded
choices, do not re-litigate:

1. **Execution order.** Criteria 16 and 18 are worked continuously to
   full delivery, Criterion 16 first and complete, then Criterion 18,
   each landing as logical commits.
2. **Native chrome in the first delivered WM version.** A native
   modern title bar and native close/minimize/fullscreen buttons ship
   in the very first delivered WM version. `themed_decorator` plays no
   role in the native path.
3. **Master-Stack tiling** is the tiling policy.
4. **One global layout mode key**, `wm.layout.mode` = `floating` |
   `tiling`, default `floating`.
5. **Master/stack rule.** The first non-minimized, non-maximized
   window is the left-half master; the remaining windows are the
   right-half stack in creation order.
6. **Non-resizeable windows float.** They never enter the tiling
   arrangement.
7. **Maximized fills the work area** and the toggle restores the
   previous geometry.
8. **Minimized uses the existing <=-32000 report convention** so the
   `TasklistController` keeps working unchanged.
9. **Three components** with the boundaries in D16W.1, public Genode
   interfaces only, no private gems headers, no vendored patches.
10. **Native core replaces `server/wm` in the same delivered vertical
    slice** (rationale in D16W.3).
11. **Upstream fallback preserved** until native parity (D16W.7).
12. **TasklistController-facing compatibility frozen** at protocol v1
    with single-writer discipline (D16W.6).
13. **Parity definition** as in D16W.7.
14. **Milestones M0-M6**, the QMP acceptance battery, the
    >=20-cycle rules-drift soak, the no-new-ledger-row check, UX
    metrics, doc/evidence obligations, and the non-goals of §5 are the
    approved plan content.

## 3. Baseline (2026-11-12, present truth)

- **Topology today.** Upstream `server/wm` + `window_layouter` +
  `decorator` (motif on release media; `themed_decorator` plus the
  Phase 11 `sponge_decorator_bridge` on the themed-chrome path). No
  Sponge-owned WM code exists at plan time.
- **TasklistController contract** (`docs/05-sponge-de.md` §4.6):
  subscribes to the wm `window_list` report and the layouter
  `window_layout` report; writes the layouter `rules` ROM
  (`rules="rom"` mode) and emits `focus_request`; is the **sole
  writer** of `focus_request` and `rules`. This contract is frozen
  (D16W.6).
- **State machine.** `docs/plans/wm-state-table.md` (D14.3 baseline
  rows are delivered W7 truth; the native extension rows are the
  target spec, honestly labeled).
- **Upstream anchor scenarios** (drive the upstream trio, never
  renamed): `run/sponge-wm.run`, `run/sponge-wm-qmp.run`,
  `run/sponge-wm-tasks.run`.
- **Known honesty item.** Phase 16 W9 recorded the themed-chrome
  full 8-zone resize sequence as flaky with each zone individually
  green. The native path must prove the full 8-zone sequence in one
  scenario run or record an honest partial. Partial is never
  relabeled green.
- **Config machinery.** `sponge_configd` closed typed registry
  (Phase 11/14/16 validator discipline) and the D16.1 label-to-key
  domain ACL model. The `wm_settings` first-party provider named in
  D16.1/D16.3 and Criterion 18 is the later settings UI owner; nothing
  in this plan changes Settings implementation scope.
- **Ledger budget.** D16.8: no silent vendored patches. This plan
  predeclares zero new patches and zero new ledger rows.

## 4. Binding Decisions (do not re-litigate)

| ID | Decision | Rationale |
|---|---|---|
| **D16W.1** | **Three components, one vertical slice.** `repos/sponge/src/sponge_wm` owns Gui virtualization, the window registry, input routing, and native content association (binding each client's content to its registry entry and its decoration). `repos/sponge/src/sponge_layouter` owns policy, state, and layout: mode handling, Master-Stack placement, floating move/resize application, minimize parking, work-area maximize/restore, and the `window_layout` / protocol-side reports. `repos/sponge/src/sponge_decorator` owns lightweight modern theme-driven chrome: the title bar and the native close/minimize/fullscreen buttons, styled from theme files per AGENTS.md §3.4 (no hardcoded visuals). Each component follows the AGENTS.md §3.2 layout (`target.mk`, `main.cc`, `README.md`). | AGENTS.md §1.4 forbids monolithic components. Policy/state/layout split from Gui/input plumbing and from chrome drawing gives each piece one responsibility and keeps the decorator replaceable. |
| **D16W.2** | **Public Genode interfaces only.** All three components use public Genode interfaces and public IPC surfaces. No private gems headers, no vendored patches to `genode/`, and no new `docs/11-environment.md` §4 ledger row. The WM itself is required to be a clean Sponge implementation: what is forbidden is copying upstream implementation code or depending on private upstream implementation details or interfaces, not writing a window manager. | AGENTS.md §5.2 (keep Sponge code in `repos/sponge/`, do not depend on Genode internal headers or private APIs) + D16.8. The whole point of owning the core is to stop depending on upstream internals, so the implementation is ours but its private details are not our dependencies. |
| **D16W.3** | **The native core replaces `server/wm` in the same delivered slice as the native chrome.** A decorator's association with client windows (how decoration content is bound to a window, which window a decoration belongs to, how the two move together) is part of upstream's private decorator association semantics inside `server/wm` and its gems-internal headers. Native chrome bolted onto upstream `wm` would have to speak that private protocol or reimplement its private semantics, which is exactly the dependence D16W.2 forbids. Owning the core is what lets `sponge_decorator` associate with windows through a Sponge-owned, public contract. Shipping chrome against upstream `wm` first would bake the forbidden dependence into the first delivery, so core and chrome land together as one tested vertical slice. | The alternative (native chrome on upstream core) cannot satisfy D16W.2. Sequencing chrome later would leave the first delivered version on `themed_decorator`, contradicting recorded approval 2. |
| **D16W.4** | **Native modern chrome is in the first delivered WM version.** Native title bar plus native close/minimize/fullscreen buttons, drawn by `sponge_decorator`. `themed_decorator` and `sponge_decorator_bridge` are fallback-path components only and are not wired into the native topology. | Recorded approval 2. |
| **D16W.5** | **Layout policy.** Global `wm.layout.mode` = `floating` \| `tiling`, default `floating`. Floating is user-directed placement and 8-zone resize. Tiling is Master-Stack: first non-minimized/non-maximized window in creation order is the left-half master, the remaining windows are the right-half stack in creation order (equal bands, top to bottom), reflowing on open, close, minimize, restore, maximize, maximize-restore, and mode switch. Non-resizeable windows always float. `Maximized` fills the work area (screen minus panel) and the toggle restores the saved previous geometry. Minimized windows park at the established `(x=-32000, y=-32000)` report convention (a reported window whose x and y are both at or below -32000 reads as parked). Leaving tiling mode keeps each window's current geometry (no hidden pre-tile history). Transition detail lives in `docs/plans/wm-state-table.md`. | Recorded approvals 3-8. Deterministic rules with no hidden history keep the state table executable as a spec. |
| **D16W.6** | **Protocol v1 freeze and single-writer discipline.** The `TasklistController`-facing surface is frozen: `window_list` (produced by the wm core), `window_layout` (produced by the layouter), `focus_request` and `rules` (produced only by `TasklistController`). Shapes, labels, and meanings stay exactly as consumed today. The tasklist remains the **sole writer** of `focus_request` and `rules`; the native components never write them. **Documentation duty (fulfilled):** protocol v1 (exact report/ROM shapes and semantics) is written and frozen in `docs/05-sponge-de.md` §4.6 by this registration's protocol-contract docs commit (commit (c)), before any code or acceptance run; M0 re-verifies the frozen spec against the delivered consumers. | Recorded approvals 8 and 12. The tasklist is the delivered deterministic restore path (U3); silently changing its inputs would strand W7. |
| **D16W.7** | **Fallback preserved and parity defined.** The upstream `wm` + `window_layouter` + `decorator` trio stays untouched and keeps running in its own scenarios as the fallback until parity. `run/sponge-wm{,-qmp,-tasks}.run` are the upstream anchors and must stay green throughout. **Parity** is reached when (a) the native product scenarios pass with the upstream trio absent from the topology and (b) the upstream fallback anchors are still green. Until both hold, the fallback is the shipped path. | Recorded approvals 11 and 13. Fallback scenarios are the regression net while the native stack matures. |
| **D16W.8** | **Scenario naming.** New scenarios use `run/sponge-wm-native*.run`. Pinned names: `run/sponge-wm-native.run` (boot/interaction smoke), `run/sponge-wm-native-qmp.run` (QMP acceptance battery), `run/sponge-wm-native-tasks.run` (tasklist/protocol compat), `run/sponge-wm-native-tiling.run` (tiling open/close/mode/reflow). No existing `run/sponge-wm*.run` name is renamed or repurposed. | Recorded approval 14 + D16W.7. Name separation keeps the upstream anchors and the native gates independently runnable. |
| **D16W.9** | **Execution order and commit shape.** Full Criterion 16 first (all of M0-M6), then Criterion 18, in one continuous work stream. Every commit is one logical change per AGENTS.md §4.2/4.3 (conventional commits, `wm` scope). | Recorded approval 1. Continuous focus avoids a half-native WM sitting beside a half-migrated Settings. |
| **D16W.10** | **Honest v1 semantics.** In the initial delivered version, maximize and fullscreen both mean work-area maximization (screen minus panel). True full-screen is a later extension and is not claimed. | Recorded approval 7 + AGENTS.md §1.4 honest UX. The work-area fill keeps the panel reachable, which is the daily-usable default. |
| **D16W.11** | **Mode key ownership.** `wm.layout.mode` lands as a `sponge_configd` closed-registry key with a typed enum validator (`floating` \| `tiling`) and default `floating`. The `wm.*` key domain is reserved for the later `wm_settings` provider label per D16.1. Until that provider exists (Criterion 18), the write path is `vct config` (the admin/manual escape hatch, D16.1), which is what the acceptance scenarios use. This plan changes nothing in Settings implementation scope; the only Settings reference is the later `wm_settings` provider as the UI owner of `wm.*`. | Recorded approval 4 + D16.1 key-domain model. A typed global key keeps the default automatic and the manual door open (AGENTS.md §1.1) without touching the Settings provider plan. |

## 5. Non-Goals (explicitly out of Criterion 16)

- **Workspaces** (virtual desktops). Not designed, not stubbed.
- **Window gaps** (gaps between tiled windows and screen edges).
- **Snap layouts / edge snapping** (drag-to-edge zone previews).
- **Animations** (window open/close/move/resize animation of any kind).
- **Per-window rules** (persistent per-window layout or placement
  rules; `rules` stays the tasklist restore channel of D16W.6).
- **Master-ratio configuration** (the master/stack split is fixed at
  left-half / right-half in v1; no config key, no drag-to-ratio).
- **Per-window float-out by dragging.** In tiling mode a tiled
  window's geometry is layouter-owned in v1 (see the state table);
  floating is the global mode or the non-resizeable exception.
- **True full-screen** beyond work-area maximization (D16W.10).
- **Settings UI for WM keys.** Owned by the later `wm_settings`
  provider (Criterion 18); unchanged scope.
- **`themed_decorator` in the native path**, and any vendored patch or
  private-header use (D16W.2).
- **A physical-hardware gate.** Criterion 16 adds none (roadmap
  wording stands).
- **Renaming or repurposing upstream `run/sponge-wm*.run` scenarios**
  (D16W.7/D16W.8).

## 6. Implementation Milestones (M0-M6)

Each milestone lands as logical commits and keeps the upstream anchors
green. Nothing here is started before this plan is checked in.

### M0, Protocol v1 verification + baseline freeze

- Protocol v1 is already written and frozen in `docs/05-sponge-de.md`
  §4.6 (this registration's protocol-contract docs commit, commit
  (c)). At execution start, verify the frozen spec line by line
  against the delivered `TasklistController` consumption and the
  upstream report producers; a mismatch is a spec bug fixed in docs
  before code, never silently in code (D16W.6).
- The `wm-state-table.md` native extension is registered with this
  plan (same docs commit, commit (b)).
- The evidence index, its baseline anchors, and the honest W9 8-zone
  record are created at acceptance execution (M5), not here
  (AGENTS.md §5.6).
- Gate: spec verification pass. Still zero native code.

### M1, `sponge_wm` core

- Window registry, Gui virtualization, input routing, native content
  association (D16W.1). Emits `window_list` per protocol v1.
- `run/sponge-wm-native.run` boots the native core in place of
  `server/wm` with at least one client window.
- Gate: scenario boots; the window renders and receives input;
  `window_list` matches protocol v1 byte-for-byte against the frozen
  spec.

### M2, `sponge_layouter`, floating policy

- Consumes `focus_request` + `rules` (sole writer unchanged), emits
  `window_layout` per protocol v1. Floating move and 8-zone resize;
  state machine per `docs/plans/wm-state-table.md` (baseline rows +
  native rows); minimize parking at `(-32000,-32000)`; work-area
  maximize/restore with previous-geometry save (D16W.5).
- `run/sponge-wm-native-tasks.run` drives the delivered tasklist path
  against the native trio.
- Gate: tasklist minimize/restore/close/focus steps green with zero
  `TasklistController` changes.

### M3, `sponge_decorator`, native chrome

- Lightweight modern theme-driven chrome (AGENTS.md §3.4): native
  title bar, native close/minimize/fullscreen buttons, colors from
  theme files. Association with windows via the Sponge-owned core
  contract (D16W.3). `themed_decorator` not present in this topology
  (D16W.4).
- Gate: chrome renders on the native topology; close, minimize, and
  fullscreen buttons perform their transitions; title-bar drag moves
  a floating window.

### M4, Tiling + mode

- `wm.layout.mode` key registration (D16W.11); Master-Stack placement
  and all reflow triggers (D16W.5); non-resizeable float; work-area
  maximize/restore interaction with the arrangement; mode-switch
  geometry retention (state table native rows).
- `run/sponge-wm-native-tiling.run` covers open/close/mode/reflow.
- Gate: every tiling step green; `window_layout` reports match the
  expected arrangement after each reflow.

### M5, Acceptance battery

- `run/sponge-wm-native-qmp.run`: the full QMP list of §7 (move,
  8-zone resize, focus, close, minimize/restore+focus,
  maximize/restore). The 8-zone full sequence must pass in one run or
  be recorded as an honest partial (W9 honesty rule).
- Tiling acceptance carried by `run/sponge-wm-native-tiling.run`
  (open/close/mode/reflow).
- >=20-cycle rules-drift soak (§7).
- UX metrics measured and recorded (§7).
- Gate: all green, or every gap recorded as an honest partial. No
  relabeling.

### M6, Close-out

- Parity check (D16W.7): native product scenarios pass with the
  upstream trio absent from the topology, and
  `run/sponge-wm{,-qmp,-tasks}.run` are still green.
- No-new-ledger-row check (§7): `docs/11-environment.md` §4 still at
  38 rows, zero new patches (D16W.2/D16.8).
- Doc sync (AGENTS.md §5.4): `docs/05-sponge-de.md` native topology
  text, `docs/plans/wm-state-table.md` honest-status lines updated to
  delivered-or-not truth, roadmap Criterion 16 status touched only
  with real receipts.
- Evidence index `docs/evidence/phase16-wm-index.md` complete.
- Criterion 16 is checked **only** when every acceptance gate above
  has receipts. This plan does not check it.
- Then Criterion 18 work begins (D16W.9).

## 7. Verification Contract

All runs are QMP-driven on the native topology unless named as
upstream anchors. A failing or flaky gate is recorded as an honest
partial and never relabeled green.

1. **QMP acceptance tests** (`run/sponge-wm-native-qmp.run`, plus
   `run/sponge-wm-native-tiling.run` for the tiling row):
   - move (title-bar drag lands at the expected geometry),
   - 8-zone resize (all four edges and all four corners, full
     sequence in one run),
   - focus (title click and tasklist click),
   - close (native chrome closer),
   - minimize/restore with focus (chrome minimizer, then tasklist
     restore lands focused per the state table),
   - maximize/restore (chrome fullscreen button fills the work area,
     toggle restores previous geometry),
   - tiling open/close/mode/reflow (arrangement matches the state
     table after every trigger).
2. **Rules-drift soak, >=20 cycles.** One cycle = tasklist-driven
   `rules` write plus restore plus a tiling reflow (mode switch in
   and out). After every cycle the reported `window_layout` must
   equal the expected geometry exactly. Any drift fails the gate.
3. **No-new-ledger-row check.** `docs/11-environment.md` §4 row count
   unchanged (38) and no patch applied to `genode/`. Any temptation
   to patch stops work per D16.8 and comes back to the user.
4. **Upstream anchors stay green.** `run/sponge-wm.run`,
   `run/sponge-wm-qmp.run`, `run/sponge-wm-tasks.run`.
5. **Parity run.** The native product scenarios with the upstream
   trio removed from the init topology, plus the anchors of item 4.
6. **UX metrics** (AGENTS.md §5.1). Target interaction counts below
   are measured at M5 and recorded in the evidence index; a miss is
   reported as measured, not rounded.

| Task | Target interactions | Path |
|---|---|---|
| Minimize a window | 1 click | native chrome minimizer |
| Restore a window | 1 click | tasklist entry |
| Close a window | 1 click | native chrome closer |
| Maximize a window | 1 click | native chrome fullscreen |
| Restore from maximized | 1 click | same toggle |
| Move a window | 1 gesture | title-bar drag (floating) |
| Resize a window | 1 gesture | 8-zone drag (floating) |
| Focus a window | 1 click | title click or tasklist entry |
| Switch to tiling | 1 command | `vct config set wm.layout.mode tiling` (admin path; later 1 click via the `wm_settings` provider, Criterion 18) |

No task requires Genode terminology from the user.

## 8. Doc and Evidence Obligations

- **Protocol v1 documentation** in `docs/05-sponge-de.md` §4.6:
  landed and frozen with this registration's protocol-contract docs
  commit (commit (c)), before any code or acceptance run; re-verified
  at M0 and changed only via a protocol bump (D16W.6).
- **State table** (`docs/plans/wm-state-table.md`): registered with
  this plan; honest-status lines updated at M6 to match what actually
  landed.
- **Evidence index** `docs/evidence/phase16-wm-index.md`, created at
  acceptance execution (M5/M6), not at registration: curated receipts
  for the QMP battery, the soak, measured UX metrics, parity and
  anchor results, and the honest-partial register. Raw per-run logs
  are large transient run artifacts and are not promoted to
  `docs/evidence/` by default; only curated writeups and receipts are
  promoted (AGENTS.md §5.6).
- **Ledger check** recorded in the evidence index: zero new rows in
  `docs/11-environment.md` §4 (D16W.2).
- **Roadmap honesty**: `docs/09-roadmap.md` Criterion 16 stays
  unchecked until receipts exist; the checkbox is updated in the same
  commit as the receipts (AGENTS.md §5.4).
- **README** is touched only when behavior actually ships.

## 9. Commit and Execution Policy

- One logical change equals one commit (AGENTS.md §4.2), conventional
  commit messages with `wm` scope (AGENTS.md §4.3).
- This registration lands as three logical docs commits: (a)
  `docs/09-roadmap.md` Criterion 16 scope, (b) this plan plus the
  `docs/plans/wm-state-table.md` extension, (c) the
  `docs/05-sponge-de.md` §4.6 protocol-v1 contract. All three are
  docs-only and carry no code claim; implementation (M1 onward) starts
  only after (a)-(c) are in.
- Work order: M0-M6 to full Criterion 16, then Criterion 18,
  continuously (D16W.9). Criteria are never half-checked.
- Build and scenario verification after every milestone gate
  (AGENTS.md §4.2).

## 10. Open Questions (recorded, not blocking)

- True full-screen semantics (covering the panel) as a post-v1
  extension of D16W.10. Not designed here.
- Per-window float-out ergonomics (drag-to-float in tiling mode).
  v1 keeps tiled geometry layouter-owned; revisit only after parity.
- Master-ratio, gaps, and snap configuration shapes. All excluded by
  §5; no design is pre-committed.
- The `wm_settings` provider's WM settings UI (including how
  `wm.layout.mode` is presented) is Criterion 18 scope and is
  deliberately untouched here (D16W.11).

---

## 11. Protocol v1.1 addendum — internal Sponge-WM contract (binding)

> Registered 2026-11-12 with the protocol foundation work. **Binding
> internal protocol** for the native trio, distinct from — and
> additive to — the frozen Tasklist-facing protocol v1
> (docs/05-sponge-de.md §4.6, D16W.6), which stays frozen and
> unchanged. Shapes, parsers/generators, and the frozen chrome
> helpers live in `repos/sponge/include/sponge/wm_protocol.h`
> (`Sponge::Wm_protocol`, internal section); deterministic contract
> tests in `repos/sponge/src/test/wm_protocol_probe/`. This addendum
> registers the contract only: it marks no milestone complete and
> claims no component implementation.

Channels (report/ROM labels; every report is a COMPLETE
last-state-wins snapshot):

| Label | Producer -> Consumer | Shape |
|---|---|---|
| `focus_state` | sponge_wm -> sponge_layouter | `<focus_state><window wid="5"/></focus_state>`; empty root = no focus |
| `window_request` | sponge_layouter -> sponge_wm | `<window_request><window wid="5" x="192" y="172" w="640" h="480"/><window wid="6" action="close"/></window_request>` |
| `focus` | sponge_layouter -> sponge_wm | `<focus><window wid="5"/></focus>` (steering only) |
| `window_chrome` | sponge_wm -> sponge_decorator | `<window_chrome><window wid="5" label="..." title="..." resizeable="yes" focused="yes" fw="648" fh="488"/></window_chrome>` |
| `decorator_margins` | sponge_decorator -> sponge_layouter + sponge_wm + probes | `<decorator_margins><floating top="28" bottom="2" left="2" right="2"/></decorator_margins>` (shape unchanged) |

Semantics (binding):

- **Snapshots, not events.** There is NO hover report, NO event-style
  window_control channel, and NO seq_number in the v1.1 control path.
  Lossless gestures travel as core->layouter Input forwarding
  (component behavior). Input and exclusive grabs are component
  behavior: documented here, deliberately not protocol.
- **Identity.** The core assigns a monotonic `wid` per window and
  emits it as the frozen `window_list` `id`; the decorator's chrome
  view title is the decimal `wid`.
- **window_request.** Geometry is INNER screen-absolute; `x=y=-32000`
  parks. `action="close"` is a STICKY request: the layouter keeps the
  close node in every snapshot until the wid disappears from
  `window_list`; the core applies the app-visible 0x0 close
  convention when processing it.
- **window_chrome.** `fw`/`fh` are the OUTER frame size (outer = the
  inner geometry expanded by `decorator_margins`). Every entry
  carries the per-window `focused="yes"`/`focused="no"` draw state (a
  missing `focused` parses as `no`, backward compatible with
  pre-focus snapshots). The producer publishes only
  create/destroy/title/resizeable/focus/size change — never move;
  destroy is the window's absence from the snapshot.
- **Focus.** `focus` is steering; `focus_state` is the sole truth for
  `window_layout`'s consumed `focused="yes"`.

Frozen chrome metrics (single source of truth: the shared pure
helpers `title_band` / `button_rect` / `sizer_zone` / `chrome_hit` /
`view_title` in `sponge_wm_protocol`; the layouter hit-tests, the
decorator is a pure renderer + margins publisher):

- Buttons 24x16, pad 4, vertically centered in the title band (the
  top margin), right aligned; right-to-left CLOSE, FULLSCREEN,
  MINIMIZE. Closer + minimizer always present; fullscreen iff
  resizeable; absent buttons are skipped and the group stays right
  aligned.
- Grip zones: the outermost min(margin, 8) px of each edge; corners
  16x16 override the edges.
- Hit order: corner > edge grip > button > title/move.

Run-side routing (all four `run/sponge-wm-native*.run`):
`window_chrome` (core->decorator), `window_request`
(layouter->core), `decorator_margins` (decorator->layouter+core+
probes), `focus_state` (core->layouter), `focus` (layouter->core).
There is no pointer channel and no core-side `focus_state` ROM route
(the core produces it). The four frozen Tasklist-facing labels keep
their routing unchanged (D16W.6).
