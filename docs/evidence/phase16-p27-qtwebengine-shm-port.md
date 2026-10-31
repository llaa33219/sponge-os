# Phase 16 P2.7 — `vfs_qtwebengine_shm` source port to the vendored 26.08 VFS API

Date: 2026-10-31. QEMU/seL4 (`KERNEL=sel4 BOARD=pc`) receipts only.
No commit was made; all P2.6 wiring and measurements in
`phase16-p26-browser-staging.md` and `phase16-p26-payload-rebuild.md`
are preserved untouched.

## 1. Outcome summary

| item | status |
|---|---|
| `vfs_qtwebengine_shm.lib.so` built from source against the vendored 26.08 tree | **done** (deliberate source port, §2–§3) |
| old removed depot binary kept out of the payload | **done** (§4) |
| symbols: 26.08 `File_system::Factory` + `Vfs_handle` model, no old-ABI types | **done** (§5) |
| grouped BIOS run: `<qtwebengine_shm/>` mount passes without ROM denial / vtable misdispatch | **see §6** |
| browser first paint + typed input | **see §6** |
| P2.4 / P2.5 regressions green | **see §7** |

## 2. Source provenance

Base recipe: **cproc/src/vfs_qtwebengine_shm 2026-10-23**

- URL: `https://depot.genode.org/cproc/src/vfs_qtwebengine_shm/2026-10-23.tar.xz`
- SHA-256: `795045d08967dd26cfca313c2869fc531f391d5cc68182d07c33b05e299e04dd`
  (downloaded twice — the p26 investigation copy and an independent
  re-download today — byte-identical)

Cross-references used while porting (both from the same depot path):

- `2026-10-09.tar.xz` SHA-256
  `445d36e23033f46a6fd99a781a98491642d9b7db45a19a7f83b9f2f98c55b230`
  (the task-named vintage; identical to 2026-10-23 except the
  `File_channel` `Attr` writeable fix and the `follow` hook)
- `2026-05-05.tar.xz` SHA-256
  `be14be41965e09861dd2fed463a2bc442db6cb2574c91a7a0367b7a04c05488e`
  (pre-refactor vintage; its `Directory_service::open(path, unsigned,
  Vfs_handle **, Allocator &)` signature is identical to the vendored
  26.08 one and its error-code choices are the proven behavior of the
  removed depot binary)

Depot version landscape (directory listing of
`https://depot.genode.org/cproc/src/vfs_qtwebengine_shm/`): versions
2021-02-22 … 2026-10-23 exist; ≤2026-05-05 target the
`File_system_factory`/`File_io_service` ABI (vtable misdispatch under
the rebuilt `vfs.lib.so`, as established in
`phase16-p26-payload-rebuild.md` §3.2), ≥2026-10-09 target the
post-2026-10-16 master API (`Vfs::File_channel`, `Open_attr`,
`Follow_error`) which does not exist in the vendored tree. No published
version compiles against 26.08 unmodified; the port below is the
deliberately scoped adaptation the task permits.

## 3. Files added / changed

Added (uncommitted):

| file | role |
|---|---|
| `repos/sponge/lib/src/vfs_qtwebengine_shm/vfs.cc` | the port (2026-10-23 body adapted to the 26.08 `Vfs_handle` model; adaptation notes in the file header) |
| `repos/sponge/lib/src/vfs_qtwebengine_shm/symbol.map` | byte-identical to the recipe (exports only `vfs_file_system_factory`) |
| `repos/sponge/lib/mk/vfs_qtwebengine_shm.mk` | recipe build file, `VFS_DIR` retargeted to the sponge `lib/src` convention |

Changed (uncommitted):

| file | change |
|---|---|
| `pkg/falkon/payload/vfs_qtwebengine_shm.lib.so` | staged build artifact (payload is git-ignored, re-creatable) |
| `pkg/falkon/PAYLIST` | `staged (63)` → `staged (64)`, `missing` section removed |
| `pkg/falkon/SOURCE` | rebuild-provenance addendum for the port |

Nothing under `genode/` was touched (`git -C genode status` clean, tree
`26.08-36-g82f0143d1f`). No P2.5 launcher files, no run-script
changes, no media resizing.

Build (serial):

```
make -j1 -C genode/build/x86_64 lib/vfs_qtwebengine_shm KERNEL=sel4 BOARD=pc
```

Staged from `bin/vfs_qtwebengine_shm.lib.so` (the stripped install
artifact), SHA-256 of staged bytes:
`56ceb55e5f831373a85f984ee5ce6941f1403c60d43751cc8a3073f095094815`.

### 3.1 Interface adaptations (source port, not a facade)

The 2026-10-23 recipe targets the post-2026-10-16 master API. The
adaptations to the vendored 26.08 model (`os/include/vfs/…`), all
recorded in the `vfs.cc` header:

1. `struct File_channel : Vfs::File_channel` → `: Vfs::Vfs_handle`
   (26.08 ctor `(Directory_service &, Allocator &, int)`); the master
   `Attr { .writeable }` becomes the 26.08 `status_flags` pass-through
   of the open `mode` (the `ram_file_system.h` idiom).
2. `File_channel::Resize_result resize()` → `Ftruncate_result
   ftruncate()`; `Dataspace_vfs_file::resize` returns
   `Vfs_handle::Ftruncate_result` (`FTRUNCATE_OK` /
   `FTRUNCATE_ERR_NO_PERM`), matching the 2026-05-05 vintage.
3. `open(path, Open_attr, Allocator &)` → `open(path, unsigned mode,
   Vfs_handle **, Allocator &)`. `attr.create` → `mode &
   OPEN_MODE_CREATE`. The master's `Open_error::DENIED` sites map to
   the 26.08 caller contract: create-on-existing returns
   `OPEN_ERR_EXISTS` (so `Vfs::File_handle`
   (`os/include/vfs/file_handle.h`) performs its documented retry
   without the CREATE flag — the only way a writeable open of an
   existing shm file can succeed, since `File_handle::_mode()` always
   sets `OPEN_MODE_CREATE` for writeable opens), file-object allocation
   failure returns `OPEN_ERR_NO_SPACE`, missing entry on a non-create
   open returns `OPEN_ERR_UNACCESSIBLE` — all three as in the proven
   2026-05-05 vintage.
4. `Follow_result follow()` (2026-10-23) dropped: 26.08 has no
   `follow` hook; symlink resolution is driven by `stat`/`openlink` and
   this file system never reports `Node_type::SYMLINK`, so the
   `NO_SYMLINK` semantics hold by construction.
5. Handle teardown: master channel `destruct()` → 26.08
   `Directory_service::close(Vfs_handle *)` override destroying the
   handle (unchanged `~File_channel` bookkeeping runs on destroy),
   following `ram_file_system.h`.
6. `Stat::type` values: master `Dirent_type::DIRECTORY/FILE` → 26.08
   `Node_type::DIRECTORY/CONTINUOUS_FILE`; `Stat_result::OK/DENIED` →
   `STAT_OK/STAT_ERR_NO_ENTRY`; `Stat::device` filled as in the
   2026-05-05 vintage (26.08 `Stat` has no `inode`).
7. `opendir` + directory handle from the 2026-05-05 vintage retained
   (26.08 exposes `Directory_service::opendir` as first-class; the
   master recipe dropped it with the `File_channel` unification). The
   directory read keeps upstream's empty-listing behavior and its
   `not implemented` log. `Dataspace_vfs_file::resize` sets `_length`
   only after a successful RAM allocation so a failed `ftruncate` never
   leaves a stale length that would make the destructor free an invalid
   capability (upstream sets it before; observable success-path
   behavior unchanged).
8. `stat` leaves `size` at zero exactly as both upstream vintages do.

The `vfs_file_system_factory` export is byte-identical to the recipe;
its `Vfs::File_system::Factory` shape (`Instance::Attempt
create(Vfs::Env &, Parent_fs &, Node const &)`, `_free`) already
matches 26.08 (same shape as `os/src/lib/vfs/capture/plugin.cc`).

## 4. Old binary kept out

The removed depot binary (old-`File_system_factory`/`File_io_service`
ABI) is **not** reused anywhere. Hash contrast:

| artifact | SHA-256 |
|---|---|
| staged `pkg/falkon/payload/vfs_qtwebengine_shm.lib.so` | `56ceb55e5f831373a85f984ee5ce6941f1403c60d43751cc8a3073f095094815` (current-tree build) |
| old depot binary (kept only in the `var/scratch/x86_64-original/` snapshot, outside the payload) | `7da33f1979ead0344c474ff29869cf96c7abf2d6fb01aaa257e3f68a97bd3aba` |

`pkg/falkon/payload/` holds 64 files.

## 5. Symbol / ABI evidence

`nm`/`readelf` on the build artifact
(`genode/build/x86_64/var/libcache/vfs_qtwebengine_shm/vfs_qtwebengine_shm.lib.so.stripped`
and the unstripped `.lib.so`):

- dynamic exports: **only** `vfs_file_system_factory`
  (`nm -D --defined-only`).
- `vfs.cc` object defines
  `_ZTIN6Genode3Vfs11File_system7FactoryE` (typeinfo for
  `Genode::Vfs::File_system::Factory`) — the 26.08 factory interface.
- `Vfs_shm::File_system::open(char const*, unsigned int,
  Genode::Vfs::Vfs_handle**, Genode::Allocator&)` — the required
  26.08 `open` signature; likewise `close(Vfs_handle *)`,
  `opendir(char const*, bool, Vfs_handle **, Allocator &)`.
- `File_channel` vtable carries the 26.08 handle virtuals
  `read(Genode::Vfs::At, Byte_range_ptr const &)`,
  `ftruncate(unsigned long long)`, `read_ready()`, `write_ready()`.
- **zero** hits for `File_system_factory`, `File_io_service`,
  `File_channel`-as-base, `Open_attr`, `Follow` (old/master-ABI
  markers). Contrast: the removed depot binary's RTTI names contain
  `Vfs_shm::File_system::Dataspace_vfs_handle` +
  `Vfs::File_io_service::Ftruncate_result` (pre-refactor model).

Undefined-symbol closure: all 29 dynamic undefs of the plugin resolve
against `genode/repos/base/lib/symbols/ld` (checked individually); 25
of 29 are the same undefs as the proven-loaded `vfs_pipe.lib.so`, the
4 plugin-specific ones (`Ram_allocator::_legacy_dataspace_size`,
  `Native_capability::_inc`, `raise(Alloc_error)`,
  `__cxa_call_terminate`) are all `T` in the ld export map.

## 6. Grouped BIOS run receipts

### 6.1 Run C1 — prescribed command, outer budget 420 s

```
timeout 420s env SPONGE_GROUPED_UI=on LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc \
  RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"
```

Log: `/tmp/opencode/p28-runC.log` (6113 lines, make exit 2).

Findings:

1. `alpha-probe: PASS`; simple-class first paint + typed delta green
   (`changed=133 baseline=0 click_changed=10876 typed="hello"`).
2. Terminal-class first paint green (`rect=(50,328 320x240)
   changed=4508`), then the **documented terminal 5-second-flush
   flake** fired: `typed-delta poll … changed=30 baseline=17` stuck at
   30 (`30 !> 2*17=34`), verdict `FAIL no typed-input delta
   (terminal-class click_changed=411)`. This is line-for-line the
   §7 signature of `phase16-p26-browser-staging.md` (`changed=30
   baseline=17`, quantified there at ~1-in-7 per run, root cause the
   vendored terminal's `_flush_delay = 5` s vs the probe's 700 ms
   baseline window), which that document re-ran past once.
3. Because the probe aborts at the failing leg, the browser leg never
   ran — falkon was not launched in C1, so the shm mount was not
   exercised. (`grep vfs_qtwebengine_shm` over the log: zero hits —
   vacuous, not evidence.)

Conclusion: C1 cannot speak to the shm mount; the flake is in the
terminal package, unrelated to this change (the port touches only
falkon's payload area). One evidence-driven re-run past a
root-caused, documented, quantified flake is not a blind retry.

### 6.2 Run C2 — re-run, budget extended to 1500 s

Budget extension justification (task: "longer budget only if needed"):
the probe's own browser first-paint poll bound is
`PAINT_ITERS_BROWSER = 3000 × 200 ms = 600 s`
(`repos/sponge/src/test/grouped_ui_probe/main.cc:198`), so a 420 s
outer budget cannot contain boot + three legs + that poll —
`phase16-p26-payload-rebuild.md` §5.1 records the 420 s run being cut
during exactly that poll. The same command with `timeout 1500s` was
run.

Log: `/tmp/opencode/p28-runC2.log` (6592 lines, make exit 2).

Findings:

1. `alpha-probe: PASS` (line 5926). Simple-class leg then failed at
   **launch**, before any window existed: the launcher popup opened
   (`popup-open poll 0 changed=9534`), the band diag was clean
   (`diag launcher bands click y=535 covered=yes`) and the QMP tablet
   click was dispatched (`QMP-TARGET tablet 14 535`), but the popup
   never closed (`popup-closed poll 0..40 open=1`) and **no
   `click-to-launch 'textedit'` line appears anywhere in the log**.
2. Concurrent infrastructure failure signature:
   `sponge_supervisor: FAULT max_skipped=3 exited=0 starting=0
   state_seen=1 state_age_ms=250` (line 5991), a storm of ~433 seL4
   kernel warnings `Attempted to invoke a null cap #376` (lines 5993+;
   C1 had **0**, p27-runC had 4), 8 `Warning: invalid signal-context
   capability`, and 8 `Warning: alpha_probe: close of ROM (session …)
   failed - the provider did not answer within 5000 ms` (C1: 4,
   p27-runC: 4).
3. The PD creation triple then failed (lines 6073–6079):
   `Error: CPU (init -> system -> sponge-de) create_thread: invalid
   PD argument` → `Error: failed to create initial thread for new PD`
   → `Error: dynamic linker is an invalid dataspace` →
   `Error: sponge-de: attempt to load an invalid executable`.
   textedit never became a child (final diag lists only `hello` and
   `debug` under `st_simple`; `window_list: 1 entries` = SPONGE DEBUG
   only), first-paint polls 0…1500 all `id=0 rect_ok=0
   rect=(0,0 0x0) changed=0`, verdict `FAIL no first paint fo[r …]`
   (line 6588).
4. **Pre-existing failure mode, not this change.** The identical PD
   triple appears in the **pre-change** run
   `/tmp/opencode/p27-runG-pkgdfailure.log` lines 12321–12327
   (`CPU (init -> system -> sponge_pkgd) create_thread: invalid PD
   argument` … `sponge_pkgd: attempt to load an invalid executable`)
   — a run staged with the old 63-file payload and no plugin at all.
   The failure sits in textedit's PD/ROM path (the system ROM area),
   whereas this change only adds one file to falkon's payload area
   served by the browser-scoped `rom_pkg` (`caps: 20000 | ram: 768M`;
   its RAM warning grew a mere 4 KB: `used RAM is 4168KB` in C1 →
   `4172KB` in C2). The plugin is dlopened only when falkon applies
   its `<vfs>` config.
5. Staged content verified in the image staging area
   (`genode/build/x86_64/var/run/sponge-desktop-disk/system/pkg/falkon/payload/`):
   64 files, `vfs_qtwebengine_shm.lib.so` SHA-256 `56ceb55e…` = the
   new build. So the bake staged the plugin correctly; the run simply
   never reached falkon.

Conclusion: C2 died one leg *earlier* than C1 on a second, distinct
pre-existing flake (PD-creation/ROM-provider stall). Neither C1 nor C2
reached the browser leg, so the shm mount is still unexercised at
runtime.

### 6.3 Run C3 — re-run, budget extended to 1800 s

Justification: 1500 s covers the 600 s browser paint poll only if the
three earlier legs run perfectly; with per-leg retries (typed polls
are 300 × 200 ms each, plus focus/click settle windows) the worst case
needs ~1800 s. Same command, `timeout 1800s`.

Command + log: `timeout 1800s env SPONGE_GROUPED_UI=on
LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so make -j1 -C
genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc
RUN_OPT="--include power_on/qemu --include log/qemu --include
boot_dir/sel4 --include image/disk"` → `/tmp/opencode/p28-runC3.log`
(6183 lines).

Verdict: **shm mount PASS** — the port's primary criterion. Simple
and terminal legs PASS. The browser leg reached falkon's window but
the probe's class placement check (c) failed — the next precise
blocker, §6.4.

### 6.4 Run C3 findings

**A. Plugin load + mount — PASS.** Zero occurrences of the p27
failure pair (`Error: /vfs_qtwebengine_shm.lib.so not found`,
`stop because parent denied ROM-session`) and of the `root.cc` loader
warnings (`could not open 'vfs_qtwebengine_shm.lib.so'`,
`could not find symbol`). Zero runtime shm errors (`mmap got invalid`,
`resizing not supported`, `Dataspace_vfs`, `shm`: 0 hits each; the
single `SharedMemory` hit is a Qt header install line in the build
preamble). The `<vfs_qtwebengine_shm/>` `File_system` was created
through `vfs_file_system_factory` with no vtable misdispatch.

**B. falkon past vfs init (the p27 fatal point) into Qt/WebEngine
startup.** Real launcher chain: `sponge-de: launcher click-to-launch
'falkon'` (:6119) → `sponge_pkgd: launch result falkon -> ok
(channel=launcher)` (:6120) → `diag st_browser child falkon
bin=bin/falkon` (:6133). Then 8 known-benign `Reloc_non_plt`
warnings (gcov/OPENSSL_memory/ZSTD_trace/sdallocx, :6138–6145), lwIP
DHCP lease `10.0.2.15` (:6149), `Falkon: Creating new profile
directory` (:6158), `Falkon: 1 extensions loaded` (:6175), and
`tasklist_controller: focus new window 'pkg_runtime_browser -> falkon
-> Falkon'` (:6177). (The `Cannot create directory
//.local/share/falkon/QtWebEngine/Default` pair is benign — present
in the p14 first-paint run too.)

**C. Class legs.** Simple: first paint `window id=2 rect=(50,108
512x493) changed=6089`, typed delta `changed=324 baseline=0
click_changed=10876 typed="hello"` — PASS. Terminal: first paint
`window id=3 rect=(50,328 320x240) changed=4494`, typed delta
`changed=17 baseline=0 click_changed=428 typed="echo grouped-ok"`,
execution delta `changed=37 baseline=0` — PASS.

**D. Browser leg — the next precise blocker (class placement check
(c), `grouped_ui_probe/main.cc:805`).** Serial shows
`Error: grouped-ui-probe: FAIL windo` — truncated by the harness
FAIL-marker teardown before the message tail, including the rect
tuple, could flush. Deduction: of all `_fail` sites only `:805`
("window placed outside the pkg_runtime assign region (x=") and
`:917` ("window vanished before focus (") start with "window";
`:917` is reachable only after `:909` logs `browser-class first
paint (label=` (absent — only the simple/terminal first-paint lines
exist), so the fail is `:805` inside `_poll_first_paint`: falkon's
window had registered (id ≠ 0) with a rect and ≥ `PAINT_FLOOR` (150)
changed points inside `WIN_REG`, but its center fell outside
`WIN_REG` (x∈[40,400), y∈[280,560)). Poll history: browser polls
0–75 all `id=0 rect_ok=0` (QtWebEngine startup is slower than the
simple/terminal legs), then the window mapped (:6177) and the
placement check fired.

Mechanism (inferred; textedit precedent): falkon's QtWebEngine
window has a minimum decorated frame larger than the generic
`pkg_runtime` assign rule (`xpos: 50 | ypos: 300 | width: 320 |
height: 240`, `run/sponge-desktop-disk.run`) — exactly as textedit's
512x493 minimum required its own rule (`xpos: 50 | ypos: 80 |
width: 512 | height: 493`; the run file's comment records that
without it textedit's origin pushes its bottom beyond the 768 px
display). With the +28 px decorator origin offset visible in both
known rects (rule y=80 → rect y=108; rule y=300 → rect y=328), a
512x493-class falkon window under the generic rule sits at (50,328
512x493) → center (306,574) → y=574 ≥ 560 (`WIN_REG` bottom) →
outside → `:805`. The exact rect tuple is lost in the truncated FAIL
tail (the `window_list` report only exposes id+label to the probe
diag).

Fix path (executed in `phase16-p26-browser-acceptance.md`): a
falkon-specific `+ assign | label_prefix: pkg_runtime_browser
-> falkon | …` rule before the generic `pkg_runtime` rule, sized to
falkon's actual frame (recovered there from window_list/window_layout
runtime evidence as a 787x569 client), then the browser-leg URL-bar
input calibration already deferred in `phase16-p26-browser-staging.md`
§7. Rule dimensions without that evidence would have been a blind
guess, which the run constraints prohibited.

## 7. P2.4 / P2.5 regression receipts

**P2.5 — Run G, ISO media path.** Command:
`timeout 420s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so make
-j1 -C genode/build/x86_64 run/sponge-de-pkgd-failure KERNEL=sel4
BOARD=pc`. First attempt `/tmp/opencode/p28-runG.log` failed
`de-survival-probe: FAIL popup did not open after the S click` —
root cause a host-side QMP dispatch race (0 `qmp: dispatching` lines
vs 7 in the green `p27-runG-pkgdfailure.log`; the probe's
`[p1] QMP-TARGET entry 32 14` marker was emitted but never consumed
after a QEMU-respawn interleave). Unrelated to this change — the P2.5
ISO carries no falkon payload (`payload|falkon` grep: 0 hits in both
logs). Re-run `/tmp/opencode/p28-runG2.log`: **GREEN** —
`de-survival-probe: scenario GREEN (P2.5 mechanisms present)` with 7
`qmp: dispatching` lines = the p27 baseline count.

**P2.4 — Run H, default BIOS path (`SPONGE_GROUPED_UI` off).**
Command: `timeout 600s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so
make -j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4
BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu
--include boot_dir/sel4 --include image/disk"`. H1
(`/tmp/opencode/p28-runH.log`) and H2 (`/tmp/opencode/p28-runH2.log`)
both failed `alpha-probe: FAIL themed panel never composited on
nitpicker` on the documented pre-existing intermittent boot flake:
`sponge-de: stop because parent denied ROM-session: label="theme"`
(:5847 in both) → `create_thread: invalid PD argument` → `dynamic
linker is an invalid dataspace` → `attempt to load an invalid
executable` (H1 :5945–5951, H2 :5884–5890), with 583/590 seL4
null-cap invocations. The same triple appears in the green pre-change
`p27-runG-pkgdfailure.log` (:12321–12327) and in `p28-runC2.log` (438
null caps) — it is a boot-order/quota-race intermittent, not
payload-correlated (the default path never loads falkon's payload).
H3 (`/tmp/opencode/p28-runH3.log`): **GREEN** —
`alpha-probe: (a) panel band rendered at (512,4) = 0xff1e1e2e`
(identical pixel to the `p27-runH-default.log` baseline),
`alpha-probe: PASS`, `Run script execution successful.`, 0 null caps.

Both required regressions are green (P2.5 via G2, P2.4 via H3); the
H1/H2 red receipts are retained above as flake evidence.

## 8. Honest boundaries

- QEMU/seL4 receipts only; no page-load acceptance attempted.
- The port's success-path behavior is the upstream 2026-10-23 one;
  the adaptations of §3.1 are interface-shape mappings (plus the
  documented `OPEN_ERR_EXISTS` caller-contract choice and the
  `_length` ordering fix), not feature changes.
- The terminal 5-second-flush flake of the P2.4 recipe remains in the
  recipe (legs and thresholds untouched, per constraint).

## 9. Artifacts

- logs: `/tmp/opencode/p28-runC.log` (C1), `/tmp/opencode/p28-runC2.log`
  (C2), `/tmp/opencode/p28-runC3.log` (C3, the shm-mount receipt),
  `/tmp/opencode/p28-runG.log` (P2.5 first attempt, QMP race),
  `/tmp/opencode/p28-runG2.log` (P2.5 GREEN),
  `/tmp/opencode/p28-runH.log` / `/tmp/opencode/p28-runH2.log` (P2.4
  flake receipts), `/tmp/opencode/p28-runH3.log` (P2.4 GREEN);
  baselines: `/tmp/opencode/p27-runG-pkgdfailure.log`,
  `/tmp/opencode/p27-runH-default.log`
- source-recipe evidence: `/tmp/opencode/qtshm/` (2026-05-05,
  2026-10-09, 2026-10-19, 2026-10-22, 2026-10-23 tarballs + extracted
  trees, SHA-256s in §2)
