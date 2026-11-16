> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §1. Prerequisites

## 1. Prerequisites

### 1.1 The Vendored Genode Tree

Sponge OS no longer depends on an external Genode checkout. The
repository carries Genode 26.05 inside it as a `git subtree` at
`genode/`, pinned to upstream commit
`492a51024217fe74ccee1ebdfb81be97046b43eb` (the
`codeberg.org/genodelabs/genode` tag `26.05^{}`). Every patch we have
applied on top is recorded in the patch ledger in
[`docs/11-environment.md`](../11-environment.md) §4. The bridge into
Sponge OS is the committed relative symlink
`genode/repos/sponge -> ../../repos/sponge`, which lets the Genode
build system discover `repos/sponge/` from inside the vendored tree
without any per-developer wiring.

Two facts that come out of this:

- **No separate `git clone genode`.** A contributor clones this
  repository with `git clone --recursive` (the `genode/` submodule
  follows automatically; `./tool/build prepare` initializes it if
  forgotten). A contributor only ever clones
  `sponge-os`. The full Genode 26.05 source tree comes with it
  (~50 MB working tree, ~150 MB with the subtree's git history).
- **No `ln -s repos/sponge`.** The symlink is committed. Moving the
  clone to a different path under a different home directory just
  works.

### 1.2 The Genode Toolchain

The Genode toolchain is **not** vendored. Genode's build system
enforces a hard `$(error)` if the compiler does not report exactly the
expected version (`GCC 14.2.0`, `binutils 2.44`), so installing the
toolchain out of band is non-negotiable.

Install once per machine:

```bash
# Arch / CachyOS: AUR (maintained by a Genode Labs employee)
yay -S genode-toolchain-bin

# Every other distro: official prebuilt tarball
wget https://genode.org/files/tool-chain/genode-toolchain-25.05.tar.xz
sudo tar xPf genode-toolchain-25.05.tar.xz
ls /usr/local/genode/tool/25.05/bin/genode-x86-gcc    # sanity check
ls -l /usr/local/genode/tool/current                 # must point at 25.05
```

The `-P` flag to `tar` is load-bearing: it preserves the absolute
paths inside the archive (`./usr/local/genode/...`) so the toolchain
ends up exactly where the Genode build system expects. Without `-P`,
you get a stray `genode/` directory in the cwd and a confusing build
error.

There is **no 26.05 toolchain tarball**. Genode uses a 2-year
toolchain cycle, and 25.05 is the official pairing for 26.05. See
[`docs/11-environment.md`](../11-environment.md) §3 for the full pin
table and the rationale.

### 1.3 Host Packages

```bash
# Arch
sudo pacman -S tcl expect qemu-system-x86 cmake ninja make rpcsvc-proto
# Debian / Ubuntu
sudo apt install tcl expect qemu-system-x86 cmake ninja-build build-essential libc-dev-bin
```

`tcl` and `expect` are needed by Genode's run tool; `cmake` and
`ninja` are needed for the seL4 kernel build and the Qt6 library
build; `qemu-system-x86` is needed for any non-base-linux run;
`rpcgen` (from `rpcsvc-proto` / `libc-dev-bin`) is needed by the
`libc` port's prepare step.

### 1.4 Mojo SDK (preferred language for Sponge-owned code)

Mojo is the preferred and default language for Sponge-owned code
(`docs/plans/mojo-language-policy.md` DLP.1): both the host tooling
under `tool/` ([`tool/README.md`](../../tool/README.md) for tool-by-tool
usage) and the Genode/seL4 components under `repos/sponge/src/` use
Mojo. C++ stays as a deliberate adapter tool: thin Genode boundary
glue and existing components that have not been migrated yet.

The dependency is declared in the committed `pyproject.toml` and
pinned in `uv.lock`, so installing it is:

```bash
uv sync
.venv/bin/mojo --version
```

That one install covers both roles: the same `mojo` binary builds the
`tool/*.mojo` helpers and emits the native objects that Genode
components link (§1.5). The `uv.lock` pin is load-bearing, not
cosmetic; §1.5 lists the SDK-bump checks it governs.

### 1.5 Mojo objects in the Genode build

Mojo reaches a Genode component as a native object plus a thin C++
adapter, not as a linked Mojo runtime. The proven path (seL4 QEMU
receipts in `docs/evidence/mojo-ffi-probe.md`):

1. Emit a position-independent relocatable object:

   ```bash
   mojo build --emit object --relocation-model=pic -o probe.o probe.mojo
   ```

2. Export entry points under the C calling convention so the C++ side
   can call them:

   ```mojo
   @export("mojo_sum")
   def mojo_sum(a: Int64, b: Int64) abi("C") -> Int64:
       return a + b
   ```

3. List the object in the component's `target.mk` with `SRC_O`.
   Genode's `generic.mk` folds `SRC_O` into the link set alongside
   `SRC_CC`:

   ```makefile
   SRC_CC := main.cc
   SRC_O  := probe.o
   ```

4. Keep the Genode boundary in a thin C++ adapter: a
   `Component::construct` glue calls the exported functions and owns
   the parent-session plumbing. Pure-Mojo session plumbing is
   unproven (see the limits), so the adapter is the honest seam.

Limits, binding (from the probe evidence):

- `--emit object` is **experimental**; the flag set can change
  between Mojo releases.
- **KGEN runtime symbols are version-sensitive.** An emitted object
  references compiler-internal runtime symbols that move between
  releases. The proven libc-free `Genode::Heap` shim covers
  allocation and stdlib `List[Int64]`, and nothing more.
- **No freestanding stdlib profile exists.** Async, Python interop,
  POSIX I/O, and pure-Mojo Genode session plumbing are outside the
  initial subset entirely: no Sponge code may rely on them until a
  probe with its own receipts proves them.

Verification requirements:

- **On every Mojo SDK bump** (any `uv.lock` change): rebuild the
  objects and run `nm -u` on each; every undefined KGEN symbol must
  be covered by the shim before the bump lands.
- **Runtime proof is seL4 QEMU.** The receipts are `mojo-ffi-probe:
  PASS sum=42` and `mojo-ffi-probe: PASS add=42 list-sum=45` on
  base-sel4, with `./tool/build run sponge-minimal` green as the
  control gate. base-linux runs are fast development feedback only
  and do not count as verification for Mojo-on-Genode work.

---

