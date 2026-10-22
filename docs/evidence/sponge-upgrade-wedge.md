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
component's whole session/upgrade/close interface. **That client-side
park is correct and stays**: the client waits for the container's
answer, and the container is the party that owes it one.

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

### GREEN — the shipped design

```
[init -> system -> victim] victim: session to Test_service established - requesting a quota upgrade
[init -> upgrade_wedge] provider: quota-upgrade request received - not answering (the stuck file-system server)
[init -> system] Warning: victim: RAM upgrade of Test_service (session 6) failed - the provider did not answer within 5000 ms
[init -> system -> victim] victim: quota upgrade returned
[init -> system -> witness] witness: round 1 ok
[init -> system -> witness] witness: round 2 ok
[init -> system -> witness] witness: round 3 ok
[init -> system -> witness] witness: round 4 ok
```

Full log: [`sponge-upgrade-wedge-green.log`](sponge-upgrade-wedge-green.log).
The container's entrypoint **never parks**: the upgrade is forwarded
without waiting, the container answers `PENDING`, and the never-answered
request **fails loudly at the deadline** (5 s) with the provider, the
service and the session named. The victim's request returns at that
moment, the witness keeps its rounds, and the run exits 0.

## 3. The designs that were measured and rejected

Three variants were implemented and **measured**, not argued:

* **Variant A — refuse the upgrade.** The container forwards the
  request without waiting and reports the quota as *not granted*
  (`OUT_OF_RAM`); the requesting client retries.
* **Variant B — forward without waiting, report "taken".** The
  container forwards the request without waiting, keeps the quota
  transfer, and answers `PENDING`; the client waits for the session
  notification — but with **no completion path**, i.e. the container
  would answer `PENDING` and then never come back.
* **Variant C — the shipped design (Variant B plus a deadline).** The
  container forwards the request without waiting, keeps the transfer,
  answers `PENDING`, and owns a **deadline**: if the provider has not
  answered when the deadline expires, the container fails the operation
  **loudly** and releases the client.

Variant A is the obvious one and it **breaks the product**: the e1000
driver of the `pc_nic` package never comes up. The reason is visible
in the code: the quota transfer is **reverted** when the request is
refused, so the provider never receives the extra quota and the
client's allocation keeps failing.

Variant B breaks the **notification chain** in two ways, and both were
measured before the deadline was added:

1. **The spurious completion.** The container's completion path asks
   the parent whether the session is still there (`Parent::session_cap`).
   The parent answers with a **valid capability** for a session whose
   quota upgrade the provider has not answered yet — the session exists,
   it is just busy. A completion path that reads "the session is still
   there" therefore completes the upgrade **spuriously**, i.e. it reports
   "granted" for a quota the provider never received, and it does so
   *before* the provider's answer. Measured: the container flipped the
   session to `CAP_HANDED_OUT` while the provider was still stuck. The
   shipped fix is in `Genode::Child::session_cap`: a session whose
   upgrade is in flight is **not ready**, and the container is told so
   instead of being handed a capability it would read as "done".
2. **The double destroy.** The container's session carries a
   **parent-side ID** (the ID of the session in its own parent's ID
   space). When the session is destroyed — by the completion path, by
   the close path, or by the deadline — that ID must be released with
   it. Releasing it twice, or not at all, is what the base's
   `Session_state` destructor guards:

   ```
   Error: dangling session in parent-side ID space: service=Timer cid=<unconstructed> ...
   ```

   Measured twice: once for a session the container destroyed while the
   parent-side ID was still registered, and once for a session whose
   late `session_response(CLOSED)` (the parent's answer, arriving after
   the deadline had already closed the session locally) destroyed it a
   second time. The shipped fix: the parent-side ID is released
   wherever the session is destroyed, and a **late** `CLOSED` answer for
   a session that is already closed is **dropped with a warning**
   instead of destroying the session twice.

Measured (fork commit `500cb2cc8a`, reverted in `3df26cf22b` — the
first, rejected attempt):

| scenario | without the patch | with the rejected patch |
|---|---|---|
| `sponge-upgrade-wedge` | **WEDGED** (0 witness rounds) | ALIVE (3+ rounds) |
| `sponge-pkg-driver` | PASS (the e1000 banner appears) | **FAIL** (no banner, the gate times out) |
| `sponge-supervisor-takeover` | PASS | **FAIL** (the gate times out) |

`git -C genode revert 500cb2cc8a` is the shipped state; the fork pin
is `3df26cf22b`. **No vendored patch is shipped for this defect, and
there is no ledger row for it** (AGENTS.md 5.2: a reverted patch loses
its row).

## 4. The shipped design

Three requirements, all measured (section 3):

1. **The container's entrypoint must never block on a call into its
   own parent.** A nested upgrade has to be forwarded without waiting.
2. **The quota transfer must not be reverted** when the provider has
   not answered yet — the quota has to reach the provider even though
   the provider is busy.
3. **The client must learn when the chain completed — or that it never
   will.** Today that notification is produced by the container
   *waiting*: the container wakes on the upper init's session signal,
   continues, and answers the client `OK`. Without the wait there is no
   producer, and the client's own wait is unbounded.

The shipped shape (fork patch, ledger row #28):

* **Forwarding without parking.** `Env::try_upgrade` / `Env::try_close`
  are the non-blocking variants of `Env::upgrade` / `Env::close`; their
  **default implementation is the blocking one**, so a component that
  does not opt in behaves exactly as before. A container opts in by
  overriding them (`Component_env`), and `Try_parent_service::
  initiate_request` uses them on the forwarded `CREATE_REQUESTED`,
  `UPGRADE_REQUESTED` and `CLOSE_REQUESTED` paths. On `PENDING` the
  phase is left as it is and the request returns `Ok()`.
* **The completion.** The forwarded paths set
  `session.async_client_notify = true`, so the container's own session
  signal is submitted to the client when the session becomes ready —
  the client is released by the *same* mechanism the upstream used,
  except that the container does not have to wait for it.
* **The deadline.** The pending operations live in a **private registry
  inside `Genode::Child`** (at most 8 entries: ID, deadline, elapsed
  time, operation). The registry is advanced by the sandbox's clock:
  the heartbeat alarm when the sandbox has one, otherwise a
  `Pending_op_timer` that arms itself when the sandbox has a Timer
  service. Without a Timer service the degradation is announced
  **once and loudly** — a silent unbounded wait is the very defect the
  deadline exists to prevent.
* **The loud failure.** When the deadline expires, the container prints
  `RAM upgrade of <service> (session <id>) failed - the provider did not
  answer within 5000 ms`, keeps the quota bookkeeping, marks the entry
  as expired and submits the session signal once, so the client is
  released. The close expiry has the same shape; it closes the session
  locally and does **not** revert the quota.
* **The retry guard.** A client that retries an upgrade whose deadline
  already expired must not move quota a second time: `Child::upgrade`
  answers `OUT_OF_RAM` at the very top, before any quota movement. The
  client's own allocator then reports the failure truthfully.
* **No new Parent RPC, no generic signal correlation, no teardown
  hook.** The registry dies with the `Child` it belongs to; a late
  `session_response(CLOSED)` after a forced close is dropped with a
  warning, never applied twice.

### The clock the deadline needs

The sandbox needs a Timer service to advance the deadline. Its
availability is determined the way a `Timer::Connection` would
determine it: the session is **requested** and a denial is reported,
not fatal. The old check ("does my config's `parent-provides` name a
Timer service?") was wrong in both directions — it ignored a Timer that
comes from the sandbox's *parent* under a different name, and it
reported "no Timer service" for a sandbox whose Timer service is
reachable. The attempt-based check is the upstream routing's own
decision, so it cannot be wrong about the routing.

Measured in the wedge scenario: the `system` sandbox obtains its Timer
service (its `parent-provides` names it) and therefore has a clock; the
**top-level** init does not get one — its request for a Timer session
is **denied by the ROM's routing** (the top-level init's own request
goes to *its* parent, not to its own timer child), so it reports the
degradation once and runs with an unbounded deadline. That is the truth
about that topology, and the warning now names the reason
(`session request failed: 0` = denied) instead of guessing from the
config.

## 5. The regression that hid the working design

For a while the scenario reported `WEDGED` with **no provider line** and
a silent witness — and the first reading ("the forward path is broken")
was wrong. The measurement that corrected it:

* the same image, booted by hand with the serial console attached to a
  **file** instead of the run tool's pseudo terminal, shows the whole
  chain working: the provider logs `quota-upgrade request received`, the
  container fails the upgrade loudly after 5 s, the victim's request
  returns, the witness reaches round 19;
* the captured log of the failing run ends **mid-line** (the victim's
  own log line, cut after `established `), and the file is *shorter*
  than the output the guest had produced.

The run tool reads the guest's console only while it waits for a
pattern. The scenario stopped reading right after the victim's line
(`sleep 45`), the console buffer filled, and the guest's console output
— which the seL4 kernel writes **polled** — blocked the whole guest.
The capture therefore ended at the point where the tool stopped
reading, and every marker after it (the provider's line, the loud
failure, the witness's later rounds) was never captured. The "wedge"
was a **harness artifact**: the container was alive and talking.

The scenario now keeps draining the console while it observes the
container (an `expect` block instead of a bare `sleep`), and it waits
for the witness's round 4 — strictly after the deadline (5 s) and after
round 3, the two markers the verdict is read from. A container that is
really wedged never reaches round 4: the wait times out, the verdict is
read from the log captured up to that point, and the run fails.

## 5a. The acceptance matrix, measured

| scenario | result |
|---|---|
| `sponge-upgrade-wedge` | **PASS** — the five-condition gate: the provider received the request, the container reported the failure loudly, the victim's request returned, the witness kept its rounds (round 4), no spurious success |
| `sponge-pkg-driver` (×3) | **PASS** — the e1000 banner appears, the nested upgrades complete through the notification (not through the 5 s deadline) |
| `sponge-minimal` | **PASS** |
| `sponge-startup-grace` | **PASS** |
| `sponge-debug-firstpaint` | **PASS** |
| `sponge-debug-scrollstorm` | **PASS** |
| `./tool/patches verify` | **OK** — all 28 ledger rows match git reality |
| `sponge-supervisor-takeover` | **FAIL** — see below (the same failure is in the log from before this patch was written) |
| `sponge-desktop-disk` | **FAIL** — the same escalation loop (the scenario's own product regression) |
| `sponge-desktop-disk-uefi-usb` | **FAIL** — the `usb_block` storage chain does not reach `configd` (the same failure as the run from 2026-10-19, i.e. pre-existing) |

The two failures share one symptom, and it is **not** the defect this
patch removes: the supervised sandbox's **state report freezes** the
moment the supervisor kills the subtree (`state_age_ms=40500` and
growing), so the supervisor keeps seeing the fault and keeps
escalating until the scenario times out. The frozen report means the
sandbox's entrypoint is not delivering its state change — a different
wedge than the quota upgrade, and one that this patch does not touch.

The symptom is **not** introduced by this patch: it is identical with
the completion probe enabled, with it disabled, and it is the symptom in
the log from before the patch was written (`state_age_ms=40500`,
`max_skipped=11`, `starting=0`, and the same
`freeing non-empty slab block` / `illegal READ` pair). The last green
run of `sponge-supervisor-takeover` (2026-10-17) shows the sandbox's
own containment doing its job — `max_skipped=3`, `state_age_ms=250`, no
escalation — while the failing runs show the containment not restarting
the wedged child at all (`max_skipped=11`, `starting=0`). The product's
supervisor wiring changed between the two (the published config grew
from 1966 to 1972 bytes), which is where the regression belongs; it is
tracked separately and is **not** closed by this patch.

What this patch does change for that scenario: the quota-upgrade path
that the scenario exercises is no longer a parking path (the victim's
request returns, the failure is loud, the container keeps serving) —
what still fails is the sandbox's own containment, i.e. a different
wedge in the sandbox's heartbeat path.

## 6. Honest limits — what this does and does not establish

**Established, with numbers:**

* The quota-upgrade protocol allows a **permanent, container-wide**
  wedge: a sandbox init's single entrypoint can be parked by a
  provider that never answers, and the containment that would notice
  is parked with it. Reproduced in QEMU, measured (0 witness rounds),
  with the scenario in the tree.
* The nesting that produces it is not simulated: it is the product's
  default route, with the provider in the parent sandbox.
* The shipped design keeps the container's entrypoint out of the
  provider's answer path: the never-answered upgrade fails **loudly at
  the deadline** (5 s), the client is released, and the container keeps
  serving (the witness reaches round 4 and beyond).
* The first fix attempt breaks a working system, and **how** it breaks
  it is measured (the e1000 driver), so the next attempt does not
  repeat it.

**Not established:**

* **That this is the 2026-10-19 field freeze.** The field's freeze is
  permanent; the wedge reproduced here is permanent in the *constructed*
  case (the provider never answers, and the provider's sandbox has no
  containment for it). In the product, the vfs lives in the top-level
  sandbox, whose containment is alive — so the chain is expected to
  unwind when that containment kills the vfs. **The field's permanence
  is therefore still unexplained**, and this document does not claim it
  is explained. The discriminating field check is in
  `docs/evidence/sponge-quota-deadlock.md` section 6.
* The reproduction uses a synthetic service rather than the product's
  `vfs_*` servers: the field's trigger is a *stuck* provider, and a
  stuck provider cannot be made deterministic in QEMU without a second
  fault injection. The protocol path, the nesting and the wedge are the
  product's.

## 7. Manual equivalent (AGENTS.md 3.5)

```bash
# the measurement
./tool/build run sponge-upgrade-wedge
#   -> "upgrade-wedge: container verdict = ALIVE"
#      "provider request   : yes"
#      "loud failure       : yes"
#      "upgrade returned   : yes"
#      "witness round 3    : yes"

# the same image with the console attached to a file (the diagnosis of
# the capture artifact of section 5)
qemu-system-x86_64 -accel kvm -nographic -m 512M \
  -serial file:/tmp/wedge.log -cdrom genode/build/x86_64/var/run/sponge-upgrade-wedge.iso \
  -machine q35 -cpu Skylake-Client -net nic,model=e1000,netdev=net0 -netdev user,id=net0

# the rejected first attempt, for the record (it breaks sponge-pkg-driver)
git -C genode checkout 500cb2cc8a
./tool/build run sponge-upgrade-wedge        # ALIVE
./tool/build run sponge-pkg-driver          # FAIL - the e1000 driver
git -C genode checkout main                 # back to the shipped pin
```

## 8. What the field should observe (once the fix lands)

* The fast scrollbar drag must not freeze the system. If it does, the
  console shows the upgrade being refused or left pending (not a hang)
  and the panel health label stays green.
* `grep -c 'RAM upgrade of .* failed'` is the new discriminator: a
  rising count means a client keeps asking for quota the provider
  cannot grant — a quota policy question, visible and bounded.

---

## 9. Addendum (2026-10-23): the same non-blocking close broke the child's teardown

The fix above (fork commit `ea04b06868`, ledger row 28) made the
container forward a **close** without waiting for the parent's answer.
The takeover regression (`run/sponge-supervisor-takeover`) found the
consequence, and it is a second, independent defect:

> **A `Genode::Child` must not own a live `Session_state` when it is
> destroyed.** The session meta data is allocated from a slab that is
> released together with the child.

At the teardown of a child whose close is still pending at the parent,
the container has three options, and two of them are wrong:

| option | consequence |
|---|---|
| destroy the session state | the slab is released with a live state → `freeing non-empty slab block`, and the state that is written after the release corrupts the memory it lived in → `illegal WRITE at address ...` |
| destroy the state and release the parent-side ID | the parent's ID space keeps a dangling entry → the next session request that reuses the ID is rejected → `requested conflicting session ID ...` |
| **keep the state alive until the parent answers** | the child's slab stays occupied → the child must not be destroyed before the parent's answer arrives |

The third option is the one that is implemented now (fork commit
`662fb14f12`):

* the teardown discards the client-side ID and keeps the state alive;
* the pending close is registered in the child's pending-operation
  registry, and `Child::pending_parent_ops()` tells the sandbox that
  the child still owns a session the parent holds;
* the sandbox destroys an abandoned child only when
  `env_sessions_closed() && !pending_parent_ops()` — otherwise the child
  is left for the next configuration application (a retry);
* the deadline of a pending close abandons the *wait*, not the session:
  it neither closes the session nor releases the parent-side ID. The
  parent's late answer (`session_response(CLOSED)`) is the completion
  channel, and it destroys the state and clears the operation.

Two smaller defects came out of the same measurement:

* `Session_state::id_at_client()` throws when the client-side ID was
  discarded (the client vanished). The teardown touched such an ID and
  aborted the container's entrypoint (`attempt to access unconstructed
  object` → `abort called - thread: ep`). `Session_state::
  id_at_client_if_valid()` is the null-safe accessor, and
  `Session_state::destroy()` now fails loudly if it is called with a
  live parent-side ID — a violation of the invariant above becomes a
  diagnosable error instead of a silent corruption.
* the sandbox's parent-service probe (the one-shot `session_cap` query
  that asks the parent whether a forwarded operation completed) is the
  completion channel for a **quota upgrade** that the container
  forwarded to its own parent. Removing it made every forwarded
  upgrade run into its deadline (`RAM upgrade of Gui (session 7) failed
  - the provider did not answer within 5000 ms`), so it stays.

### Measurement (QEMU/base-sel4, Sponge OS)

| scenario | result |
|---|---|
| `run/sponge-supervisor-takeover` | **PASS** — `ep_wedge: incarnation alive` appears three times (the respawn after `published <system> kill=2` is visible), and none of `freeing non-empty slab block`, `illegal WRITE at address`, `illegal READ at address`, `attempt to access unconstructed object`, `Uncaught exception`, `abort called`, `requested conflicting session ID`, `too many pending session operations` appears |
| `run/sponge-upgrade-wedge` | **PASS** — the container verdict is `ALIVE`, the never-answered upgrade still fails loudly at the deadline, the witness keeps its rounds |
| `run/sponge-pkg-driver` (twice) | **PASS** — `driver-probe: PASS` and `sponge-pkg-driver: PASS`, no corruption string |

The scenario that the teardown defect broke first
(`run/sponge-supervisor-takeover`) was red before the fix with
`freeing non-empty slab block` + `illegal WRITE at address 0x285d8` in
the supervised sandbox, and the respawn of the wedged child never
happened.

**Not fixed by this commit (honest scope).**
`run/sponge-desktop-disk` is still red, and it was red before it as
well (the same two failures, both pre-existing in the fork commit
`ea04b06868`):

* the pending-operation registry of a child held at most 8 entries,
  and the sandbox's many short-lived ROM sessions filled it
  (`too many pending session operations - failing the close of session
  91`). The registry now holds 64 entries, which fits a configuration
  generation of a busy sandbox; the string is gone from the log.
* the desktop is still red, and the cause is **not** a capability
  budget. The order in the log is the diagnosis: a near-NULL read kills
  the *client* first, and everything after it is downstream.

  The faulting instruction is at `ip=0xc3e61` in the dynamic linker
  (the ld.so is mapped at 0x0 in every PD, so the address identifies the
  code, not the process). `addr2line` against
  `genode/build/x86_64/debug/ld-sel4.lib.so.debug` names it:

  ```
  Genode::Expanding_parent_client::upgrade(Parent::Client::Id, Rpc_in_buffer<160> const&)
    -> Genode::Rpc_in_buffer<160>::string()      (rpc_args.h:131)
       -> Genode::Rpc_in_buffer<160>::valid_string()  (rpc_args.h:119)
  ```

  The disassembly pins it down further: the faulting instruction is
  `mov 0x8(%rdi),%rax` with `%rdi == 0` - i.e. the **third argument
  (the `Upgrade_args` object) is NULL**, and the crash happens at the
  very first use of that argument (`args.string()`). The preceding
  `cmp $0x1,%rsi` is the `id == Env::pd()` test, and it is *not* taken:
  this is a **session** upgrade (not a PD upgrade).

  So the sequence in `run/sponge-desktop-disk` is:

  1. `illegal READ at address 0x8 ... pd='init -> system -> sponge-de'
     thread='ep'` - the client-side upgrade path of sponge-de dereferences
     a NULL argument buffer. The component's entrypoint dies with it.
  2. `seL4: Attempted to invoke a null cap` and
     `system: attempt to request cap for unavailable session: service=ROM
     cid=114 ... state=CREATING` - the *sandbox* is still waiting for the
     ROM session of the dead component.
  3. `CPU (init -> system -> sponge-de) create_thread: invalid PD
     argument` and `failed to create initial thread for new PD` - the
     respawn of the dead component cannot even start.

  The `alpha-probe` then never gets its package installation done.

  **Instrumented run (temporary logging in `Component_env::upgrade` and
  `Expanding_parent_client::upgrade`, since reverted).** The log of the
  same scenario shows, for every *healthy* upgrade of sponge-de:

  ```
  [init -> system -> sponge-de] DBGX Component_env::upgrade id=25 args=0x403f9680
  [init -> system -> sponge-de] DBGX EPA::upgrade          id=25 args=0x403f9680
  ```

  i.e. a valid *stack* address for the `Upgrade_args` object - and for
  the upgrade that kills the component:

  ```
  [init -> system -> sponge-de] DBGX Component_env::upgrade id=29 args=0x0
  [init -> system -> sponge-de] DBGX EPA::upgrade          id=29 args=0x0
  Error: illegal READ at address 0x8 ... ip=0xc40fa
  ```

  The `Upgrade_args` **object itself is at address 0** - the caller
  passed a NULL reference, and the caller's own return address is
  garbage as well (it points into no mapping of the PD). Both values
  come out of the caller's stack frame, so the **caller's frame is
  corrupt** when the upgrade is issued: the client-side upgrade is
  called with a smashed stack, not with a malformed argument.

  The upgrade that fails is a *session* upgrade (id 29, not
  `Env::pd()`), it happens on sponge-de's entrypoint right after the
  `dlopen` of the EGL/mesa libraries, and it is the only upgrade of
  that component in the run. The client-side path is therefore the
  suspect, and the fork's contribution to it is the machinery that
  makes a session complete *before* the client parks in the Blockade
  (`async_client_notify` + the shared `Signal_receiver`): a completion
  that races the client's Blockade registration can leave the client
  with a stale `Connection`/signal state. The exact race - notification
  delivered while no sigh is registered, delivered twice, or the
  `Connection` object's sigh re-registration racing the completion - is
  still open; the fix must make the Blockade/notification handshake
  idempotent for a session that completes before the client parks.

Both are consequences of the same root (a close that the parent has
not confirmed yet) and are the next step of this work; they are
tracked here rather than silently left in the log.
