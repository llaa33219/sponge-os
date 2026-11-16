# Mojo FFI probe: object emission, C-ABI export, and a libc-free KGEN shim on seL4

> Scope: feasibility receipts for Mojo as a language of Sponge-owned code
> on Genode/base-sel4. All results are QEMU / base-sel4 observations, not
> real-hardware claims. Date: 2026-11-16. The probe sources were scratch
> and were removed after the runs; nothing from this probe is committed
> to the repository. The Mojo snippets below are reconstructions of the
> proven shape, not the removed sources, and the exact printed receipts
> are the only runtime results recorded here.

## 1. Question

Can a Mojo-compiled object link into a Genode component and run on seL4
without libc, and which part of the Mojo language does that actually
support? The probe answers the linking and allocation questions only. It
is not a language survey and claims no feature beyond what the receipts
in §2-§4 show.

## 2. Probe A: `mojo build --emit object` + `@export`/`abi("C")` linked via `SRC_O`

Command shape:

```
mojo build --emit object --relocation-model=pic -o mojo_ffi_probe.o mojo_ffi_probe.mojo
```

The emitted object is linked into the Genode component through `SRC_O`
in `target.mk`; `genode/repos/base/mk/generic.mk` folds `SRC_O` into the
link set alongside `SRC_CC`. Entry points carry `@export("name")` with
the `abi("C")` effect so the C++ side calls them under the C calling
convention (reference: <https://mojolang.org/docs/reference/decorators/export/>).
A thin C++ `Component::construct` glue calls the exported functions and
prints the receipt.

Reconstructed shape (not the removed source):

```mojo
@export("mojo_sum")
def mojo_sum(a: Int64, b: Int64) abi("C") -> Int64:
    return a + b
```

Receipt, seL4 QEMU:

```
mojo-ffi-probe: PASS sum=42
```

## 3. Probe B: libc-free `Genode::Heap` KGEN shim with `List[Int64]`

An emitted Mojo object reaches its runtime through KGEN symbols,
allocation among them. The probe replaced those entry points with a
libc-free shim backed by `Genode::Heap`, so the component links no libc
at all. With the shim in place, stdlib `List[Int64]` (heap allocation,
indexed writes, and iteration) runs inside the component.

Reconstructed shape (not the removed source):

```mojo
@export("mojo_add")
def mojo_add(a: Int64, b: Int64) abi("C") -> Int64:
    return a + b

@export("mojo_list_sum")
def mojo_list_sum(n: Int64) abi("C") -> Int64:
    var values = List[Int64](length=Int(n), fill=0)
    for i in range(Int(n)):
        values[i] = Int64(i)
    var total = Int64(0)
    for value in values:
        total += value
    return total
```

Receipt, seL4 QEMU:

```
mojo-ffi-probe: PASS add=42 list-sum=45
```

## 4. Control gate

The standard `./tool/build run sponge-minimal` also passed in the same
environment. The probe wiring disturbs nothing in the normal build and
run path.

## 5. Honest supported subset

Proven by the receipts above:

- `mojo build --emit object` produces an object that links into a Genode
  component via `SRC_O`, with `@export`/`abi("C")` entry points callable
  from C++ glue (§2).
- A libc-free KGEN shim on `Genode::Heap` satisfies the allocation needs
  exercised by this object, and stdlib `List[Int64]` runs on top of it
  (§3).

Caveats, binding:

- **`--emit object` is experimental.** The official compilation
  documentation marks the `object` emit option experimental
  (<https://mojolang.org/docs/tools/compilation>). Treat object emission
  as a probe-proven path that can change underfoot, not as a stable
  contract.
- **KGEN symbols are version-sensitive.** The runtime symbols an emitted
  object references are compiler-internal and move between Mojo
  releases. A shim that links with one toolchain can fail to link with
  the next. Pin the toolchain and re-run this probe on every bump.
- **No freestanding stdlib profile exists.** Mojo offers no supported
  no-OS / no-stdlib build profile. Whatever the stdlib touches beyond
  what §2-§3 exercised (I/O, threads, time, and the rest) stays out of
  scope until proven per area.

Not proven, do not claim:

- **Async.** Untouched by the probe.
- **Python interop.** Untouched by the probe.
- **POSIX I/O.** Untouched by the probe, and no POSIX layer exists in
  the libc-free component.
- **Pure-Mojo Genode session plumbing.** Component entry, parent
  sessions, RPC, and `Component::construct` in Mojo are not proven. The
  probes run behind a C++ entry glue and say nothing about session
  plumbing in Mojo.

## 6. References

- Compilation targets and emit options:
  <https://mojolang.org/docs/tools/compilation>
- `@export` decorator:
  <https://mojolang.org/docs/reference/decorators/export/>
