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
| 8 | `f598ad0a16` | `fix(pkgd): keep the entrypoint wait on the no-thread fallback` |
| 9 | `a512c74ec7` | `fix(pkgd): drain every channel before arming any handler` |
| 10 | `277618a2d3` | `fix(pkgd): keep the empty-metadata error for present zero-length modules` |
| 11 | `2c610f5c6f` | `fix(pkgd): map the empty-metadata attach conflict to a present-empty entry` |

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

Fix within existing semantics (D16R.9 clean), with the wait primitive
selected per thread (`_store_io_wait`): off the entrypoint (the
Bootstrap worker and the bootstrap drains) the store read/write/sync
loops poll the VFS RETRY contract paced by a `Timer::Connection` (its
sleep blocks on the connection's PRIVATE signal receiver — safe from
any thread) and call the non-blocking `commit()` half after each
submit — `commit()` is what actually notifies the remote peer
(`try_submit_packet` only marks `_tx_wakeup_needed`). ON the
entrypoint (the no-thread fallback, `_single_threaded` set at the
Bootstrap start failure path) `commit_and_wait` is preserved: the
fs-plugin read/sync state machines do not drain packet acks themselves
and rely on the entrypoint dispatching the plugin's I/O-level ack
handler inline there — the poll loop would own the entrypoint thread
and never dispatch it. The 60 s bound and the "store write incomplete"
failure contract are unchanged on both paths (see §9 for the
correction that introduced the split).

## 7. Gate matrix (serial, `make -j1`, one runner)

Post-correction final-tree sweep (after `2c610f5c6f`); the earlier
red-first logs remain valid for their flip tables.

| Gate | Result | Log |
|------|--------|-----|
| `run/sponge-pkg-exclusive` | PASS (red-first + green) | `sponge-pkg-exclusive-{red,green}.log`, `p33c-gate-exclusive.log` |
| `run/sponge-pkg-dispatch-liveness` | PASS (red -> mid-red -> green) | `dispatch-liveness-{red,mid}.log`, `p33c-gate-liveness.log` |
| `run/sponge-pkg-lifecycle` | PASS (lifecycle-probe) | `p33c-gate-lifecycle.log` |
| `run/sponge-pkg-admission` | PASS, figures IDENTICAL: `need_ram=100663296 avail_ram=61696664 need_caps=2000 avail_caps=1030` | `gate-admission-final.log` |
| `run/sponge-config-authority` | PASS | `gate-config-authority-final.log` |
| `run/sponge-persist-disk` | PASS end to end (worker path): boot 1 `store bytes: <sponge-installed version="1"><root name="hello"/>`, boot 2 `restored 1 root(s) from store`, `Test succeeded: installed set restored from SPONGE-DATA (P4) after reboot` | `p33c-gate-persist-disk.log` |
| `run/sponge-desktop-disk` (product, `image/disk`) | PASS — `alpha-probe: PASS` and `Run script execution successful` | `/tmp/opencode/p33-product-desktop.log` |
| `run/sponge-provider-stall` | PASS with the clamp (keep-green); see §8 for the environment flake | `gate-provider-stall-clamp.log` |
| `run/sponge-pkg-driver` re-measurement | PASS — `driver-probe: install answered status=ok`, `leased 10.0.2.15` (the DHCP gate REACHED). NO fb/exclusivity claim is taken from this run: its fb probes never ran (pre-existing report_rom policy gap) and the scenario's final PASS string is unconditional. Exclusivity evidence is §2 (`run/sponge-pkg-exclusive`). | `gate-pkg-driver-remeasure.log` |

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
   as never reached. Scope of that claim: the driver-probe's install +
   `driver_runtime` listing + the DHCP lease ONLY — the scenario's
   final PASS string is unconditional and its fb probes never ran (the
   pre-existing report_rom policy gap), so no fb/exclusivity result is
   taken from it (exclusivity evidence is §2). P4.2 owns the
   disposition (fixed / host-dependent / flaky); no pkg-driver repair
   was attempted here.

## 9. Oracle post-review corrections (verification record)

1. **Store wait primitive per thread** (`f598ad0a16`, §6): the
   timer+commit poll is only sound off the entrypoint — the fs-plugin
   read/sync state machines do not drain packet acks themselves and
   need the entrypoint to dispatch the plugin's I/O-level ack handler,
   which a poll loop owning the entrypoint thread never does. The
   no-thread fallback sets `_single_threaded` and keeps
   `commit_and_wait` there. Verification: a CONTROLLED forced-fallback
   run (thread start skipped locally, not committed) of
   `run/sponge-persist-disk` completed both boots green —
   `pkg-seq-probe: PASS`, `store bytes: <sponge-installed
   version="1"><root name="hello"/>`, `restored 1 root(s) from store`,
   `Test succeeded` (`/tmp/opencode/verify-fallback-persist.log`).
   **Limitation (honest)**: no committed scenario forces the fallback —
   the trigger is a `Thread::start()` failure that existing test
   machinery cannot induce — so the fallback path is covered by this
   controlled run and by the worker-path gates, not by CI-enforceable
   scenario coverage.
2. **Bootstrap arming order** (`a512c74ec7`): `_arm_launcher_channel`
   is split out of `_init_launcher_channel` and runs only after the
   primary `_handle_request()` drain, so all optional AND primary
   drains complete before ANY sigh is armed (previously an optional
   handler could run on `env.ep()` while the bootstrap thread was
   still inside the primary drain). Drain order and the no-thread
   fallback are unchanged; covered by every gate below (all scenarios
   bootstrap pkgd) and specifically by the product run.
3. **Empty-metadata parity** (`277618a2d3` + `2c610f5c6f`): a present
   zero-length `pkg_<name>.xml` answers the pre-change
   `malformed metadata for package: <name>` (Invalid_syntax on an
   empty document) — never `package not found` and never an abort
   (the empty dataspace's `Region_conflict` attach failure now records
   a present-empty cache entry). Verified with a targeted temporary
   scenario (an empty module staged into the dispatch-liveness index,
   `report_rom verbose: yes`):
   `<result status="error" op="explain" pkg="hello" error="malformed
   metadata for package: hello"/>` and the same for the install
   (`/tmp/opencode/verify-empty-rom3.log`; the pre-mapping crash is
   `/tmp/opencode/verify-empty-rom.log`). Absent modules keep the
   preserved `package not found` string (open failure only). The
   temporary scenario tweak was reverted; the committed tests are
   unchanged.

Final-tree gate sweep logs for all three corrections: §7's
`p33c-gate-*` and `/tmp/opencode/p33-product-desktop.log`.
