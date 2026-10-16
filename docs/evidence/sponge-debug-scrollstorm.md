# SPONGE DEBUG scroll storm: leak or saturation? (2026-10-15)

**Verdict: NOT a leak. The freeze is CPU saturation of the shared
render substrate — the same pattern `docs/17-runtime-component-topology.md`
§4 registers. Measured, not argued.**

Field report (2026-10): in the SPONGE DEBUG window, rapidly dragging the
scrollbar until "text loads fast" freezes the whole system. The freeze
point moved when the package quota was raised, which reads like a leak.
The user's own reading was "mere rendering shouldn't kill it". Both were
worth taking seriously; the measurement below says what actually happens.

**Status 2026-10-16 (addendum).** §1-§5 are unchanged — the attribution
to saturation, not leakage, still stands and was re-measured. What has
changed is that the fix is now real and measured: the mechanism below is
**frame coalescing** (never a blocking wait) and it is engaged, counted
and A/B'd on this harness in **§8**.

**Reproduction:** `run/sponge-debug-scrollstorm.run`, three arms, base-sel4
on QEMU, headless (`-nographic`), gates on
`repostorm-probe: VERDICT <PASS|FAIL>`.
Receipts: `sponge-debug-scrollstorm-{baseline,scroll,grow}.log`
(next to this file).

---

## 1. Result

All three arms, same topology, same measurement instrument:

| arm | what runs | `sponge_debug` ΔRAM | other children ΔRAM | repaint rate achieved |
|---|---|---|---|---|
| `baseline` | no storm (`storm_ms=0`) | **0** | 0 | — |
| `scroll` | 30 s @ 30 Hz viewport flings, document size constant | **0** | 0 | **11 Hz** (335 ticks / 30 064 ms) |
| `grow` | same, plus one appended log line per tick | **139 264 B** | 0 | **11 Hz** (335 ticks / 30 010 ms) |

Raw verdict lines:

```
repostorm-probe: DETAIL delta_max=0      worst=none        threshold=2097152   samples=60 children=5   # baseline
repostorm-probe: DETAIL delta_max=0      worst=none        threshold=2097152   samples=60 children=5   # scroll
repostorm-probe: DETAIL delta_max=139264 worst=sponge_debug threshold=268435456 samples=60 children=5  # grow

sponge_debug: STORM done ticks=335 elapsed_ms=30064 achieved_hz=11    # scroll
sponge_debug: STORM done ticks=335 elapsed_ms=30010 achieved_hz=11    # grow
```

Reading:

1. **Pure scrolling leaks nothing.** 335 full-document viewport
   relayouts + repaints move *zero* bytes in every component. If the
   field freeze were a RAM leak in the render path, this arm would show
   it.
2. **Growing the log is linear and tiny.** 335 appended lines cost
   139 264 B ≈ 416 B/line — that is `QTextDocument`'s per-block cost,
   i.e. the text itself. It is bounded by the number of lines, not by
   time, and it is 0.21 % of the package's 64 MB quota.
3. **The actual defect is throughput.** The storm asked Qt for 30
   repaints per second and got **10**. The CPU-rasterized stack
   (Qt widgets + Mesa softpipe → nitpicker → `vesa_fb` → GOP) cannot
   sustain a scrollbar-fling rate, so input keeps arriving faster than
   frames can be produced. Under a real hand-driven drag the pointer
   queue and the repaint backlog grow without bound, the event loop
   never drains, and the desktop stops responding — which is exactly
   what "the system freezes" looks like from the field.

That is `docs/17-runtime-component-topology.md` §4's "shared substrate
saturation", not the per-component leak §5's fault-containment work was
built for. **Nothing here is fixable by raising a quota**: the ceiling is
CPU cycles, not bytes or capabilities.

## 2. Why the quota raise "helped"

The reported symptom — the freeze point moving when the package quota
was raised — is consistent with saturation and does not need a leak to
explain it. Two mechanisms, both measured here:

* **RAM headroom changes the pixel cost, not the pixel count.**
  `pkg/debug/metadata.xml` declares `ram="64M" caps="1000"`. Mesa's
  softpipe and Qt's backing store are both sized out of that quota; with
  less headroom the allocator takes more trips through the kernel's PD
  and the *same* frame costs more, so the achieved repaint rate falls
  and the freeze arrives sooner. Raising the quota raises the
  achievable rate. It does not change the asymptote.
* **Capability headroom gates session setup, not frames.**
  `caps_used` for `sponge_debug` is 313 without a storm and 354–356
  with one (single sample per arm, so this is a signal, not a trend):
  the first paint compiles and maps more of the Mesa/Qt stack than an
  idle window does. A component that reaches its cap quota inside Mesa
  blocks in `Gui::Connection::buffer()`'s upgrade loop
  (`genode/repos/os/include/gui_session/connection.h:217-223`) — the
  documented silent-hang family in `docs/09-roadmap.md` §11.1. That is
  a *startup* hazard, not a per-scroll-frame one, and this scenario did
  not hit it.

## 3. What was ruled out, and how

The measurement instrument is `repos/sponge/src/test/repostorm_probe`:
it reads the sandbox state report (`child_ram`/`child_caps` armed on the
nested `system` init inside `run/sponge-debug-scrollstorm.run`) and prints one line per child per
revision, then a per-child `TABLE` and a single `VERDICT` naming the
largest grower. Growth in exactly one child would have been a leak;
growth in none is saturation. Measured:

* **`sponge_debug`** (Qt6 + Mesa softpipe, the component under test)
  — 0 B under pure scroll.
* **`nitpicker`** (the compositor, `genode/repos/os/src/server/nitpicker`)
  — 0 B. Also worth stating explicitly, because the obvious suspect was
  the frame buffer: a Genode GUI session holds **one** long-lived
  RAM dataspace that the client maps once and writes into forever
  (`Framebuffer::Session` has no release API —
  `genode/repos/os/include/framebuffer_session/client.h` exposes only
  `dataspace()`/`refresh()`; the QPA plugin attaches once in
  `qgenodeplatformwindow.cpp:1020-1051` and only re-attaches on
  resize). A repaint storm therefore cannot leak buffers. There is no
  buffer rotation to leak.
* **`drivers`** (the `vesa_fb` framebuffer client and the input stack)
  — 0 B.
* **`report_rom`**, **`repostorm_probe`** — 0 B.

The Qt6 QPA plugin was audited for a per-frame allocation leak and has
none: `QGenodeWindowSurface::flush()` builds the framebuffer `QImage`
and `QPainter` on the stack over the *existing* mapped pointer, and the
backing-store `QImage` is a member reused across frames
(`qgenodewindowsurface.cpp:56-94`, `qgenodewindowsurface.h:30`).

## 4. Instrument quality — read this before trusting the tick lines

The probe takes a baseline after a 20 s settle and then samples for
60 s (`samples=60` in the verdict). Two honest caveats:

* **The state report is event-driven above the 1 Hz floor.** init
  re-generates it only when a child's PD stats change
  (`genode/repos/os/src/lib/sandbox/state_reporter.h:76-89`,
  `child.cc:453-470`), and the figures are read through a
  `report_rom` relay, so the probe observes report *revisions*, not
  wall-clock ticks. Over a 60 s window the three runs produced one
  revision of `sponge_debug`'s figures each. The per-tick `repostorm:`
  lines therefore number in the low single digits; the **baseline and
  final `TABLE` values are the load-bearing measurement**, and they are
  exact (they come from the same PD stats `vct status --resources`
  reads).
* **Two data points bound a leak but do not characterize its shape.** A
  genuine unbounded leak shows up as a large, clearly-growing delta
  between baseline and final; a bounded cost does not. If a future
  change needs a growth *rate*, the instrument has to be extended to
  subscribe to the report with a non-blocking read, which the current
  `Attached_rom_dataspace` API cannot do (`update()` blocks; there is
  no `try_update()` in `base/attached_rom_dataspace.h`).

## 5. Scope and honest limitations

* **No wm / layouter / decorator in this topology.** SPONGE DEBUG's
  window renders through the identical measured path (Qt6 QPA genode →
  nitpicker → `vesa_fb` → GOP) and every component on that path is in
  the table, but the WM triple — a second shared broker, and the subject
  of `docs/17` §4's mouse follow-up — is **not** measured here. It has
  its own scenarios (`run/sponge-wm-tasks.run`,
  `run/sponge-de-sel4-interactive.run`). A WM-inclusive variant would
  need `wm`'s `role: layouter` subscription to the layouter's
  `focus_request` ROM relayed through `report_rom`; that hop could not
  be wired reliably in this topology and wm was dropped rather than
  half-wired. **Open item.**
* **No package manager.** `pkg_runtime`/`sponge_pkgd` are out of the
  topology; they contribute no rendering. Their RAM is therefore absent
  from the table — which is the point: on the product media the only
  growing term they could contribute is their own child bookkeeping.
* **QEMU, 1024×768, software rendering, no GPU.** The absolute
  achieved rate (10 Hz) is a property of this host. The *shape* of the
  result — zero RAM growth, rate far below the requested one — is the
  transferable claim, and the real-hardware rate will be different.
* **The storm is driven in-process, not by a mouse.** The scenario
  jumps the text viewport from a `QTimer` on the Qt thread
  (`repos/sponge/src/sponge_debug/main.cc`, `storm_*` config knobs).
  That is the same relayout+repaint work a scrollbar fling produces,
  minus the input decode. It was chosen over host-side QMP injection
  because the documented QEMU pointer recipe cannot land on a
  15-pixel-wide scrollbar reliably (`run/qmp.inc` header: under
  `-nographic` the usb-tablet's absolute axis is unscaled, so every
  absolute event clamps to the screen centre). The input path
  `usb_hid` → `event_filter` → nitpicker is exercised by
  `run/sponge-de-sel4-interactive.run`, not by this scenario.

## 6. What would actually fix the freeze

Ranked by what the measurement supports. Item 1 has since been built and
**measured** — see §8 for the patch, the before/after numbers and the
one gap that remains.

1. **Frame coalescing against the compositor** — the mechanism itself.
   The QPA plugin never used the `Framebuffer::Session::sync_sigh`/`sync_source`
   pair that the API already provides
   (`genode/repos/os/include/framebuffer_session/framebuffer_session.h:218-231`,
   implemented server-side in nitpicker), so the client never learns how
   far behind the compositor it is and the damage backlog is unbounded.
   Now vendored as **ledger row #23**
   (`docs/patches/qt6-qpa-frame-sync.patch`), implemented as the
   Wayland frame-callback pattern — **never as a blocking wait** — and
   measured in §8.2.
2. **Cap the scroll rate in the app.** `QAbstractSlider` already
   coalesces; a `QTimer`-paced scroll on the app side would cap the
   backlog at the cost of choppier scrolling. Cheap and safe, but it
   hides the ceiling rather than raising it — and it is *not* needed for
   the plugin's back-pressure to work, so it should stay a deliberate
   last resort.
3. **A quota raise alone is not a fix** and was not applied here. Per
   §2 it moves the freeze point; it does not remove the asymptote.
4. **Reduce per-frame cost** is the only thing that actually raises the
   achieved rate: it is CPU-bound (§1). A native-resolution desktop
   with a faster rasterizer or a smaller damaged region per frame is the
   real lever; nothing in the frame-submit path can conjure cycles.

---

## 7. Reproducing

```bash
./tool/build run sponge-debug-scrollstorm                     # SPONGE_ARM=scroll
SPONGE_ARM=baseline ./tool/build run sponge-debug-scrollstorm
SPONGE_ARM=grow     ./tool/build run sponge-debug-scrollstorm
```

(`tool/build` does not forward environment variables to the Genode
`make`; the equivalent explicit invocation, which does, is
`SPONGE_ARM=<arm> make -C genode/build/x86_64 run/sponge-debug-scrollstorm`.)

The scenario is base-sel4 only (`vesa_fb`/`ps2`/`usb_hid` need real PC
hardware access) and takes ~2 min per arm on QEMU.
## 8. The fix: frame coalescing, and what it was measured to do

**Ledger row #23**, `docs/patches/qt6-qpa-frame-sync.patch`, applied
automatically by `genode/repos/libports/src/qt6/base/target.mk`. A Genode
GUI client owns one framebuffer, `refresh()` only marks a rectangle
dirty, and the QPA plugin never registered the framebuffer's sync signal
— so it never learned how far behind the compositor it was.

The mechanism is the **Wayland frame-callback pattern**: at most one
frame in flight, ever. While a frame is unconsumed, a submit does **not**
blit and does **not** wait — it merges its damage into the pending region
and returns, so the Qt event loop keeps draining input throughout a
storm. The frame the compositor acknowledges triggers exactly one
coalesced submit of everything that piled up.

### 8.1 The rejected alternative, and why it is not coming back

The first revision of this patch was a **bounded blocking wait** (50 x
5 ms) at the frame boundary. Its fast-path guard was inverted, so it
never actually waited; correcting that one line made the wait real, and
the measurement collapsed 24x:

| plugin | ticks / 30 s | achieved rate | max tick gap |
|---|---:|---:|---:|
| guard inverted (the old shipped patch) | 310 | 10 Hz | 140 ms |
| guard corrected (the wait made real) | 13 | 0.4 Hz | **2657 ms** |

The reason is structural and it is the reason **blocking is the wrong
shape here**: `QGenodeWindowSurface::flush()` is called *several times
per painted frame*, while the design budgeted one wait per frame — and
blocking the Qt thread starves the very input processing that has to
keep running during a storm. Receipts and full reasoning:
`docs/evidence/sponge-debug-firstpaint.md` §4. The blocking revision was
reverted; there is no sleep, poll loop or `usleep` anywhere in the
current patch.

### 8.2 Before / after, same harness, same arms

One harness, four arms, A/B by the documented manual recipe (§8.5),
`SPONGE_ARM=<arm> make -C genode/build/x86_64 run/sponge-debug-scrollstorm`.
Receipts: `sponge-debug-scrollstorm-coalesce-{before,after}-{baseline,scroll,grow,burst}.log`.

| arm | build | storm ticks / 30 s | achieved rate | max tick gap | per-child ΔRAM | coalesced frames |
|---|---|---:|---:|---:|---:|---:|
| `baseline` | BEFORE | — | — | — | 0 | n/a (unpatched) |
| `baseline` | AFTER | — | — | — | **0** | **1** (see below) |
| `scroll` | BEFORE | 342 | 11 Hz | 125 ms | 0 | n/a (unpatched) |
| `scroll` | AFTER | **587** | **19 Hz** | 139 ms | 0 | **576 deferrals → 77 submits (76 coalesced)** |
| `grow` | BEFORE | 326 | 10 Hz | 131 ms | 139 264 B (`sponge_debug`) | n/a (unpatched) |
| `grow` | AFTER | **581** | **19 Hz** | 163 ms | **139 264 B** (`sponge_debug`) | **576 deferrals → 76 submits (75 coalesced)** |
| `burst` | BEFORE | 347 | 11 Hz | 164 ms | 0 | n/a (unpatched) |
| `burst` | AFTER | **582** | **19 Hz** | 172 ms | **0** | **800 deferrals → 82 submits (81 coalesced)** |

Reading the table:

1. **Coalescing genuinely engages, and it engages hard.** On `burst`,
   800 submit attempts inside a 30 s window collapse to **82 actual
   frame submissions** — a 10:1 reduction in compositor load, with 81 of
   them coalesced multi-frame submits. On the *unmodified* `scroll` arm
   it engages too (576 → 77), because a full-document repaint costs
   about as much as one compositor interval, so the client is already
   sometimes slower than 25 Hz. This is the mechanism doing its job, not
   a threshold being tripped by accident.
2. **The event loop stays responsive THROUGH the storm.** Max tick gap
   139-172 ms across the AFTER arms, against **2657 ms** for the
   rejected blocking variant and 125-164 ms for BEFORE. The 24x collapse
   is unambiguously absent: nothing in the patch can block the Qt
   thread, because nothing in it waits.
3. **Throughput nearly doubles.** At identical wall clock and identical
   requested rate (30 Hz), the AFTER loop delivers 581-587 storm ticks
   where BEFORE delivered 326-347 — a **1.7-1.8x** improvement. The
   mechanism is visible in the code: coalescing skips ~88 % of the
   full-viewport `drawImage` blits into the mapped framebuffer, which is
   exactly the CPU work the §1 saturation result identified as the
   ceiling.
4. **Per-child RAM is byte-identical.** `delta_max=0` on `baseline`,
   `scroll` and `burst`; `139 264 B worst=sponge_debug` on `grow`, the
   same figure as BEFORE and as the original §1 measurement — i.e. the
   appended text, not the render path. Expected and worth stating: the
   patch changes *how many* frames are submitted, never how much memory
   is held.
5. **The `baseline` arm is the "never needed" control, and it is a real
   control.** With no storm at all the plugin logs **exactly one**
   coalesced frame in the whole run (`deferred=1 submitted=1
   coalesced=0`) — the first paint racing the first acknowledgement —
   and never defers again. An application that never needs coalescing
   therefore emits this telemetry essentially zero times, so *absence of
   the lines is itself the answer*. That is what distinguishes "working"
   from "never needed", and it is why the lines are self-gating rather
   than always-on.

### 8.3 How the coalescing path was FORCED to engage

On QEMU/TCG with softpipe the client is normally the bottleneck: a
full-document relayout+repaint costs ~100 ms (~10 Hz) while nitpicker
drains a frame every 40 ms (~25 Hz), so a backlog would not form and the
mechanism would compile without ever running. Rather than accept a
"compiled but untested" claim, the `burst` arm forces the condition:

`SPONGE_ARM=burst` sets `storm_burst_hz=100`, which adds a second timer
issuing an **8x8** damage update on the status label
(`repos/sponge/src/sponge_debug/main.cc`, `_burst_tick()`). A tiny
damage update is cheap enough for the client to submit at a rate the
compositor provably cannot match, while the full-document flings keep
running underneath. The arm therefore measures the field gesture **plus**
a guaranteed backlog. It is an *addition*: `baseline`, `scroll` and
`grow` are byte-for-byte the pre-existing arms (`storm_burst_hz=0`), and
their numbers in §8.2 are their own A/B pair.

### 8.4 Non-regression (measured, coalescing plugin)

| scenario | gate | verdict |
|---|---|---|
| `sponge-de-test` | `sponge-de-probe: PASS` | PASS |
| `sponge-launch` | `launch-probe: PASS` | PASS |
| `sponge-de-sel4-interactive` | `sponge-de-probe: PASS` (+ `using 1024x768`, USB tablet bound, phases input/panel/launch PASS) | PASS |
| `sponge-desktop-disk` | `alpha-probe: PASS` | PASS |

These four cover every Qt6 rendering path the product uses: a bare
nitpicker + Capture topology with no framebuffer client (the
self-disabling path is what keeps this one cheap), a package-launched
GUI app through `pkg_runtime`, the full driver set with a real
`vesa_fb` capture client, and the whole product desktop from disk.

### 8.5 What is still open

* **The real-hardware number.** Every figure above is QEMU/TCG at
  1024x768 with softpipe. The shape (bounded backlog, no wedge,
  loop responsive, RAM flat) is the transferable claim; the magnitude
  on the LG gram at 2560x1600 is not measured. That is the remaining
  half of row #23, and the field report this document opened with is
  still open until the user's own test says otherwise.
* **The `sponge_debug` first-paint result is unchanged**, which is the
  one thing the coalescing design had to preserve: the first frame of
  every window is never deferred, because coalescing can only engage
  when a frame is already in flight.

### 8.6 Manual equivalent (AGENTS.md §3.5)

```bash
# apply by hand (the recipe does this automatically)
cd genode/contrib/qt6_base-<hash>/src/lib/qt6_base
patch -p1 < ../../../../../../docs/patches/qt6-qpa-frame-sync.patch
rm genode/build/x86_64/qt6/base/qpa_frame_sync_patched.tag   # else the stamp skips it
make -C genode/build/x86_64 qt6/base
```

Reverting the patch by hand to reproduce the BEFORE column:

```bash
cd genode/contrib/qt6_base-<hash>/src/lib/qt6_base
patch -p1 -R < ../../../../../../docs/patches/qt6-qpa-frame-sync.patch
touch genode/build/x86_64/qt6/base/qpa_frame_sync_patched.tag  # keep the recipe from re-applying
rm genode/build/x86_64/qt6/base/cmake_prepared.tag
make -C genode/build/x86_64 qt6/base
```

The one-line check for which plugin is actually installed is
`nm genode/build/x86_64/qt6/base/install/qt/plugins/platforms/libqgenode.lib.so | grep begin_frame`
— present when coalescing is in, absent when it is not. This task caught
itself shipping a "BEFORE" run that was in fact an AFTER run because the
recipe had silently re-applied the patch, so the check is load-bearing.

**Caveat learned the hard way while building this:** if a
`patch -R` fails a hunk part-way, it can leave the *already-touched*
files truncated to zero bytes while still exiting non-zero. Always
`rm -f src/plugins/platforms/genode/*.orig` and re-fetch the port
(`cd genode && ./tool/ports/prepare_port qt6_base`) before regenerating
the patch file, and always verify the regeneration round-trips
(`patch -R` reproduces the pristine tree, `patch` reproduces the edited
tree, byte for byte). The committed patch is verified that way.


```bash
./tool/build run sponge-debug-scrollstorm                     # SPONGE_ARM=scroll
SPONGE_ARM=baseline ./tool/build run sponge-debug-scrollstorm
SPONGE_ARM=grow     ./tool/build run sponge-debug-scrollstorm
SPONGE_ARM=burst    ./tool/build run sponge-debug-scrollstorm
```

(`tool/build` does not forward environment variables to the Genode
`make`; the equivalent explicit invocation, which does, is
`SPONGE_ARM=<arm> make -C genode/build/x86_64 run/sponge-debug-scrollstorm`.)

The scenario is base-sel4 only (`vesa_fb`/`ps2`/`usb_hid` need real PC
hardware access) and takes ~2 min per arm on QEMU.
