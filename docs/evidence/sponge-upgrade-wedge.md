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

## 3. Why the first fix attempt was reverted

Two variants were implemented and **measured**, not argued:

* **Variant A - refuse the upgrade.** The container forwards the
  request without waiting and reports the quota as *not granted*
  (`OUT_OF_RAM`); the requesting client retries.
* **Variant B - forward without waiting, report "taken".** The
  container forwards the request without waiting, keeps the quota
  transfer, and answers `PENDING`; the client waits for the session
  notification.

Variant A is the obvious one and it **breaks the product**: the e1000
driver of the `pc_nic` package never comes up. The reason is visible
in the code: the quota transfer is **reverted** when the request is
refused, so the provider never receives the extra quota and the
client's allocation keeps failing.

Variant B keeps the transfer but breaks the **notification chain**: the
container answers `PENDING` and the client parks - but the
notification that would release it travels through the container
(`Child::session_ready` -> the client's session signal), and a container
that is *not* waiting drops the upper init's session signal. The
client therefore parks forever, and the e1000 driver hangs at startup
instead of failing.

Measured (fork commit `500cb2cc8a`, reverted in `3df26cf22b`):

| scenario | without the patch | with the patch |
|---|---|---|
| `sponge-upgrade-wedge` | **WEDGED** (0 witness rounds) | ALIVE (3+ rounds) |
| `sponge-pkg-driver` | PASS (the e1000 banner appears) | **FAIL** (no banner, the gate times out) |
| `sponge-supervisor-takeover` | PASS | **FAIL** (the gate times out) |

`git -C genode revert 500cb2cc8a` is the shipped state; the fork pin
is `3df26cf22b`. **No vendored patch is shipped for this defect, and
there is no ledger row for it** (AGENTS.md 5.2: a reverted patch loses
its row).

## 4. The fix this defect needs

The requirements are now measured, and they are three:

1. **The container's entrypoint must never block on a call into its
   own parent.** A nested upgrade has to be forwarded without waiting.
2. **The quota transfer must not be reverted** when the provider has
   not answered yet - the quota has to reach the provider even though
   the provider is busy.
3. **The client must learn when the chain completed.** Today that
   notification is produced by the container *waiting*: the container
   wakes on the upper init's session signal, continues, and answers
   the client `OK`. Without the wait there is no producer - and the
   client's own wait is unbounded.

The smallest shape that satisfies all three is an **asynchronous
completion of the nested upgrade, off the container's entrypoint**: the
container forwards the upgrade, keeps the transfer, answers the client
`PENDING`, and a completion path (a dedicated thread, or a
timer-driven re-drive in the sandbox) notifies the client when the
upper init's provider has answered. The re-drive has a natural place
to live: the sandbox already owns a timer (the heartbeat alarm), and
the pending upgrades are already marked in the session state
(`Session_state::UPGRADE_REQUESTED`).

Until that exists, the defect is **contained rather than fixed**: the
container's park ends when the provider's own sandbox kills the
provider (the containment of the provider's sandbox, not the
container's), so the system recovers on its own - but the container's
own containment is dead while it is parked, so the components that
wedged with it are not restarted.

## 5. Honest limits - what this does and does not establish

**Established, with numbers:**

* The quota-upgrade protocol allows a **permanent, container-wide**
  wedge: a sandbox init's single entrypoint can be parked by a
  provider that never answers, and the containment that would notice
  is parked with it. Reproduced in QEMU, measured (0 witness rounds),
  with the scenario in the tree.
* The nesting that produces it is not simulated: it is the product's
  default route, with the provider in the parent sandbox.
* The first fix attempt breaks a working system, and **how** it breaks
  it is measured (the e1000 driver), so the next attempt does not
  repeat it.

**Not established:**

* **That this is the 2026-10-19 field freeze.** The field's freeze is
  permanent; the wedge reproduced here is permanent in the *constructed*
  case (the provider never answers, and the provider's sandbox has no
  containment for it). In the product, the vfs lives in the top-level
  sandbox, whose containment is alive - so the chain is expected to
  unwind when that containment kills the vfs. **The field's permanence
  is therefore still unexplained**, and this document does not claim it
  is explained. The discriminating field check is in
  `docs/evidence/sponge-quota-deadlock.md` section 6.
* The reproduction uses a synthetic service rather than the product's
  `vfs_*` servers: the field's trigger is a *stuck* provider, and a
  stuck provider cannot be made deterministic in QEMU without a second
  fault injection. The protocol path, the nesting and the wedge are the
  product's.

## 6. Manual equivalent (AGENTS.md 3.5)

```bash
# the measurement
./tool/build run sponge-upgrade-wedge
#   -> "upgrade-wedge: container verdict = WEDGED"
#      "witness rounds : 0"

# the first fix attempt, for the record (it breaks sponge-pkg-driver)
git -C genode checkout 500cb2cc8a
./tool/build run sponge-upgrade-wedge        # ALIVE
./tool/build run sponge-pkg-driver          # FAIL - the e1000 driver
git -C genode checkout main                 # back to 3df26cf22b
```

## 7. What the field should observe (once the fix lands)

* The fast scrollbar drag must not freeze the system. If it does, the
  console shows the upgrade being refused or left pending (not a hang)
  and the panel health label stays green.
* `grep -c 'RAM upgrade of .* failed'` is the new discriminator: a
  rising count means a client keeps asking for quota the provider
  cannot grant - a quota policy question, visible and bounded.
