# Containment churn: bounding the heartbeat respawn loop

**Date:** 2026-10-18
**Scenario:** `run/sponge-containment-churn.run` (new)
**Components:** `init` (fork patch, ledger row #24), `test/churn_probe`
(new), `test/ep_wedge` (`liveness_ms` mode, new), `test/takeover_probe`
(`repeat_ms` mode, new), `sponge_pkgd` (bootstrap entrypoint fix)
**Field report:** LG gram 17ZD90N, UEFI USB, 2560x1600 — boot → desktop
paints (mouse, background, panel) → `⚠ sponge_pkgd unresponsive(1)` →
**total freeze**: mouse dead, no console takeover, no recovery.

Design authority: `docs/17-runtime-component-topology.md` §5 ("Respawn
budget and clamp leak"), `docs/evidence/top-level-supervision.md`,
`docs/evidence/logdisk-field-diagnosis.md` §3(a), AGENTS.md §5.2.

---

## 1. The report, and what was already known

The §5 row already recorded the *shape* of this failure class:

> `<heartbeat restart_after_skipped="3"/>` has no attempt budget and no
> backoff, so a child that cannot start is respawned forever […] a
> healthy boot of the same scenario shows zero of it.

But the row's own evidence was a **flake**: "Reproduced only under a
`usb_block` storage stall in `run/sponge-desktop-disk-uefi-usb`". A row
whose reproduction is a storage stall is a row with no test, and it also
meant the two candidate mechanisms — an unbounded loop, and a per-cycle
RAM leak — were never separated from each other.

This run does three things the row could not: it makes the loop
deterministic, it measures what the loop costs, and it separates the two
candidate mechanisms instead of assuming they are the same failure.

## 2. Topology

```
init  (TOP-LEVEL)
+ timer, report_rom, platform/acpi/pci_decode
+ nitpicker + fb (vesa_fb)      the composited display chain
+ churn_probe                    Tier-0 observer (NEW)
+ takeover_probe                 pixel oracle, now able to re-announce
+ system (init)                  THE SANDBOX UNDER TEST
  + desktop                     sponge_console: a real Gui client
                                INSIDE the churning container
  + critical                    ep_wedge: wedges its entrypoint on
                                EVERY incarnation
  + bystander                   ep_wedge liveness_ms (NEW): emits a
                                line forever
```

`<heartbeat rate_ms="500"/>` on the sandbox, `<heartbeat
restart_after_skipped="3" restart_budget="N"/>` on `critical`.

Four independent oracles, because a single one cannot distinguish "the
system survived" from "the observer stopped":

| Oracle | Where | Claim it supports |
|---|---|---|
| `bystander` | **inside** the sandbox | the container itself kept answering |
| `churn_probe` | outside, Tier-0 | the container's own RAM budget, and that Tier-0 stayed responsive |
| `takeover_probe` | outside, Tier-0 | the compositor chain still owned and painted the display |
| `restart budget exhausted` | init | the loop **stopped**, rather than merely being slow |

`churn_probe` is the measuring instrument. Its subject is the sandbox's
**own** remaining RAM, read from the top-level `<ram avail=/>` node that
`init_ram="yes"` puts in the state report
(`genode/repos/os/src/lib/sandbox/library.cc:134-137`). That choice is
the point: a leak in the abandon/respawn path debits the **container**
without ever appearing as a growing child, so per-child accounting cannot
see it.

## 3. The RED arm — 48 respawns, no bound

`SPONGE_CHURN_BUDGET=0` (the `restart_budget` attribute omitted, i.e.
upstream's shipped behaviour), base-sel4 on QEMU:

```
[init -> system -> critical]  ep_wedge: alive — entrypoint wedging now     (x48)
[init -> churn_probe] churn-probe: DETAIL label=sandbox samples=40 cycles=0
                            total_avail_delta=-37292 tail_spread=10240
                            init_avail=74289296 init_used=1024000
Error: Test execution timed out          <- the budget marker never appeared
```

Cadence, recovered from the bystander's timestamped lines interleaved
with the respawn receipts:

```
first 26 respawn timestamps (ms): 2000 4500 6500 8500 10500 13000 15000
                                   17000 19000 21500 23500 26000 28500 …
median interval = 2500 ms
```

**2.5 s per cycle**, forever, with heartbeat 500 ms and
`restart_after_skipped` 3 — ~24 cycles/minute, ~1 440/hour. Each cycle
destroys and rebuilds a PD, its ROM sessions and its `Sliced_heap`
session slabs, so the cost is CPU in `init` *and* in the kernel while
the child stays permanently unusable. It also never converges, which is
what makes it fatal for supervision: an outer supervisor that waits for
the system to settle before escalating waits forever.

## 4. What the loop actually costs — two candidates, separated

§5 named two mechanisms. They are **not** the same thing, and only one
of them reproduces here.

**The unbounded loop: reproduced, deterministically.** §3.

**The per-cycle RAM leak (the `~35 KB/cycle` field signature): NOT
reproduced in a healthy storage chain.** The container's `avail`
oscillates by exactly ±10 240 B per cycle and returns to baseline; over
the 40-tick RED window the total movement was **−37 292 B across 48
respawn cycles**, i.e. ~780 B/cycle of drift, and `tail_spread` 10 240 B
— one allocation quantum. `init` reports PD statistics in 10 KiB units,
so a healthy abandon/create pair balances.

That is a real finding, and it narrows the §5 row rather than repeating
it: the field's `pkg_runtime: configured RAM exceeds available RAM,
proceed with N` with **N decreasing** cannot be produced by the restart
path alone on a healthy chain. It needs the condition the field had and
this topology does not — an abandoned child whose environment sessions
cannot complete their close handshake, because the server on the other
end (`rom_sys`/`cached_fs_rom` over a stalled `usb_block`) never answers.
`Sandbox::Library::_destroy_abandoned_children()` only destroys a child
once `env_sessions_closed()` is true
(`genode/repos/os/src/lib/sandbox/library.cc:302-321`); a child that can
never close keeps a whole `Child` object — and its `_heap` allocations
and `Sliced_heap` slabs — alive, and `ready_to_create_child` then builds
a fresh one on the next alarm.

So the honest statement is two-part, and both halves matter:

1. the unbounded loop is reproducible anywhere and is the cascade risk;
2. the per-cycle *leak* is conditional on a stalled storage chain and is
   **not** needed for the freeze — a 2.5 s loop that never converges is
   sufficient, and it is what this fix removes.

Bounded loops cannot exhaust anything, which is why the budget is the
fix that matters regardless of which half the field hit.

## 5. The fix

**`init` — `restart_budget` (fork patch `f44bc47ee5`, ledger row #24).**

Upstream's heartbeat restart (`0b36aa771d`, genodelabs/genode#5718) has
neither a budget nor a backoff. The patch adds `restart_budget` to the
`<heartbeat>` node:

- the count lives on the config model's `<start>` node, because the
  `Child` is destroyed and rebuilt on every restart and only that object
  outlives an incarnation;
- a child that **answers a heartbeat again** resets the count, so the
  budget bounds a run of failures, not a lifetime. (The first
  implementation reset on `skipped == 0`, which is also true of every
  *fresh* incarnation — the budget never accumulated and the loop ran
  unchanged. Caught by the scenario, not by reading the code.)
- on exhaustion the child is torn down through the existing restart path
  but **not** recreated; the config model keeps an empty `<start>` node
  and init logs:

  ```
  Warning: critical: heartbeat restart budget (4) exhausted after 4
           restarts - child left stopped, escalation is up to whoever
           owns the config
  ```

- only a new configuration generation — which requires the config owner
  to act — brings it back. That is precisely the hand-off the product's
  `sponge_supervisor` needs, and it is why the child is left *stopped*
  rather than merely slowed.
- **`restart_budget` 0 means unlimited**: the attribute is opt-in per
  `<start>` node and upstream behaviour is unchanged wherever it is
  absent. The product media sets it to **3** on every contained child
  (`run/sponge-desktop-disk{,-nvme,-uefi,-uefi-nvme,-uefi-usb}.run`,
  `run/driver_runtime.inc`) — three restarts outlast a transient (a
  stalled storage read, a slow first paint) and are short enough that the
  supervisor reaches its console phase while the user is still looking.

**`sponge_pkgd` — the entrypoint no longer waits on storage.**

`docs/evidence/logdisk-field-diagnosis.md` §3(a) records the rule for a
LOG provider: *a provider must not issue its `File_system` request on the
thread that serves its own traffic*. `pkgd` repeated exactly that
defect, and `Component::construct()` runs on `env.ep()`'s dispatch
thread, so all three storage calls blocked the entrypoint the sandbox
heartbeat watches:

| call | what it blocks on |
|---|---|
| `_init_store()` → `_vfs_env.construct` | the `File_system` session (vfs_var mounting ext2fs on the boot device) |
| `_load_store()` | `vfs.stat()` / `open` / `read` |
| `_seed_roots_from_bake()` → `_save_store()` | `open` / `write` / `sync` / `commit_and_wait()` |

The fix splits `Main`'s constructor: it does only work that provably
cannot touch storage (reading pkgd's **own** inline `<config>`, which
`init` serves without a filesystem), then starts a dedicated
`Bootstrap` thread and returns. `_bootstrap()` runs the store mount,
the store load, the bake seeding, the manifests, the first config
generation and the pending-request drain on that thread. `env.ep()`
stays free to answer for the whole of it — and keeps dispatching the
storage chain's completion signals, because only the *blocking calls*
moved.

Hand-over ordering is load-bearing and is commented at the call site:
the pending request is drained **before** the primary `sigh()` is armed,
so there is no window in which both threads could run
`_handle_request()` against the same unsynchronised `_roots` /
`_installed` / `_last_request_sig`. The existing de-duplication replay
made that race *usually* harmless — which is precisely why it must not
be left to chance. If the thread cannot be created at all, the code
falls back to the old single-threaded path rather than skipping the
bootstrap.

## 6. RED → GREEN

Same image, same topology, same gates; the only difference is whether
`restart_budget` is present.

| | RED (`SPONGE_CHURN_BUDGET=0`) | GREEN |
|---|---|---|
| `critical` respawns | **48** in the window, still climbing | **4** (5 lines counting the first) |
| budget marker | never appears → timeout FAIL | appears, names child + budget |
| container `avail` tail spread | 10 240 B (1 quantum) | **0** over 8 tail samples |
| `bystander` after the stop | n/a (never stops) | emits past t=20 s |
| display after the stop | n/a | `takeover_probe: repeat state=desktop` |

GREEN run, verbatim:

```
[init -> system]            Warning: critical: heartbeat restart budget (4)
                            exhausted after 4 restarts - child left stopped,
                            escalation is up to whoever owns the config
[init -> churn_probe]       churn-probe: DETAIL label=sandbox samples=40
                            cycles=0 total_avail_delta=8356788 tail_spread=0
                            tail_ticks=8 init_avail=82673136
[init -> churn_probe]       churn-probe: VERDICT PASS

sponge-containment-churn: PASS - wedged critical child contained: respawn
loop bounded at 4, container RAM settled, bystander answering after the
budget was spent, display held the desktop
Run script execution successful.
```

`total_avail_delta` is **positive** 8.36 MB on purpose: the last wedged
incarnation was left stopped, so its 8 MiB came back to the container.
That is the shape a bounded loop should have.

## 7. What the supervisor can and cannot survive

The field report says the supervisor "never got to act". Measured, on
this topology, with a loop churning a sibling every 2.5 s:

- **`churn_probe` sampled continuously for the whole window.** A
  Tier-0 component with its own `Timer`, reading the sandbox's report
  through the **top-level** relay, is untouched by anything the sandbox
  does to itself.
- **`takeover_probe` kept repainting and kept re-announcing the desktop
  colour** across the churn and after it. The composited display chain —
  `nitpicker` → `fb` → the pixel oracle — is entirely Tier-0 (§2 of the
  topology document) and does not degrade.
- The **in-sandbox** `bystander` kept emitting throughout.

So a containment loop inside `system` does **not** starve Tier-0, and the
supervisor's timer chain is not the thing that failed. What the field
machine actually lacked was *an end to the loop*: with the child
permanently wedged and no budget, every supervisor poll re-read a system
that was still churning, so its grace window never elapsed into a
"contained" verdict and the console phase was structurally unreachable —
not because the supervisor was starved, but because it was correctly
refusing to declare a fault it had just seen fixed four times. The
budget is what gives it something to observe.

What remains genuinely outside a userspace supervisor's reach, unchanged
from `docs/17` §5 and restated here so it is not mistaken for something
this fix addresses: a **kernel-level** freeze (seL4 scheduling or mapping
saturation) starves every user thread including the supervisor's, and
the supervisor cannot observe or act in that state. This work bounds
userspace churn; it does not make a saturated kernel recoverable.

## 8. Regression receipts

| Scenario | Kernel | Result |
|---|---|---|
| `sponge-containment-churn` | sel4 | **PASS** |
| `sponge-containment-churn` (RED arm) | sel4 | **FAIL as designed** — 48 respawns, timeout |
| `sponge-pkg-lifecycle` | sel4 | **PASS** |
| `sponge-desktop-disk` | sel4 | **PASS** |
| `sponge-desktop-disk-uefi-usb` | sel4 | **PASS** |
| `sponge-pkg-driver` | sel4 | **PASS** |
| `sponge-supervisor-takeover` | sel4 | **PASS** |
| `sponge-minimal` | linux | **PASS** |

Logs: `churn-regression-*.log` in this directory — one per row. The
RED-arm receipts are in §3/§6.

Verifying the receipts turned up a second pre-existing defect, fixed
here because it is the same failure class and it sits in the very
scenario whose log was being read: **`sponge-desktop-disk-uefi-usb`
declared a `vct_tty` child it never built.** `docs/17` §1 says that
variant is the one that carries `vct_tty` (and is sized for it at 280000
caps against the other four's 263000), the scenario has the `<start
name="vct_tty">` node and a staging guard for `bin/vct_tty` — but the
guard is `if {[file exists "bin/$b"]}` and `vct_tty` was in no variant's
`$build_components`, so the copy was skipped **silently** and every boot
carried

```
[init -> rom_sys] Error: /bin/vct_tty not found
[init -> system] Error: vct_tty: environment ROM session denied (label="vct_tty", …)
```

A product child that has never once started, invisible to the scenario's
gates because none of them assert it. Under the OLD unbounded restart
that child was also a permanent churn source — one of the cycles this
whole document is about. Adding `vct_tty` to `$build_components` fixes
it; the re-run logs `vct_tty: ready (tty_request channel)`, zero "not
found", and all four structural gates still PASS. **The lesson worth
more than the fix:** a staging guard that silently skips a missing
binary turns a build-list omission into a runtime mystery. The guard
should be the other way round for anything a `<start>` node names.

### What deliberately did NOT get a budget

- `run/sponge-alpha.run` — the flat ISO reference topology. It has **no
  `<heartbeat>` restart nodes at all** (only comments explaining that
  fault containment does not apply there), so there is nothing to bound.
- `run/sponge-heartbeat-restart.run` — the dedicated diagnostic harness
  for the restart mechanism, and it is blocked by the pre-existing
  flat-ISO HPET issue (`docs/17` §5), so it does not run. Its subject
  IS the restart behaviour; adding a budget would change what it
  measures. Noted here so the omission reads as a decision rather than
  an oversight.
- `run/supervisor.inc`'s control arm — it mirrors the product's
  `restart_after_skipped="3"` on purpose so the control asserts the same
  containment the product has. The arm asserts "no fault injected", so a
  budget can never fire there.

### One regression run failed, and it is reported rather than re-run away

The **first** `sponge-desktop-disk` run after the changes failed, and not
on any assertion of theirs:

```
[init -> system] Warning: pkg_runtime: configured RAM exceeds available RAM, proceed with 4253772732
[init -> system -> sponge_configd] sponge_configd: applied 5 baked default(s)
[init -> system] Error: Uncaught exception of type 'Genode::Attached_dataspace::Region_conflict'
[init -> system] Warning: abort called - thread: ep
```

The **sandbox init's own entrypoint** aborted on a dataspace attachment.
`sponge_pkgd` had not logged a single line at that point, so this is not
the pkgd change.

It is the same *class* `docs/evidence/sponge-debug-firstpaint.md` §5
records — `Attached_dataspace::Region_conflict` aborting a broker
mid-boot — but at a **different site**: there the victim was
`report_rom` under a `usb_block` stall; here it is `init` itself during
its own start-up, on plain AHCI. The candidate the vendored tree offers
for an `init`-side attach failure is its own `Heap` running out of
virtual address range (`core_local_rm.cc:50-53` returns
`REGION_CONFLICT` when the VA allocation fails), i.e. the same
per-PD mapping pressure ledger rows #6/#7/#14/#15 exist for — and
`sponge-minimal.run` does not build `sponge_pkgd` at all, so the pkgd
change cannot be the variable.

Discriminated rather than assumed: **`sponge-desktop-disk` was re-run
twice on the identical tree and PASSed both times** (`exit=0`). So this
is a pre-existing flake on the product topology, not a regression from
this work — and it is registered here rather than quietly dropped,
because a broker that can abort on an attach is exactly the fragility
`docs/17` §3 identifies and this task is about. It is **not** fixed.

One incident in three runs of the same tree is also a reminder that
"all regressions green" on this topology is a weaker claim than it
looks; the honest statement is three of four runs green, with the fourth
failing on a defect that predates it.

`sponge-minimal` needs `BOARD=linux` alongside `KERNEL=linux`; run
with the build directory's default `BOARD ?= pc` it takes
`boot_dir/linux`'s disk-image path and dies in `build_initrd` before
the guest starts. An invocation error, not a failure.

## 9. Defects found while building this

Recorded because each is a class of mistake that only this scenario's
shape catches.

| # | Defect | Caught by |
|---|---|---|
| 1 | The budget counter reset on `skipped_heartbeats == 0`, which is also true of every *fresh* incarnation — the budget never accumulated and the loop was unchanged | the scenario's GREEN run failed the "bounded" gate |
| 2 | A nested `init` requests ROM label `config`; routing it to `<parent>` unrenamed hands the sandbox the **top-level** config module, and the sandbox then tries to start `nitpicker`, `fb` and `platform` inside itself | the boot log's `platform: configured caps exceed available caps` |
| 3 | HID attribute syntax (`key: value`) written into the sandbox's **XML** config makes the whole document unparsable — the sandbox config vanishes silently rather than failing loudly | the same boot log |
| 4 | A `|` inside a `#` comment in a HID block is parsed as an attribute separator (`Error: stray '|' at line 128`) | the run tool's schema check |
| 5 | Genode's log server emits one `write()` per format argument, so a gate regex ending in `.*` can match mid-line and swallow the token the assertion needs (`churn-probe: VERDICT ` with no PASS/FAIL) | the scenario's own leak assertion |
| 6 | Gates must be ordered by **emission** order, not by assertion order: the budget is spent at ~10 s and the probe's verdict lands at ~30 s, so gating the verdict first swallows the budget line and then waits 120 s for a second one | two 120 s timeouts |
| 7 | `Genode::Thread`'s constructor does not start the thread; `start()` is separate and its `Start_result` must be checked | `pkg-lifecycle` regressed to "install did not answer" |
| 8 | `sponge-desktop-disk-uefi-usb` declares a `vct_tty` child that no build list produces, and the staging guard skips it silently | reading that scenario's own regression receipt |
| 9 | `sponge-minimal` on base-linux needs `BOARD=linux` as well as `KERNEL=linux`; with the build directory's default `BOARD ?= pc` it takes `boot_dir/linux`'s disk-image path and dies in `build_initrd` before the guest starts | the first regression sweep reported it as a failure |

## 10. Follow-up, stated rather than implied

- The **stalled-storage-chain leak** (§4) is not reproduced and not
  fixed. The candidate is named and located
  (`Library::_destroy_abandoned_children()` waiting forever on
  `env_sessions_closed()`). Reproducing it needs a wedged ROM server on
  the child-closing path — the `usb_block` stall the §5 row cites — not
  a wedged child, and no deterministic trigger for it exists yet. A
  bounded loop makes it non-fatal rather than fixing it.
- The **`~35 KB/cycle` field signature remains unconfirmed** on this
  topology. The next discriminating measurement is on the real machine,
  with the budget in place: if `proceed with N` still decreases with N
  now that the loop is bounded, the leak is real and independent; if it
  is flat, it was the loop.
- `sponge_pkgd`'s **request-path** `_save_store()` is still synchronous
  on `env.ep()`. The boot-time path — the field trigger — is fixed. A
  user-initiated install on a wedged disk can still block the ep; the
  budget now bounds what that costs instead of it being unbounded.