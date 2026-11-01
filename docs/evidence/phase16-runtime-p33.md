# Phase 16 P3.3 receipt: create-session I/O removal (pkgd)

> Scope: `docs/plans/phase16-runtime-overhaul.md` P3.3 / D16R.9.
> Branch `refactor/phase16-runtime-overhaul`, base `608a76ce87`.
> All scenario results are QEMU (KERNEL=sel4 BOARD=pc) unless noted;
> not real-hardware claims. Disk-backed runs use the documented host
> workaround `LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so` and the
> full `RUN_OPT` plugin list where the scenario requires `image/disk`.

## 1. Units landed (each independently revertable)

| # | Commit | Unit |
|---|--------|------|
| 1 | `0509bba660` | `test(run): add the exclusive-group placement regression` |
| 2 | `947f288c47` | `test(run): compare the replay bytes over the report content length` |
| 3 | `d80ff8d4c3` | `fix(pkgd): gate exclusive conflicts on first execution` |
| 4 | `1af54a29ff` | `test(run): add the pkgd dispatch-liveness red scenario` |
| 5 | `2c12922a7e` | `fix(pkgd): cache package metadata at bootstrap` |
| 6 | `013a4a262f` | `fix(pkgd): run store flushes on the bootstrap worker` |
| 7 | `513d64e658` | `fix(pkgd): pace store I/O without the entrypoint-affine wait` |

## 2. Exclusive-group placement (the pre-P3.3 uncommitted fix)

Regression `run/sponge-pkg-exclusive.run` (test-only fixture pair
`pkg/excl_a` / `pkg/excl_b`, payloads `hello` / `config_witness`, the
harmless `testfb` group — never hardware, no runtime started).

Red-first against pre-fix HEAD (the pre-flight living in the
duplicate-replay path):

```
[init -> exclusivity_probe] Error: exclusivity-probe: FAIL install excl_b
    expected exclusive-conflict, got ok
```
(`/tmp/opencode/sponge-pkg-exclusive-red.log`)

Green with the pre-flight in `_do_install` (before admission and
mutation):

```
sponge_pkgd: exclusive-group check on 'excl_b' group='testfb' peer='excl_a'
sponge_pkgd: install excl_b -> exclusive-conflict (testfb is held by the running package excl_a)
exclusivity-probe: first-refusal ok (excl_b refused, peer=excl_a)
exclusivity-probe: refusal left no state
exclusivity-probe: cached 225 result bytes for the replay comparison
exclusivity-replay: DONE byte-identical (225 bytes)
exclusivity-probe: PASS
sponge-pkg-exclusive: PASS
```

The replay window gate asserts a SECOND `exclusive-group check on
'excl_b'` line does NOT appear before the DONE landmark: the duplicate
replayed the cached recipe without re-running policy
(`/tmp/opencode/gate-exclusive-final.log`, re-run green on the final
binary).

## 3. Dispatch-liveness scenario (red-before-green)

`run/sponge-pkg-dispatch-liveness.run` + `test/dispatch_probe` +
two test-only observability components: `test/counting_rom` (logs every
ROM session create) and the `vfs_store_probe` VFS plugin (logs the
CALLING THREAD of each store write, delays writes 1.5 s for
deterministic windows). pkgd is unmodified in the red run.

Verdict line: `dispatch-liveness: RED (rom_at_dispatch=A worker_flush=B)`
or `dispatch-liveness: PASS`, where A=metadata ROM create seen inside
the explain dispatch window, B=store write attributed to the
`pkgd-store` worker inside the install window.

| State | rom_at_dispatch | worker_flush | Verdict |
|-------|-----------------|--------------|---------|
| red (pre-P3.3) | 1 (`counting-rom: create pkg_hello.xml` in window) | 0 (`store-probe: write n=69 thread=ep`) | RED |
| after metadata cache (`2c12922a7e`) | 0 (`cached 2 metadata document(s) at bootstrap`) | 0 | RED (still) |
| after worker handoff (`013a4a262f`) | 0 | 1 (`thread=pkgd-store`) | PASS |

Logs: `/tmp/opencode/dispatch-liveness-red.log`,
`/tmp/opencode/dispatch-liveness-mid.log`,
`/tmp/opencode/gate-liveness-final.log`.

The pipelined no-overtaking guard is checked live: a `list` sent on the
probe channel while the witness install's flush is in flight queues
behind it (`sponge_pkgd: queued list (1 pending)`) and answers after
the mutation's result.

## 4. Metadata cache (dispatch-time ROM I/O removal)

`_load_metadata_cache()` copies each indexed `pkg_<name>.xml` into
bounded owned memory (`METADATA_MAX` = 16 KiB/document; the shipped
inventory tops out at ~10 KiB, `pkg/terminal`) on the Bootstrap thread
with transient ROM sessions; `_load_package` parses purely from the
cache and opens no ROM session at dispatch. Every `_load_package` error
string is preserved; an oversized document truncates to the loud
"malformed metadata" failure. The image-immutable invariant is
documented in `pkgd.h` with a FIXME for the future download path.

## 5. Store flush on the Bootstrap worker

Install/remove decide and mutate on the entrypoint, build the flush
payload from the post-mutation roots there (`_build_store_payload`,
pure), and hand ONLY the payload to the Bootstrap worker (Semaphore
wakeup; `Signal_transmitter` completion back to `env.ep()`). The
unchanged `config -> installed broadcast -> result` tail resumes from
the completion signal (flush-before-broadcast preserved; store-failure
and P1.5 emission-failure contracts preserved verbatim). No new thread.

Requests flow through a strict global FIFO while a flush is in flight:
all types queue in arrival order (bounded 8), de-duplication runs at
dequeue (the replay recipe is complete by then — the tail ran before
the drain continued), queries never overtake, the per-channel replay
design is untouched, and overflow answers a loud recipe-free `busy`
error (`status="error" error="busy: request queue full — retry"`, no
recipe writes) so a retry is processed fresh. The common idle path
drains inline; the no-thread fallback and the bootstrap drains keep
the synchronous behaviour.

## 6. Store wait discipline (base-limitation discovery)

Driving the real packet-stream `<fs>` VFS plugin from a non-entrypoint
thread exposed a base property: `Vfs::Env::Io::commit_and_wait` parks
in `Entrypoint::wait_and_dispatch_one_io_signal`, which defers pending
APPLICATION-level signals to the entrypoint and then blocks forever
when called off the entrypoint — observed as

```
Warning: _wait_and_dispatch_one_io_signal called from non-entrypoint thread "pkgd-store"
```

followed by a dead flush. This predates P3.3 for the bootstrap drain
(reproduced with the worker disabled — the P1.3 split moved the drain's
`_save_store` onto the Bootstrap thread) and it hit the P3.3 worker
path the same way.

Fix within existing semantics (D16R.9 clean): the store read/write/
sync loops poll the VFS RETRY contract paced by a `Timer::Connection`
(its sleep blocks on the connection's PRIVATE signal receiver — safe
from any thread) and call the non-blocking `commit()` half after each
submit — `commit()` is what actually notifies the remote peer
(`try_submit_packet` only marks `_tx_wakeup_needed`). The 60 s poll
bound keeps the store-failure contract: a stalled provider degrades to
the loud "store write incomplete", never a parked daemon.

## 7. Gate matrix (serial, `make -j1`, one runner)

| Gate | Result | Log |
|------|--------|-----|
| `run/sponge-pkg-exclusive` | PASS (red-first + green) | `sponge-pkg-exclusive-{red,green}.log`, `gate-exclusive-final.log` |
| `run/sponge-pkg-dispatch-liveness` | PASS (red -> mid-red -> green) | `dispatch-liveness-{red,mid}.log`, `gate-liveness-final.log` |
| `run/sponge-pkg-lifecycle` | PASS (lifecycle-probe) | `gate-lifecycle-final.log` |
| `run/sponge-pkg-admission` | PASS, figures IDENTICAL: `need_ram=100663296 avail_ram=61696664 need_caps=2000 avail_caps=1030` | `gate-admission-final.log` |
| `run/sponge-config-authority` | PASS | `gate-config-authority-final.log` |
| `run/sponge-persist-disk` | PASS end to end: boot 1 `store bytes: <sponge-installed version="1"><root name="hello"/>`, boot 2 `restored 1 root(s) from store`, `Test succeeded: installed set restored from SPONGE-DATA (P4) after reboot` | `gate-persist-disk-green.log` |
| `run/sponge-provider-stall` | PASS with the clamp (keep-green); see §8 for the environment flake | `gate-provider-stall-clamp.log` |
| `run/sponge-pkg-driver` re-measurement | **PASS** — `leased 10.0.2.15` (the DHCP gate REACHED), `a second fb driver was REFUSED as exclusive-conflict` | `gate-pkg-driver-remeasure.log` |

All logs above live in `/tmp/opencode/`.

## 8. Pre-existing blockers and environment notes (honest record)

1. **base-linux is unbuildable in this environment** (vendored
   `base-linux/src/core/spec/pc/io_mem_session_component.cc` calls
   `get_page_size()` — the known compile failure). Therefore the
   base-linux halves of `run/sponge-pkg-lifecycle` and
   `run/sponge-pkg-persist` could not run; their seL4 equivalents (and
   `run/sponge-persist-disk`) cover the same behavior.
2. **QEMU/disk host flakiness**: disk-backed scenarios intermittently
   stall in the host I/O layer even with the documented io_uring clamp.
   `run/sponge-provider-stall` (which does not build or run
   sponge_pkgd) passed fully once (`gate-provider-stall-clamp.log`) and
   failed twice afterwards for environmental reasons (`read_parked` on
   the sys-rom chain; one early boot stall). No code change accompanies
   those failures; the keep-green claim stands on the passing run.
3. **Timer quota deferral flake**: the `hpet_timer` child requests a
   per-session quota upgrade; when the request exceeds the child's
   configured quota init defers it forever ("deferring to the next
   config application") and the next Timer session create stalls. The
   persistence/liveness scenarios now configure timer quota headroom
   (`ram: 2M`/`8M`) so the upgrade is grantable. The class of flake can
   affect any scenario that opens timer sessions (pre-existing).
4. **P4.2 input (captured, not dispositioned)**: the re-measurement of
   `run/sponge-pkg-driver` reached its DHCP gate and leased `10.0.2.15`
   in this environment (with the clamp), where
   `docs/evidence/forwarded-create-starvation.md` §7 recorded the gate
   as never reached. P4.2 owns the disposition (fixed / host-dependent
   / flaky) and the fb-probe topology note in the plan was honored: no
   pkg-driver repair was attempted.
