# Phase 16 evidence: G0 repository split and terminal lifecycle close-out

> Status, 2026-11-18: **G0 complete**. The repository split is pinned and
> the existing product/regression anchors are green after the terminal
> lifecycle repair. G1 has not started. No native-compositor functionality,
> seL4 compositor success, or default-topology flip is claimed.

## 1. Pinned repositories and integration commits

| Repository | Pinned commit |
|---|---|
| `genode` | `1183655258b23aebfc8ba97bd907a31198497dfd` |
| `sponge-wm` | `3d1de1b01e841db15270f05181b97043d1594053` |
| `sponge-de` | `a5ce5e261fead1103c674c72ea85c596b91b20fd` |
| `sponge-compositor` | `9e1943ca680059a641693a0c412c34adceb6ec03` |

The split itself landed as `d9e6fefff1`. Follow-up product repairs landed
as `92e30e0380`, `374ff251fc`, `f3a4c4c208`, and `13fbfa39ca`.

The F7 terminal lifecycle repair is split into independently revertable
commits:

| Commit | Unit |
|---|---|
| `6b5e54ca01` | Base expired-close resolution, live-session accounting, and direct regression test |
| `3037e82068` | Fork File_system/ROM route-label preservation |
| `1183655258` | Fork completion clock, asynchronous reaping, and quota plumbing |
| `684a28fb3c` | Terminal QMP build-list and payload-staging repair |
| `90c78961d0` | Genode pin, terminal quotas/probe, and patch-ledger rows #39–#41 |

Rows #39–#41 in `docs/11-environment.md` are the authoritative record of
the three new Genode patches. They are pre-compositor F7 repairs, not a
compositor-specific patch dependency or permission to add more rows.

## 2. Final lifecycle and terminal design

- `wait4` now only harvests the child PID and exit status. It does not
  destroy a child whose session states may still have parent-owned closes.
- The 50 ms `fork_ep` clock repeatedly initiates teardown, resolves pending
  operations, and destroys a harvested child only when environment sessions
  are closed, no parent operation is pending, and the live-session factory
  is empty.
- Expired close operations continue parent-state probing without another
  timeout warning. A late `Gone` therefore still destroys the state and
  releases its parent ID.
- File_system routes preserve the identity/resource label pair. ROM parent
  routes again use the final path element for `label_prefix="/bin"`, while
  local loaded-object lookup keeps the original full label.
- Terminal children use `fork_caps=128` and `fork_ram=16777216`; the shell
  requests 512 caps and is clamped by the runtime to 459. Both quota-transfer
  results are checked and reported. This fits overlapping children in the
  deferred-reap window without hiding an under-provisioned child.
- The QMP scenario uses the canonical `build [concat ...]` form, produces
  and packs `vct-bin.tar`, and strips the optional `vct` bridge where no
  provider exists.

## 3. Verification matrix

All commands were run serially with `make -j1` from the repository root
unless the row says otherwise. Logs are retained under `/tmp/opencode/`.

| Gate | Result and receipt |
|---|---|
| `run/child_pending_op`, `KERNEL=linux BOARD=linux` | **PASS**, including the new expired-close late-completion phase and every pre-existing phase; `g0-f7-review-child-pending-op.log` |
| `run/sponge-terminal`, `KERNEL=sel4 BOARD=pc` | **PASS**. Four sequential `cat` forks, then `grep`, `which`, and `vim --version`, all `&&`-chained before the final `ls -l /bin`; no quota-transfer or resource-request failure; `g0-f7-review-terminal-caps512.log` |
| `run/sponge-terminal-qmp`, `KERNEL=sel4 BOARD=pc` | **PASS** through real QMP/PS2 keyboard input; `g0-f7-review-terminal-qmp.log` |
| `run/sponge-launch`, `KERNEL=sel4 BOARD=pc` | **PASS**; `g0-f7-sponge-launch-sel4.log` |
| Default `run/sponge-desktop-disk` | **Run script execution successful**; `g0-f7-desktop-default.log` |
| `SPONGE_GROUPED_UI=on` desktop | **PASS** on the unchanged-code retry; `g0-f7-desktop-grouped-retry.log` |
| `SPONGE_APP_ISOLATION=on` desktop | **PASS**; `g0-f7-desktop-app-isolation.log` |
| Supervisor containment and default product validation | `default rc=0`, `contain rc=0`; `g0-postfix-validation.summary` |
| Provider-stall disk topology | **PASS**; `g0-provider-stall-cmdline.log` |
| Full stability soak | **PASS**, 1838 seconds with the probe's unchanged 30-minute budget; `g0-postfix-stability.log` |
| NVMe, UEFI, UEFI-NVMe, UEFI-USB | all `rc=0`; `g0-final-acceptance.summary` |
| `./tool/build prepare` | build directory, managed `build.conf`, and Qt6 host tools verified |
| `./tool/build verify` | **OK**, all 41 patch-ledger rows plus the hardware-compatibility gates |

The post-implementation review initially rejected two issues: ROM-label
preservation broke non-`ls` toolset routing, and unchecked quota transfers
could under-provision overlapping children. Both were corrected and the
same review then returned **ACCEPT**. The expanded probe is the receipt for
both corrections, not just for the original `ls` path.

## 4. Honest limitations and non-regression notes

- The first grouped-UI run stopped with the launcher popup still open. An
  identical-code retry passed the complete scenario. The transient input
  failure is retained in `g0-f7-desktop-grouped.log`; it is not rewritten as
  a first-run pass.
- Base-linux `sponge-launch` still fails after launcher-channel relaunch
  with `demo window green pixel never appeared`. This is the already
  documented F3 limitation in `docs/evidence/phase16-runtime-sweep.md` §7.
  The seL4 acceptance axis is green. No new base-linux success is claimed.
- If no Timer service can be armed, the fork code explicitly warns that
  pending operations and child reaping are unbounded; it does not silently
  claim safe reaping in that configuration.
- G1 remains gated by D16C.12. Upstream nitpicker remains the shipped
  compositor and `SPONGE_COMPOSITOR=native` remains opt-in.
