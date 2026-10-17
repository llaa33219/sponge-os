# sponge_logdisk — persistent system log on the writable partition

A LOG server that writes the system log to a file on the media's
writable partition, so a freeze plus a reboot still leaves evidence.

## What it is

```
component --LOG--> sponge_logdisk --File_system--> vfs_var (P4: /log)
```

The product scenario routes the LOG requests of the Tier-0 children it
can reach to this component instead of to core. Everything the sink
receives is written to `/log/boot.log` on SPONGE-VAR, one file per
boot, with the previous boot preserved at `/log/boot.log.1`.

## Why it exists

The vendored Genode core terminates the LOG stream on the serial line:

```
repos/base/src/core/include/log_session_component.h
  Log_session_component::write()  -> Genode::log()
  repos/base/src/core/core_log.cc -> Kernel::log()
  repos/base-hw/src/core/kernel_log.cc -> main_print_char() -> the UART
```

On the serial-less LG gram that UART is unreadable, and a concurrent fb
console is ruled out by the user's design rule (docs/17 §4), so a freeze
leaves nothing at all to read. The user's standing requirement is that
problems must surface error logs; this is the mechanism for that.

## Design

### Size cap and rotation

`max_bytes` (default 8 MiB) with exactly ONE previous boot kept. SPONGE-VAR
is 64 MiB and shares a filesystem with pkgd's installed-set store, so an
unbounded log is a filesystem-full event that would take the package
manager down with it.

### Per-boot files

Every boot moves a surviving `<path>` to `<path><rotate_suffix>` BEFORE
opening a fresh one, so a boot never overwrites the previous boot's
evidence. Upstream's `file_terminal` opens with `O_CREAT|O_RDWR` at
offset 0 and never truncates, which is the wrong default here: boot 2
would overwrite boot 1 from byte 0.

### Eagerness

One `Vfs_handle::write` plus a `sync()` per drained batch, and the write
result is awaited — a completed `log()` call is at most one drain behind
the media. A periodic tick line (`tick_ms`) makes a freeze visible as a
GAP between two ticks rather than as an absent log.

### The two invariants that make it safe on the LOG path

**(A) The LOG service must never block on the filesystem.** Opening the
log's `File_system` session is a blocking session request: it returns
only once `vfs_var` has mounted ext2fs. Issued from the thread that
serves LOG writes, the first component to log would block inside
`log()` until the whole storage chain came up — and `report_rom`, which
every driver depends on for its reports, logs early. The first
prototype did exactly this and the product media froze right after
`acpi` finished parsing its tables.

So the LOG root is served on a **secondary entrypoint**
(`Genode::Entrypoint(env, 16*1024, "logsink")`), which starts its own
signal-proxy and RPC threads. Sessions dispatched there only format into
a RAM backlog and submit a signal; the component's **main** entrypoint
blocks on the filesystem meanwhile.

**(B) All filesystem I/O happens on the main entrypoint.**
`Vfs_handle::write` answers asynchronously and the Vfs library turns that
into a synchronous call via `commit_and_wait()`, which dispatches
signals on the component's `Env` entrypoint. Doing that from the root's
entrypoint while the main entrypoint is blocked in a session request
would deadlock. Sessions never enqueue I/O.

The root is announced via its own entrypoint, not `env.ep()`: init
creates a LOG session by invoking the root's `create` RPC, and that RPC
is dispatched on the entrypoint the root is managed on — on `env.ep()`
it would run on the very thread blocked in the File_system request.

### Degradation

| failure | behaviour |
|---|---|
| no `<vfs>` node in the config | LOG sessions are served, lines are buffered and dropped |
| `File_system` session never granted | same — the component blocks in the session request but keeps serving LOG from the secondary entrypoint |
| a write fails (full, unmounted) | `warning("write failed — persistence disabled")`, the handle is closed, logging continues in RAM |
| `rename` unsupported by the filesystem | the current file is truncated in place; the cap still holds, the previous boot's bytes do not survive |

## Config

```xml
<config path="/boot.log" max_bytes="8388608" rotate_suffix=".1"
        tick_ms="5000" tick_log="no">
  <vfs>
    <fs name="log"/>
  </vfs>
</config>
```

| attribute | default | meaning |
|---|---|---|
| `path` | `/boot.log` | file path on the mounted filesystem |
| `max_bytes` | `8388608` | size cap; exceeding it rotates |
| `rotate_suffix` | `.1` | appended to the previous boot's name |
| `tick_ms` | `0` | tick period; `0` disables ticks, timestamps and the Timer session |
| `tick_log` | `no` | echo each tick onto the component's OWN log session (for headless run scenarios) |

Without `<fs label="..."/>` the session label is `/`. The vfs server
requires the label's **last element** to start and end with `/`, and it
prefixes the policy match with the requesting child's name — so a policy
must match the *suffix* (`label_suffix: /var/`) rather than the raw
client label. See the scenario for the working spelling.

## Minimum privilege

`File_system` (vfs_var, policy root `/log`) and `LOG` sessions only. A
`Timer` session is opened **only** when `tick_ms` is non-zero — an
unrouted Timer request would block construction outright.

## Reading the log back

From a terminal on the product media (after the read-back mount of
docs/13-installation.md §"Reading the log"):

```
cat /var/boot.log.1      # the previous boot — the one that froze
tail -f /var/boot.log   # the current boot
```

Off the media, with any ext2 tool:

```
e2cp sponge-var.img:/log/boot.log.1 -
```