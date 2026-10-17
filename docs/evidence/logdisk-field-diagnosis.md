# Persistent log on the writable partition — what works, what does not

Date: 2026-10-17
Design authority: `docs/17-runtime-component-topology.md` §5 (field
diagnosis), `docs/14-boot-storage-architecture.md` §4.3 (three-area
layout), AGENTS.md §1.2 (minimum privilege) / §5.3 (fail loud)

## 1. The question

The LG gram has no serial port, and a concurrent fb console is ruled
out by the user's design rule (`docs/17` §4). When the desktop freezes
during a scroll storm, there is therefore **no record of what the
machine was doing**: the only stream that could tell us terminates in
`Core::Core_log -> Kernel::log -> the seL4 boot UART`
(`repos/base/src/core/core_log.cc`,
`repos/base-hw/src/core/kernel_log.cc`).

Question: can a component log stream be made durable on the writable
partition, so that a freeze plus a hard reset leaves evidence?

## 2. What was built and measured

`repos/sponge/src/sponge_logdisk` — a LOG server that writes to a file
on a `File_system`:

- size-capped (`max_bytes`, default 8 MiB) with exactly **one** previous
  boot kept at `<path>.1`; SPONGE-VAR is 64 MiB and shares a filesystem
  with pkgd's store, so an uncapped log is a filesystem-full event;
- **per-boot files**: an existing file is renamed aside *before* a fresh
  one is opened, so a boot never overwrites the previous boot's
  evidence;
- eager: one `Vfs_handle::write` + `sync()` per drained batch, result
  awaited;
- a periodic tick line, so a freeze shows up as a **gap between two
  ticks** rather than as an absent log;
- a 256 KiB RAM backlog, so lines emitted while the `File_system`
  session is still being resolved are not lost;
- ANSI colour escapes stripped, so the file is plain text and greppable;
- lines prefixed `<monotonic ms> [init -> … -> child] message`;
- minimum privilege: one `File_system` (vfs_var, policy root `/log`)
  and the `LOG` sessions it serves. A `Timer` session is opened only
  when `tick_ms` is non-zero.

### Receipts

| Receipt | Result |
|---|---|
| `./tool/build run sponge-desktop-disk-uefi-usb` with `SPONGE_LOGDISK=on` | **PASS** — `UEFI USB-stick media boots on OVMF and the usb_block storage chain serves /system from the stick (DE started)`; all three structural gates green; `persistent log sink ON — SPONGE-VAR /log/boot.log (+ .1)`; **no** `File_system` denial in the serial log, i.e. the sink's session on vfs_var was granted and the file was opened |
| the same scenario with the sink wired and the sink's `File_system` requested with the label `vfs_log` | **FAIL (bounded)** — `[init -> sponge_logdisk] Error: stop because parent denied File_system-session: label="vfs_log"`. This is what pinned the vfs policy spelling (§3) |
| the same scenario, sink issuing its `File_system` request from the LOG-serving thread | **FAIL — boot deadlock.** Serial stops after `[init -> acpi] XSDT OEM 'BOCHS '`; `report_rom` (the report hub every driver depends on) is stuck behind its own `log()` call waiting for the sink, which is itself blocked on the storage chain. This is the finding that produced invariant (A) in the component header |

## 3. Two non-obvious findings

**(a) A LOG provider must not issue its `File_system` request on the
thread that serves LOG writes.** Opening the session blocks until
`vfs_var` has mounted ext2fs; `report_rom` logs during early boot, so
the first logger deadlocks against the storage chain. The fix is a
secondary `Genode::Entrypoint` for the LOG root: sessions dispatched
there only format into RAM and submit a signal, while the main
entrypoint blocks on the filesystem.

**(b) The root must be managed on its own entrypoint, not `env.ep()`.**
init creates a LOG session by invoking the root's `create` RPC, which is
dispatched on the entrypoint the root is managed on. On `env.ep()` that
RPC runs on the very thread blocked in the `File_system` request. The
same shape appears upstream in
`genode/repos/os/src/test/event_filter/main.cc:215`.

**(c) The vfs policy must match a label SUFFIX.** The vfs server requires
the label's last element to start and end with `/`
(`os/src/server/vfs/main.cc:820-833`) and applies it as a root offset
under the policy's `root`; and init prefixes a routed session label with
the requesting child's name. So `<fs label="vfs_log"/>` is denied twice
over. The working spelling is a `/`-rooted label plus a suffix policy:

```
+ fs | name: log | label: /var/            # client
+ policy | label_suffix: /var/ | root: /log | writeable: yes   # server
```

with `/log/var` pre-created on the medium (the session root is resolved
at session-creation time, so the directory must exist on a freshly
flashed stick).

## 4. Why it is OFF by default

Measured, not assumed.

**Coverage.** init evaluates a child's **own** `<route>` for that
child's session requests (`base/include/base/child.h:628` hands every
child a LOG env session, routed with the child's policy). A Tier-0
`default-route` therefore only reaches the Tier-0 children that carry
no `<route>` node — `report_rom`, `nitpicker`, `timer`. Reaching the
sink from the nested `system` sandbox needs the sink to be a child of
*that* sandbox, and that was measured to be impossible with the current
media layout:

```
Error: stop because parent denied File_system-session: label="/var/"
```

for every route shape tried from inside the nested sandbox
(`+ parent | + any-child`, `+ any-child` alone, an explicit
`+ service File_system | + child vfs` rule on the *outer* sandbox's
route). The nested sandbox's children cannot obtain a `File_system`
from a service provided by a sibling of the outer sandbox, and that is
exactly what the log sink needs.

So the components a scroll-storm freeze implicates — `pkgd`, `configd`,
`themed`, `wm`, `layouter`, `decorator`, `sponge-de`, the package
runtimes — are **not** captured today.

**Coupling.** Every component whose LOG routes to the sink cannot start
before the sink announces its service (init leaves a child `STUCK` with
an incomplete env, `os/src/lib/sandbox/child.cc:40-49`). A sink that
captures a tenth of the stream and is a boot dependency for it is a bad
trade. Default-ON is deferred to the follow-up named in `docs/17` §5.

**Core's own log is not capturable at all.** core writes it to the
kernel's log memory and the serial line; `sponge_logdisk` is a userspace
component above it. The kernel-phase and core-phase log stays a
serial/`SPONGE_FBCON=on` concern.

## 5. The red→green proof: attempted, blocked

`run/sponge-logdisk-wedge.run` was written and exercised against this
component: two boots against one image, `test/ep_wedge` wedges in boot 1,
boot 2 rotates the log, and the assertions are made on the bytes the
**host** reads off the image with `e2cp` (not through any guest channel).
It does **not** ship, because it is not green, and the repository
convention is that a scenario in `run/` passes. What it established on
the way, in order, is worth keeping:

1. **A stale boot image silently invalidates a two-boot scenario.** The
   build directory's default QEMU include list carries `image/iso`, and
   `power_on/qemu` prefers the ISO over the disk image; the ISO is only
   rebuilt when the run directory changes, so boots ran against a
   previous run's `image.elf`. Two source revisions of the sink produced
   **byte-identical** guest output until `image/iso` was dropped from
   the include list. Any two-boot scenario here must drop it.
2. **`run_power_cycle` must not be called before a two-boot sequence.**
   It spawns an extra QEMU against the same image to exercise the
   power-cycle path and leaves it holding the image's write lock; the
   real boot then fails with QEMU's `Failed to get "write" lock`.
   `run/sponge-persist-disk.run` drives two boots the same way — straight
   into `run_genode_until`.
3. `mke2fs` in this environment exits non-zero on success and writes its
   block-allocation progress to stderr, so its exit status cannot be the
   success signal; the `e2mkdir` that follows is.
4. **`tool/mkdata` cannot run from inside a Genode run script in this
   environment.** Its Mojo launcher dies with
   `ModuleNotFoundError: No module named 'mojo'` under tclsh's `exec`,
   which also breaks `run/sponge-persist-disk.run` — a pre-existing
   failure, unrelated to this work. `sponge-logdisk-wedge.run` therefore
   spelled the docs/14 §4.3 P4 sequence out inline.

## 6. Follow-up that closes it

Move the sink **inside** the nested `system` sandbox and give that
sandbox its own `vfs` + `part_block` over the log partition, so the
sink's `File_system` is a sibling grant like every other service inside
the sandbox. Two wins beyond coverage: the sink inherits the sandbox's
heartbeat containment (a Tier-0 component has none — the top-level init
cannot run a heartbeat because core provides no Timer), and the nested
sandbox's own announcements (`child "x" announces service …`) finally
become readable, which is what a containment event looks like from the
outside.

## 7. The read-back command

With the sink enabled and the log partition readable from a terminal:

```
cat /var/boot.log.1      # the previous boot — the one that froze
tail -f /var/boot.log   # the current boot
```

Off the media, with any ext2 tool:

```
e2cp sponge-var.img:/log/boot.log.1 -
```

The in-desktop read-back mount (a `File_system` session labelled for the
log area, exposed by the terminal package) is **not** wired: it would
have to route through `pkg_runtime` to `vfs_var`, and per §4 a nested
component cannot obtain a `File_system` from a Tier-0 sibling. It is
part of the same follow-up.