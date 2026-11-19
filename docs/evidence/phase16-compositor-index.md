# Phase 16 compositor evidence index

> Status, 2026-11-19: **G1 and G2 verified; Tier-0 selection is
> default-native**. The native Mojo compositor passes the
> two-opaque-view/clipping/Capture slice and the G2 input battery on seL4
> QEMU. G1 rasters match upstream nitpicker byte-for-byte. G2 matches
> upstream exactly on the Event-injection stream. The full trace remains
> diagnostic, with the attach-time hover-establishment divergence
> documented in section 3. Selection is default-native under the
> 2026-11-19 user approval: unset or `SPONGE_COMPOSITOR=native` runs
> `sponge_compositor`, `SPONGE_COMPOSITOR=upstream` is the explicit
> nitpicker fallback, and invalid values fail loudly. Default-native and
> upstream-fallback runs are receipts in section 3. No WM/DE integration,
> translucent blending, physical hardware, or product integration is
> claimed, and G3-G5 remain uncompleted.

## 1. Public repositories and independence

The implementation is public at:

- <https://github.com/llaa33219/sponge-compositor>
- <https://github.com/llaa33219/sponge-wm>
- <https://github.com/llaa33219/sponge-de>

The G1/G2 component lives in `sponge-compositor`. WM and DE are not present
in the minimal scenario and are unchanged. They remain independent through
public Genode `Gui`, `Capture`, `Event`, and `Input` interfaces; no private
nitpicker header or WM/DE source is included by the compositor.

## 2. Delivered slice

- Mojo-owned scene/session/view tables, view stack, command reduction,
  clipping/damage, opaque painter, and input hit-testing (pointer position,
  button state, focus, and Event delivery to the hit view), using
  fixed-capacity `InlineArray` storage and 64-bit View IDs. Alpha input
  masks pass clicks through to lower views where the mask says so.
- Thin C++ adapters for Gui, per-session virtual Framebuffer, Capture,
  Event, Input, panorama ROM, and the `Genode::Heap` KGEN shim.
- Two real opaque view clients and a real Capture probe. The expected
  raster is test-oracle data only, not a second scene model. A real input
  probe drives the G2 battery through public sessions only.
- Default-native selection (user-approved 2026-11-19): unset or
  `SPONGE_COMPOSITOR=native` selects `sponge_compositor`, and
  `SPONGE_COMPOSITOR=upstream` selects the nitpicker fallback. Invalid
  values fail loudly.
- Golden, differential, and input scenarios:
  `run/sponge-compositor-opaque.run`, `run/sponge-compositor-diff.run`, and
  `run/sponge-compositor-input.run`.

Scene: screen 1024x768, background `#1e1e2e`; A `(100,80,400,300)` fill
`#3366cc` with a white border and absolute Front; B `(300,200,400,300)`
fill `#cc3333` with a black border and absolute Back. A wins the overlap.

G2 input scene: two overlapping top-level views on separate Gui sessions,
the top an alpha view with a rectangular input-mask hole over the opaque
bottom view, so a click inside the hole falls through to the lower session.

## 3. Verification receipts

| Gate | Result and receipt |
|---|---|
| G1 upstream opaque oracle | `g1-capture: PASS`, `checksum=0x670d6b5d9fc0ed7b`; `/tmp/opencode/g1-opaque-upstream-final.log` |
| G1 native opaque golden | `g1-capture: PASS`, same checksum and `expect_checksum` assertion; `/tmp/opencode/g1-opaque-native-damage.log` |
| G1 native vs upstream differential | `g1-diff: MATCH`, same checksum and assertion; `/tmp/opencode/g1-diff-final.log` |
| G2 input battery, upstream | all five phases and overall `g2-input: PASS`; `/tmp/opencode/g2-input-upstream-2.log` |
| G2 input battery, native | all five phases and overall `g2-input: PASS`; `/tmp/opencode/g2-input-native-7.log` |
| G2 input battery, default-native selection | all five phases, overall `g2-input: PASS`, `trace-inject` `0xc50006d9e1676755`; `/tmp/opencode/g2-input-native-default.log` |
| G2 input battery, explicit upstream fallback | all five phases, overall `g2-input: PASS`, `trace-inject` `0xc50006d9e1676755`; `/tmp/opencode/g2-input-upstream-fallback.log` |
| G2 Event-injection differential | `g2-input: trace-inject` exact match on both Tier-0s, `0xc50006d9e1676755` (including the default-native and upstream-fallback runs above) |
| G2 full-trace digest (diagnostic) | not a stability gate; upstream values vary across runs, while the exact Event-injection digest above matches |
| Mojo scene object | `nm -u build/scene.o`: empty; `/tmp/opencode/g1-scene.o` also empty |
| Mojo painter object | `nm -u build/painter.o`: empty; `/tmp/opencode/g1-painter.o` also empty |
| Mojo input object | `nm -u build/input.o`: empty |
| `./tool/build verify` | all 41 patch-ledger rows and hardware-compatibility gates pass |
| WM/DE independence anchor | `wm-probe: PASS` after setting the documented Qt DE budget to 1000 caps; `/tmp/opencode/g1-wm-anchor-caps1000.log` |
| `git diff --check` | clean |

**Full-stream divergence (explicit).** The full-trace digest is not a
stability gate, and upstream's value varies across runs. A bounded diagnostic
record comparison showed upstream emitting two extra attach-time
hover-establishment records, `abs_motion/top/511/383/-1` and
`hover_leave/top/0/0/-1`, before 536 otherwise identical records. This is
the G2 allowed attach-time hover-establishment divergence enumerated under
plan section 8.2 (the upstream differential clause). It is not a bug
silently ignored, and it is not a reason to synthesize a center hover in
native to force a match. The exact Event-injection stream (`trace-inject`)
is the gate and matches; the full digest stays diagnostic. Host clicks
reach the battery through closed-loop `absmove`/`move` navigation confirmed
against the `pointer` ROM (framebuffer and USB-tablet readiness rendezvous
first), and unscripted buttons or off-count totals fail loudly.

The emitted Mojo objects (scene, painter, input) use `--emit object
--relocation-model=pic --target-cpu=x86-64`. The baseline flag is required:
the original host-CPU build emitted AVX and faulted in `sc_scene_init`; the
baseline build contains no AVX instructions and passes the same 95-check
C-ABI harness.

Damage lifecycle is also verified: view creation, pixel rebinding,
destruction, and geometry/stacking changes all add damage. An early Capture
can no longer consume the only refresh and leave later views unpainted.

`docs/evidence/mojo-scene-probe.md` records the caller-provided 8192-byte
state block and the independent object-emission receipts.

## 4. G2 limits (explicit, not silent failures)

G2 delivers alpha/input-mask hit testing, click-driven focus transitions
(leave-before-enter), real Event injection to the hit session, and
pointer-report validation. The following stay outside G1/G2 and are not
claimed:

- `child_view`, `associate`, `view_capability`, `release_view_id`,
  title/background policy, blit/panning, mode/sync notifications, and other
  G3-and-later protocol corners degrade to explicit warnings or documented
  safe results.
- Keyboard and global-key sequences, touch, pointer-shape reporting, and
  translucent blending are not exercised or implemented. Alpha input masks
  are consumed for hit testing from G2 on, but blending translucent views
  is not designed here.
- WM/DE integration, GPU acceleration, and physical-hardware validation are
  not proven; the battery is headless on seL4 QEMU only.
- Capacity is fixed at 8 sessions, 32 views, and 3 merged damage rects;
  exhaustion returns protocol errors rather than growing without bound.
- The run-scenario Tier-0 seam is default-native since 2026-11-19 (unset
  or `SPONGE_COMPOSITOR=native`; `SPONGE_COMPOSITOR=upstream` is the
  explicit fallback). That selection change rests on the G1/G2 receipts
  plus the default/fallback runs above; it is not WM/DE integration and
  not product-integration evidence. The `sponge-wm` anchor was also
  corrected from 300 to the documented 1000 DE caps because the old
  value blocked Qt session creation; this is a scenario-budget fix, not
  a compositor dependency.

Root causes fixed during G2 (curated, no raw logs promoted): full drivers
inline config, the first-marker QMP rendezvous, the upstream focus/hover
domain, native alpha/mask plane binding, syntax-agnostic `Node` config
parsing, report config scoped to scenarios with `report_rom`, and
closed-loop QMP navigation.
