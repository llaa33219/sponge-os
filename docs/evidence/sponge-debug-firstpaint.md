# SPONGE DEBUG first paint vs the heartbeat budget (2026-10-16)

**Question:** on the LG gram 17ZD90N (2560x1600, `boot_fb`/GOP) the
current img shows the SPONGE DEBUG window "appearing but never finishing
rendering, then dies", with **no error text**; the previous img rendered
it fine. Which of the three changes in the img can produce that?

**Verdict, measured, not argued:**

| hypothesis | verdict | evidence |
|---|---|---|
| **H2** the heartbeat kills a slow-but-healthy first paint | **falsified** | the client reaches the frame boundary in ~1 s against a 6 s budget; 1 spawn; 0 skipped heartbeats; identical with the heartbeat removed |
| **H1** the Qt6 QPA frame-pacing patch corrupts or stalls the first frame | **falsified as shipped** — and correcting its inverted guard is what would cause this exact symptom | A/B at 2560x1600: patched 1300 ms vs reverted 990 ms (noise); with the guard corrected the repaint throughput collapses 24x (§4) |
| **H3** the remainder clamp starves the client | **not supported** | the client's PD has tens of MiB of `avail` throughout; nothing is clamped or denied on the render path |
| **H4** the QEMU `uefi-usb` storage stall is unrelated flakiness | **confirmed** | the same scenario PASSes the storage gate on retry; the crash-respawn cascade in the failing log is a *consequence* of that stall (§5) |

**The field regression is therefore NOT reproduced in QEMU, and none of
the three candidate mechanisms reproduces it.** §5 records the one
real defect the failing log does contain and what is still open.

---

## 1. The instrument: `run/sponge-debug-firstpaint.run`

Two things were missing and both had to be built before any hypothesis
could be tested:

1. **A measurement of the client's own first paint.** `sponge_debug`
   now reports its own progress on the serial console
   (`repos/sponge/src/sponge_debug/main.cc`, `FirstPaintProbe`):

   ```
   sponge_debug: construct ms=115
   sponge_debug: paint-begin ms=329
   sponge_debug: FIRSTPAINT ms=1300
   ```

   `paint-begin` is the first `QEvent::Paint` delivered to the window;
   `FIRSTPAINT` is a queued zero-delay callback, so it runs after that
   paint has **returned** — i.e. after the frame has been handed to the
   QPA plugin. The clock starts in the constructor, so every number is
   "milliseconds since this component's own entrypoint", which is the
   quantity the heartbeat budget is stated in.

   The field report said the client dies silently; a client that cannot
   say where it stopped is exactly what this tool exists to rule out, so
   the probe is unconditional.

   Each line is one `log()` call and deliberately short. The serial
   console interleaves with the sandbox state report, and a line long
   enough to be split across two writes comes out truncated mid-number —
   measured, twice, before the format was shortened. (One early run
   reported a first paint of "11 ms" purely because the gate matched and
   the run tool powered the guest off mid-line.)

2. **A topology that reaches the field resolution.** The scenario boots
   the real render chain (`sponge_debug` -> Qt6 QPA -> `nitpicker` ->
   `vesa_fb` -> GOP) with the production heartbeat
   (`<heartbeat rate_ms="2000"/>` + `<heartbeat restart_after_skipped="3"/>`)
   at `SPONGE_FB_W` x `SPONGE_FB_H`, default **2560x1600** — the LG
   gram's panel resolution. The host-side gate asserts three things on
   the captured serial log, so a failure names what went wrong:

   * the framebuffer really came up at the requested resolution
     (`[init -> drivers -> fb] using 2560x1600`);
   * the client constructed **exactly once** (each respawn constructs
     again, so a count above one *is* a containment kill);
   * the largest `skipped_heartbeats` value the sandbox ever reported
     for the client is printed (init writes that counter per child,
     `genode/repos/os/src/lib/sandbox/child.cc:389-390`, and the
     Tier-0 `report_rom` relays `system -> state` with `verbose: yes` so
     it is visible without patching anything).

   `SPONGE_HEARTBEAT=off` is the toggle arm. The heartbeat is the
   containment feature and is **not** disabled anywhere in the product;
   it is removed only inside this test scenario.

   Two deviations from the product shape, stated rather than hidden:
   the package is a direct child of `system` instead of a child of a
   nested `pkg_runtime` (same generated signal, same 2 s period, same
   3-skipped budget — the extra layer only adds a Gui/ROM route hop that
   init's concurrent child start makes racy), and the display driver is
   `vesa_fb` at the requested mode instead of `boot_fb` (only the
   producer of the pixels differs; the whole client path under test is
   identical, and no OVMF/disk/USB-storage chain is needed to reach it).

## 2. H2 — the heartbeat does not kill the first paint

Same file, same binary, one knob. Receipts:
`firstpaint-2560x1600-patch-shipped.log`,
`firstpaint-2560x1600-heartbeat-off.log`.

| arm | time to first paint | client spawns | max skipped heartbeats |
|---|---:|---:|---:|
| `SPONGE_HEARTBEAT=on` (production) | **1300 ms** | 1 | 0 |
| `SPONGE_HEARTBEAT=off` (containment removed) | **1322 ms** | 1 | 0 |

The containment arm finishes in ~1.3 s against a **6 s** budget, never
misses a single 2 s heartbeat, and is never respawned. Removing the
containment changes nothing (1322 ms is inside the run-to-run spread:
1157, 1300, 1322 ms across the shipped-patch runs).

QEMU/TCG is several times slower than the LG gram's native CPU, so
1.3 s here is an upper bound for the real machine on this path. **H2 is
falsified**: the heartbeat is not killing a slow-but-healthy first
paint, and it is not killing anything else here.

## 3. H1 — the QPA frame-pacing patch, A/B

A/B per the documented manual recipe (`docs/evidence/sponge-debug-
scrollstorm.md` §8.6), same topology, same resolution. Receipts:
`firstpaint-2560x1600-patch-shipped.log`,
`firstpaint-2560x1600-patch-reverted.log`.

| arm | time to first paint | client spawns | max skipped heartbeats |
|---|---:|---:|---:|
| patch applied (shipped) | 1300 ms | 1 | 0 |
| patch reverted by hand | 990 ms | 1 | 0 |

Indistinguishable — a single 2560x1600 first paint involves one frame,
and the patch's wait is per frame boundary. The code says why
independently of the measurement: **`wait_for_frame()` as shipped never
waits** (§4). So it cannot stall or corrupt a first frame, and H1 is
falsified **as shipped**.

## 4. The patch was inert, and what replacing it cost

`docs/patches/qt6-qpa-frame-sync.patch` (ledger row #23) originally
added a bounded back-pressure **wait** at each frame boundary. Its
fast-path guard was inverted:

```cpp
void QGenodePlatformWindow::wait_for_frame()
{
        if (!_previous_frame_consumed())     /* <-- inverted */
                return;
        for (unsigned i = 0; i < SYNC_WAIT_STEPS; i++) { ... usleep ... }
}
```

`_previous_frame_consumed()` is true exactly when there is **nothing to
wait for**. With the negation the function returns immediately in the
one case the wait exists for, and only reaches the (non-sleeping)
polling loop in the other. The wait never slept.

That is consistent with everything on record for row #23: the wait itself
had never been observed to fire. **The mechanism row #23 claimed to
deliver was not being delivered.**

Correcting the one-line guard and rebuilding the plugin
(`patch -p1`, remove `genode/build/x86_64/qt6/base/qpa_frame_sync_
patched.tag` and `cmake_prepared.tag`, `make -C genode/build/x86_64
qt6/base`) was measured on the repaint-storm harness, same scenario,
same arm. Receipts: `scrollstorm-guard-inverted-shipped.log`,
`scrollstorm-guard-corrected-run1.log`, `scrollstorm-guard-corrected-run2.log`.

| plugin | ticks in 30 s | achieved rate | max tick gap |
|---|---:|---:|---:|
| shipped (guard inverted) | 310 | **10 Hz** | 140 ms |
| guard corrected | 13 | **0.4 Hz** | 2657 ms |
| guard corrected (2nd run) | 13 | **0.4 Hz** | 2654 ms |

**A 24x throughput collapse**, reproducible. The reason is structural:
`wait_for_frame()` is called from `QGenodeWindowSurface::flush()`, which
Qt's raster path invokes once per backing-store flush — several times
per painted frame — and each call can sleep up to the 250 ms ceiling.
The design assumes one wait per frame; the call site is per flush. And
blocking the Qt thread starves the very input processing that has to keep
running during a storm.

Two conclusions were load-bearing for the diagnosis, and both still hold
for the code that replaced it:

* the then-shipped patch was inert and was **not** the cause of the
  field regression — it could not be, it did nothing;
* **"fixing the inverted guard" would have produced exactly the field
  symptom** the user was reporting. That one-character change was never a
  cleanup, and it is why H1 was falsified *as shipped* rather than
  dismissed.

### 4.1 What replaced it (2026-10-16): coalescing, not waiting

The blocking revision was reverted and row #23 was **re-implemented as
frame coalescing** — the Wayland frame-callback pattern. There is no
`sleep`, no poll loop and no `usleep` anywhere in the current patch:
while a frame is unconsumed, a submit does not blit and does not wait,
it merges its damage into the pending region and returns, and the
acknowledged frame triggers exactly one coalesced submit. The liveness
signal is an event, not a wait.

Measured on this very harness, `SPONGE_ARM` A/B
(`docs/evidence/sponge-debug-scrollstorm.md` §8.2 for the full table):

| arm | build | storm ticks / 30 s | max tick gap | coalesced frames |
|---|---|---:|---:|---|
| `scroll` | BEFORE (no patch) | 342 | 125 ms | n/a |
| `scroll` | AFTER (coalescing) | **587** | 139 ms | 576 deferrals → 77 submits |
| `burst` | BEFORE (no patch) | 347 | 164 ms | n/a |
| `burst` | AFTER (coalescing) | **582** | 172 ms | 800 deferrals → 82 submits |

The 24x collapse is gone (max gap 139-172 ms against 2657 ms) and the
mechanism that row #23 promised now measurably fires. The `baseline` arm
logs exactly **one** coalesced frame in a whole run — the control that
shows the telemetry distinguishes "worked" from "never needed", and that
the first paint is never deferred.

**The H1 verdict is unchanged in its conclusion and now has a stronger
basis:** the QPA patch is not the field regression. First paint is
never deferred by construction (coalescing can only engage when a frame
is already in flight), and the whole run keeps up with a 2 s heartbeat
(`sponge-de-test`, `sponge-launch`, `sponge-de-sel4-interactive`,
`sponge-desktop-disk` all PASS with the coalescing plugin — §8.4 of the
scrollstorm document).

## 5. The `uefi-usb` storage stall and the crash-respawn cascade

The first `run/sponge-desktop-disk-uefi-usb` attempt failed: the
`usb_block` storage chain never reached `configd` inside 300 s, and the
same serial log carries a cascade worth recording.

**H4 confirmed — the stall is a flake, not a regression.** On retry the
scenario PASSes all three structural gates plus the storage-chain gate
(receipt: the run's own `PASS — UEFI USB-stick media boots on OVMF and
the usb_block storage chain serves /system from the stick`). OVMF's GOP
hands that run **2560x1600** — the same resolution as the LG gram, which
is why it is the right QEMU proxy for this report.

**The cascade in the failing log is a consequence of the stall, and it
is worth a separate fix.** In order:

```
[init -> system] Warning: pkg_runtime: configured RAM exceeds available RAM, proceed with 3805597276
[init -> system -> cfg_probe] cfg_probe: config size=0
[init -> system -> report_rom] Error: Uncaught exception of type
                                     'Genode::Attached_dataspace::Region_conflict'
[init -> system -> report_rom] Warning: abort called - thread: ep
[init -> system -> sponge-de]   Warning: Libc RNG not configured
[init -> system] Warning: pkg_runtime: ... proceed with 3805526038     <- 71 KB less
[init -> system] Warning: sponge-de:   ... proceed with 133850220
... repeats indefinitely, interleaved with seL4
    "Attempted to invoke a null cap" warnings
```

`report_rom` is the broker for `config`, `installed`, `launcher_result`,
`state` and `shape` inside `system`. Once it aborts:

* every child whose ROM comes from it loses its config source;
* `pkg_runtime` stops servicing its entrypoint, the heartbeat abandons
  it after ~6 s and respawns it;
* the respawned `pkg_runtime` asks the **dead** `report_rom` for its
  config, and dies immediately — so the cycle is unbounded;
* each cycle leaks: the `remainder clamp` value shrinks by ~35 KB per
  iteration (3805597276 -> 3805526038 -> 3805394676 -> ...), because
  `ram: 512G` makes `init` grant the child's whole remaining pool and
  the destroyed child does not give all of it back;
* `sponge-de` is caught in the same loop and is re-clamped to whatever
  is left (133 MB).

Two defects are visible here and neither is caused by the heartbeat or
by the clamp:

1. **Unbounded respawn loop.** `restart_after_skipped="3"` has no
   attempt budget and no backoff, so a child that cannot start is
   respawned forever. Containment is supposed to bound a fault, not to
   amplify one.
2. **Per-respawn RAM leak** in the clamped grant, ~35 KB per cycle here,
   which on a small box turns the loop into a machine-wide starvation.

Both were reproduced only under the storage stall and neither reproduces
on a healthy boot (`sponge-desktop-disk-uefi-usb` PASSes with zero
`Region_conflict` and zero respawns). They are therefore **not** the
field regression — the field machine boots its own storage — but they
are real, they are in the img, and a slow or flaky USB stick on real
hardware is enough to enter them. They are registered as open items in
`docs/17-runtime-component-topology.md` §5 rather than fixed here: a
respawn budget is an init-semantics change and a leak fix is a quota-
accounting change, and both deserve their own red→green evidence
rather than being folded into a first-paint investigation.

The `report_rom` abort itself was not root-caused. The candidate in
`genode/repos/os/include/report_rom/rom_module.h` —
`write_content()` re-allocating a module's backing store via
`Constructible::construct()` whenever a writer submits a larger report —
is *not* a leak: `Genode::Constructible` derives from `Reconstructible`,
whose `construct()` calls `destruct()` first
(`genode/repos/base/include/util/reconstructible.h`), so the old
attachment is torn down. `Region_conflict` therefore comes from
somewhere else in `report_rom`'s attach path and needs its own
hypothesis-driven run, with the growth forced deterministically.

## 6. What this does and does not establish

Established:

* at the field resolution the client reaches the frame boundary in
  ~1.3 s against a 6 s containment budget, never misses a heartbeat and
  is never respawned, with or without containment;
* the QPA patch is excluded as the field cause. As originally shipped it
  was inert; making its wait real is measured to be a 24x throughput
  regression and was reverted. It has since been **replaced by frame
  coalescing** (§4.1), which contains no wait at all, keeps first paint
  un-deferred by construction, and is measured engaging on the storm
  harness with no liveness regression;
* the remainder clamp leaves the client with tens of MiB of headroom —
  nothing on the render path is clamped or denied;
* the QEMU `uefi-usb` storage stall is a flake; the crash-respawn
  cascade and the per-respawn leak in that log are real but are
  consequences of the stall.

**Not established:** the field regression itself. It is not reproduced
in QEMU at 2560x1600 with or without any of the three changes. The next
step is a field-side capture, not another QEMU run: `sponge_debug` now
prints `construct ms=`, `paint-begin ms=` and `FIRSTPAINT ms=` on the
real console, so the next boot on the LG gram will say directly whether
the window dies before its first paint, during it, or after it — and
whether `construct` appears more than once, which would prove a
containment kill on the real topology instead of inferring one.

If the field console shows exactly one `construct` and no `FIRSTPAINT`,
the client is not being killed by containment and the failure is inside
the paint itself; if `construct` repeats, the heartbeat is killing it on
the real topology for a reason the QEMU topology does not reproduce
(slow ROM reads from `rom_sys`/`rom_pkg` are the obvious candidate, and
the `+N ms` numbers separate "slow to start" from "slow to paint").

## 7. Reproducing

```bash
# the measurement the field report needs (default 2560x1600, production heartbeat)
./tool/build run sponge-debug-firstpaint
SPONGE_HEARTBEAT=off make -C genode/build/x86_64 run/sponge-debug-firstpaint

# a different resolution
SPONGE_FB_W=1920 SPONGE_FB_H=1080 make -C genode/build/x86_64 run/sponge-debug-firstpaint

# the QPA A/B (the manual recipe; AGENTS.md 3.5)
#   revert  : patch -p1 -R, touch qpa_frame_sync_patched.tag, rm cmake_prepared.tag,
#             make -C genode/build/x86_64 qt6/base
#   re-apply: patch -p1,     rm qpa_frame_sync_patched.tag,
#             make -C genode/build/x86_64 qt6/base
# verify with:
#   nm genode/build/x86_64/qt6/base/install/qt/plugins/platforms/libqgenode.lib.so \
#       | grep begin_frame

# the repaint-storm harness the guard correction was measured on
SPONGE_ARM=scroll make -C genode/build/x86_64 run/sponge-debug-scrollstorm
```

base-sel4 only (`vesa_fb`/`ps2`/`usb_hid` need real PC hardware
access), ~2-4 min per arm on QEMU.