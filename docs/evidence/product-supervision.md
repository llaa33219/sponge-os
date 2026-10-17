# Top-level supervision in the product media: wired, gated, measured

**Date:** 2026-10-17
**Change:** `sponge_supervisor` + `sponge_console` deployed in the five
desktop-disk product scenarios
(`run/sponge-desktop-disk{,-nvme,-uefi,-uefi-nvme,-uefi-usb}.run`),
closing the last `[ ]` row of
[`docs/17-runtime-component-topology.md`](../17-runtime-component-topology.md)
§5 ("`sponge_supervisor` is not yet wired into the six product
scenarios").
**Predecessor:** [`top-level-supervision.md`](top-level-supervision.md)
— the lean-topology red→green proof, which this change re-verified
unchanged and then rolled out.

---

## 1. The topology decision that had to be made first

The lean proof owned the supervised subtree's config because the run
script *authored* it. The product does not: `docs/14` §4.4 made the
desktop composition DATA on disk — every product scenario writes
`/system/init/system.config` from a HID literal at staging time, and
the nested `system` init read it from `rom_sys`.

Three options were weighed.

| Option | Verdict |
|---|---|
| (a) the supervisor reads the staged config through a ROM and re-emits it | **chosen** |
| (b) the supervisor carries its own copy of the desktop config | rejected |
| (c) leave the config on `rom_sys` and let the supervisor rewrite only a wrapper | impossible |

(a) keeps **staging the single source of truth**: `run/supervisor.inc`
derives both templates from the very file the scenario already writes,
mechanically, and refuses to run if anything about that file drifted.
(b) would mean a second authored copy of a ~13 KB config that has to
stay byte-compatible with the first — and the drift would be invisible
until a fault made the supervisor rebuild a subtree that no longer
matched the media. (c) is not a thing: the restart lever is a rewrite of
the config the sandbox itself reads.

The one wrinkle in (a) is *where* the supervisor reads it from. It lives
in Tier 0, and `rom_sys` exists only behind the storage chain — so a ROM
route into the supervisor would drag the whole `part_block → vfs →
cached_fs_rom` chain into the supervisor's dependency set, i.e. put the
supervisor below the storage stack it is supposed to survive. The
templates therefore travel as a **boot module** (`supervisor.templates`),
staged by the same run script that stages `/system`. That is the same
staging decision expressed at the Tier-0 boundary, and it keeps the
supervisor's session list exactly what the lean proof established:
`Timer`, `Report`, and two ROMs of its own.

## 2. What the product topology forced, measured one item at a time

Every item below was a measured failure of the first product run, not a
design opinion. The four defects that a lean topology cannot produce:

### 2.1 A truncated config is worse than no config

`Sponge::element_text()` recognised only a bare `<config>` tag. A config
produced by `tool/hid format --output-xml` is full of
`<config verbose="no">` and `<config/>` nodes, so the scanner counted an
opening tag that was never closed and returned the FIRST nested
`</config>`:

```
13662-byte template  ->  forwarded as 3325 bytes
```

The forwarded config parses, applies, and starts three children instead
of eleven. The desktop simply never came up, and the only thing in the
log was the supervisor's boot-grace escalation, firing on a sandbox
that was fine. Fixed by making `element_text()` handle attributes,
self-closing tags and quoted attribute values (`text_scan.h`), plus a
guard in `_publish()`: a config that does not end in `</config>` is
**refused**, never forwarded. A visible stall beats a mystery.

### 2.2 The sandbox has its own containment, and it fires first

The product sandbox runs `<heartbeat rate_ms="2000"/>` with
`restart_after_skipped="3"` on its critical children: it abandons and
respawns a wedged child itself, at `skipped > 3`, i.e. about 8 s. The
lean supervisor acted on the *first* fault it saw, so on this media it
would kill the whole desktop for a transient that the sandbox was about
to fix by itself — and a transient is exactly what a slow Qt6 first
paint looks like.

The escalation rule now implemented:

> **The supervisor acts only when the supervised sandbox's own
> containment has failed or been exhausted** — i.e. the fault is still
> there after `grace_ms`, which is longer than the sandbox's own restart
> threshold — **or when the sandbox has published nothing at all within
> `boot_grace_ms`** (which covers the sandbox init itself not coming up).

The fault flag is re-read on every tick of that window, so a fault the
sandbox fixed costs exactly one log line:

```
FAULT max_skipped=3 exited=0 state_seen=1 state_age_ms=250
escalation - fault held for 4000ms before any action (the supervised sandbox has its own containment)
nominal — subtree healthy, no action
```

This is a *timing* rule. No detection threshold changed
(`fault_skipped`, `poll_ms`, `grace_ms`, `console_hold_ms`,
`boot_grace_ms` are the lean proof's values).

### 2.3 A clean probe exit is not a fault — but only if it is excluded

`alpha_probe` prints its PASS marker and exits 0; `cfg_probe` likewise.
Genode keeps an exited child in the state report until it is abandoned,
so an unconditional "any exit is a fault" rule reads the product's own
success marker as a dead component and tears the desktop down minutes
after it booted. The supervisor therefore takes the same pair
sponge-de's health watcher excludes (`repos/sponge/src/sponge-de/main.cc`,
the `probe` special case) — but as **configuration**
(`ignore_exited="alpha_probe,cfg_probe"`), so panel and supervisor
cannot disagree about what a clean exit is.

Getting it right took two attempts, both instructive and both recorded
here because both are silent:

* `strcmp(p, name.string())` over the list's tail compares
  `"alpha_probe,cfg_probe"` with `"alpha_probe"` and never matches — the
  whole exclusion silently off.
* `String<N>::size()` is a **static capacity** (65 for `String<64>`);
  writing `name.size()` compiles into a comparison that can never be
  true, and `String<N>::length()` counts the terminating null. The
  length must come from `strlen(name.string())`.

Both were found by the control arm, whose whole subject is "the
supervisor must not act".

### 2.4 `settle_ms` is a product-media parameter, not a constant

The lean proof runs the sandbox heartbeat at 500 ms; the product runs it
at 2000 ms. A **respawned** child therefore needs 10–12 s to climb back
over `fault_skipped`, not 1.75 s (measured from the state report: 3–4
reports per skipped step at the product's `delay_ms=1000`). With the
lean proof's 3 s settle window the supervisor declared the fault
"contained" while the respawned child had not yet reached the detection
threshold — and two kills out of three ended in `contained` instead of
the console. The console phase was **structurally unreachable**.

Fix: `settle_ms = 20000` in the product policy, over the measured 12 s.
The window must exceed the respawn-to-refault time or it races it. It is
also the wait for the console's `Gui` session to close before the
desktop is rebuilt, so a longer window costs a longer dark period after
a fatal fault and nothing else.

## 3. The console in the product topology

The lean proof's console was the only `Gui` client because its
topology had no desktop. On the product media `nitpicker` and the
framebuffer driver are **Tier-0, outside the supervised subtree**, and
the desktop stack is inside it — so the same structural property is
reached differently:

* the console gets **its own nitpicker domain**, `console`, at layer 8 —
  above `pointer` (0), `panel` (1), `default` (3) and `launcher` (4) — so
  it is the topmost client without moving any domain the shipped desktop
  already uses;
* the supervisor publishes `<system_console>`, the desktop config with
  the desktop **clients** removed (`sponge-de`, `alpha_probe`), so while
  the console is up the supervised subtree holds no `Gui` client at all.
  The *services* (wm, layouter, decorator, configd, themed, pkgd,
  pkg_runtime) stay, which is what lets the subtree come back healthy
  without fetching anything again.

The framebuffer keeps exactly one writer (`vesa_fb`/`boot_fb` via its
Capture session). "Takeover" is over the **display**, never over the
device — unchanged from the lean proof and for the same measured reason
(`docs/11-environment.md` row 12).

Ordering is unchanged too: the supervisor destroys the wedged subtree
*before* it arms the console, and rebuilds the desktop only after the
console reported `DISARMED`. The receipt is a second incarnation of the
fault child, not a log line.

## 4. The pixel oracle needed a different sample point

`test/takeover_probe` samples the screen centre — correct in the lean
topology, where the centre is flat background for both roles. On the
product media it is wrong for a measured reason: **nitpicker parks the
pointer at the centre at boot**, and the `pointer` component paints its
glyph exactly there.

```
takeover_probe: sample band=0x1e1e2e centre=0x909490 corner=0x1e1e2e
```

`0x909490` is the cursor; the desktop background is `0x1e1e2e`. The
probe grew a `sample_x`/`sample_y` config attribute and the product arm
samples 768x576 (bottom-right quadrant), where the claim is unambiguous:

```
takeover_probe: console-up state=console centre=0x601010
takeover_probe: pixel state=desktop centre=0x1e1e2e
```

The "desktop" state colour is the product `nitpicker` background
(`#1e1e2e`), so the claim does not depend on any particular theme.

## 5. The product-topology proof

`SPONGE_SUPERVISOR_TEST` (env-gated, **off by default** — shipped media
never carries a fault child) stages a wedging child inside the
supervised sandbox plus the pixel oracle, and gates the whole chain in
the product topology. Two arms, and the difference between them is the
fault child's own containment — nothing else:

### 5.1 `SPONGE_SUPERVISOR_TEST=control` — the half that matters most

```
ep_wedge: alive — entrypoint wedging now
sponge_supervisor: FAULT max_skipped=3 exited=0 state_seen=1 state_age_ms=250
sponge_supervisor: escalation - fault held for 4000ms before any action (the supervised sandbox has its own containment)
sponge_supervisor: nominal — subtree healthy, no action
alpha-probe: PASS
sponge-desktop-disk: SUPERVISOR CONTROL PASS - the sandbox's own
  containment held, the supervisor killed nothing and armed no console
```

The wedged child carries the product's own deployment
(`<heartbeat restart_after_skipped="3"/>`), so the sandbox restarts it
itself and the supervisor does nothing — **while the desktop reaches
`alpha-probe: PASS` underneath**. The negative assertions (no
`published <system> kill=2`, no `published <system_console>`, no
`sponge_console: ARMED`) run against the run tool's accumulated output,
because no log-line gate can express an absence.

### 5.2 `SPONGE_SUPERVISOR_TEST=takeover` — detect → kill → console → hand back

```
sponge_supervisor: FAULT max_skipped=3 exited=0 state_seen=1 state_age_ms=250
sponge_supervisor: escalation - fault held for 4000ms before any action (...)
sponge_supervisor: FAULT max_skipped=5 ...
sponge_supervisor: published <system> kill=2 (13928 bytes)
sponge_supervisor: phase=2 (fault survived the escalation window, subtree killed)
ep_wedge: alive — entrypoint wedging now            <-- SECOND incarnation: the kill receipt
sponge_supervisor: published <system_console> kill=3 (10288 bytes)
sponge_supervisor: phase=3 (unrecoverable - console owns the display)
sponge_console: ARMED role=console label=console 1024x768 owns the display
takeover_probe: console-up state=console centre=0x601010     <-- PIXEL receipt
sponge_supervisor: subtree healed under the console
sponge_supervisor: CONSOLE RELEASED — display handed back, ...
sponge_console: DISARMED — display released
takeover_probe: pixel state=desktop centre=0x1e1e2e           <-- PIXEL receipt
sponge_supervisor: published <system> kill=4 (13928 bytes)
sponge_supervisor: phase=1 (desktop restored)

sponge-desktop-disk: SUPERVISOR TAKEOVER PASS - fault detected in
  the product subtree, subtree killed, console owned the display by
  pixels, display handed back
```

The fault child here has a bare `<heartbeat/>` (monitoring on,
`restart_after_skipped` 0), so nothing below the supervisor restarts it
and the supervisor is the single restart authority.

**One honest limitation.** A *persistent* fault plus a full-subtree
restart lever are mutually exclusive with a fully booted desktop: while
the supervisor keeps killing, the desktop never gets the minutes it
needs to boot. So the recovery receipt in this arm is the **display
handover** (console disarmed, `phase=1 desktop restored`, pixels back to
the desktop background), not a second `alpha-probe: PASS`. A second
PASS is structurally unreachable under a fault that never heals, which
is the same finding as the `docs/17` §5 "[ ] respawn budget" row. The
healthy-product path (`alpha-probe: PASS` with the supervisor live) is
what the five scenario regressions and the control arm both prove.

Two further observations, recorded rather than smoothed over:

* tearing the subtree down while a child is being created logs
  `Error: CPU (init -> system -> sponge_configd) create_thread: invalid
  PD argument` — the child dies mid-creation. The sandbox recovers and
  the run passes; the line is a consequence of the total restart, not a
  new fault class.
* the supervisor's periodic `nominal — subtree healthy` line is the only
  in-guest evidence that the supervisor is alive on real hardware (no
  serial console there). On the product media it is also invisible to a
  run gate until `match_max` is raised — the boot floods the serial
  console and the run tool keeps only the last 2000 bytes by default.
  Both arms raise it.

## 6. Results

| Scenario | Kernel | Result |
|---|---|---|
| `sponge-desktop-disk` (supervisor live, default arm) | sel4 | **PASS** — `alpha-probe: PASS`, `published <system> kill=1 (13589 bytes)`, no fault declared |
| `sponge-desktop-disk` + `SPONGE_SUPERVISOR_TEST=control` | sel4 | **PASS** |
| `sponge-desktop-disk` + `SPONGE_SUPERVISOR_TEST=takeover` | sel4 | **PASS** |
| `sponge-supervisor-takeover` (lean, after the component changes) | sel4 | **PASS** |
| `sponge-supervisor-takeover` + `SPONGE_TAKE_NO_FAULT=1` (control) | sel4 | **PASS** |
| `sponge-desktop-disk-uefi` | sel4 | **PASS** (its own gates: host structural layout + QEMU banner; the boot modules carry the supervisor — `sponge_supervisor` 77 KB, `sponge_console` 119 KB, `supervisor.templates` 23.8 KB, `supervisor.config` 179 B) |
| `sponge-desktop-disk-uefi-nvme` | sel4 | **PASS** (same gate shape) |
| `sponge-desktop-disk-uefi-usb` | sel4 | **PASS** (same gate shape) |
| `sponge-desktop-disk-nvme` | sel4 | **FAIL — pre-existing, NOT a regression**; see below |
| `sponge-debug-scrollstorm` (default arm) | sel4 | **PASS** — `repostorm-probe: VERDICT PASS` |
| `sponge-minimal` | sel4 | **PASS** |
| `sponge-de-test` | sel4 | **PASS** — `sponge-de-probe: PASS` |
| `sponge-pkg-lifecycle` | sel4 | **PASS** — `lifecycle-probe: PASS` |

Tier-0 cost: `image.elf` 16 MiB (was 15 MiB) against the §4.5 budget of
80 MiB. The supervisor and the console are base-lib binaries with no
libc and no Qt.

### 6.1 `sponge-desktop-disk-nvme` — red at HEAD, red with the supervisor

This one needs stating precisely rather than rounding up.

The scenario **fails identically before and after this change**, for a
reason already registered in `docs/17` §5 ("`sponge-desktop-disk-nvme`
stops before admission is reached (pre-existing)"): the scenario wires
`sponge_pkgd`'s `<bake/>` node and a `bake_manifest` ROM route but never
calls `bake::stage`, so

```
[init -> rom_sys] Error: /bake_manifest.json not found
[init -> system -> sponge_pkgd] Error: stop because parent denied ROM-session: label="bake_manifest"
[init -> system -> alpha_probe] Error: alpha-probe: FAIL sponge_pkgd did not answer install hello
```

is what the scenario produces at `HEAD` too. Measured by reverting
`run/sponge-desktop-disk-nvme.run` to `HEAD` and running it: same
`alpha-probe: FAIL`, same gate timeout. The supervisor is **not** the
cause: in the supervised run it declared the fault four times and killed
the subtree **zero** times (`grep -c 'published <system> kill=2\|phase=2'`
= 0) — every fault was a probe that had already exited or a child the
sandbox was restarting, and the escalation window closed on its own each
time. That is the escalation rule working, on a scenario whose real
problem is elsewhere.

One difference is recorded rather than smoothed over: in the supervised
run the guest later took a seL4 fault (`vm fault on data at address 0x8
... in thread "child of: 'rootserver'"`) before the gate timed out; the
baseline run did not show one inside the same window. Both runs fail the
same gate for the same pre-existing reason, and no other scenario shows
this fault with the supervisor live, so it is registered as an **open
observation on this scenario**, not as a verdict. Closing it properly
means first fixing the missing `bake::stage` in that scenario, which is
a separate change with its own evidence.

### 6.2 The four envelope variants

The four other desktop-disk scenarios differ only in the storage driver
and the firmware path, and they carry the identical supervisor wiring —
`run/supervisor.inc` makes the transform shared and *asserted*, so a
scenario that drifted would fail loudly at staging time rather than boot
an unsupervised desktop. Their receipts are the `.log` files promoted
next to this one.

## 7. `sponge-alpha.run` — evaluated, deliberately not wired

`run/sponge-alpha.run` is a **flat** topology: `wm`, `layouter`,
`decorator`, `sponge_configd`, `sponge_themed`, `sponge_pkgd`,
`sponge-de` and `pkg_runtime` are direct children of the **top-level**
init. The supervisor works by OWNING the config of a nested sandbox
through `report_rom`; the top-level init's config is not reachable that
way — there is no parent to relay it, and the supervisor cannot be
handed the config of the init that started it.

Three further reasons, in the order they matter:

1. **No Timer on this host's ISO path.** The supervisor's own clock
   comes from a Tier-0 `timer` child. `docs/17` §5 records that flat ISO
   boots deny the HPET `IO_MEM` on this host, so `timer` cannot start
   (`timer: no route to service "IO_MEM"`). The disk product media is
   unaffected; alpha's default media mode is the ISO.
2. **The containment argument does not apply.** Alpha's desktop children
   are supervised by the top-level init, which cannot run a heartbeat at
   all (`env.parent()` is core). Making the supervisor meaningful there
   would require restructuring alpha into the nested-`system` shape the
   disk scenarios already use — a topology change that would invalidate
   alpha's own `alpha-probe: PASS` evidence.
3. **No `state` report to read.** With no nested sandbox, there is no
   `system -> state` report for the supervisor to read through the
   top-level relay; the whole observation channel would have to be built.

Decision: **not applicable, unchanged.** The reasoning is recorded in
`docs/17` §5 and in the scenario file, so it is a decision rather than
an omission. Alpha remains the ISO/live-media reference scenario.

## 8. vct / Leitzentrale

`vct` is **not present** in any of the five desktop-disk scenarios (it
is a Phase-2/ISO component; `sponge_configd` is what the product media
carries), so there is no backend to share and nothing to duplicate —
`AGENTS.md` §3.3 rule 5 is satisfied by construction. For the record:
`vct status` reads the **top-level** init's state report
(`repos/sponge/src/vct/init_state.h`, ROM label `state`), so if vct were
wired into a supervised media it would list `sponge_supervisor` and
`console` as ordinary children — a readout needs no new backend. No
`vct supervisor` subcommand is added: the supervisor's own state is
already observable in two places the user has today (the console banner
on the display, and the panel's health label, which reads the same
`system_state` report the supervisor does). Recorded as a possible
follow-up, not as a gap.

## 9. Files

| Path | What |
|---|---|
| `run/supervisor.inc` | the shared, asserted product wiring (new) |
| `repos/sponge/src/sponge_supervisor/main.cc` | escalation grace, `ignore_exited`, truncation guard |
| `repos/sponge/src/sponge_supervisor/text_scan.h` | `element_text()` handles attributes and self-closing tags |
| `repos/sponge/src/test/takeover_probe/main.cc` | `sample_x`/`sample_y` |
| `run/sponge-desktop-disk*.run` | five call sites + the env-gated proof |
| `repos/sponge/run/supervisor.inc` | committed symlink, so the include resolves from the build repo |

No vendored-tree patch was needed for any of this, so
`docs/11-environment.md`'s patch ledger gains no row.