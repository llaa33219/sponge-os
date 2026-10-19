# The quota-request wedge: `sponge_pkgd unresponsive(N)` and the freeze that never heals

**Field report (LG gram 17ZD90N, 2026-10-19, latest img).** Boot is fine.
Dragging the SPONGE DEBUG scrollbar fast produces a total freeze; the
mouse is dead; the panel health label reads `⚠ sponge_pkgd unresponsive(N)`.
The new fact that discriminates: **the freeze is permanent.** Leaving it
alone does not help. That rules out pure saturation — a saturated system
drains its backlog the moment the load stops — and establishes that a
thread is blocked.

**Answer, measured.** The blocked thread is a component's entrypoint stuck
in an **unbounded quota-upgrade retry loop against its sandbox init**, and
the reason it never heals is that the sandbox **answers "granted" for a
grant that never happened**. Two unbounded behaviours compose:

1. `Sandbox::Child::resource_request` reports success for a quota transfer
   that failed, so the client retries forever.
2. Even with an honest refusal, the client's retry loop has no bound.

Measured on `run/sponge-quota-deadlock` (red and green in §5):

| | upgrade RPCs against the shared sandbox entrypoint | false "granted immediately" | outcome |
|---|---:|---:|---|
| before | **3701** | **3701** | never converges |
| after | **4** | **0** | containment settles on its restart budget |

A further, *code-proven* consequence of the same protocol gap: if the
sandbox answers `PENDING` instead of `OK`, the client blocks forever
(§2). That is the deadlock variant; QEMU did not reproduce it (§6), and
this document does not claim it did.

---

## 1. What the health label actually reports

Not the guess it looks like. `sponge-de` renders
`⚠ <name> unresponsive(<n>)` (`repos/sponge/src/sponge-de/main.cc:669-678`,
glyph at `panel/panel_widget.cc:472`) from the `skipped_heartbeats`
attribute of the `system` sandbox's state report
(`genode/repos/os/src/lib/sandbox/child.cc:389-391`). With
`restart_after_skipped: 3` at `rate_ms: 2000`, the label fires at the
**first** missed heartbeat — about 2 s — while containment fires only at
the fourth. `unresponsive(1)` is an early warning, not a kill.

So the field says: **`sponge_pkgd`'s entrypoint stopped dispatching for
more than 2 s.** It does not say pkgd is slow, and it does not say the
kernel is saturated.

## 2. The protocol gap, named

`Parent::upgrade` has no "denied" result. `Parent::Upgrade_result` is
`{ OK, PENDING, OUT_OF_RAM, OUT_OF_CAPS }`
(`genode/repos/base/include/parent/parent.h:184`), and a parent that
cannot grant simply does not produce a terminal answer. Client side
(`genode/repos/base/src/lib/base/component.cc:252-258`):

```cpp
void upgrade(Parent::Client::Id id, Parent::Upgrade_args const &args) override
{
        Mutex::Guard guard(_mutex);

        if (_parent.upgrade(id, args) == Parent::Upgrade_result::PENDING)
                _block_for_session();
}
```

`_block_for_session()` parks the thread in `Signal_receiver::
wait_for_signal()`, which is a core RPC that never receives a reply — and
it does so **holding `_mutex`**, the lock that serialises the whole
component's session/upgrade/close interface. That is a permanent, silent,
whole-thread wedge with no diagnostic anywhere.

Reached from four unbounded loops in the tree, each of which asks its
parent for more quota and, on refusal, asks again:

| site | what it allocates | on the product |
|---|---|---|
| `base/src/lib/base/signal.cc:234` `Signal_receiver::manage` | one PD cap per signal context | **the input/compositor path** — one context per signal source per client view |
| `base/src/lib/base/rpc_cap_alloc.cc:44` `Rpc_entrypoint::_alloc_rpc_cap` | one PD cap per RPC object | every RPC object in every component |
| `base/include/rm_session/connection.h:32` `Rm_connection::create` | heap growth | every component whose heap outgrows its quota |
| `os/include/file_system_session/connection.h:42` `_retry` (`UPGRADE_ATTEMPTS = ~0U`) | per `dir`/`file`/`symlink`/`node`/`watch` | every file-system client |

### The second-order hazard: the nested upgrade runs on the parent's entrypoint

`Genode::Child::upgrade` forwards an upgrade to the session's provider
(`genode/repos/base/src/lib/base/child.cc:400`). When that provider is a
service routed to init's **own** parent — which is what the product
media's `+ any-service | + parent | + any-child` default route makes of
every `Rm` / `File_system` / `Block` / `Terminal` request — the provider
is a `Genode::Try_parent_service`, and `genode/repos/base/include/
base/service.h:371` issues a **second, nested, blocking**
`Parent::upgrade` from inside init's RPC dispatch thread:

```cpp
case Session_state::UPGRADE_REQUESTED:
        ...
        _env.upgrade(session.id_at_parent->id(), args.string());
```

`Sandbox::Child` registers its `Genode::Child` on `_env.ep().rpc_ep()`
(`genode/repos/os/src/lib/sandbox/child.h:438`) — init's **single main
entrypoint**, which is also where the heartbeat alarm and the
`session_requests` ROM handler run. A blocked upgrade there therefore
stops every child's session plumbing *and* the containment timer that
would have noticed. This is why the field signature is "everything
stops, including the mouse, forever" rather than "one component is
slow".

## 3. What was ruled out, and how

* **`Connection_base::retry_with_upgrade` is dead code.**
  `genode/repos/base/include/base/connection.h:74` defines it with
  `enum { UPGRADE_ATTEMPTS = ~0U }`, and the task brief names it as the
  prime suspect. A whole-tree grep finds **zero callers**. The live
  unbounded loops are the four in §2.
* **init blocking inside `close_all_sessions`.** Candidate A. It does
  not block: `Child::_close` issues the close request with
  `initiate_request()` and returns `Close_result::PENDING`
  (`genode/repos/base/src/lib/base/child.cc:507-543`) — asynchronous.
* **`_with_pd` blocking.** `Local_connection::with_session`
  (`genode/repos/base/include/base/local_connection.h:131-149`) falls
  through to `denied_fn()` on an invalid cap; it never waits. So the
  `pd.transfer_quota()` inside the old `resource_request` could not
  deadlock, only fail.
* **`report_rom` Region_conflict.** Candidate B. Not reproduced here;
  consistent with ledger row #24's finding that the ~35 KB/cycle clamp
  leak needs a `usb_block` storage stall and did not reproduce on a
  healthy chain.
* **The kernel page-table flush path.** The 2026-10-17 measurement
  (`docs/evidence/sponge-debug-panelfreeze.md` §3) found a correctly
  sized 2560x1600 configuration healthy at 19 Hz / 387 ms / **0**
  flushes. It is a real cost, but it is a *cost*: it recovers when the
  load stops, so it cannot explain a permanent freeze.

## 4. The defect that was measured

`genode/repos/os/src/lib/sandbox/child.cc`, `resource_request`, as
shipped by ledger row #21:

```cpp
if (ram_satisfiable && caps_satisfiable) {
        _with_pd([&] (Pd_session &pd) {
                if (req_ram.value &&
                    pd.transfer_quota(ref_account_cap(), req_ram) != OK)
                        warning(name(), ": immediate ram grant failed");   // <-- warned
                ...
        });
        _resources.assigned_quota.ram.value  += req_ram.value;
        log("child \"", name(), "\": resource request granted immediately");  // <-- claimed
        _child.notify_resource_avail();
        return;
}
```

Two errors, and they are the same error:

1. **The account is wrong.** `pd.transfer_quota(ref_account_cap(), …)`
   is bounded by the **child's own unassigned quota**
   (`genode/repos/base/src/core/include/account.h:137-146`:
   `if (amount.value > _transferrable_quota().value) return EXCEEDED`).
   That quota is precisely what ran out and prompted the request, so the
   call returns `EXCEEDED` **no matter how much slack the sandbox holds**.
   Row #21's satisfiability test looked at the sandbox's *own* stats — a
   different account.
2. **The result is discarded.** A failed transfer only produced a
   `warning`, and the code then incremented `assigned_quota`, logged
   `granted immediately`, and notified the child.

The child therefore loops forever: allocation → `OUT_OF_CAPS` →
`upgrade(PD)` → `Child::upgrade` → `resource_request` → "granted" →
allocation fails → … Each iteration is a round trip to the sandbox's
main entrypoint, shared with every other child.

## 5. RED → GREEN

`run/sponge-quota-deadlock.run` — a sandbox in which a child with a
**fixed** quota (`victim | caps: 3000 | ram: 4M`, no `<resource>`)
spends its quota and then drains its PD with 1000 signal contexts. The
request is unsatisfiable by construction: nothing will ever reconfigure
the system. Witnesses at two levels (`ep_wedge behave` at Tier-0, and
`bystander | liveness_ms` inside the sandbox) plus the victim's
`restart_after_skipped: 3 | restart_budget: 3` containment.

```
# RED  (genode at f44bc47ee5, row #21's shortcut intact)
$ make -C genode/build/x86_64 run/sponge-quota-deadlock
  requests resources: ...            3701
  resource request granted immediately  3701
  immediate ram grant failed            3701
  bystander still alive?                 yes
exit 2 (FAIL - the run times out waiting for a marker that never comes)

# GREEN (342900343b)
  child "victim": resource request NOT granted (requested ram=8192/caps=0,
    assigned ram=4194304/caps=3000, configured ram=4194304/caps=3000,
    our slack ram=2698598/caps=18) - deferring to the next config application
  Warning: victim: heartbeat restart budget (3) exhausted after 3 restarts
  requests resources:                    4
  resource request granted immediately   0
quota-deadlock: PASS
exit 0
```

**925x** fewer upgrade RPCs on the shared entrypoint, and the false
success is gone. The scenario's host-side assertions are what make this
falsifiable: a gate that merely waits for a log line cannot tell "the
request was refused" from "the request was granted once and never
repeated", so the scenario greps the captured serial log for
`granted immediately` (must be 0) and counts `requests resources:` (must
be ≤ 4, one per incarnation).

Two things are worth stating plainly about GREEN:

* **The victim still fails.** It asks for quota its config forbids, and
  the honest answer is no. What changes is that the answer is *truthful*,
  *bounded*, and *contained*: 4 requests, then heartbeat containment
  exhausts its budget and stops. The alternative — a config that
  authorises growth, e.g. `<resource name="RAM" quantum="8M"/>` — is the
  operator's decision (AGENTS.md §1.1, control), and the log now tells
  them exactly which number is the obstacle.
* **925x fewer RPCs is a bandwidth number, not a latency number.** On
  QEMU with one vCPU the bystander kept ticking at its nominal 1 Hz in
  both arms, so this scenario does **not** by itself demonstrate the
  field's total freeze. What it demonstrates, without ambiguity, is that
  a component could keep the sandbox's shared entrypoint saturated
  forever while the sandbox cheerfully reported every request granted.

## 6. Honest limitations — what this does and does not establish

**Established, with numbers:**

* A quota request that the sandbox cannot satisfy was answered as
  granted, for an account-based reason that makes the failure certain
  rather than possible (§4). The false-grant path and the unbounded
  retry are both gone.
* An unsatisfiable request is now a loud, bounded, contained event
  instead of an endless silent one, and it is diagnosable from the log
  line alone — the line names the requested, assigned, configured and
  our-slack values, which is exactly the set an operator needs.
* Four unbounded upgrade loops are bounded by a shared
  `Parent::UPGRADE_ATTEMPTS`.

**Not established:**

* **The QEMU reproduction of the *total* freeze, including the mouse,
  was not achieved.** `run/sponge-debug-scrollstorm` at 2560x1600 and
  the field's drag shape do not reproduce it, and neither does
  `run/sponge-quota-deadlock`. The deadlocked *variant* of §2 —
  `Child::upgrade` returning `PENDING` so the client blocks in
  `_block_for_session()` holding `_mutex` — is proved from the source
  and not from a run. Its trigger needs a routed (not parent-routed)
  session whose upgrade acknowledgement never arrives, which the product
  topology has and this harness does not reproduce.
* **QEMU's one vCPU flatters the RED arm.** A single-threaded spinner on
  the only core degrades differently than a spinner competing with a
  compositor and a USB HID stack across four cores on the LG gram. The
  transferable claim is the shape (an unbounded loop on a shared
  entrypoint, reported as success), not the magnitude.
* **This patch does not change any product quota.** The product media's
  `sponge_pkgd | caps: 200`, `sponge_configd | caps: 200` and
  `sponge_themed | caps: 200` (`run/sponge-desktop-disk-uefi-usb.run`)
  are the small budgets most exposed to this path, and they are
  unchanged. No bump was made without measurement (task constraint).
  What changed is that exhausting one is now visible and contained
  rather than fatal.

**The discriminating field check**, one command, on the LG gram's
console during the drag:

```
grep -c 'requests resources'      <console log>
grep -c 'granted immediately'     <console log>
```

* A rising `granted immediately` count, or any `granted immediately`
  after this patch, means the false-success path is back.
* A steady `requests resources` with `NOT granted` and then
  `heartbeat restart budget exhausted` means the request is now being
  refused truthfully and contained — the freeze's *symptom* is gone but
  the underlying quota need is not, and the fix is a quota policy
  decision.
* `grep -c 'mapping cache full'` stays the discriminator for the
  2026-10-17 mechanism (`docs/evidence/sponge-debug-panelfreeze.md` §8);
  hundreds during the drag and zero while idle would mean that mechanism
  is the live one after all, and this document's chain is not.

## 7. Regressions

`genode` at `342900343b`.

Green: `sponge-quota-deadlock` (4 upgrade RPCs, 0 false grants),
`sponge-minimal`, `sponge-desktop-disk` (`alpha-probe: PASS`),
`sponge-desktop-disk-uefi-usb`, `sponge-pkg-driver`,
`sponge-supervisor-takeover`.

**Not green, and not a regression:** `sponge-debug-firstpaint` and
`sponge-debug-scrollstorm` both exit 2 with `Error: Test execution
timed out` — **on the patched tree and on the unpatched baseline
(`f44bc47ee5`) alike**. Their in-band telemetry is healthy and unchanged
by the patch (`STORM done … achieved_hz=20 max_gap_ms=196` patched vs
`20 / 132` baseline, `VERDICT PASS` both; `FIRSTPAINT ms=828` vs `729`).
The failure is an expect gate that does not match a line the run
provably captured, and both scenarios are the two whose QEMU line ends in
`-serial mon:stdio`. Registered as an open defect with a reproduction
command in
[`sponge-quota-deadlock-regressions.md`](sponge-quota-deadlock-regressions.md);
it is not diagnosed here.

## 8. Manual equivalent (AGENTS.md §3.5)

The whole change is seven files in the vendored tree, applied by bumping
the submodule pin:

```bash
# what is actually installed
git -C genode log --oneline -1
git -C genode diff f44bc47ee5 342900343b --stat

# run the repro by hand
make -C genode/build/x86_64 run/sponge-quota-deadlock

# reproduce the RED arm without reverting anything
git -C genode stash          # or: git -C genode checkout f44bc47ee5
make -C genode/build/x86_64 run/sponge-quota-deadlock   # exits 2
```

Reverting is `git -C genode revert 342900343b`, and per AGENTS.md §5.2
the ledger row in `docs/11-environment.md` §4 is removed in the same
commit.