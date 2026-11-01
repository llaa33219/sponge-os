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
