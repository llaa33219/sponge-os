# Phase 16 compositor evidence index

> Status, 2026-11-18: **G1 complete**. The native Mojo compositor passes
> the two-opaque-view/clipping/Capture slice on seL4 QEMU and matches
> upstream nitpicker byte-for-byte. Upstream remains the shipped default.
> G2 has not started; no input-delivery, WM/DE integration, or default flip
> is claimed.

## 1. Public repositories and independence

The implementation is public at:

- <https://github.com/llaa33219/sponge-compositor>
- <https://github.com/llaa33219/sponge-wm>
- <https://github.com/llaa33219/sponge-de>

The G1 component lives in `sponge-compositor`. WM and DE are not present in
the minimal scenario and are unchanged. They remain independent through
public Genode `Gui`, `Capture`, and `Event` interfaces; no private nitpicker
header or WM/DE source is included by the compositor.

## 2. Delivered slice

- Mojo-owned scene/session/view tables, view stack, command reduction,
  clipping/damage, and opaque painter, using fixed-capacity `InlineArray`
  storage and 64-bit View IDs.
- Thin C++ adapters for Gui, per-session virtual Framebuffer, Capture,
  Event, Input, panorama ROM, and the `Genode::Heap` KGEN shim.
- Two real opaque view clients and a real Capture probe. The expected
  raster is test-oracle data only, not a second scene model.
- Opt-in selection through `SPONGE_COMPOSITOR=native`; unset/upstream keeps
  upstream nitpicker. Invalid values fail loudly.
- Golden and differential scenarios:
  `run/sponge-compositor-opaque.run` and `run/sponge-compositor-diff.run`.

Scene: screen 1024x768, background `#1e1e2e`; A `(100,80,400,300)` fill
`#3366cc` with a white border and absolute Front; B `(300,200,400,300)`
fill `#cc3333` with a black border and absolute Back. A wins the overlap.

## 3. Verification receipts

| Gate | Result and receipt |
|---|---|
| Upstream opaque oracle | `g1-capture: PASS`, `checksum=0x670d6b5d9fc0ed7b`; `/tmp/opencode/g1-opaque-upstream-final.log` |
| Native opaque golden | `g1-capture: PASS`, same checksum and `expect_checksum` assertion; `/tmp/opencode/g1-opaque-native-damage.log` |
| Native vs upstream differential | `g1-diff: MATCH`, same checksum and assertion; `/tmp/opencode/g1-diff-final.log` |
| Mojo scene object | `nm -u build/scene.o`: empty; `/tmp/opencode/g1-scene.o` also empty |
| Mojo painter object | `nm -u build/painter.o`: empty; `/tmp/opencode/g1-painter.o` also empty |
| `./tool/build verify` | all 41 patch-ledger rows and hardware-compatibility gates pass |
| WM/DE independence anchor | `wm-probe: PASS` after setting the documented Qt DE budget to 1000 caps; `/tmp/opencode/g1-wm-anchor-caps1000.log` |
| `git diff --check` | clean |

Both emitted objects use `--emit object --relocation-model=pic
--target-cpu=x86-64`. The baseline flag is required: the original host-CPU
build emitted AVX and faulted in `sc_scene_init`; the baseline build contains
no AVX instructions and passes the same 95-check C-ABI harness.

Damage lifecycle is also verified: view creation, pixel rebinding,
destruction, and geometry/stacking changes all add damage. An early Capture
can no longer consume the only refresh and leave later views unpainted.

`docs/evidence/mojo-scene-probe.md` records the caller-provided 8192-byte
state block and the independent object-emission receipts.

## 4. G1 limits (explicit, not silent failures)

- Event sessions exist and provide a real dataspace, but `submit_batch`
  logs that event injection is not implemented before G2.
- `child_view`, `associate`, `view_capability`, `release_view_id`, focus,
  title/background policy, blit/panning, mode/sync notifications, and other
  G2+ protocol corners degrade to explicit warnings or documented safe
  results. They are not claimed to work.
- Capacity is fixed at 8 sessions, 32 views, and 3 merged damage rects;
  exhaustion returns protocol errors rather than growing without bound.
- The scenario proves only two opaque views and Capture. It does not prove
  input, alpha/input masks, translucent blending, WM/DE integration, GPU
  acceleration, or physical hardware.
- No existing production topology is switched. The `sponge-wm` anchor was
  also corrected from 300 to the documented 1000 DE caps because the old
  value blocked Qt session creation; this is a scenario-budget fix, not a
  compositor dependency. G2 requires separate approval after these receipts.
