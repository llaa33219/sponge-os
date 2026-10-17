# The SPONGE DEBUG scroll freeze at the panel resolution (2026-10-17)

**Field report:** on the LG gram 17ZD90N (2560x1600, `boot_fb`/GOP), a
fast scrollbar drag on the SPONGE DEBUG window makes the panel health
label read `⚠ sponge_pkgd unresponsive(N)`, and immediately after that
the **whole system freezes — including the mouse**.

**Question:** is the compositor still the bottleneck, and can the
freeze be reproduced in QEMU?

**Answer, measured:** the compositor's per-frame cost is *not* it. The
saturator this scenario found is **the seL4 kernel's page-table flush
path**, reached through a **capability quota too small for the
panel-resolution buffer a component maps** — `flush page table entries -
mapping cache full`, hundreds of times per minute under a repaint storm.
A kernel-level counter, not a proxy, tracks it: **567 flushes / 3 Hz /
3672 ms stall → 0 flushes / 19 Hz / 387 ms.**

**And the honest headline:** with the quotas the **product media**
already ships (`fb` 24000, `nitpicker` 20000), the panel-resolution
configuration is **healthy** — 19 Hz with a 387 ms worst event-loop gap
and zero kernel flushes. **The freeze is therefore NOT reproduced in QEMU
on a correctly-sized configuration**, and this document does not claim it
is. What *is* reproduced is a real, 6x-throughput, kernel-level
saturation of the exact class that would produce the field symptom, in
the one place in this repository where it was still present: the
measurement harness itself. §8 states what the field must check.

---

## 1. The instrument: field-shape knobs on the existing harness

No new topology was written. `run/sponge-debug-scrollstorm.run` already
boots the real render chain (`sponge_debug` → Qt6 QPA → `nitpicker` →
`vesa_fb` → GOP); what it could not do was reach the **field's two
defining parameters** — the panel resolution and the size of the window
being dragged. Everything below is opt-in, and every default leaves the
four pre-existing arms bit-identical:

| knob | default | what it buys |
|---|---|---|
| `SPONGE_FB_W/H` | stock recipe (1024x768) | the LG gram's 2560x1600 panel |
| `SPONGE_STORM_WIN` | the product 700x480 | the client's window geometry |
| `SPONGE_HEARTBEAT` | `off` | production containment + the per-child `skipped_heartbeats` starvation ranking |
| `SPONGE_SMP` | 1 | guest vCPU count (the LG gram is multi-core) |
| `SPONGE_FB_PERIOD_MS` | driver default 20 | the last stage's frame rate |
| `SPONGE_FB_CAPS` / `SPONGE_NITPICKER_CAPS` / `SPONGE_DRIVERS_CAPS` | the original 150/200/1500, **derived from the requested resolution** (§6) | the control door for the fix |
| `SPONGE_MIN_HZ` / `SPONGE_MAX_GAP_MS` | 0 = do not assert | the pass/fail bound |
| `SPONGE_ARM=freeze` | — | the field gesture for 60 s instead of 30 s |

`sponge_debug` gained one config knob, `storm_win_w`/`storm_win_h`: every
cost on the render path is proportional to the **damaged area**, so a
2560x1600 window on a 2560x1600 framebuffer is the only geometry that
carries the field's per-frame magnitude. Unset, the component keeps the
product geometry and behaves exactly as before.

## 2. PASS/FAIL, defined before the measurement

* A storm that never reaches its own `STORM done` telemetry line is a
  **stall** (FAIL) — the Qt event loop stopped draining.
* `SPONGE_MIN_HZ` asserts the storm's own `achieved_hz`;
  `SPONGE_MAX_GAP_MS` asserts its `max_gap_ms`, the largest interval
  between two delivered event-loop ticks.

Two measurement bugs had to be fixed before either bound could be
trusted, and both are recorded because both produced **false PASSes**:

1. **The instrument's window did not contain the load.** The probe
   settles 20 s and samples 60 s; the storm starts at 24 s and runs 60 s,
   so the storm **outlived the measurement meant to observe it** (84 s vs
   80 s). Worse, the ordering was unstable: the probe's own exit
   coincided with the storm stalling mid-budget (last tick at 57.2 s of
   60 s, then no serial output at all for the rest of the gate), so which
   of the two end-of-run markers landed **last** flipped between runs.
   Two sequential expect gates therefore cannot work — a gate consumes
   everything up to its match. Fixed by deriving the probe's window from
   the storm (`duration_ms = storm_start_ms + storm_ms + 15000`), which
   removes the race by construction; the storm's telemetry is then always
   in the buffer before the single VERDICT gate matches, and is asserted
   host-side from the captured log.
2. **A prefix match cut the line in the middle of the numbers.** The
   `STORM done` gate used to match `STORM done .*` and returned on
   `STORM done ticks=1`. QEMU's `-nographic` serial splits a long write;
   the gate now matches the full telemetry shape.

## 3. The bisection: what the collapse is, and what it is not

Six runs, one topology, QEMU/seL4 headless, **one vCPU** (QEMU's
default). `freeze` arm (60 s @ 30 Hz viewport flings + a 100 Hz
8x8-damage burst timer), heartbeat armed everywhere.

| # | framebuffer | client window | storm rate | max tick gap | kernel page-table flushes |
|---|---|---|---:|---:|---:|
| r2 | 1024x768 | 700x480 | 15 Hz | 512 ms | 170 |
| **r6** | 1024x768 | **2560x1600** | **16 Hz** | **442 ms** | 310 |
| **r4** | **2560x1600** | 700x480 | **3 Hz** | **2054 ms** | **521** |
| **r3** | **2560x1600** | **2560x1600** | **3 Hz** | **3672 ms** | **567** |
| r7 | 2560x1600 | 700x480, driver period **100 ms** (5x slower) | 3 Hz | 1418 ms | 567 |
| r5 | 2560x1600 | **no storm at all** | — | — | **25** |

Read in order, each row kills one hypothesis:

1. **Not the client's own rasterisation** (r6 vs r3). A 2560x1600 client
   window is the *heaviest* raster in the matrix, and on a 1024x768
   framebuffer it is completely healthy: 16 Hz, 442 ms. The same client
   work on a 2560x1600 framebuffer is 3 Hz with a 3672 ms stall. The cost
   therefore tracks the **framebuffer**, not the work the client does.
2. **Not the damage area** (r2 vs r4). Identical 700x480 damage, framebuffer
   4x larger in area, throughput 5x worse.
3. **Not the rate the compositor is polled at** (r7). Making the driver
   pull 5x more slowly (`period_ms` 100) leaves the rate at 3 Hz. The
   cost is not "too many frames"; it is "too expensive per access".
4. **The counter that tracks all of it** is the kernel's own:
   `Warning: flush page table entries - mapping cache full - PD: ...
   out of CAP`. 170-310 flushes in the healthy rows, **521-567 in the
   collapsed ones, and 25 at the same resolution with no storm at all**.
   A quiet 2560x1600 system does not flush; the storm makes it flush
   hundreds of times.

## 4. Mechanism, named

`base-sel4` gives every protection domain a **mapping cache** sized by the
PD's capability quota. A component that maps a 2560x1600x32 panel buffer
needs 16 MiB of mappings' worth of page-table-registry capabilities; give
it fewer than that and the cache overflows **on access**, so the kernel
falls back to `flush page table entries` — a full page-table walk plus a
TLB shootdown. Under a repaint storm that access happens once per
composited frame, hundreds of times a minute.

This is a kernel-level, system-wide cost: it is charged on whichever
thread touches the mapping, and the resulting TLB and page-table traffic
competes with **every** component. That is precisely the shape the field
report describes — `sponge_pkgd` missing a 2 s heartbeat while idle, and
`nitpicker`'s input path (the mouse, whose chain per `docs/17` §2 has zero
nodes inside `system`) stopping, from damage produced by a window
(`docs/17` §3/§4: "a dead mouse points at a saturated shared substrate").

### The starvation ranking says what it says — and what it does not

With `SPONGE_HEARTBEAT=on` the sandbox prints a per-child
`skipped_heartbeats` counter (init writes it per child,
`genode/repos/os/src/lib/sandbox/child.cc:389-390`). The ranking, at the
field resolution:

| child | quiet 2560x1600 | storm 2560x1600 |
|---|---:|---:|
| `usb_hid` | 30 | 30 |
| every other child | 0 | 0 |

Two honest findings, both of which constrain how this number may be used:

* **No component's entrypoint was starved** — not even at 3 Hz with a
  3.7 s stall. The heartbeat is dispatched by the *entrypoint*, and
  `sponge_debug`'s Qt thread is not its entrypoint, so a stalled Qt
  thread is invisible to it. The counter is a **blocked-entrypoint**
  detector, not a CPU-share meter.
* **`usb_hid`'s counter climbs on a quiet system too**, so it is
  structural, not starvation. Two components here are structurally
  incapable of answering and had to be excluded from the instrument:
  `usb_hid` (its entrypoint is bound up in the DDE-Linux USB host) and
  `repostorm_probe` itself, whose whole measurement runs inside
  `Component::construct` and therefore never returns to the event loop.
  Arming the probe's own heartbeat made init kill and respawn it — a
  measurement artefact that destroys the instrument, recorded here
  because it cost two runs.

## 5. RED → GREEN

Same scenario, same arm, same bounds; the only difference is whether the
panel-buffer mapping is given the caps a panel resolution needs. Receipts:
`panelfreeze-{red,green}-2560x1600.log` (next to this file).

```
# RED   — the original 1024x768-era quotas, via the explicit control door
SPONGE_ARM=freeze SPONGE_FB_W=2560 SPONGE_FB_H=1600 \
SPONGE_FB_CAPS=150 SPONGE_NITPICKER_CAPS=200 SPONGE_DRIVERS_CAPS=1500 \
SPONGE_HEARTBEAT=on SPONGE_MIN_HZ=15 SPONGE_MAX_GAP_MS=600 \
  make -C genode/build/x86_64 run/sponge-debug-scrollstorm
# -> storm achieved only 3 Hz (bound: >= 15) - the Qt event loop could
#    not keep up                                        [FAIL, exit 2]

# GREEN — the same command without the three *_CAPS knobs
# -> achieved_hz=19  max_gap_ms=387   flushes=0          [PASS, exit 0]
```

| | RED | GREEN |
|---|---:|---:|
| storm `achieved_hz` | **3 Hz** | **19 Hz** |
| max event-loop gap | **1379 ms** | **387 ms** |
| kernel page-table flushes | **532** | **0** |
| client frame submissions / 60 s | 43 | 161 |
| exit | 2 (FAIL) | 0 (PASS) |

6.3x throughput, and the kernel counter is **discrete, not gradual**:
567 → 0. That is what distinguishes this from the quota-raise story
`docs/evidence/sponge-debug-scrollstorm.md` §2 already debunked ("it moves
the freeze point; it does not remove the asymptote"). Here the asymptote
does not move at all — the client's frame rate is unchanged at ~2.7
submissions/s in both arms, because the Qt6 QPA frame-coalescing patch
(ledger row #23) already bounds it. What changed is that each of those
submissions stopped costing a kernel page-table flush.

## 6. The fix

`run/sponge-debug-scrollstorm.run` now **derives** the mapping-cap
quotas from the requested resolution instead of carrying hard-coded
1024x768-era values, and raises the `drivers` sub-init quota that holds
`fb`'s. Asking for the field resolution therefore produces a run that can
answer the question instead of one that re-measures a harness artefact
(`AGENTS.md` §1.1, convenience), and the three `*_CAPS` knobs remain the
explicit control door (`AGENTS.md` §1.1, control).

**On the task's "no quota bumps as a fix" rule — stated plainly, because
this change *is* a quota change.** It is not the debunked case: the
package RAM quota that was raised in the field moved a threshold on a
continuum, and this does not. A capability quota is not a performance
knob here, it is a **hard precondition**: base-sel4 sizes a PD's mapping
cache from it, and a 16 MiB mapping cannot fit in a 1000-cap cache. The
kernel says so in one line, with a counter, and that counter goes to
exactly zero — there is no intermediate regime and nothing left to tune.

## 7. Regression receipts (pre-existing arms unchanged)

Same four arms, defaults only, no env knobs. The numbers sit inside the
run-to-run spread of the receipts in
`docs/evidence/sponge-debug-scrollstorm.md` §8.2, which is the point:
the defaults were not touched.

| arm | exit | storm | burst | vs §8.2 (coalescing) |
|---|---:|---|---|---|
| `baseline` | 0 | n/a (no storm armed) | n/a | PASS |
| `burst` | 0 | 20 Hz / 190 ms | 26 Hz / 189 ms | 19 Hz / 172 ms |
| `grow` | 0 | 20 Hz / 162 ms | — | 19 Hz / 163 ms |
| `freeze` @ 2560x1600 | 0 | 19 Hz / 387 ms | 19 Hz / 393 ms | new arm |

## 8. What this does and does not establish about the field

**Established, with numbers:**

* the freeze's field signature is *not* a repaint-throughput problem in
  the compositor's logic — a correctly-sized 2560x1600 configuration
  sustains **19 Hz with a 387 ms worst gap** on a **single** QEMU vCPU,
  which is several times slower than the LG gram's CPU;
* the mechanism that *can* produce this signature is base-sel4's mapping
  cache overflowing on a panel-resolution mapping, and it is
  kernel-wide, which is what makes the mouse and an idle `pkgd` fall too;
* the **product media already sizes this correctly** — `fb | caps: 24000`
  and `nitpicker | caps: 20000` in `run/sponge-desktop-disk-uefi*.run`,
  sized by the Phase-15 work and independently confirmed by
  `run/sponge-debug-firstpaint.run` (`fb`/`nitpicker` 6000 at
  2560x1600). `sponge-de` (8000), `decorator` (6000), and the five
  *resizeable* GUI packages (1000-1100) were checked against this
  scenario: `r10` measures a client mapping a **full-panel** 16 MiB
  framebuffer with its unchanged production `caps="1000"` at
  **19 Hz / 374 ms / 0 flushes**, so a *client*'s window size is not a
  risk; `wm` and `decorator` were checked in the source and attach only
  command/ROM dataspaces, never a panel-sized pixel buffer;
* therefore the field freeze is **not reproduced here, and this document
  does not claim it is**.

**Not established — the field check, in one command.** If the field's
cause is this mechanism, the LG gram's console carries the same kernel
line, hundreds of times, during the drag and never while idle. That is a
single grep on the real console, and it is the discriminating test:

```
grep -c 'mapping cache full' <console log>      # during the drag
```

* **0 while idle and hundreds during the drag** → this mechanism. The
  remediation is the caps of whichever panel-buffer mapper in the live
  config is under-sized; `fb` and `nitpicker` are the only two, and they
  are already sized in the shipped media, so this outcome would mean the
  field image differs from the repo and the drift is the bug.
* **0 throughout** → the field freeze has a different cause, and this
  scenario has excluded, with numbers: a RAM/caps leak (delta_max 0 in
  every arm), the client's rasterisation cost, the compositor's poll
  rate, and blocked entrypoints. The next candidates in order are the
  ones already registered in `docs/17` §5 (an unbounded respawn loop and
  the per-respawn remainder-clamp leak, both reproduced in
  `docs/evidence/sponge-debug-firstpaint.md` §5), which the panel
  health label's own `unresponsive(N)` counter would corroborate.

## 9. Reproducing

```bash
# the freeze this scenario found, as the control arm
SPONGE_ARM=freeze SPONGE_FB_W=2560 SPONGE_FB_H=1600 \
SPONGE_FB_CAPS=150 SPONGE_NITPICKER_CAPS=200 SPONGE_DRIVERS_CAPS=1500 \
SPONGE_HEARTBEAT=on SPONGE_MIN_HZ=15 SPONGE_MAX_GAP_MS=600 \
  make -C genode/build/x86_64 run/sponge-debug-scrollstorm      # FAIL

# the field configuration, same command, three knobs dropped
SPONGE_ARM=freeze SPONGE_FB_W=2560 SPONGE_FB_H=1600 \
SPONGE_HEARTBEAT=on SPONGE_MIN_HZ=15 SPONGE_MAX_GAP_MS=600 \
  make -C genode/build/x86_64 run/sponge-debug-scrollstorm      # PASS

# full-screen client window (the heaviest raster in the matrix)
... SPONGE_STORM_WIN=2560x1600
```

base-sel4 only (`vesa_fb`/`ps2`/`usb_hid`), ~5-8 min per run on QEMU.
Receipts next to this file: `panelfreeze-*.log`.

## 10. Honest limitations

* **QEMU gives the guest ONE vCPU.** The LG gram is multi-core, so the
  QEMU numbers are an upper bound on per-frame cost and a lower bound on
  how much concurrency the real machine has. `-smp` exists as a knob for
  this reason; it was not needed for any conclusion here, because every
  collapsed arm was collapsed for a reason a second core cannot fix (a
  kernel page-table flush).
* **The measurement is a rate and a kernel counter**, not a CPU profile.
  The flush counter is the kernel's own, which is why it is preferred to
  any host-side sampling scheme here.
* **`usb_hid` and `repostorm_probe` carry no usable starvation signal**
  (§4). Every other child in this topology does, and none of them moved.
* **One vCPU means "no entrypoint was starved" is weaker than it looks**:
  a single-threaded hog cannot be starved of a core it already owns. The
  ranking would sharpen with `SPONGE_SMP=4`, which is the field-faithful
  setting and the first thing to run when the field console check in §8
  comes back "0 throughout".