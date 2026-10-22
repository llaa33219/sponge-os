# Forwarded close starvation — the last defect of the non-blocking-session chain

**Artifact type:** runtime measurement (instrumented QEMU/base-sel4 boots)
**Scenario:** `run/sponge-desktop-disk.run` (product desktop-disk media)
**Fork commits:** `ea04b06868` → `73e5859da8` (defect), `74c84f9f0a` (fix)
**Ledger row:** #30 (`docs/11-environment.md` §4)

## 1. The question

The non-blocking session machinery of ledger row #28 lets a container
forward a child's session operation (create / quota upgrade / close) to its
own parent **without parking the container's single entrypoint**. The
container then owns a deadline for the operation and is supposed to complete
it when the parent answers.

For a **close**, the completion never happened: the container asked the
parent to close the session, the parent forwarded the request to the
provider, the provider answered — and the container never learned about it.
The session state stayed alive, the parent-side ID stayed reserved, and the
client that had asked for the close waited for a signal that never came.

The product symptom: `run/sponge-desktop-disk` fails at the alpha-probe
(`sponge_pkgd did not answer install hello`), the desktop never boots.

## 2. The measurement (corrected)

The instrumented boot counts `Child::_close` against
`Child::session_response(CLOSED)`, per component. The prior reading of this
table mixed the two sandboxes: the "answered" column was grouped by
*provider* and the "unanswered" column by *client*, so the two halves came
from different components. Corrected (raw counters, reproduced with the
markers listed in §3 — `Child::_close`, `Child::session_response`,
`Child::session_closed`, `Try_parent_service::probe_at_parent` and
`Child::_resolve_pending_op`):

| component | closes | answered | unanswered |
|---|---|---|---|
| root init (`[init]`) | 29 | 29 | 0 |
| system sandbox (`[init -> system]`) | 33 | 4 | **29** |
| **total** | **62** | **33** | **29** |

Root init, 29 closes → 29 answers (nitpicker/Gui 18, rom_sys/ROM 5,
report_rom/ROM 2, timer/Timer 2, platform/Dma 1, ahci/Block 1); the 29
clients are console 14, system 12, ahci/part_block/pci_decode 1 each.

System sandbox, 33 closes → 4 answers (all report_rom/ROM, i.e. the
container's *local* provider) → **29 unanswered**; the 33 clients are
layouter 11, wm 10, decorator 6, sponge_themed 2, cfg_probe 1,
driver_runtime 1, pkg_runtime 1, sponge_pkgd 1.

So: **62 closes, 33 answered, 29 unanswered — and every unanswered close is
a close the system sandbox forwarded to the root init.** The container's
*local* closes (served by its own report_rom child) are answered normally
(4 of 4 in this boot); the 29 forwarded ones are not answered at all.

## 3. Discriminating the three candidate links

### C3 — the publish to the provider: **works**

The root init's close reaches the provider and the provider answers:

```
[init] DBGX close_entry child=system id=88
[init] DBGX _close child=system service=ROM id=88 phase=5 id_at_parent=0
[init] DBGX register_pending op=2 child=system id=88
[init] DBGX session_response(CLOSED) child=rom_sys service=ROM id=13 phase=7
[init] DBGX session_closed child=system service=ROM id=88 sigh_valid=1
```

`Child::close` → the Routed_service publishes the close into the provider's
`session_requests` ROM → the provider closes the session → the answer
arrives as `session_response(CLOSED)`. All 33 of the root init's closes are
answered this way.

### C1 — the session signal at the root init: **delivered, but nobody waits**

The last line above shows `sigh_valid=1`: the root init *does* submit the
container's session signal when it destroys the closed session
(`Child::session_closed` → `Signal_transmitter(_session_sigh).submit()`).

The signal is delivered to the container's `Component_env::Blockade`
receiver — and the container's entrypoint never parks (that is the whole
point of row #28), so **no thread waits for it**. The answer reaches the
container and is dropped.

### C2 — the probe: **broken, and this is the defect**

The container's completion channel is the probe
(`Try_parent_service::probe_at_parent` → `_resolve_pending_op`). The
instrumented probe shows it never even asks the parent:

```
[init -> system] DBGX probe_short name=ROM id=88 phase=7 state=2
[init -> system] DBGX resolve op=2 child=sponge_themed id=8 phase=7 state=2
```

`phase=7` is `CLOSE_REQUESTED` — the phase the container puts the session in
*when it forwards the close*. `probe_at_parent` classified the session by its
**local** phase and answered `Pending` for that phase, so the probe never
asked the parent anything and the pending operation could only end in the
5000 ms deadline ("close of ROM (session 8) failed - the provider did not
answer within 5000 ms").

**Verdict: C2.** The publish works, the signal works, the probe is the
broken link — and it is broken *recursively*: the container waits for the
parent's answer, the parent's answer is only observable through the probe,
and the probe never asks.

## 4. What the leak actually cost (cap arithmetic, before → after)

The pinned session state of the container holds, per unfinished close:

* the container's **client-side session ID** (the ID the *client* uses to
  address the session),
* the container's **session state** (RAM) and the **capability to the
  provider's session**,
* the **donated cap quota** of the session (transferred to the parent and
  never given back to the client).

The measurable damage is **not** a provider cap wall. The provider's caps
*were* released: the root init closed every session at the provider and the
providers answered (C3). The "out of CAP" line in the boot
(`flush page table entries - mapping cache full - PD: init -> rom_lib`) is
**pre-existing and unrelated** — it appears exactly once in the defective
boot, exactly once in the fixed boot, and exactly once in the passing
2026-10-18 baseline (`genode/build/x86_64/var/run/sponge-desktop-disk-uefi/uefi-boot.log`,
`alpha-probe: PASS`).

The real damage is the **ID conflict**, and it is fatal:

```
[init -> system] Error: sponge_pkgd requested conflicting session ID 16
    (service=ROM args=..., label="sponge_pkgd -> bake_manifest", ...)
[init -> system] Error: existing session: ... cid=16 ...
    label="sponge_pkgd -> pkg_index.xml" state=CLOSING
```

`pkgd` closes a ROM session and immediately requests the next one. Its own
ID space hands out the same ID again — but the container still holds the
old session state (state `CLOSING`, i.e. the close that never completed),
so the container **denies** the new request. The denial is fatal in Genode
(`stop because parent denied ROM-session`), so `sponge_pkgd` dies, the
desktop loses its package feed, and the alpha-probe times out.

The chain is therefore:

```
forwarded close never completes
  -> the container keeps the session state (CLOSING) and its client ID
  -> the client reuses that ID for its next request
  -> the container denies the new request ("conflicting session ID")
  -> the requesting component dies ("parent denied ROM-session")
  -> the containment restarts the component, which repeats the pattern
```

and the *parked* clients add the second half: the client that asked for the
close blocks in `Env::close` until the container submits the signal that
never comes, so its entrypoint is stuck, its heartbeat stops, and the
containment treats it as a wedge.

Before → after (same scenario, same media):

| quantity | defective (`73e5859da8`) | fixed (`74c84f9f0a`) |
|---|---|---|
| forwarded closes answered by the container | 0 of 29 | all of them (80 `Gone` resolutions in the longer, now-booting run) |
| "close of … failed - the provider did not answer" (healthy providers) | 2 (the boot aborts before the remaining 27 reach the deadline) | **0** |
| "RAM upgrade … failed - the provider did not answer" (healthy providers) | 3 | **0** |
| "requested conflicting session ID" | 1 (fatal) | **0** |
| "requested session cap for unknown ID" | 0 | **0** (the answer is now an error) |
| "out of CAP" (pre-existing, unrelated) | 1 | 1 |
| alpha-probe | `FAIL sponge_pkgd did not answer install hello` | **`PASS`** |

## 5. The fix (`74c84f9f0a`)

1. **`Try_parent_service::probe_at_parent`**
   (`genode/repos/base/include/base/service.h`): a session whose operation
   is in flight is no longer classified by the *local* phase. The parent is
   asked, and its answer is unambiguous: a valid capability = the operation
   went through, an invalid capability without an error = the parent is still
   busy with it, an error = the parent closed the session.

2. **`Child::session_cap`** (`genode/repos/base/src/lib/base/child.cc`): the
   parent's side of that answer. A session whose close is in flight is *not
   ready* (an invalid capability, like the quota-upgrade case) — and its
   phase is left alone, because the phase is what makes the parent publish
   the close request to the provider; reporting the capability as "handed
   out" would cancel the close before the provider sees it. A session the
   parent does not know anymore is answered with an **error**, which is
   what the base protocol defines an error answer of `session_cap` to mean
   ("in the error case, the parent implicitly closes the session").

3. **`Genode::Child::expire_pending_ops`**
   (`genode/repos/base/src/lib/base/child.cc`): the pending operations are
   resolved on *every* tick (the tick rate is the granularity of the
   completion), and the sandbox drives the tick from its own 50 ms timer
   (`genode/repos/os/src/lib/sandbox/library.cc`,
   `genode/repos/os/src/lib/sandbox/heartbeat.h`) instead of the heartbeat
   period. With the heartbeat period (2 s in the product topology) the
   client that asked for the close waited for *seconds* — the alpha-probe's
   install step needs seven closes in twelve seconds.

4. **Pending-operation lifetime**
   (`genode/repos/base/include/base/child.h`,
   `genode/repos/base/src/lib/base/child.cc`): a pending operation must not
   outlive its session state. The registry refers to the session by
   pointer, and the teardown of a child discards the client-side ID of a
   session whose close is still pending — so the two places that clear a
   pending operation on the provider's answer now look it up by the session
   instead of by the client-side ID. This removes a use-after-free (the
   operation pointed at a released session state) and the six spurious
   "close of … failed" warnings of the teardown path.

The fork's invariants are kept: the container never parks its entrypoint, a
provider that never answers still fails loudly at the 5000 ms deadline, and
the leak stays bounded.

## 6. Verification

| gate | result |
|---|---|
| `run/sponge-desktop-disk` | **PASS** (`alpha-probe: PASS`), 0 close failures, 0 upgrade failures, 0 conflicting IDs, 0 unknown-ID warnings |
| `run/sponge-upgrade-wedge` | **PASS** — container verdict `ALIVE`, the never-answered upgrade still fails loudly at 5000 ms ("RAM upgrade of Test_service (session 6) failed - the provider did not answer within 5000 ms") and the stuck provider's close fails the same way (as in `docs/evidence/sponge-upgrade-wedge-green.log`) |
| `run/sponge-supervisor-takeover` | **PASS** ("wedge detected, subtree killed, console owned the display, desktop restored") |
| `run/sponge-pkg-driver` | **PASS** |
| `run/sponge-desktop-disk-uefi-usb` | **PASS** ("UEFI USB-stick media boots on OVMF and the usb_block storage chain serves /system from the stick") |
| `./tool/patches verify` | **OK** (all ledger rows match git reality) |

The single remaining "out of CAP" line is the pre-existing rom_lib
page-table mapping-cache warning described in §4 — present in the defective
boot, in the fixed boot and in the passing 2026-10-18 baseline.
