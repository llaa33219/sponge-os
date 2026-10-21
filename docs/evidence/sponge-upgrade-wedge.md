# The quota upgrade that never gets an answer: the container's entrypoint parks, and the freeze never heals

**Field report (LG gram 17ZD90N, 2026-10-19, latest img).** Boot is fine.
Dragging the SPONGE DEBUG scrollbar fast produces a total freeze; the
mouse is dead; the panel health label reads `⚠ sponge_pkgd unresponsive(N)`.
The discriminating fact: **the freeze is permanent.** Leaving it alone
does not help. That rules out saturation — a saturated system drains its
backlog the moment the load stops — and establishes that a thread is
blocked.

**Answer, measured.** The blocked thread is **a sandbox init's
entrypoint**, and the protocol that blocks it is the **quota upgrade**.
The cycle, named end to end:

```
victim  --Parent::upgrade-->  sandbox init  --nested Parent::upgrade-->
         top init  --PENDING-->  session provider  (never answers)
```

The sandbox init forwards the upgrade to its own parent and then *waits
for the answer*. `Parent::Upgrade_result::PENDING` is not a promise — it
means "the request has been taken, the session's provider has not acted
on it yet" — and a provider that is stuck (the field's file-system
server on a stalled USB stick) never answers. The waiting thread is the
sandbox init's **single entrypoint**: it serves every child's session
plumbing *and* the containment alarm. A parked entrypoint therefore takes
the whole container down **silently and forever** — the alarm that would
have noticed is parked with it, so the containment never fires, the
children are never killed, and nothing is ever restarted. That is the
field signature ("everything stops, including the mouse, forever")
rather than "one component is slow".

---

## 1. The code, quoted

The client side (`genode/repos/base/src/lib/base/component.cc`):

```cpp
void upgrade(Parent::Client::Id id, Parent::Upgrade_args const &args) override
{
        Mutex::Guard guard(_mutex);

        if (_parent.upgrade(id, args) == Parent::Upgrade_result::PENDING)
                _block_for_session();
}
```

`_block_for_session()` parks the thread in `Signal_receiver::
wait_for_signal()`, holding `_mutex` — the lock that serialises the
component's whole session/upgrade/close interface.

The container side (`genode/repos/base/include/base/service.h`,
`Try_parent_service::initiate_request`, the `UPGRADE_REQUESTED` case)
issued that upgrade **from inside init's own RPC dispatch thread**:

```cpp
_env.upgrade(session.id_at_parent->id(), args.string());
```

`Sandbox::Child` registers its `Genode::Child` on
`_env.ep().rpc_ep()` — init's **single main entrypoint**, which also
carries every other child's `Parent::session` / `upgrade` / `close` /
`exit`, the heartbeat alarm, and the `session_requests` ROM handler. A
blocked upgrade there stops every child's session plumbing *and* the
containment timer that would have noticed.

The trigger in the product: the storm drives the client over its RAM
and caps quota, the client asks for more, and the session it asks about
is one of the **child-provided** services (the `vfs_*` File_system
servers of the product media, `run/sponge-desktop-disk-uefi-usb.run`
lines 1475-1520). For those, the provider lives in the *parent* sandbox,
so the upgrade has to be forwarded up the chain — the nesting above.

## 2. The reproduction (QEMU, base-sel4)

`run/sponge-upgrade-wedge.run` + `repos/sponge/src/test/upgrade_wedge/`
(three roles in one binary, selected by the config attribute `role`):

| role | what it does |
|---|---|
| `provider` | announces the service `Test_service` and **never answers a quota upgrade** for it — the field's stuck file-system server, made deterministic |
| `victim` | opens a session to `Test_service` and asks for a quota upgrade on that session |
| `witness` | opens a **fresh Timer session** once per second and logs each round |

The witness is the oracle. A session request goes through the sandbox's
entrypoint; the witness deliberately does *not* use `Test_service`,
because the provider is stuck in the victim's upgrade — a request for
that service would block on the stuck provider and not on the container.
A silent witness therefore means **the container is wedged**, and a
talking witness means the container is alive.

The victim's `Test_service` session is **parent-routed** (the product's
default route), so the sandbox init forwards the upgrade to the top init
and the top init forwards it to the provider child: the nesting is not
simulated, it is the default route.

### RED — unpatched base (`git -C genode checkout 20d0ff31d8`)

```
[init -> upgrade_wedge] provider: announced Test_service
[init -> system -> victim] victim: session to Test_service established - requesting a quota upgrade
[init -> upgrade_wedge] provider: quota-upgrade request received - not answering (the stuck file-system server)
Error: Test execution timed out
```

Full log: [`sponge-upgrade-wedge-red.log`](sponge-upgrade-wedge-red.log).
**Zero** witness rounds: the sandbox init's entrypoint is parked from
the victim's upgrade onwards, and the container never recovers. The run
exits 2 (the gate never matches).

### GREEN — with the fix

```
[init -> system -> victim] victim: session to Test_service established - requesting a quota upgrade
[init -> system] Warning: victim: RAM upgrade of Test_service failed
[init -> system] child "victim" requests resources: ram_quota=4K
[init -> system] child "victim": resource request NOT granted (...) - deferring to the next config application
[init -> upgrade_wedge] provider: quota-upgrade request received - not answering (the stuck file-system server)
[init -> system -> witness] witness: round 1 ok
[init -> system -> witness] witness: round 2 ok
[init -> system -> witness] witness: round 3 ok
```

Full log: [`sponge-upgrade-wedge-green.log`](sponge-upgrade-wedge-green.log).
The upgrade is **refused truthfully** ("RAM upgrade of Test_service
failed"), the victim does not park, the sandbox's entrypoint keeps
serving, and the witness keeps its rounds. The run exits 0.

## 3. The fix

Three edits in the vendored tree (fork commit `500cb2cc8a`, ledger row
#28):

1. **`genode/repos/base/include/base/env.h`** — a new
   `Env::try_upgrade()`, the **non-blocking** variant: the parent's
   answer is passed through as-is, `PENDING` included.
2. **`genode/repos/base/src/lib/base/component.cc`** —
   `Component_env::try_upgrade()` implements it: one RPC, no wait. The
   existing `Env::upgrade()` (which waits) is **unchanged**, so a leaf
   component keeps waiting for its provider's answer.
3. **`genode/repos/base/include/base/service.h`** —
   `Try_parent_service::initiate_request()` uses `try_upgrade()` and
   reports a quota that was **not** granted as not granted
   (`Initiate_error::OUT_OF_RAM` → `Parent::Upgrade_result::OUT_OF_RAM`),
   instead of pretending success.

`genode/repos/libports/src/lib/libc/internal/env.h` forwards the new
method for libc-based components.

**Why this is the root and not a mitigation.** The container's entrypoint
never blocks on a call into its own parent again; a provider that never
answers can no longer take a whole sandbox with it. The requesting client
gets a terminal answer and retries (its retry loop is already bounded by
ledger row #25's `Parent::UPGRADE_ATTEMPTS`), so the failure is loud
and bounded instead of silent and eternal.

## 4. Honest limits

* **A nested upgrade is now refused, not waited for.** A client that
  upgrades a session to a service provided by the *parent* sandbox (the
  product's `vfs_*` File_system servers) gets `OUT_OF_RAM` instead of a
  completed quota transfer. The quota itself still moves up the chain
  (the transfer happens before the answer), so the client's retried
  operation succeeds; what the client loses is the *promise* that the
  upgrade is already in place. The alternative — letting the container
  park until the provider answers — is the field's freeze.
  Completing the promise needs a background re-drive of the pending
  upgrade in the sandbox (a timer-driven retry that notifies the client
  when the chain completes); that is registered as a follow-up, not
  shipped here.
* The same-level case (the provider is a child of the *same* sandbox)
  is unchanged: the container answers `PENDING`, the leaf waits in
  `Env::upgrade()`, and the provider's answer releases it. The wait is
  bounded by the containment, because the container's entrypoint is
  alive and its alarm can fire.
* The reproduction uses a synthetic service, not the product's `vfs_*`
  servers: the field's trigger is a *stuck* provider, and a stuck
  provider cannot be made deterministic in QEMU without a second fault
  injection. The protocol path, the nesting and the wedge are the
  product's.

## 5. What the field should observe

* The fast scrollbar drag no longer freezes the system. If it does, the
  console shows the upgrade being refused (not a hang) and the panel
  health label stays green.
* `grep -c 'RAM upgrade of .* failed'` on the console is the new
  discriminator: a rising count means a client keeps asking for quota
  the provider cannot grant — a quota policy question, visible and
  bounded, not a freeze.
* The mouse stays alive in every case, because the mouse's components
  live in the same sandbox as the client that used to wedge it.

## 6. Manual equivalent (AGENTS.md §3.5)

```bash
# what is installed
git -C genode log --oneline -1

# run the repro
./tool/build run sponge-upgrade-wedge        # exit 0 with the fix

# reproduce the RED arm without reverting anything
git -C genode stash
./tool/build run sponge-upgrade-wedge        # exit 2 (the container wedges)
git -C genode stash pop
```

Reverting is `git -C genode revert 500cb2cc8a` plus the removal of
ledger row #28 in the same commit (AGENTS.md §5.2).
