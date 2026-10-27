# Phase 16 runtime overhaul — session-machinery patch and create-path I/O audit

**Artifact type:** design-significant engineering evidence (capability/patch-set
and I/O-path analysis; AGENTS.md §5.6)
**Date:** 2026-10-28
**Phase 16 task:** P0.5 (`docs/plans/phase16-runtime-overhaul.md`)
**Pinned state:** main repo `a5f5af32c3`; Genode fork submodule `HEAD` =
`82f0143d1f` (ledger row #31), fork tip verified at audit time.
**Status:** inventory record only. This document makes **no** classification
decision, runs **no** scenario, and proposes **no** patch. It is the raw input
the P4.1 classification consumes.

---

## 1. Scope and how to read this document

P0.5 inventories two things:

1. **Ledger rows #25–#31** (the session-machinery cluster) with their exact
   fork SHAs, touched files, and dependencies — plus the unledgered cluster
   member `73e5859da8`, whose absence from revert lists matters to any later
   drop (Part C of `docs/evidence/patch-classification.md`).
2. **Every storage I/O site reachable from a session-create or
   session-serving path** in the components this overhaul touches:
   `sponge_pkgd`, `sponge_configd`, `sponge_logdisk`, and the ROM/VFS
   provider chain (`cached_fs_rom`, `vfs`, `report_rom` routing) behind them.

What this document deliberately does **not** do:

- **No reclassification.** Verdicts (K-U / R-T / …), drop-when cells, and
  removal-verification lists live in `docs/evidence/patch-classification.md`
  (Part A rows 25–31, Part B/C for the unledgered commits). This audit
  cross-references that matrix; it does not repeat or revise it.
- **No root-cause claim on the NIC uplink regression.** It remains open
  (§7) and is dispositioned only by P4.2.
- **No new base patches.** Any site that could only be fixed by patching
  Genode upstream code beyond the existing ledger rows is marked
  **STOP/escalate** (§6), not scheduled as a task.

Phase labels used in the I/O tables:

| Label | Meaning |
|---|---|
| **(a)** | `Component::construct` / constructor / member-initializer time, on `env.ep()` |
| **(b)** | request-dispatch path — synchronously while answering a request (signal handler, or the one-time bootstrap-thread drain noted per site) |
| **(c)** | worker/bootstrap thread |
| **(d)** | signal/timer handler (non-request) |
| **(up)** | Genode upstream / fork-tree code, not a Sponge component |

---

## 2. Ledger rows #25–#31 — session-machinery inventory

All SHAs resolve in the `genode/` submodule (`sponge-os-genode` fork) unless
noted. File lists are the fork commits' diffs; the "machinery" column is the
session path the row guards. Verdict/drop-when: see
`docs/evidence/patch-classification.md` §6 (Part A) — **not repeated here**.

### Row 25 — `342900343b` — sandbox: a deferred quota request must never be answered falsely + bound the upgrade retry loops
- **Files (fork):** `repos/os/src/lib/sandbox/child.cc` (`Sandbox::Child::resource_request`,
  +106/−35 across the commit), `repos/base/include/parent/parent.h`
  (`Parent::UPGRADE_ATTEMPTS = 64U`), `repos/base/include/base/connection.h`
  (`retry_with_upgrade`), `repos/os/include/file_system_session/connection.h`
  (`_retry`), `repos/base/include/rm_session/connection.h`, `repos/base/src/lib/base/rpc_cap_alloc.cc`,
  `repos/base/src/lib/base/signal.cc`.
- **Machinery:** quota upgrade request path (`Parent::upgrade` →
  `resource_request`) and the four upgrade-retry loops it bounds — the
  `UPGRADE_ATTEMPTS` constant spans `base/`, `libports/libc`, and `os/`, all
  inside this one commit.
- **Dependencies:** supersedes row #21 (`10d172b3dd`, same
  `resource_request` function; ledger consolidation). If #21 and #25 are
  ever dropped together, add `run/sponge-launch-order-regression.run` to the
  verification set (classification Part A, row 21).
- **Touched by this phase?** Yes indirectly — `Sandbox::Child::resource_request`
  sits on every forwarded quota upgrade in the create chain (FD1). P1.3/P3.3
  refactors change what pkgd asks for, not this machinery.

### Row 26 — `dda40f3a87` — init: a child that has never answered a heartbeat is STARTING, not wedged
- **Files (fork):** `repos/base/include/base/child.h` (sticky
  `_heartbeat_responded` + accessors), `repos/base/src/lib/base/child.cc`,
  `repos/os/src/lib/sandbox/child.h` (`Heartbeat::startup_grace_ms`, the
  STARTING classification), `repos/os/src/lib/sandbox/child.cc`
  (`report_state()` `heartbeat_state="starting"`),
  `repos/os/src/lib/sandbox/heartbeat.h` (elapsed-ms accumulation),
  `repos/os/src/init/init.hsd` (documents `restart_budget` + `startup_grace_ms`).
- **Machinery:** heartbeat monitoring surface of the sandbox `Child`; the
  STARTING-vs-WEDGED distinction every long constructor (Qt6/Mesa loads over
  slow storage) depends on.
- **Dependencies:** pairs with row #24 (`f44bc47ee5`, same `<heartbeat>`
  surface). The product topology's `startup_grace_ms: 120000` attributes are
  consumer-side values, not part of the patch.

### Row 27 — `20d0ff31d8` — base-sel4: a device untyped keeps its free index (mid-untyped BAR mapped wrong)
- **Files (fork):** `repos/base-sel4/patches/sel4-device-untyped-ladder.patch`
  (new port patch, two hunks: kernel `untyped.c` free-index retention +
  `boot.c` 64 GiB device-untyped grid), `repos/base-sel4/ports/sel4.hash`
  (re-stamp `fcfb8bdd…` → `28c4a439…`), `repos/base-sel4/src/core/include/initial_untyped_pool.h`
  (retype error now names the failing range).
- **Machinery:** not session machinery — seL4 untyped retype path behind
  every `IO_MEM`/frame allocation. Listed because P0.5 covers "any row whose
  files this phase touches" only in spirit: this phase does **not** touch
  base-sel4. Inventory completeness is the reason it is here.
- **Dependencies:** builds on row #13 (drop #27 before #13, never #13
  alone) and row #14 (core-side high-phys fast-forward). Rides unledgered
  `e16699624d` (port-patch auto-apply) + `57075c4d26` (hash stamp).

### Row 28 — `1672f3649a` — a never-answered quota upgrade must not park the container's entrypoint; teardown must not free held sessions
- **Files (fork) — two commits, revert both as one unit:**
  - `ea04b06868` (the non-blocking upgrade/close): `repos/base/include/base/env.h`
    (`Env::try_upgrade` / `Env::try_close` / non-blocking `try_session`),
    `repos/base/include/base/service.h` (`Try_parent_service::initiate_request`
    UPGRADE/CLOSE paths), `repos/base/include/base/child.h` +
    `repos/base/src/lib/base/child.cc` (private pending-operation registry,
    `expire_pending_ops`, 5 s deadline), `repos/os/src/lib/sandbox/pending_session_op.h`
    (new; the sandbox's `Pending_op_timer` clock), `heartbeat.h`, `library.cc`,
    `repos/base/lib/symbols/ld`.
  - `1672f3649a` (teardown invariant): `repos/base/include/base/session_state.h`,
    `repos/base/src/lib/base/session_state.cc`, `repos/base/src/lib/base/component.cc`,
    `child.h`/`child.cc` teardown paths.
- **Machinery:** **the root of the forwarded-session cluster.** Non-blocking
  `Env::try_*`, the pending-operation registry, `pending_session_op.h`.
- **Dependencies:** rows #29, #30, #31 and unledgered `73e5859da8` all build
  on this machinery. The first attempt `500cb2cc8a` was reverted by
  `3df26cf22b` (net-zero pair; classification Part B). Revert order for any
  future cluster drop is reverse application (classification Part C).

### Row 29 — `0a329d4fff` — keep the `Genode::Env` vtable layout ABI-stable for prebuilt components
- **Files (fork):** `repos/base/include/base/env.h` (the three row-#28
  virtuals re-appended at the END of `Genode::Env`; `Env::close` back at
  `vptr+0x68`), `repos/libports/src/lib/libc/internal/env.h` (libc
  forwarding overrides), `repos/libports/lib/import/import-qt6_qmake.mk`
  (base-header stamp dependency for the qmake build).
- **Machinery:** ABI layout of `Genode::Env` — exists only to repair the
  vtable shift row #28 introduced. Row #29's own failure mode (stale
  prebuilt `sponge-de` mis-dispatching `Env::close` as `try_upgrade`) is the
  D16R.15 revertability lesson.
- **Dependencies:** **paired with row #28 with a hard ordering rule:
  never drop #28 while #29 stands, never drop #29 before #28.**

### Row 30 — `74c84f9f0a` — a forwarded close must complete when the parent answers
- **Files (fork):** `repos/base/include/base/service.h`
  (`Try_parent_service::probe_at_parent` asks the parent instead of
  classifying by local phase), `repos/base/src/lib/base/child.cc`
  (`Child::session_cap` "not ready" for in-flight closes; `expire_pending_ops`
  every tick), `repos/base/include/base/child.h`,
  `repos/os/src/lib/sandbox/library.cc` + `heartbeat.h` (always-armed 50 ms
  `Pending_op_timer` replaces heartbeat-period completion).
- **Machinery:** forwarded CLOSE completion path.
- **Dependencies:** row #28's non-blocking machinery; the 50 ms
  `Pending_op_timer` completion tick from unledgered `73e5859da8`'s family.
  **Cluster warning (classification Part C):** the row's own revert list is
  `74c84f9f0a` only — a real cluster drop must also revert `73e5859da8`.

### Row 31 — `82f0143d1f` — a refused create must be resolved; the environment sessions must recover from it
- **Files (fork) — three commits, revert all three as one unit:**
  - `e0c2706225` (non-blocking forwarded create):
    `repos/base/include/base/service.h`
    (`Try_parent_service::initiate_request` CREATE path via direct
    `_env.parent().session(...)`; `Service::complete_create()` appended),
    `repos/base/include/base/local_connection.h`, `repos/base/include/base/child.h` +
    `repos/base/src/lib/base/child.cc` (`Op::Create` pending ops),
    `repos/os/src/lib/sandbox/pending_session_op.h`, `service.h`, `child.h`, `library.cc`.
  - `205b84fcad` (quota-erosion recovery):
    `repos/base/include/base/local_connection.h`
    (`Local_connection_base::reissue()` re-issues an env session with a
    bigger quota when a refused create arrives), `service.h`, `child.h`, `child.cc`.
  - `82f0143d1f` (re-issued env session must not outlive its pending op):
    `repos/base/include/base/child.h`, `repos/base/src/lib/base/child.cc`.
- **Machinery:** forwarded CREATE completion + environment-session
  (log/binary/linker) recovery — **the exact chain the FD1 uplink regression
  sits on** (driver runtime → system sandbox → top-level init → `nic_router`).
- **Dependencies:** row #28's non-blocking machinery and this row's own
  `complete_create()` completion path, introduced by `e0c2706225` (not by
  row #30). Quota erosion is chain-length function (6144 → 5334 → 4524 →
  3714 bytes at 810 B/hop). **Cluster warning:** the row's revert list omits
  `73e5859da8`; include it.

### Unledgered cluster member — `73e5859da8` — a forwarded session request must not park the container either
- **Files (fork):** `repos/base/include/base/service.h`,
  `repos/base/src/lib/base/child.cc`, `repos/base/src/lib/base/component.cc`
  (+75/−3). Extends row #28's non-blocking forwarding to the forwarded
  *session request* path; sits between #29 (`0a329d4fff`) and #30
  (`74c84f9f0a`) in history.
- **Status here:** inventory only. Classification verdict is R-T
  (classification Part B). **Named in no ledger row's revert list** — any
  cluster drop must include it (classification Part C).

**Rows #25–#31 dependency graph (compressed):**

```
#21 ──superseded──▶ #25 (quota upgrade accounting; UPGRADE_ATTEMPTS)
#24 ◀──pairs──▶ #26 (heartbeat surface)
#13 ──▶ #27 (base-sel4 untyped ladder; + #14)
#28 (ea04b06868 + 1672f3649a; non-blocking try_* + pending-op registry)
 ├──▶ #29 (Env vtable ABI repair; hard ordering with #28)
 ├──▶ 73e5859da8 (forwarded session request; unledgered)
 │     ├──▶ #30 (74c84f9f0a; forwarded close completion)
 │     └──▶ #31 (e0c2706225 + 205b84fcad + 82f0143d1f; forwarded create + env reissue)
 └──▶ all of #29/#30/#31 revert only after #28, in reverse order
```

---

## 3. Storage I/O reachable from session-create / session-serving paths

### 3.1 Topology anchor (product run wiring)

From `run/sponge-desktop-disk-uefi-usb.run` (representative product
topology; `run/sponge-desktop-disk.run` is the same shape):

- **ROM providers:** `rom_sys` (cached_fs_rom chroot `/system`),
  `rom_lib` (chroot `/system/lib`), `rom_pkg` (chroot
  `/system/pkg/falkon/payload`) — started at run:1528/1538/1561, all backed
  by `vfs_sys` File_system policies (run:1479–1481).
- **File_system providers:** `vfs_sys` (read-only ext2 over `part_block`),
  `vfs_var` (read-write ext2; policies `root: /pkgd` for the package store
  **and** `label_prefix: sponge_logdisk -> | root: /log` for the log sink,
  run:1493–1500 — this shared provider is FD4), `vfs_home`.
- **pkgd/configd channels:** Report/ROM pairs through `report_rom`
  (run:743–767), no direct RPC interfaces.
- **binary_prefix:** `+ config | binary_prefix: bin/` on the `sponge_pkgd`
  start node (run:875).

The provider chain behind every ROM create in a package launch is:
`launching child → (system sandbox) → rom_pkg/rom_sys (cached_fs_rom) →
vfs_sys (vfs) → rump ext2 → part_block → disk`. The baseline measured this
chain failing at the create: *"files: create of ROM (session 4) failed -
the provider did not answer within 60000 ms"* and the same for falkon
(`docs/evidence/phase16-runtime-baseline.md`, files/falkon RED rows).

### 3.2 sponge_pkgd — no session-serving surface; storage I/O on the bootstrap thread and (partly) in request dispatch

pkgd implements **no `Root`/`Rpc_object`** (README: "does not expose an RPC
interface"). All "serving" is Report writes relayed by report_rom. Storage
I/O sites (all `repos/sponge/src/sponge_pkgd/main.cc`):

| Site | Lines | Phase | What |
|---|---|---|---|
| Member-init ROM/Report opens | 224, 227, 244, 254, 264, 340–342, 368–370, 382, 548 | (a) | `_request_rom`, `_result_reporter`, `_runtime_reporter`, `_driver_runtime_reporter`, `_installed_reporter`, `_config_rom` — upstream session creates, init-served ROMs, **not** disk storage (comment 3600–3602) |
| `_load_index()` | 924–940 (called 3488) | (c) | `pkg_index.xml` ROM open + parse |
| `_init_store()` → `_vfs_env.construct` | 2785–2788 (called 3493) | (c) | **the only File_system session** in the component (Vfs::Root mount) |
| `_load_store()` | 2845–2930 (called 3494) | (c) | stat/open/read `/store/installed.xml` (2764) with `commit_and_wait` (2882) |
| `_init_bake()` / `_seed_roots_from_bake()` | 3032–3035 / 3049–3111 (called 3503–3504) | (c) | bake ROM reads; storage **write** via `_save_store()` at 3108–3109 |
| `_init_binary_manifest()` | 3136–3152 (called 3510) | (c) | `pkg_binaries` ROM |
| `_init_admission()` / `_init_driver_admission()` | 3438–3441 / 3462–3465 (called 3519–3520) | (c) | `runtime_state` / `driver_runtime_state` ROMs |
| `_sync_installed_from_roots()` → `_load_package` | 967–968 (called 3527) | (c) | one `pkg_<name>.xml` ROM open per resolved package |
| first `_generate_runtime_config()` + `_generate_installed_report()` | 2113 / 2387 (called 3544–3549) | (c) | Report writes (first emission) |
| initial `_handle_request()` drain | 1351, 1356 (called 3565) | (c) once | the first request's full I/O profile runs on the bootstrap thread |
| `_init_launcher_channel()` | 2812–2828 (called 3581) | (c) | launcher/tty/probe ROM opens |
| `_resolve` → `_load_package` on requests | 967–968 via 1492 (explain), 1660 (install), 1747/1795 (sync) | **(b)** | per-package ROM opens **synchronously in the request path** |
| `_read_budget` → `state.update()` | 3236 via 1711–1715, 3326–3329 | **(b)** | report-ROM refresh RPC in install/launch admission |
| `_save_store()` on install/remove | **1749, 1797** | **(b)** | **synchronous VFS write** (open 2978–2981, ftruncate 2987, write 2995–2997, `commit_and_wait` 3000, `sync` 3010–3011) inside the request handler |
| `_generate_runtime_config()` on install/remove/launch | 2078–2079 → 2113 (called 1750, 1798, 1928) | **(b)** | Report writes (both runtimes emitted together) |
| `_config_rom.update()` repro stall | 1968, 1989, 2024 | (b)/(c) | test-knob reads; 1988–1991 is a busy IPC stall, not a timer |

**Bootstrap worker (known-good pattern, §5.1):** `struct Bootstrap :
Genode::Thread` at 577–599 ("pkgd-store", 64 KiB); started 3605–3606;
`entry()` (584–588) runs `_bootstrap()` (3485–3584) then parks. Fallback
3610–3611: thread-start failure runs the whole bootstrap inline on
`env.ep()`. Hand-back: the thread *becomes* the initial dispatcher — it
drains `_handle_request()` once (3565), then arms the signal handlers
(3566–3567, 3581). No locks; exclusion by construction (bootstrap
completes before handlers are armed).

**binary_prefix rewriting** (P1.2/P0.3 input): read at 3593–3595 (ctor);
rewrite at 1103–1175 inside `_parse_package` (runs (c) at bootstrap and
(b) on request-path `_resolve`); output stays in memory
(`Package::config_xml`, 1174) and leaves via the runtime-config Report
(2310 → 2113). Composition-only sites: 2289–2290, 3171.

Stale-comment caveat: comments at 1046, 1107, 1540, 2204 say "runs on the
ep thread" — since the Bootstrap split the same code also runs on the
bootstrap thread (never concurrently; safe by construction).

### 3.3 sponge_configd — no session-serving surface; synchronous VFS write in request dispatch

configd implements **no `Root`/`Rpc_object`**. It publishes the config store
as a Report (`_broadcast_reporter`, main.cc:217) that report_rom serves as
ROM "config" to watchers. Storage I/O (all
`repos/sponge/src/sponge_configd/main.cc`):

| Site | Lines | Phase | What |
|---|---|---|---|
| `_init_store()` → `_vfs_env.construct` | 1794 (called 2121) | (a) | File_system session (persistence store) |
| `_load_store()` | 1819 stat, 1830–1831 open, 1840–1859 read + `commit_and_wait` RETRY loop (called 2122) | (a) | read `/store.xml` (2104–2107); torn-file fallback 1901–1906 |
| `_init_bake()` / `_apply_bake_defaults()` | 1643–1646 (called 2132) / 1672–1677, 1721 (called 2137) | (a) | bake ROM opens + reads |
| first `_save_store()` (bake seeding) | called 2138 | (a) | full write path (below) at construct time |
| `_config_rom.update()` / initial dispatch | 1629, 1787, 2147, 2164, 2181, 2204 | (a) | init-served config ROM; 2204 can reach `_save_store` synchronously |
| **`_do_set` → `_save_store()`** | **1548** (bake-reset path 1470–1474) | **(b)** | **synchronous VFS write in request dispatch**: tmp open 1971–1978, ftruncate 1986, write+`commit_and_wait` 1995–2000, `sync` RETRY 2010–2011, atomic `rename` 2020 (crash-consistent, comment 1962–1967) |
| `_ensure_de_channel()` | 1356, 1360 (called 1381; also at construct 2173 when the `<de_config/>` gate is present) | **(b)** (lazy path) | lazy parent ROM+Report session opens **inside a signal handler** (first DE write, ungated scenarios) |
| `_request_rom.update()` / `_de_request_rom->update()` / `_lz_model_rom->update()` | 1369 / 1383 / 1608 | (d) | ROM refresh RPCs on update signals |

Note (§5.3): configd has **no** bounded-retry ROM pattern; its RETRY loops
(1843–1850, 1994–2011) are unbounded VFS commit spin loops. The
ROM-update-signal + bounded-retry pattern lives in
`repos/sponge/src/test/partition_check/main.cc` (§5.3).

### 3.4 sponge_logdisk — the secondary-entrypoint pattern (known-good, with construct-time I/O)

logdisk serves LOG sessions (`Sponge::Session : Rpc_object<Log_session>`,
main.cc:156, `write()` 172–179; `Sponge::Root : Root_component<Session>`,
main.cc:183, `_create_session` 193–199). Sites (all
`repos/sponge/src/sponge_logdisk/main.cc`):

| Site | Lines | Phase | What |
|---|---|---|---|
| `_create_session` / `Session::write` | 193–199 / 172–179 | secondary ep (RAM-only) | label parse + heap alloc; format into RAM backlog (`log_line` 571–610, append 327–352, signal 609). **Zero I/O on the serve path by design** |
| `_config_rom.update()` | 301 (called 637) | (a) | config ROM read |
| `_vfs.construct` | 645 (try-block 642–649) | (a) | **File_system session open — blocks until vfs_var mounts ext2fs** (comment 51–59, 621–624) |
| `_rotate_existing()` | 381 stat, 392 rename, 408–411 open/ftruncate (called 654, also (d) at 448) | (a)/(d) | rotate prior boot's `/boot.log` |
| `_open()` | 426–429 open, 434 stat (called 655, also (d) at 449) | (a)/(d) | open `/boot.log` on the policy root `/log` |
| `_boot_header()` → `_emit()` | 458–460 write, 463 `commit_and_wait`, 482–483 `sync` (called 656) | (a) | boot header write at construct |
| `_emit()` from drain | 441–484 (via `handle_work` 692, `handle_tick` 695–703, `_drain` 514–554) | (d) | batched writes on the main entrypoint |
| `_timer.construct` | 670 | (a) | Timer session (blocking if unrouted; only when `tick_ms` ≠ 0) |

**Secondary entrypoint (known-good pattern, §5.2):** `Genode::Entrypoint
_root_ep { _env, 16*1024, "logsink", … }` at 225–226 (the public Entrypoint
constructor starts its own RPC + signal-proxy threads); `Root` managed on it
(227) and announced at 640 — deliberately not `env.ep()`, so init's LOG
`create` RPC never runs on the thread blocked in the File_system request
(comment 627–633). All filesystem I/O is confined to the main entrypoint
handlers (273–276). Documented at `docs/17-runtime-component-topology.md:723`
and README.md:57–84.

### 3.5 ROM/VFS/provider call paths (Genode upstream, fork tree)

| Site | Location | Create-path behavior |
|---|---|---|
| `cached_fs_rom` `handle_session_request` | `genode/repos/os/src/server/cached_fs_rom/main.cc:340–395` | on a new ROM session: `try_open` + `fs.status` **synchronously** (359–361), cache eviction under quota pressure (365–375), `Cached_rom` allocation (377), sealed → `deliver_session_cap` (380–382); unsealed → `try_open` + `Transfer` (385–393) with the bounded "retry when next pending transfer completes" (393) |
| `vfs` `Root::_create_session` | `genode/repos/os/src/server/vfs/main.cc:748–837` | policy `root` attribute required (803–808); label last-element must start/end with `/` (819–831 — the `vfs_log` denial class in `docs/evidence/logdisk-field-diagnosis.md` §3); `_vfs_env.fs().directory(...)` stat in the create path (836–837) |
| forwarded CREATE (rows #28–#31) | `genode/repos/base/include/base/service.h` (`Try_parent_service::initiate_request`, `complete_create`), `genode/repos/base/src/lib/base/child.cc`, `genode/repos/base/include/base/child.h:380–462` (pending-op registry), `genode/repos/base/include/base/local_connection.h:137` (`reissue`) | non-blocking forward + `Pending_op_timer` completion + 5 s deadline |
| quota upgrade (row #25) | `genode/repos/os/src/lib/sandbox/child.cc:772` (`Sandbox::Child::resource_request`), `genode/repos/base/include/parent/parent.h` (`UPGRADE_ATTEMPTS`) | bounded upgrade retry accounting |
| heartbeat classification (rows #24/#26) | `genode/repos/os/src/lib/sandbox/child.h:213–222` (`startup_grace_ms`), `child.cc:389–401` (`heartbeat_state`), `heartbeat.h` | lifecycle surface the create chain's children ride on |

These sites are **Genode upstream/fork-tree machinery**. They already carry
their patches (rows #25–#31). Nothing in P3.3 edits them; see §6 for what
would be a NEW base patch.

---

## 4. Candidate offenders and their P3.3 fix pattern

P3.3 (plan) removes storage I/O from session-create paths: *"creates decide
via the pure core and hand actual I/O to the existing bootstrap/worker
thread pattern. Uses only the row #28–#31 non-blocking machinery (D16R.9)."*
"Fix pattern" below means the pattern P3.3 applies **when it reaches the
site** — this audit schedules nothing and edits nothing.

| # | Offender | Where | Why it is on a create/serve path | P3.3 fix pattern | Verification scenario |
|---|---|---|---|---|---|
| O1 | pkgd `_save_store()` synchronous VFS write in request dispatch | `sponge_pkgd/main.cc:1749` (install), `1797` (remove) | the install/remove answer includes a full write+sync (open 2978–2981 … sync 3010–3011) before the broadcast; a slow vfs_var stalls the answer and the pkgd heartbeat | decide in the pure core; queue the write to the `Bootstrap` worker (the existing pattern, §5.1) and answer after handoff, or after the worker confirms; never write on `env.ep()` | P3.1 create-half probes; product scenarios green (`sponge-pkg-lifecycle`, `sponge-pkg-driver`, `sponge-desktop-disk`) |
| O2 | pkgd per-package ROM opens in request dispatch | `sponge_pkgd/main.cc:967–968` via 1492/1660/1747/1795 | `_resolve` → `_load_package` opens `pkg_<name>.xml` ROM sessions synchronously while answering explain/install/remove — each is a create down the rom_sys/cached_fs_rom chain (the 60 s provider-stall class) | cache metadata at bootstrap; refresh via ROM-update signals with the §5.3 bounded-retry pattern; the pure core decides on cached data | P3.1 create-half probes; `sponge-config-authority.run` RED/GREEN accounting unchanged |
| O3 | pkgd admission ROM refresh in dispatch | `sponge_pkgd/main.cc:3236` via 1711–1715, 3326–3329 | `state.update()` report-ROM RPC inside install pre-flight and launch admission | read the budget from the last signal-driven snapshot (update handler + bounded retry, §5.3); no `.update()` on the answer path | P3.1 create-half probes; `sponge-pkg-lifecycle`, `sponge-pkg-driver` |
| O4 | configd `_save_store()` synchronous VFS write in `_do_set` | `sponge_configd/main.cc:1548` (and 1474, 2138) | every successful `set` writes tmp+sync+rename (1970–2023) synchronously in the request signal handler; the same entrypoint answers the DE channel | same as O1: hand the write to a worker (logdisk-style deferred writer), keep the crash-consistent tmp+rename semantics inside the worker | P3.1 create-half probes; `sponge-configd-persist.run`, `sponge-configd-persist-corrupt.run` behavior unchanged |
| O5 | configd lazy session opens inside signal dispatch | `sponge_configd/main.cc:1356, 1360` via 1381 (`_ensure_de_channel`; construct-time call at 2173 when gated) | in ungated scenarios the first DE write opens parent ROM+Report sessions **in the handler**; a session request there can block (denied-request semantics are ambiguous in-tree — logdisk header 78–81 says blocks; configd expects exceptions at 1653–1664) | open the channel at bootstrap like the vct channels (main.cc:2146–2147 pattern), or move the lazy open to the worker; a create must never be initiated from a dispatch handler | P3.1 create-half probes; `sponge-de-settings*` scenarios |
| O6 | logdisk construct-time blocking I/O | `sponge_logdisk/main.cc:645` (File_system open), 654–656 (rotate/open/boot header) | construct blocks until vfs_var mounts — the entrypoint is occupied (mitigated: the secondary entrypoint keeps LOG serving; documented, deliberate) | **not a P3.3 target** — the serve path is already RAM-only (§5.2). Record as known-good-with-caveat; if the construct block ever matters, the fix is a worker handoff of the open, not a base patch | `sponge-desktop-disk-uefi-usb` with `SPONGE_LOGDISK=on` still writes `/log/boot.log` (P3.2 gate) |

Shared-provider note (FD4, owned by P3.2 not P3.3): `vfs_var` serves both
the package store (`root: /pkgd`, run:1499) and the log sink
(`label_prefix: sponge_logdisk ->`, run:1287/1500) — a slow/denied log
session can starve package-store creates. The plan's fix is path separation
(`vfs_pkg` / `vfs_log`, P3.2 `fix(vfs)` commits); no base patch involved.

---

## 5. Known-good patterns (reference implementations)

### 5.1 pkgd `Bootstrap` worker
`sponge_pkgd/main.cc:577–599` (thread), 3485–3584 (all storage I/O ordered
behind it: index → store init → store load → bake → binary manifest →
admission → installed rebuild → first emission → drain → arm handlers),
3605–3611 (start + inline fallback). Rationale comment 552–576: a blocking
File_system request on `env.ep()` wedges the entrypoint. Hand-back by
becoming the initial dispatcher (3565–3567), not by shared state. **This is
the template P1.3/P3.3 generalize.**

### 5.2 logdisk secondary entrypoint
`sponge_logdisk/main.cc:225–227` (root on its own `Entrypoint`), 640
(announce via `_root_ep.manage`), 571–610 (RAM backlog + signal), 514–554
(drain on the main entrypoint), invariants in README.md:57–84 and
`docs/17-runtime-component-topology.md:723`. Serve path: RAM only; all FS
I/O on one designated entrypoint. **This is the template for any component
that must keep serving while its storage side may block.**

### 5.3 ROM update signal + bounded retry
`repos/sponge/src/test/partition_check/main.cc`: `MAX_UPDATE_RETRIES = 64`
(71), `Signal_handler _update_handler` (99), `.sigh(_update_handler)` (114),
`_check()` in the constructor (115) and on every update signal, retry bound
at 136–138 and 204 (`if (_retries++ < MAX_UPDATE_RETRIES) return;`).
Origin: `docs/evidence/task-2-phase12-storage.md` "Fix 4" — the
synchronous first read saw an empty report_rom module; the signal+bound fix
made the probe tolerate the empty-first-publish. **This is the template for
O2/O3 refreshes.** (Absent from configd today — configd's own RETRY loops
at 1843–1850/1994–2011 are unbounded VFS commit spins, not this pattern.)

---

## 6. Sites that would require a NEW base patch — STOP/escalate

Per D16R.9 ("no new base semantics") and AGENTS.md §5.2, the following are
**not tasks**. If a future need lands here, stop and escalate to the
maintainers before any code is written.

| Site | Location | Why a new base patch | STOP condition |
|---|---|---|---|
| S1 | `cached_fs_rom` synchronous `try_open`/`fs.status` in `handle_session_request` (upstream `os/src/server/cached_fs_rom/main.cc:359–361`) | making the provider answer ROM creates without touching the File_system synchronously means changing an upstream server's create contract — no ledger row covers it | STOP/escalate |
| S2 | `vfs` `Root::_create_session` create-path stat + label rule (upstream `os/src/server/vfs/main.cc:819–837`) | the `/`-rooted label rule and the directory stat are upstream provider semantics; "fixing" the `vfs_log` denial class here would be a new upstream patch (the Sponge-side fix is config spelling, as `logdisk-field-diagnosis.md` §3 shows) | STOP/escalate |
| S3 | any further change to `Env`/`Parent` protocol, `Try_parent_service`, the pending-op registry, or `Genode::Child` beyond rows #28–#31 + `73e5859da8` | the session-machinery cluster is exactly the young, regression-bearing set FD5 guards; one open regression (§7) sits on it | STOP/escalate — disposition via P4.2 first (D16R.17) |
| S4 | `Root_component` default `create_session`/`upgrade`/`close` semantics | upstream base; the defaults are what every component inherits | STOP/escalate |
| S5 | seL4 / base-sel4 untyped or mapping paths (row #27 family) | kernel patch territory; drop/replace is governed by the classification matrix, not this phase | STOP/escalate |

---

## 7. Open NIC uplink regression (documented; not root-caused here)

Per D16R.17 and the task's constraint, this audit records the regression and
claims **no cause**:

- `run/sponge-pkg-driver` fails at its DHCP gate today: the driver package
  comes up (`driver-probe: PASS`), but the uplink session crossing
  driver runtime → system sandbox → top-level init → `nic_router` never
  completes; the pre-change run reached `dhcp offer from 10.0.2.2`
  (`docs/evidence/forwarded-create-starvation.md` §7).
- It is "a regression of the create chain around ledger row #31"
  (plan D16R.17); the fork-commit chain under capture is
  `74c84f9f0a` → `e0c2706225` → `205b84fcad` → `82f0143d1f`.
- What §7 already states and this audit repeats only as bounds: no create
  expires, no session is denied, no pending operation is left behind; the
  rejected upgrade-wedge fix broke the e1000 path and was reverted
  (`docs/evidence/sponge-upgrade-wedge.md`, net-zero pair `500cb2cc8a` +
  `3df26cf22b`).
- Disposition (fixed-in-place / revert / replace) is P4.2's alone; nothing
  in the create chain may be pruned before it exists.

---

## 8. Honest limits of this audit

- The I/O inventory is source-trace based (file:line reading); no scenario
  was run (task constraint). Phase labels (a)–(d) come from control-flow
  reading; the pkgd static-buffer comments (1046/1107/1540/2204) are stale
  about which thread runs `_load_package` (bootstrap or ep, never
  concurrently).
- Denied/unrouted parent-session request semantics are ambiguous in-tree
  (blocking per `sponge_logdisk/main.cc:78–81`; exception-based per
  `sponge_configd/main.cc:1653–1664`). Marked uncertain wherever it
  matters (O5).
- Row #27 is inventoried for cluster completeness; nothing in this phase
  touches base-sel4 files.
- The provider-chain failure seen at ROM creates in the baseline
  ("create of ROM … the provider did not answer within 60000 ms") is
  recorded as measured behavior of the audited chain (§3.1); this audit
  assigns it no cause and no fix.
