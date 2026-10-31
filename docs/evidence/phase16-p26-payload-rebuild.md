# Phase 16 P2.6 follow-up — Falkon payload ABI rebuild against the current tree

Date: 2026-10-31. QEMU/seL4 (`KERNEL=sel4 BOARD=pc`) receipts only.
No commit was made; all P2.6 wiring and measurements in
`phase16-p26-browser-staging.md` are preserved untouched.

## 1. Outcome summary

| item | status |
|---|---|
| `libc`, `vfs`, `vfs_lwip`, `vfs_oss` in `pkg/falkon/payload/` replaced by current-tree builds | **done** |
| previous `Timeout::Timeout(Timer::Connection&)` jump-slot failure | **gone** (structurally eliminated, §3.3) |
| old plugins left to misdispatch against the new `vfs.lib.so` | **none** — 5 rebuilt from in-tree sources, 1 removed (§4) |
| browser first paint + typed input | **not reached** — blocked one wall past the old failure, precisely characterized (§4) |
| P2.4 / P2.5 regressions | **green** (§6) |

The run now loads and relocates the whole closure against the rebuilt
libc/vfs set with **zero** jump-slot or `Linker::Not_found` errors. The
next blocker is `vfs_qtwebengine_shm.lib.so`: falkon's `<vfs>` config
mounts `<qtwebengine_shm/>`, and no buildable source for that plugin
exists for the Genode 26.08 VFS API. Per the task's stop condition this
is reported, not worked around.

## 2. Exact files changed

Payload (git-ignored, re-creatable per `pkg/falkon/SOURCE`):

| file | action | source |
|---|---|---|
| `pkg/falkon/payload/libc.lib.so` | replaced | `genode/build/x86_64/bin/libc.lib.so` (stripped) |
| `pkg/falkon/payload/vfs.lib.so` | replaced | `bin/vfs.lib.so` |
| `pkg/falkon/payload/vfs_lwip.lib.so` | replaced | `bin/vfs_lwip.lib.so` |
| `pkg/falkon/payload/vfs_oss.lib.so` | replaced | `bin/vfs_oss.lib.so` |
| `pkg/falkon/payload/vfs_pipe.lib.so` | replaced | `bin/vfs_pipe.lib.so` |
| `pkg/falkon/payload/vfs_capture.lib.so` | replaced | `bin/vfs_capture.lib.so` |
| `pkg/falkon/payload/vfs_gpu.lib.so` | replaced | `bin/vfs_gpu.lib.so` |
| `pkg/falkon/payload/vfs_jitterentropy.lib.so` | replaced | `bin/vfs_jitterentropy.lib.so` |
| `pkg/falkon/payload/vfs_xoroshiro.lib.so` | replaced | `bin/vfs_xoroshiro.lib.so` |
| `pkg/falkon/payload/vfs_qtwebengine_shm.lib.so` | **removed** | see §4 |

Tracked files: `pkg/falkon/PAYLIST` (inventory sync),
`pkg/falkon/SOURCE` (rebuild provenance addendum), this document.
No `genode/` edits (`git -C genode status` clean; tree is
`26.08-36-g82f0143d1f`), no run-script or `repos/sponge` code changes,
no ABI shim, no old-binary reuse.

Builds (serial):

```
make -C genode/build/x86_64 lib/libc lib/vfs lib/vfs_lwip lib/vfs_oss KERNEL=sel4 BOARD=pc
make -C genode/build/x86_64 lib/vfs_pipe lib/vfs_capture lib/vfs_gpu \
     lib/vfs_jitterentropy lib/vfs_xoroshiro KERNEL=sel4 BOARD=pc
```

The five plugin targets were added because the new `vfs.lib.so`'s
plugin interface is ABI-incompatible with every 2026-04 depot plugin
(§3.2); they are rebuilt from the sources in the vendored tree
(`os/src/lib/vfs/{capture,xoroshiro}`, `gems/src/lib/vfs/{gpu,oss,pipe}`,
`libports/src/lib/vfs/jitterentropy`).

## 3. nm / readelf evidence

### 3.1 New artifacts bind the new Timeout API only

`readelf -rW` on the payload copies (the shipped bytes):

- `libc.lib.so` JUMP_SLOTs:
  `_ZN6Genode7TimeoutC1ERN5Timer10ConnectionERNS_15Timeout_handlerE`
  (new two-arg ctor), `_ZN6Genode7Timeout17schedule_one_shotENS_12MicrosecondsE`
  (new one-arg schedule), `Timeout::~Timeout`.
- `vfs_lwip.lib.so` JUMP_SLOTs: the same new two-arg ctor,
  `_ZN6Genode7Timeout17schedule_periodicENS_12MicrosecondsE`, `~Timeout`.
- `vfs_oss.lib.so`: no `Timeout` jump slots at all.

Old one-arg ctor `_ZN6Genode7TimeoutC1ERN5Timer10ConnectionE` and the old
handler-taking `schedule_one_shot/periodic(Microseconds, Timeout_handler&)`
appear **nowhere** in the payload any more (`nm -D` sweep, zero hits).
The current `base/lib/symbols/ld` export map carries the two new
constructors (lines 315–318) and no one-arg ctor, so every remaining
jump slot resolves.

### 3.2 Old vs new VFS factory interface (the plugin verdict)

| artifact | factory typeinfo | evidence of interface generation |
|---|---|---|
| depot `vfs_pipe.lib.so` (2026-04) | `Genode::Vfs::File_system_factory` (`_ZTIN6Genode3Vfs19File_system_factoryE`) | also defines `Vfs::File_io_service::*`, `File_system::apply_config(Node)`, `Dir_vfs_handle(Directory_service&, File_io_service&, Allocator&, path)` |
| current-tree `vfs_capture.lib.so` | `Genode::Vfs::File_system::Factory` (`_ZTIN6Genode3Vfs11File_system7FactoryE`) | `Instance::Attempt create(Env&, Parent_fs&, Node const&)`, `destruct()` |
| new `vfs.lib.so` lookup (`os/src/lib/vfs/root.cc`) | expects `Vfs::File_system::Factory *` from `vfs_file_system_factory()` | `Global_file_system_factory` exports: gone (`nm -D` zero hits); new `libc.lib.so` imports `_ZN6Genode3Vfs4Root7FactoryC1ERNS_9AllocatorE` |

The two factory types are different classes with different vtables
(`create` slot signatures and return ABI differ: `File_system *` vs
`Instance::Attempt` value), `Vfs::File_io_service` no longer exists, and
`Vfs::Vfs_handle` changed layout (ctor `(Directory_service&, Allocator&,
int)`; `read(At, …)`, `ftruncate`, `notify_read_ready` virtuals). Any
2026-04 plugin invoked under the new `vfs.lib.so` is vtable
misdispatch at the first virtual call — which is why all mounted
plugins were rebuilt and none of the old binaries were kept.

### 3.3 Export-delta audit (no silent holes)

Old vs new export surfaces, taken from the pre-replacement copies still
staged in `var/run/` and the new payload:

- old `libc.lib.so` defines 3938 dynamic symbols, new 3848 (520 dropped);
- old `vfs.lib.so` defines 2086, new 1927 (665 dropped);
- dropped ∩ (all remaining payload imports) = **empty**. Every import
  the old closure satisfied is satisfied by the new set.

84 payload imports resolve against nothing (e.g. `PR_*` NSPR shims,
`freebsd11_*`, mesa `iris_xe_*`/`vtn_*` helpers). All 84 were equally
unresolved in the old closure (empty intersection with the old
exports), i.e. pre-existing lazy-slot holes that the first-paint proofs
already tolerated. The benign `Reloc_non_plt` warnings in every run
(`__gcov_dump`, `OPENSSL_memory_*`, `ZSTD_trace_*`, `sdallocx`) are the
eager half of the same set.

## 4. `vfs_qtwebengine_shm` — exactly why it blocks, and why it is not rebuilt

falkon's runtime config (`pkg/falkon/metadata.xml`) mounts
`<dir name="shm"> <qtwebengine_shm/> </dir>`. Three facts close every
path except a source port:

1. **The depot binary cannot be kept.** It is a 2026-04 plugin:
   factory typeinfo `Genode::Vfs::File_system_factory`, old
   `File_system`/`File_io_service` vtable (§3.2). Under the new
   `vfs.lib.so` the `Root::Factory` probe calls its `create` through the
   new slot layout — vtable misdispatch at vfs-config time. The task
   forbids accepting that, so the binary was removed.

2. **Removal fails loud and fatal at vfs init** (run C, §5):

   ```
   [init -> rom_pkg] Error: /vfs_qtwebengine_shm.lib.so not found
   [init -> system -> pkg_runtime_browser -> falkon] Error: stop because
       parent denied ROM-session: label="vfs_qtwebengine_shm.lib.so", …
   ```

   The `Root::Factory` probe dlopens `vfs_qtwebengine_shm.lib.so`; the
   ROM request falls through `rom_pkg` (404) to the parent and is
   denied, and a denied session is fatal for the component
   (`base/src/lib/base/component.cc:252`, "stop because parent denied
   …-session"). The failure sits in `Component::construct` during the
   `<vfs>` apply — after the whole closure loaded cleanly, before
   falkon's `main`. This is the next precise blocker.

3. **No published source matches this tree's VFS API.** The depot
   publishes `cproc/src/vfs_qtwebengine_shm` versions from 2021-02-22 to
   2026-10-23, but they bracket the vendored API without hitting it:
   - ≤ 2026-05-05 target the pre-August-2026 interface
     (`File_system_factory`, `apply_config`, `File_io_service`) — same
     generation as the depot binary;
   - ≥ 2026-10-09 target the post-`vfs/2026-10-16` master API
     (`Vfs::File_channel`, `Open_attr open(path, Open_attr, Allocator&)`,
     `Follow_error`/`follow`) — none of these types exist in the
     vendored tree;
   - the vendored tree is the Genode **26.08 release** lineage
     (`git describe`: `26.08-36-g82f0143d1f`) whose
     `Directory_service::open` is `open(path, unsigned mode,
     Vfs_handle **, Allocator &)` and whose handle class is `Vfs_handle`
     (`read(At,…)`, `ftruncate`, `notify_read_ready`).

   Building any published version yields another ABI-mismatched plugin
   (different `open` slot signature / handle class), and adapting one
   across the API generations is an invented interface mapping — the
   shim-equivalent the task forbids. So the stop condition applies: a
   26.08-ABI `vfs_qtwebengine_shm` must come from upstream cproc (a
   26.08-era recipe) or from an explicitly scoped porting task.

For the record: bypassing the mount by editing falkon's config would
only move the failure — the plugin is qtwebengine's shared-memory
substrate (its own doc comment: WebEngine creates files in the plugin
root, `ftruncate`s once, then `mmap`s them as shared memory), so
WebEngine would fail at its first `base::SharedMemory` use instead, and
config editing is outside this task's payload-replacement scope anyway.

## 5. Run receipts

### 5.1 Run C — grouped BIOS scenario (the prescribed command)

```
timeout 420s env SPONGE_GROUPED_UI=on LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc \
  RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"
```

Log: `/tmp/opencode/p27-runC.log` (6235 lines; outer budget cut the run
during the browser leg's bounded first-paint poll — falkon was already
stopped, so the poll can only end in the leg's own FAIL verdict; not
retried).

Findings, in order:

1. `alpha-probe: PASS` and both P2.4 grouped legs green:
   simple-class first paint + typed delta `changed=325 baseline=0`
   (typed="hello"); terminal-class first paint + typed delta
   `changed=15 baseline=0` (typed="echo grouped-ok") + execution delta
   `changed=37 baseline=0`. Thresholds and legs untouched (the known
   terminal 5-second-flush flake did not fire this run).
2. Real launcher chain reached falkon: `launcher click-to-launch
   'falkon'` → `sponge_pkgd: launch result falkon -> ok
   (channel=launcher)` → `diag st_browser child falkon bin=bin/falkon`.
3. The rebuilt closure loaded and relocated: only the 8 known-benign
   `Reloc_non_plt(libQt6WebEngineCore.lib.so)` warnings — identical to
   every successful falkon proof. **No** `jump slot relocation failed`,
   **no** `Linker::Not_found`, **no** `abort called` anywhere in the
   log: the Timeout failure of `p26-runB` is gone.
4. Then the §4.2 stop: `rom_pkg` 404 + denied ROM-session for
   `vfs_qtwebengine_shm.lib.so`; falkon exits at vfs init; the probe's
   `diag first-paint layer=never-registered` keeps polling its bound.

### 5.2 Runs G/H — regression receipts

| run | command | result |
|---|---|---|
| G: P2.5 `sponge-de-pkgd-failure` | `timeout 420s env LD_PRELOAD=… make -j1 … run/sponge-de-pkgd-failure KERNEL=sel4 BOARD=pc` (ISO media path) | `de-survival-probe: PASS` + `de-survival-probe: scenario GREEN (P2.5 mechanisms present)` |
| H: P2.4 default BIOS path (grouped off) | run C's command without `SPONGE_GROUPED_UI` | `alpha-probe: PASS` + `Run script execution successful.` |

Logs: `/tmp/opencode/p27-runG-pkgdfailure.log`,
`/tmp/opencode/p27-runH-default.log`. No P2.5 launcher files were
touched (`git diff` shows only the pre-existing P2.6 run/probe edits).

## 6. Measurements (fresh)

| measurement | value | P2.6 §5 (depot closure, preserved) |
|---|---|---|
| closure file count | 63 | 64 |
| closure size (`du -sb pkg/falkon/payload`) | 532,840,379 bytes | 532,932,275 bytes |

Delta: −1 file (the removed plugin), −91,896 bytes net. The P2.6 §5
staged-content / P3-margin figures describe the depot closure at RUN B
and are left as historical records; the new-media P3 is content-sized
by the run regardless. Nothing was resized.

## 7. Honest boundaries

- QEMU/seL4 receipts only; no page-load acceptance attempted (out of
  scope).
- The `Timeout` ctor jump slot is proven correct by signature and
  export-map evidence (§3.1) plus a full clean relocation pass; the
  actual call site (the `socket`/lwip mount) sits behind the
  `qtwebengine_shm` wall in config order, so the runtime bind receipt
  lands with the next blocker's resolution.
- The five rebuilt plugins are proven to load and to satisfy the new
  factory ABI by construction (same tree, same headers); only `pipe`,
  `jitterentropy` and `xoroshiro` are mounted by falkon's config and
  were exercised through the vfs apply in run C up to the shm node.
- `vfs_oss`/`vfs_capture`/`vfs_gpu` are rebuilt for closure coherence
  (the payload's `falkon.config` mounts them) but not exercised by the
  `metadata.xml` runtime config.

## 8. Artifacts

- logs: `/tmp/opencode/p27-build-libs.log`, `p27-build-plugins.log`,
  `p27-runC.log`, `p27-runG-pkgdfailure.log`, `p27-runH-default.log`
- export-delta evidence: `/tmp/opencode/p27-old-{libc,vfs}.lib.so`
  (pre-replacement copies from `var/run` staging), `p27-{undef,def,ldmap,unresolved,libc-dropped,vfs-dropped}.txt`
- `vfs_qtwebengine_shm` source-vintage evidence:
  `/tmp/opencode/qtshm/` (depot `cproc/src/vfs_qtwebengine_shm`
  `2026-10-09` and `2026-10-23` tarballs + extracted trees)
