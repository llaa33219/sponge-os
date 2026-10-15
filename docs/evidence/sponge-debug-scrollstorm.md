# SPONGE DEBUG scroll storm: leak or saturation? (2026-10-15)

**Verdict: NOT a leak. The freeze is CPU saturation of the shared
render substrate — the same pattern `docs/17-runtime-component-topology.md`
§4 registers. Measured, not argued.**

Field report (2026-10): in the SPONGE DEBUG window, rapidly dragging the
scrollbar until "text loads fast" freezes the whole system. The freeze
point moved when the package quota was raised, which reads like a leak.
The user's own reading was "mere rendering shouldn't kill it". Both were
worth taking seriously; the measurement below says what actually happens.

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

Not done here, and deliberately so — each needs its own measurement:

1. **Back-pressure on repaints.** The QPA plugin never uses the
   `Framebuffer::Session::sync_sigh`/`sync_source` pair that the API
   already provides (`framebuffer_session.h:218-231`, implemented
   server-side in nitpicker) so the client never waits for the
   compositor. Registering them would make the client stall on the
   server instead of queueing frames. That is a **vendored change** and
   `docs/11-environment.md` §4 row 17 closes the vendored budget for
   Phase 16 — it needs an explicit Phase-17+ ledger row and certainty
   first. Not attempted.
2. **Cap the scroll rate in the app.** `QAbstractSlider` already
   coalesces; a `QTimer`-paced scroll on the app side would cap the
   backlog at the cost of choppier scrolling. Cheap and safe, but it
   hides the ceiling rather than raising it.
3. **A quota raise alone is not a fix** and was not applied here. Per
   §2 it moves the freeze point; it does not remove the asymptote.

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