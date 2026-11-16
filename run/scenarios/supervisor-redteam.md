# Supervision, containment, and red-team probes

Breadcrumb: [`run/README.md`](../README.md) → Supervision, containment, and red-team probes

Red-to-green proofs and adversarial probes for the quota wedge,
heartbeat containment, top-level supervisor takeover, app isolation,
and startup classification, plus the shared `run/supervisor.inc` wiring.
Full catalog entries follow verbatim (cautions and limitations
included); the parent index carries one-line summaries.

## Scenarios
- `sponge-quota-deadlock.run` — **the quota-request wedge**: a
  `Parent::upgrade` that the sandbox cannot satisfy must fail *loudly and
  boundedly*, never spin forever on the sandbox's shared main entrypoint.
  Addresses the 2026-10-19 LG gram field report (fast scrollbar drag →
  `⚠ sponge_pkgd unresponsive(N)` → mouse dead → **the freeze never
  heals**). `Parent::upgrade` has no "denied" answer, so the client
  cannot distinguish "the parent will grant this" from "the parent will
  never grant this" — and ledger row #21's immediate-grant path answered
  such a request by transferring the **child's own exhausted leftover
  quota** (which is what `Account::transfer_quota` bounds the transfer
  by), warned about the failure, and then logged `granted immediately`
  anyway. The client therefore looped forever, one RPC per iteration,
  against `_env.ep().rpc_ep()` — the one entrypoint that also carries
  every other child's `Parent` RPC, the heartbeat alarm, and the
  `session_requests` handler.

  The topology makes the request unsatisfiable **by construction**: a
  child with a *fixed* quota (`victim | caps: 3000 | ram: 4M`, no
  `<resource>`) spends it and then drains its PD with 1000 signal
  contexts, the same allocation the product's input/compositor chain
  performs under load. Witnesses at two levels (`ep_wedge behave` at
  Tier-0, `ep_wedge liveness_ms` inside the sandbox) plus the product
  containment shape (`restart_after_skipped: 3 | restart_budget: 3`).
  Everything inside the sandbox reaches its services through
  `+ any-service | + parent | + any-child`, so every one of the victim's
  sessions really is a `Try_parent_service` — the nesting is the default
  route, not a simulation.

  Measured RED → GREEN: **3701 upgrade RPCs and 3701 false
  `granted immediately` before, 4 RPCs and 0 false grants after** (925x),
  with containment settling on its restart budget instead of never
  converging. The host-side assertions grep the captured serial log
  (false-grant phrase must be 0, request count must be ≤ 4, in-sandbox
  bystander must have run), because a gate that only waits for a log
  line cannot tell "refused" from "granted once and never repeated". No
  control knob: the RED arm is `git -C genode checkout f44bc47ee5` and
  re-run. The *deadlocked* variant (`PENDING` → the client blocked in
  `Component_env::_block_for_session()` holding its mutex, plus the
  nested `Try_parent_service::initiate_request` → `_env.upgrade()` on
  init's own dispatch thread) is proved from source and **not**
  reproduced — stated plainly in the scenario header. Evidence:
  `docs/evidence/sponge-quota-deadlock.md`; receipts:
  `docs/evidence/sponge-quota-deadlock-regressions.md`. base-sel4 only.

- `sponge-containment-churn.run` — **bounds the heartbeat respawn
  loop**, the row `docs/17-runtime-component-topology.md` §5 registered
  as open with only a flake as evidence. A nested `system` sandbox runs
  the product containment shape (`<heartbeat rate_ms="500"/>` plus
  `restart_after_skipped="3"` on the critical children) and hosts a
  child that wedges its entrypoint on EVERY incarnation. Four
  independent oracles, because a single one cannot distinguish "the
  system survived" from "the observer stopped":
  (1) `test/ep_wedge <config liveness_ms>` — a **new** mode emitting a
  line forever from a *sibling inside* the churning sandbox;
  (2) `test/churn_probe` (**new**) — a Tier-0 observer that samples the
  sandbox's **own** remaining RAM from the top-level `<ram avail=/>`
  node `init_ram="yes"` puts in the state report. That is the subject
  on purpose: a leak in the abandon/respawn path debits the *container*
  without ever appearing as a growing child, so per-child accounting
  cannot see it;
  (3) `test/takeover_probe` with the new `repeat_ms` knob — re-announces
  the composited screen state periodically, so "the display still holds
  the desktop" is assertable at the END of the window rather than only
  at the start;
  (4) init's own `restart budget (N) exhausted` warning (ledger row
  #24) — the marker that the loop **stopped**, not merely ran slowly.
  Marker: `sponge-containment-churn: PASS`. `SPONGE_CHURN_BUDGET=0`
  omits the budget attribute and is the RED arm (the scenario times out
  waiting for a marker that never comes); `SPONGE_CHURN_DUMP=1` prints
  the staged configs. Measured: **2500 ms median respawn cadence, 48
  respawns in a 20 s window with no end**; bounded to 4 restarts plus
  the exhaustion warning. The per-cycle RAM leak the §5 row cites did
  **not** reproduce without a stalled storage chain — see the evidence
  file for why that narrows the row rather than repeating it. Receipts:
  `docs/evidence/containment-churn.md`. base-sel4 only, and it boots
  the **disk** image for the same flat-ISO HPET reason as
  `sponge-supervisor-takeover.run`.

- `sponge-supervisor-takeover.run` — closes the top-level containment
  gap in `docs/17-runtime-component-topology.md` §5. Red→green proof of
  the whole chain in headless QEMU: a child of the supervised `system`
  sandbox wedges its entrypoint on every incarnation, the Tier-0
  `sponge_supervisor` sees the rising `skipped_heartbeats` in that
  sandbox's own state report, rewrites the config with a fresh
  `<start>` version (the only restart lever a config owner has — see
  `docs/evidence/config-regen-survival.md`), and when the fault
  survives the grace window arms `sponge_console`, which takes the
  display as the only Gui client. The takeover is asserted by a
  **pixel oracle** (`test/takeover_probe`, a Capture client reading the
  composited screen), not by a log line, and the desktop's return is
  asserted the same way. Marker:
  `supervisor-takeover: PASS - wedge detected, subtree killed, console
  owned the display, desktop restored`. Env knob
  `SPONGE_TAKE_NO_FAULT=1` runs the control (healthy) variant, which
  asserts with a *negative* check that the supervisor never reports a
  fault and never kills a healthy subtree. Env knob
  `SPONGE_TAKE_DUMP=1` prints the staged templates and configs.
  base-sel4 only, and it boots the **disk** image rather than a flat
  ISO — the flat-ISO HPET blocker (`docs/17` §5) denies `timer` its
  IO_MEM, while the disk path finds the same HPET normally.
  Receipts: `docs/evidence/top-level-supervision.md`.

- `run/supervisor.inc` — the **shared product wiring** for top-level
  supervision, used by all five desktop-disk scenarios. It exists
  because the same four edits would otherwise be five hand-maintained
  copies: (1) derive the supervisor's two config templates from the
  `/system/init/system.config` the scenario already stages — staging
  stays the single source of truth and the supervisor never carries a
  second copy of the desktop composition; (2) add the two
  `report_rom` relay policies and move the `system` sandbox's config ROM
  from `rom_sys` to the relay; (3) add the console's own nitpicker
  domain (`console`, layer 8 — above `pointer`/`panel`/`default`/
  `launcher`) plus its policy; (4) start `sponge_supervisor` and
  `sponge_console` **before** the `system` sandbox, which must stay the
  last `<start>` node so its RAM remainder clamp (`docs/17` §6.1) is
  intact. Every transform is **asserted**: a missing or drifted anchor
  aborts the scenario loudly instead of booting a desktop without
  supervision. Call sites:

  ```tcl
  source .../supervisor.inc
  supervisor::prepare $sys_dir $sysconfig_hid     # templates + policy + version stamp
  install_config [supervisor::tier0 { <the scenario's own config> }]
  foreach m [supervisor::boot_modules] { lappend boot_modules $m }
  ```

  Version stamping is the subtle part: every top-level `<start>` is
  stamped with a sentinel that becomes `@KILL@` in the templates and
  `"1"` in the config init boots with — and `"1"` is exactly what the
  supervisor's first publish substitutes, so the initial publish matches
  every running child and the desktop is never rebuilt at boot.
  `supervisor::enabled 0` turns the whole wiring off, which is what the
  `SPONGE_DISK_FAIL` failure channel uses (its subject is the corrupted
  config, a diagnostic that predates the supervisor). Mirrored at
  `repos/sponge/run/supervisor.inc` by a committed relative symlink.

- **`SPONGE_SUPERVISOR_TEST`** (env-gated, **off by default**; the
  shipped media never carries a fault child) — the product-topology
  proof, in `run/sponge-desktop-disk.run`. It stages a wedging child
  inside the supervised sandbox plus `test/takeover_probe` (the pixel
  oracle) and gates the whole chain against the real product config:

  * `SPONGE_SUPERVISOR_TEST=takeover` (or `on`/`1`) — the fault child has
    no local containment (`<heartbeat/>` ⇒ `restart_after_skipped` 0), so
    the supervisor is the single restart authority:
    detect → kill → console (asserted by **pixels**) → release → the
    display is handed back. Marker:
    `sponge-desktop-disk: SUPERVISOR TAKEOVER PASS`.
  * `SPONGE_SUPERVISOR_TEST=control` — the same fault inside the
    **product's own** containment (`restart_after_skipped="3"`), so the
    sandbox respawns it forever and the supervisor must do nothing. The
    desktop reaches `alpha-probe: PASS` underneath, and the negative
    assertions (no `published <system> kill=2`, no
    `published <system_console>`, no `sponge_console: ARMED`) run
    against the run tool's accumulated output, because no log-line gate
    can express an absence. Marker:
    `sponge-desktop-disk: SUPERVISOR CONTROL PASS`.

  Both arms raise `match_max` first: the product boot floods the serial
  console and the run tool keeps only the last 2000 bytes by default,
  so the supervisor's own lines would be dropped before a late gate
  could see them. Receipts: `docs/evidence/product-supervision.md`.

- **`SPONGE_APP_ISOLATION=on`** (env-gated, **off by default**) — the
  **P5.1 app-isolation acceptance arm** of `run/sponge-desktop-disk.run`
  (docs/evidence/phase16-runtime-acceptance.md). The product decorator
  now ships a `+ closer` control (it exists in the stock motif
  decorator but was never enabled), so windows are closable from the
  title bar. The arm stages the focused `isolation` bake profile
  (`pkg/bake/isolation.profile`: hello + textedit + calculator — the
  launcher popup domain fits at most 8 entries and the desktop set
  already fills it exactly) and injects the `ep_wedge` wedge-fixture
  package (`pkg/ep_wedge/`, `<config wedge_after_ms="6000"/>`) via
  `bake::add_package`, then runs `test/app_isolation_probe`:
  1. **kill** — launch textedit + calculator from the REAL launcher,
     kill textedit with the decorator closer (geometry derived from
     the live `window_layout` report), require the victim gone from
     `window_list`, witness DE input via a QMP launcher-toggle press
     change on sponge-de's `input` report (de_survival C2 shape),
     prove the sibling calculator still takes typed input (the
     grouped_ui typed-delta shape), reset via pkgd remove+install
     (wm_tasks step-8 shape — never a launch substitute), relaunch
     textedit from the real launcher and require `launcher_result ok`
     + first paint + typed delta.
  2. **wedge** — launch ep_wedge from the real launcher, let its
     entrypoint park (gated `ep_wedge: wedge fired - entrypoint
     parked`), then require DE input + sibling typed delta + sibling
     windows retained + the class state live and updating, with zero
     `sponge_supervisor: version-change` lines (no sibling
     construction-version churn).
  The single marker `app-isolation-probe: PASS` is emitted only after
  both legs. `alpha_probe` runs its `minimal="yes"` hello-only smoke
  contract on this focused media (D16.7). Mutually exclusive with the
  other probe arms and with `SPONGE_SUPERVISOR_TEST` (they share the
  probe_request channel and the ep_wedge marker).

- **`run/sponge-startup-grace.run`** — the red→green proof for the
  classification defect behind the 2026-10-19 LG gram loop
  (`published system kill=25`). In the classic Genode
  `Component::construct()` runs on the entrypoint thread, so a child
  whose construct outlives the heartbeat budget cannot answer a single
  heartbeat and is read as a wedge. Three arms, one scenario:

  | arm | knobs | claim |
  |---|---|---|
  | GREEN | default (`startup_grace_ms=60000`) | a child whose construct takes 20 s against a 10 s budget is STARTING: one incarnation, no fault, nothing killed |
  | RED (sandbox half) | `SPONGE_STARTUP_GRACE_MS=0` | without the grace the child is killed and respawned 4× and then left stopped — FAIL as designed |
  | RED (supervisor half) | `SPONGE_SUP_STARTING=0` | with the sandbox patient, the supervisor still tears the subtree down (`published <system> kill=2`) — the field's signature, isolated |

  `SPONGE_START_DELAY_MS` moves the construct time; the product's own
  heartbeat parameters (`rate_ms` 2000, `restart_after_skipped` 3,
  `restart_budget` 3) are used verbatim.

