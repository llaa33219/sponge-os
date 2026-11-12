# Phase 16 Criterion 16 (custom Sponge WM): Evidence Index

> Status: Criterion 16 delivered and verified at commit `191222130e`.
> Every acceptance gate of `docs/plans/phase16-sponge-wm.md` §6 (M0-M6)
> and §7 has receipts below. Curated receipts only: raw per-run logs
> are transient run artifacts and are not promoted here (AGENTS.md
> §5.6). Receipts are commit hashes, not log paths.
>
> Honesty rules in force: no gate is relabeled, no timing is invented,
> no real-hardware claim is made, and the physical-hardware gate is a
> non-goal for this criterion (roadmap wording stands). There are
> **zero native partials**: every native gate below is a full PASS.

## 1. Host deterministic suites (measured)

| Suite | Source of truth | Result |
|---|---|---|
| Protocol v1 contract | `repos/sponge/src/test/wm_protocol_probe/` (host self-test) | `self-test: PASS (209 checks)` |
| Core window registry | `repos/sponge/src/sponge_wm/` (host self-test) | `self-test: PASS (123 checks)` |
| Layout policy model | `repos/sponge/src/sponge_layouter/` (host self-test) | `self-test: PASS (144 checks)` |
| Decorator chrome geometry | `repos/sponge/src/sponge_decorator/tests/` (host self-test) | `self-test: PASS (142 checks)` |

## 2. Native scenario gates (measured, PASS)

- **base-linux native smoke** (`run/sponge-wm-native.run`): PASS,
  including the non-resizeable fixture and the same-wid
  destroy/recreate sequence with a fresh close after recreation (the
  `d0b2687976`/`1fac8b46a6` resurrection contract).
- **seL4 native QMP acceptance battery**
  (`run/sponge-wm-native-qmp.run`): PASS on the full battery in one
  run: move, full 8-zone resize, focus, minimize/restore,
  maximize/restore, and close. The 8-zone full sequence passes as one
  sequence (no partial on the native path). Decorator focus evidence,
  verbatim: `chrome view <2,4> focus no (2 unfocused flip(s))`.
- **Native tasklist/protocol compat** (`run/sponge-wm-native-tasks.run`):
  PASS with the delivered `TasklistController` unchanged (protocol v1
  transitions rows 1/3/5/8-11 plus remove/reinstall/relaunch and
  close/recreate slot reset).
- **Full 20-cycle tiling acceptance** (`run/sponge-wm-native-tiling.run`):
  PASS with the non-resizeable fixture floating at (850,560)160x160
  and the remaining client at master (8,56)496x704. Master-Stack
  reflow assertions are exact after every trigger.

## 3. Product and upstream anchors (measured, PASS)

- **Native product** (`SPONGE_WM=native` topology transform,
  `3501970d83`/`2ec697239f`): `run/sponge-alpha.run` PASS and
  `run/sponge-desktop-disk.run` PASS with the upstream trio absent
  from the topology.
- **Default upstream product**: `run/sponge-alpha.run` PASS and
  `run/sponge-desktop-disk.run` PASS on the default upstream topology.
- **Upstream anchors** (`4b2163d5f8` restored their routing and
  startup): base-linux `run/sponge-wm.run` PASS, seL4
  `run/sponge-wm-qmp.run` PASS, seL4 `run/sponge-wm-tasks.run` PASS.

Parity (D16W.7) therefore holds on both limbs: the native product
scenarios pass with the upstream trio absent, and the upstream
fallback anchors stay green.

## 4. Rules-drift soak (measured)

Exactly 20 cycles, no drift: after every cycle the reported
`window_layout` geometry matched the expected arrangement exactly
(tasklist `rules` write + restore + tiling reflow per cycle, §7.2).

## 5. UX metrics (AGENTS.md §5.1, measured)

Every task meets its target interaction count:

| Task | Measured | Path |
|---|---|---|
| Minimize a window | 1 click | native chrome minimizer |
| Restore a window | 1 click | tasklist entry |
| Close a window | 1 click | native chrome closer |
| Maximize a window | 1 click | native chrome fullscreen |
| Restore from maximized | 1 click | same toggle |
| Focus a window | 1 click | title click or tasklist entry |
| Move a window | 1 gesture | title-bar drag (floating) |
| Resize a window | 1 gesture | 8-zone drag (floating) |
| Switch to tiling | 1 command | configd backend, spelled `vct config set wm.layout.mode tiling` |

No task requires Genode terminology from the user. The tiling switch
is 1 command through the `sponge_configd` backend, the same write
`vct config set wm.layout.mode tiling` performs; the later 1-click
`wm_settings` path stays Criterion 18 scope.

## 6. Ledger check (measured)

`docs/11-environment.md` §4 remains at **38 rows** and no `genode/`
change ships with this delivery (D16W.2/D16.8: zero new patches, zero
new ledger rows).

## 7. Honest-partial register

- **Native path: empty.** Every native gate above passed in full.
- **W9 themed-path flakiness (historical, outside the native path).**
  The Phase 16 W9 themed-chrome full 8-zone resize sequence
  (`docs/evidence/phase16-w9-resize.md`) recorded flakiness with each
  zone individually green on the upstream `themed_decorator` path.
  That record is historical and outside the native path; it is not a
  native partial and is not relabeled. The native path proves the
  full 8-zone sequence in one scenario run (§2).
- **Physical hardware: non-goal.** Criterion 16 adds no
  physical-hardware gate; all receipts here are host or QEMU runs.

## 8. Commit receipts (all resolve at HEAD `191222130e`)

| Area | Commits |
|---|---|
| Native WM product topology (upstream trio absent) | `3501970d83`, `2ec697239f` |
| Focus metadata (protocol, snapshots, chrome application) | `5a93452a22`, `47f5b8d92c`, `f1c6ec0ea8` |
| Window lifecycle (re-registration, in-place revival) | `d0b2687976`, `1fac8b46a6` |
| QMP runtime acceptance | `22f62d52c9` |
| Same-session view recreation (close-after-recreate) | `191222130e` |
| Non-resizeable windows float in tiling | `73e0874cd9` |
| Upstream anchor routing and startup | `4b2163d5f8` |

Protocol v1 stays frozen (`docs/05-sponge-de.md` §4.6); the internal
v1.1 contract is `docs/plans/phase16-sponge-wm.md` §11. State machine:
`docs/plans/wm-state-table.md`.
