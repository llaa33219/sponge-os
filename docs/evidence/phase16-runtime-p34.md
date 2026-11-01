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
| 5 | `489f977822` | `fix(base-sel4): keep RPC object keys unique across incarnations` |
| 6 | `48a946ba15` | `fix(base): preserve pending sessions across reissue and destruction` |

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

## 4. Green flip — containment by name and process revival are proven

Final receipt: `/tmp/opencode/debug-p34-rawfree-contain.log` (after
`c280ed1aad` plus the two lifecycle repairs below). The CONTAIN publish
target remains the only behavior change:

```
sponge_supervisor: published <system> kill=2 target=pkg_runtime_fault bumped=1 (18200 bytes)
sponge_supervisor: version-change pkg_runtime_fault 1 -> 2
ep_wedge: incarnation alive — wedging in 3000 ms   <- real second incarnation
ep_wedge: incarnation alive — wedging in 3000 ms   <- real third incarnation
sponge-desktop-disk: SUPERVISOR CONTAIN PASS
```

Proven by the final witness walk:

* **no global publish after the fault** — `target=pkg_runtime_fault
  bumped=1`, and every sibling renders byte-identical;
* **sibling and DE construction versions stable** — only the victim's
  version changes;
* **sponge-de panel shown exactly once**, `alpha-probe: starting` once;
* **post-wedge DE functional PASS**;
* **victim revived by name twice** — real second and third process
  incarnations, not just construction-version changes;
* **no console takeover**;
* **contained by name across two cycles**.

## 5. Base recreation defects isolated and repaired

The initial green named only the configuration half: the victim's node
was rebuilt, but its process could not start. The unchanged product
takeover arm failed on the same path, proving that the new per-name
supervisor logic was not the cause. Two base lifecycle defects were
isolated and fixed without changing protocol or asynchronous semantics:

1. `489f977822` stops `Rpc_obj_key` recycling, so cap-receive lookup can
   no longer alias a new server incarnation to a stale client handle
   (`docs/evidence/phase16-runtime-capability-key-reuse.md`).
2. `48a946ba15` stamps `Session_state` incarnations and links pending
   operations to their requester, so reissue cannot complete against a
   replacement and real destruction clears the requester's operation
   before freeing the state (`docs/evidence/
   phase16-runtime-pending-session-lifetime.md`).

The combined result is not a partial green. Product takeover now reaches
**`SUPERVISOR TAKEOVER PASS`** with real revivals and console-to-desktop
handback (`/tmp/opencode/debug-p34-rawfree-prod-takeover.log`), and the
P3.4 contain arm reaches full **`SUPERVISOR CONTAIN PASS`**. Lean takeover, heartbeat respawn, and product default remain green with zero
`requested conflicting session ID`.

## 6. Regression matrix (serial, one runner)

| Scenario | Result | Receipt |
|---|---|---|
| `SPONGE_SUPERVISOR_TEST=contain` (red, global behaviour) | **FAIL as designed** — witness walk names the global publish + 13 sibling rebuilds | `phase16-runtime-p34-contain-red.log` |
| `SPONGE_SUPERVISOR_TEST=contain` (green, flip + lifecycle fixes) | **PASS** — full containment-by-name plus real second/third victim incarnations | `/tmp/opencode/debug-p34-rawfree-contain.log` |
| `SPONGE_SUPERVISOR_TEST=takeover` (product) | **PASS** — `SUPERVISOR TAKEOVER PASS` with real revivals and display handback | `/tmp/opencode/debug-p34-rawfree-prod-takeover.log` |
| `SPONGE_SUPERVISOR_TEST=control` (product) | **PASS** — `SUPERVISOR CONTROL PASS`, `alpha-probe: PASS`, no kill, no console | `phase16-runtime-p34-control.log` |
| `run/sponge-startup-grace` (green arm) | **PASS** — 20 s construct survived as STARTING, no kill | session log |
| `run/sponge-containment-churn` | **PASS** — `churn-probe: VERDICT PASS`, budget-bounded respawn | session log |
| `run/sponge-de-pkgd-failure` | **PASS** — `de-survival-probe: scenario GREEN (P2.5 mechanisms present)` | `/tmp/opencode/p34-final-de-pkgd-failure.log` |
| `run/sponge-supervisor-takeover` (lean) | **PASS** — `supervisor-takeover: PASS` with real revivals and desktop restore | `/tmp/opencode/debug-p34-rawfree-lean.log` |
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

1. `sponge-desktop-disk-{uefi,uefi-nvme}` are structure-only gates in
   QEMU (D15.16); the UEFI boot claim stays real-hardware (15-3).
2. All other claims here are QEMU/base-sel4 receipts, not real-hardware
   certification. Host io_uring and timing flakes are recorded per log;
   no failing gate is silently converted into a pass.
