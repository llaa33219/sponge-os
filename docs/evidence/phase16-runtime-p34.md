# Phase 16 P3.4 receipt: supervisor gates scoped per runtime subtree

> Scope: `docs/plans/phase16-runtime-overhaul.md` P3.4 ("Supervisor
> gates"). Branch `refactor/phase16-runtime-overhaul`, base
> `770fbeccc7`. All scenario results are QEMU (KERNEL=sel4 BOARD=pc),
> serial `make -j1`, one runner, host workaround
> `LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so`, `RUN_OPT` with
> `image/disk`; not real-hardware claims.

## 1. Units landed (each independently revertable)

| # | Commit | Unit |
|---|--------|------|
| 1 | `6d41b05937` | `feat(supervisor): render per-name restart markers from a name->version table` |
| 2 | `70e57cd384` | `feat(run): scope the supervisor lifecycle gates per runtime subtree` |
| 3 | `105f5fed9e` | `feat(run): expose per-child heartbeat values and gate the contain arm` |
| 4 | `c280ed1aad` | `fix(supervisor): contain a runtime fault by name` (the flip) |

## 2. What P3.4 changed (and what it deliberately did not)

The supervisor's restart lever (`Start_model::matches()` keys on
name+version only) gains a **targeted** form. The staging stamps the
runtime subtrees — every `pkg_runtime*` sandbox and `driver_runtime`,
all direct children of `system` — with a per-name sentinel that becomes
`@KILL:<name>@` in the templates; the supervisor keeps a fixed
name->version table for those markers and a CONTAIN action on a fault
attributed to one of them bumps **only that child's** entry. Every
other node renders byte-identical bytes and is never rebuilt. Global
bumps (boot publish, console takeover, nominal restore, non-runtime or
unattributable faults) still bump every slot; plain `@KILL@` templates
(`run/sponge-supervisor-takeover.run`) keep working against the shared
global counter, and the first publish renders `1` on every node
(first-publish-all-1, verified: `kill=1 target=global bumped=15` with
zero `version-change` lines and a single `panel shown`).

New witnesses (all bounded, all in the supervisor):

* the publish receipt grows `target=<name|global> bumped=<n>`;
* the state scan logs per-child **construction-version changes** —
  the `version` attribute each child carries in the sandbox state
  report is its construction version
  (`genode/repos/os/src/lib/sandbox/child.cc:378`), so a logged change
  is a rebuild; the log is capped at 16 lines per scan
  (`version-change-suppressed N` beyond that);
* faults are attributed to one child name for the whole fault run
  (`FAULT ... child=<name>`);
* `grace_ms.<name>` / `settle_ms.<name>` policy lookups override the
  globals for the attributed fault's escalation, re-fault grace and
  CONTAIN settle (console/RELEASE stay global). The spelling is
  `+ gate | key: grace_ms.<name>=<ms>` because the HID parser mangles
  dotted ATTRIBUTE names while a dotted key inside a value survives
  both conversions (measured with `tool/hid`).

## 3. Red-first receipt (the arm fails on global behaviour)

`SPONGE_SUPERVISOR_TEST=contain` on `run/sponge-desktop-disk` stages
`pkg_runtime_fault` (ep_wedge, `wedge_after_ms=3000`, bare
`<heartbeat/>` — no `restart_budget`, so the supervisor is the sole
restart authority) before `pkg_runtime_simple`, with its own gates
(`grace_ms.pkg_runtime_fault=3000`, `settle_ms.pkg_runtime_fault=1000`).
The arm's gates are bounded non-fatal expects followed by a witness
walk, so the red fails with findings instead of dying at a timeout.
Receipt: `phase16-runtime-p34-contain-red.log` (against the pre-flip
global CONTAIN behaviour).

```
ep_wedge: incarnation alive — wedging in 3000 ms
ep_wedge: wedge fired - entrypoint parked
sponge-de: panel shown
alpha-probe: PASS                                    <- POST-wedge DE functional PASS
sponge_supervisor: FAULT max_skipped=3 ... child=pkg_runtime_fault
sponge_supervisor: escalation - fault held for 3000ms ...
sponge_supervisor: published <system> kill=2 target=global bumped=15 (18200 bytes)
sponge_supervisor: version-change pkg_runtime_browser 1 -> 2
  ... (13 sibling construction versions changed: terminal, simple,
       cfg_probe, alpha_probe, sponge-de, pkgd, themed, configd,
       decorator, layouter, wm, report_rom)
sponge_supervisor: contained - child=pkg_runtime_fault subtree healthy for 1000ms
...
sponge-desktop-disk: SUPERVISOR CONTAIN FAIL:
  - the victim revived by name (2nd incarnation): no match within 120s
  - ...
  - a post-fault publish was NOT a targeted one-node containment:
    ... published <system> kill=2 target=global bumped=15 (18200 bytes)
  - a construction version changed OUTSIDE the contained child: ...
  - only 1 victim incarnations seen (expected 3 ...)
```

The red fails on exactly the global-CONTAIN behaviour (global publish,
sibling rebuilds, no named revival) — the witness infrastructure itself
worked.

**Two arm-tuning measurements** (both first-red-run findings, recorded
because they are the per-subtree gates' own receipt):

1. `grace_ms.pkg_runtime_fault=1000` was shorter than the post-kill
   state-report lag (~1250 ms measured): the CONTAIN phase re-read the
   **stale** pre-kill fault (`state_age_ms=1250`) and armed the console
   1.25 s after every kill. 3000 ms outlasts the lag with margin.
2. The wedge fires 3 s into each incarnation while `alpha-probe` needs
   ~5-6 s for its assertion sequence (measured in-run: `wedge fired`
   precedes `alpha-probe: PASS` by ~4-6 s), so the gate order itself is
   the post-wedge receipt.

## 4. Green flip — containment by name is proven; process revival is blocked by base

Receipt: `phase16-runtime-p34-contain-green.log` (after `c280ed1aad`).
The flip's whole effect is the CONTAIN publish target:

```
sponge_supervisor: published <system> kill=2 target=pkg_runtime_fault bumped=1 (18200 bytes)
sponge_supervisor: version-change pkg_runtime_fault 1 -> 2
sponge_supervisor: contained - child=pkg_runtime_fault subtree healthy for 1000ms
```

Proven by this run (witness walk findings green on all of them):

* **no global publish after the fault** — `target=pkg_runtime_fault
  bumped=1`, and the render leaves every sibling byte-identical;
* **sibling and DE state-report construction versions stable** — the
  only `version-change` line names `pkg_runtime_fault`;
* **sponge-de panel shown exactly once**, `alpha-probe: starting` once;
* **post-wedge DE functional PASS** (wedge fires before the DE's
  functional gate by construction);
* **no console takeover**;
* **contained by name** (`contained - child=pkg_runtime_fault`).

Not proven in this environment (the arm stays red on it):

* **victim revived by name** — the node is rebuilt (construction
  version `1 -> 2` is the state-report receipt) but the new **process**
  cannot start. `Error: CPU (init -> system -> pkg_runtime_fault)
  create_thread: invalid PD argument` → `failed to create initial
  thread for new PD` → `dynamic linker is an invalid dataspace` →
  `pkg_runtime_fault: attempt to load an invalid executable`. The
  failed-to-load child reports no heartbeat skips, so the supervisor
  sees "healthy" and the recurrence never comes back.

## 5. The blocker is pre-existing base behaviour (out of P3.4 scope)

The same create failure hits the **pre-existing product takeover arm**
at this base commit, with no P3.4 behaviour involved — receipt:
`phase16-runtime-p34-product-takeover.log` (`SPONGE_SUPERVISOR_TEST=
takeover`, unchanged mechanics):

```
published <system> kill=2 target=global bumped=15 (18188 bytes)
Error: pkg_runtime_browser: attempt to load an invalid executable
Error: cfg_probe: attempt to load an invalid executable
Error: alpha_probe: attempt to load an invalid executable
Error: CPU (init -> system -> wm) create_thread: invalid PD argument
Error: Test execution timed out          <- its kill-receipt gate
```

The arm's documented kill receipt (`ep_wedge: incarnation alive` —
`docs/evidence/product-supervision.md` §5.2) can no longer complete:
config-driven child recreation after a teardown fails at the base
layer. The lean `run/sponge-supervisor-takeover` topology shows the
degradation one step later: its first recreation works (`kill=2` →
`incarnation alive`), the next one fails with `attempt to attach
invalid segment dataspace` / `failed to create initial thread for new
PD` / `Trying to invoke an invalid capability, stop.`, and its final
desktop-restore pixel gate times out (`phase16-runtime-p34`-external
run log, this session). The pre-P3.3 receipts show recreation working
repeatedly (`docs/evidence/product-supervision-prod-disk-takeover.log`
2026-10-17, `docs/evidence/churn-regression-sponge-supervisor-
takeover.log` 2026-10-19), i.e. the degradation arrived with the P3.3
base series (fork commits `1672f3649a`, `e0c2706225`, `205b84fcad`,
`74c84f9f0a`, `73e5859da8` — teardown/forwarded-create semantics) that
predates this task's base `770fbeccc7`.

P3.4 deliberately does not touch that: D16 forbids base changes and
the task forbids adding base semantics. **Follow-up (exact):** root-
cause the create-after-teardown path in the fork's base series and
re-run `SPONGE_SUPERVISOR_TEST={takeover,contain}` — both arms' revival
gates unlock together, because they share the one recreate mechanism.

## 6. Regression matrix (serial, one runner)

| Scenario | Result | Receipt |
|---|---|---|
| `SPONGE_SUPERVISOR_TEST=contain` (red, global behaviour) | **FAIL as designed** — witness walk names the global publish + 13 sibling rebuilds | `phase16-runtime-p34-contain-red.log` |
| `SPONGE_SUPERVISOR_TEST=contain` (green, flip) | **partial green** — containment-by-name all proven; revival blocked by §5 | `phase16-runtime-p34-contain-green.log` |
| `SPONGE_SUPERVISOR_TEST=takeover` (product) | **FAIL, pre-existing** (§5) — kill receipt unreachable | `phase16-runtime-p34-product-takeover.log` |
| `SPONGE_SUPERVISOR_TEST=control` (product) | **PASS** — `SUPERVISOR CONTROL PASS`, `alpha-probe: PASS`, no kill, no console | `phase16-runtime-p34-control.log` |
| `run/sponge-startup-grace` (green arm) | **PASS** — 20 s construct survived as STARTING, no kill | session log |
| `run/sponge-containment-churn` | **PASS** — `churn-probe: VERDICT PASS`, budget-bounded respawn | session log |
| `run/sponge-de-pkgd-failure` | **inconclusive** — its first-paint gate timed out at `de-survival-probe: [boot] ok`; the run tool's retry hit the image write-lock (known run-tool retry hazard) | session log |
| `run/sponge-supervisor-takeover` (lean) | **partial** — kill receipts + named revivals + console takeover fire (3 cycles), final desktop-restore pixel gate times out (§5 degradation) | session log |
| `run/sponge-desktop-disk` (default arm) | **PASS** — `alpha-probe: PASS`, `kill=1 target=global bumped=14`, zero version changes (first-publish-all-1) | `phase16-runtime-p34-product-default.log` |
| `run/sponge-desktop-disk-nvme` | **PASS** — `alpha-probe: PASS` | session log |
| `run/sponge-desktop-disk-uefi` | **PASS (structure gate)** — QEMU UEFI boot is the D15.16 documented gap | session log |
| `run/sponge-desktop-disk-uefi-nvme` | **PASS (structure gate)** — same gap | session log |
| `run/sponge-desktop-disk-uefi-usb` | **PASS** — `alpha-probe: PASS` (11-child heartbeat roster incl. `vct_tty`) | session log |

## 7. The per-child heartbeat exposure

The five desktop-disk scenarios declare their per-child gate triplets
(`restart_after_skipped` / `restart_budget` / `startup_grace_ms`) via
`supervisor::heartbeat_values` and mark each child's node with an
`@HB:<name>@` placeholder; `driver_runtime.inc` carries the same
triplet as namespace variables for its generated node. The defaults
(3 / 3 / 120000) render **byte-identical** to the previous inline
literals (asserted by construction: the renderer emits the same format
string, and the default-arm run produces the same staged config and a
green `alpha-probe: PASS`). Both placeholder directions are fail-loud
staging anchors.

## 8. Residual limits

1. **Victim process revival** (the §5 base blocker) — the containment
   is proven at the config/state-report level; the restarted process's
   execution needs the base create-after-teardown fix first.
2. **de-pkgd-failure regression inconclusive** this session (Qt6
   first-paint gate timeout + run-tool image-lock retry); re-run with a
   quieter host before citing it either way.
3. **The lean takeover's final restore gate** is timing-flaky under the
   §5 degradation (the desktop window does not repaint inside the gate
   window while the fault churns).
4. `sponge-desktop-disk-{uefi,uefi-nvme}` are structure-only gates in
   QEMU (D15.16); the UEFI boot claim stays real-hardware (15-3).
