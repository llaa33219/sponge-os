# Phase 16 evidence: RPC object-key reuse broke config-driven recreation

> Scope: prerequisite lifecycle repair for P3.4. All results are QEMU /
> base-sel4 observations, not real-hardware claims. The fix is fork commit
> `489f977822` (`fix(base-sel4): keep RPC object keys unique across
> incarnations`), ledger row #32 in `docs/11-environment.md`.

## 1. Failure and isolation

P3.4's containment arm initially proved only the configuration half: the
victim's construction version advanced, but its process did not revive. The
same failure existed on the unchanged product takeover arm, so the new
per-name supervisor logic was not the cause.

The investigation separated four hypotheses:

- **ELF/build content:** refuted. The failing `0x30000/0xef2d3` segment is
  the R E segment of `ld-sel4.lib.so`; the boot `ld.lib.so` bytes match the
  expected `var/libcache/ld-sel4/ld-sel4.lib.so.stripped` object.
- **Config/template drift:** refuted. First and reconstructed configs differ
  only in intended version attributes and render byte-identically.
- **Resource exhaustion:** refuted. Passing arms carry the same warnings,
  and a failing rebuild had more available RAM than initial construction.
- **Lifecycle/capability state:** confirmed. Heartbeat respawn works, while
  config-generation recreation fails on plain global `@KILL@` as well as the
  new per-name path.

Runtime bisect at `74c84f9f0a`, `e0c2706225`, and `205b84fcad` found the
same defect at all three pins. `74c84f9f0a` failed hard twice; the later two
showed the defect with scenario recovery. The introducing transition is
therefore at or before `74c84f9f0a`, whose forwarded-close completion change
is the best-supported mechanism. Scenario recovery is not defect absence.

## 2. Root cause

Temporary slot-number instrumentation captured the exact alias in
`base-sel4/src/lib/base/ipc.cc` (all instrumentation was subsequently
reverted and rebuilt):

1. `Capability_space::lookup(rpc_obj_key)` hits a stale client-side `Data`.
2. `Capability_space::reset_sel(rcv_sel_ref)` deletes the **fresh** received
   capability and returns the looked-up old handle.
3. The old handle's kernel slot is null or dies during old-incarnation
   teardown, producing `dynamic linker is an invalid dataspace`,
   `create_thread: invalid PD argument`, invalid ELF attachment, and null-cap
   storms.

Core's `Rpc_cap_factory` recycled key values through `core_sel_alloc`, while
client key-to-`Data` mappings remain until their last reference. A recycled
key can therefore name two different server incarnations. The receive path's
assumption that a lookup hit means "same object" is false in that window.
`reset_sel` does not free `Data`-owned slots; `kill_thread` and
signal-context double deletion were ruled out as the primary cause.

A four-file `Session_state` generation guard was also tested and reverted: it
fired, but did not change the active failure distribution. It was not shipped.

## 3. Fix

Fork commit `489f977822` changes only
`repos/base-sel4/src/core/rpc_cap_factory.cc`:

- keys come from a shared monotonic `addr_t` counter starting at
  `2^NUM_CORE_SEL_LOG2`, outside the pager `cap_sel` range required by
  upstream issue #5779;
- `free()` no longer returns keys to `core_sel_alloc`;
- counter wrap and `Rpc_obj_key::INVALID` are guarded;
- IPC, badge semantics, wire format, and pager-key allocation are unchanged.

This restores the identity invariant the receive path already assumes:
a lookup hit can only name the same server incarnation. It adds no
receive-side guard, drain, protocol change, or new asynchronous semantics.

## 4. Red-to-green evidence

All commands were serial `make -j1` with the documented host io_uring clamp
where needed. Logs are under `/tmp/opencode/`.

| Gate | Result |
|---|---|
| `run/sponge-supervisor-takeover` before fix | FAIL: `create_thread: invalid PD argument`, invalid executable/dataspace, no revival (`debug-p34-h2-lean-takeover.log`, `debug-p34-bisect-74c84f9f0a-r{1,2}.log`) |
| Same scenario after fix, twice | PASS with real driver revivals and desktop restore; zero alias failures (`debug-p34-keyfix-lean-{1,2}.log`) |
| `SPONGE_SUPERVISOR_TEST=contain` after fix | Full `SUPERVISOR CONTAIN PASS`: second and third victim incarnations revive, one node bumped per publish, sibling and DE versions stable (`debug-p34-keyfix-contain.log`) |
| Unpatched toggle control | Failure returns (`debug-p34-keyfix-toggle-unpatched.log`) |
| `run/sponge-containment-churn` | `churn-probe: VERDICT PASS` |
| Default `run/sponge-desktop-disk` | `alpha-probe: PASS` |
| Conflicting-ID leak check | Zero `requested conflicting session ID` in all final logs |

## 5. Remaining boundary

Product takeover now gets past the key-alias failure and rebuilds all 15
children, but reaches a separate deterministic `illegal READ` in the rebuild
wave (`debug-p34-keyfix-product-takeover-*.log`). Addresses vary at a fixed
instruction pointer, consistent with a stale-pointer dereference rather than
key-space collision. That previously masked defect is **not** fixed by this
commit and must not be hidden behind a partial-green claim.
