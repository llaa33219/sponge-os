# Mojo G1 scene probe: caller-provided state and an empty undefined-symbol set

> Scope: implementation-time proof for the Mojo 1.0.0b2 scene/painter
> subset used by the G1 compositor. This is a host compiler and C-ABI
> harness receipt, not a claim that native composition has passed on seL4.
> The seL4 gate is `run/sponge-compositor-opaque.run` and its differential
> companion.

## Verified contract

- The scene, sessions, views, stacking, command reduction, clipping,
  damage, and opaque painting are Mojo-owned. C++ supplies only raw state
  bytes and pixel dataspaces; it does not inspect or model the scene.
- Every `sc_*` entry takes an 8192-byte caller-provided state block. This
  replaces an earlier experimental inline-asm `.data` storage mechanism.
  No inline asm, module-global mutable state, architecture-specific
  storage, List, or heap allocation remains in the scene/painter path.
- View IDs and stacking neighbors are `UInt64`, matching public Genode
  `View_id` (`unsigned long`). Pixel buffers use the 4-byte
  `Pixel_rgb888` layout and caller-provided pitches.
- The behavioral C-ABI harness passes 95 checks, including the golden
  scene, Front/Back and relative stacking, 64-bit IDs, damage merging and
  clearing, buffer rebinding, capacity errors, and stale-ID handling.

## Independent build receipts

Commands used the repository's pinned helper, not an unpinned system
Mojo:

```
./tool/mojo_objects --emit object --relocation-model=pic --target-cpu=x86-64 \
  -o /tmp/opencode/g1-scene.o scene.mojo
./tool/mojo_objects --emit object --relocation-model=pic --target-cpu=x86-64 \
  -o /tmp/opencode/g1-painter.o painter.mojo
```

Toolchain reported by the helper: `pinned Mojo (1.0.0b2)`.

Results:

```
[mojo_objects] nm -u /tmp/opencode/g1-scene.o:
(no undefined symbols)

[mojo_objects] nm -u /tmp/opencode/g1-painter.o:
(no undefined symbols)
```

The scene object exports exactly the ten `sc_*` functions declared in
`repos/sponge-compositor/src/sponge_compositor/abi.h`. No KGEN or libc
symbol is required by these two objects. The `Genode::Heap` KGEN shim is
still supplied by the component for the documented adapter boundary and
for any later subset expansion, but it is not exercised by this core.

## Baseline CPU and damage lifecycle

`--target-cpu=x86-64` is mandatory for these objects. An earlier host-CPU
build emitted AVX instructions and faulted in `sc_scene_init` on seL4; the
baseline build contains no AVX instructions and preserves the same behavior.
View creation, pixel rebinding, destruction, and geometry/stacking changes
also add damage, so a Capture that consumes an earlier refresh cannot leave
later views unpainted.

## Limit

These receipts prove the current Mojo syntax, object-emission path,
caller-state ownership boundary, and C-ABI behavior only. They do not
prove Genode session plumbing, native Gui/Capture/Event integration, or
seL4 runtime behavior. Those require the G1 scenario receipts and are
recorded separately in `phase16-compositor-index.md` at gate close-out.
