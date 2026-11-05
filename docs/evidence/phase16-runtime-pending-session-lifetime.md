# Phase 16 evidence: pending-session incarnation and destruction lifetimes

> Scope: complementary base-lifecycle repair for P3.4. Fork commit
> `48a946ba15` (`fix(base): preserve pending sessions across reissue and
> destruction`), ledger row #33 in `docs/11-environment.md`. All results are
> QEMU / base-sel4 observations, not real-hardware claims.

## 1. Relationship to the key-reuse fix

`489f977822` stopped `Rpc_obj_key` recycling and removed the cap-receive
alias that discarded fresh capabilities. Lean takeover and P3.4 containment
then passed, but product takeover exposed a separate pending-operation
lifetime failure. Two additional lifetime defects had to be fixed; neither
is a protocol or asynchronous-semantics change.

## 2. Replaced-incarnation operations

A pending operation stores a `Session_state *`. A re-issued request can
reconstruct that state in place, leaving the operation pointing at the new
incarnation. The fix stamps every `Session_state` with a monotonic
`generation` and records it in `Child::Pending_op`. `_resolve_pending_op`,
`_expire_pending_op`, and close-answer handling never act on a mismatched
incarnation; legitimate current-incarnation completion and ID release stay
unchanged.

## 3. Destruction without clearing the requester

A second path allowed a provider or local-service close to destroy a state
without clearing the requester's pending registry. The surviving operation
then dereferenced freed memory. The repair adds `pending_op_owner` and
clears that back-reference at `Session_state::destroy()` before the factory
releases the state. It deliberately does not sever in `~Session_state()`:
in-place reissue needs the old operations as stale-close evidence.

The first version cleared the back-reference on every `_clear_pending_op`
call. Runtime evidence showed that a provider purge could clear its own
(empty) registry and accidentally sever the requester's link. Both clear
overloads now null the link only when `pending_op_owner == this`, so
non-owner provider purges leave the requester's lifetime link intact.

Final lifetime trace (`/tmp/opencode/debug-p34-rawfree-evidence.log`):

- requester registers `Op::Close` for the state;
- provider `report_rom` clears its own registry and leaves the requester's
  link intact;
- `Session_state::destroy()` routes the clear to the actual requester before
  releasing the state;
- no stale `generation` read or illegal access follows.

All temporary instrumentation was reverted and rebuilt.

## 4. Final red-to-green matrix

Serial `make -j1`, host io_uring clamp where needed. Logs are in
`/tmp/opencode/`.

| Gate | Result |
|---|---|
| Product takeover before the lifetime fix | `illegal READ` at `_resolve_pending_op` reading a freed/replaced `Session_state` (`debug-p34-pendingfix-prod-takeover.log`, `debug-p34-lifetime-evidence.log`) |
| Product takeover after the combined fixes | **`SUPERVISOR TAKEOVER PASS`**, real revivals and console-to-desktop handback; zero illegal READ and zero invalid-PD/dataspace failures (`debug-p34-rawfree-prod-takeover.log`) |
| `SPONGE_SUPERVISOR_TEST=contain` | **`SUPERVISOR CONTAIN PASS`**, real second and third victim incarnations, one node bumped per publish, sibling and DE versions stable (`debug-p34-rawfree-contain.log`) |
| Lean `run/sponge-supervisor-takeover` | `supervisor-takeover: PASS` with real revivals and desktop restore (`debug-p34-rawfree-lean.log`) |
| `run/sponge-containment-churn` | `churn-probe: VERDICT PASS` |
| Default `run/sponge-desktop-disk` | `alpha-probe: PASS` |
| `run/sponge-de-pkgd-failure` | `de-survival-probe: scenario GREEN (P2.5 mechanisms present)` (`p34-final-de-pkgd-failure.log`) |
| Conflicting-ID leak check | Zero `requested conflicting session ID` in every final log |

## 5. Scope and invariants

The combined patches change no wire format, badge semantics, provider close
protocol, or asynchronous model. They establish three local invariants:

1. an RPC object key names only one server incarnation;
2. a pending operation may complete only for the `Session_state` incarnation
   that registered it;
3. real destruction clears the requester's pending operation before the
   state's memory is released, and only the owner may sever that link.

Product takeover, per-name containment, heartbeat respawn, and the default
product path are all green with those invariants active.

## 6. Follow-up: the create-expiry close ordering (ledger row #34)

A post-merge review of the row-#31 create expiry (`Child::_expire_pending_op`,
`Op::Create`) found one remaining lifetime hole. The expiry registered the
protective `Op::Close` at the parent and then released the client with the
`SERVICE_DENIED` notification — but `Child::session_ready` and
`Env_service::session_ready` both clear the pending operations of the
session, so the notification silently dropped the close that had just been
registered. `_pending_close_op` then reported no close in flight and
`Child::session_cap` destroyed the session state while the parent-side ID
was still live: the RED run re-derives exactly the `destroying session with
a live parent-side id` / `dangling session in parent-side ID space` pair
that the coherent expiry of row #31 was supposed to eliminate.

The ordering fix (fork commit `9088c7227c`, ledger row #34): capture
`close_in_flight` first, deliver the `SERVICE_DENIED` notification first,
and register `Op::Close` afterwards. The close then replaces the expired
create in the one-operation-per-session registry and survives the clear,
so the session state and the parent-side ID stay protected until the
parent's answer destroys the session.

The close is registered with `entry.id` — the ID under which the create
was registered — not `session.id_at_client()`: the denial callback may
already have discarded the session's client-side ID (the RED run reports
`cid=<unconstructed>` in its dangling-ID error), and the registry keys and
the `_pending_close_op`/`session_cap` lookups all use the registered ID.

Red-to-green on base-linux, exact command:

    make -j1 KERNEL=linux BOARD=linux run/child_pending_op

- **RED** (`/tmp/opencode/child-pending-op-red.log`, unpatched): `FAIL close
  op survives create expiry (pending_parent_ops)` and `FAIL session state
  not destroyed by session_cap while close is in flight`, with
  `Error: destroying session with a live parent-side id: service=Pending
  cid=42 ... state=DENIED` and `Error: dangling session in parent-side ID
  space: service=Pending cid=<unconstructed> ... state=CLOSED` in the log;
  `child-pending-op: VERDICT FAIL`, `make` exits 255.
- **GREEN** (`/tmp/opencode/child-pending-op-green.log`, patched): all four
  checks PASS and `child-pending-op: VERDICT PASS`; `Run script execution
  successful`, exit 0 (independently rerun).
- **Related base-linux non-regression**
  (`/tmp/opencode/child-pending-op-related.log`): `run/log` (`Test done.`),
  `run/sub_rm` (`--- end of sub-rm test ---`) and `run/thread` (`test
  completed successfully`) all end in `Run script execution successful`.

The new harness `repos/base/src/test/child_pending_op/` +
`repos/base/run/child_pending_op.run` locks the invariant: an expired
create leaves a pending close, and `session_cap` never destroys a session
state whose close is in flight.
