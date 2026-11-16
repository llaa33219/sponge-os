> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §4. Run Scripts (Scenarios)

## 4. Run Scripts (Scenarios)

Each file in `run/` defines one boot scenario. A scenario defines:

- Which kernel base to use.
- Which components to include.
- The configuration of each component (resources, routing).
- (For automated tests) the verification script.

Scenarios:

| Scenario | Purpose | Status |
|---|---|---|
| `run/sponge-minimal.run` | Minimum Sponge OS boot (init + vct) | ✅ base-linux, ✅ base-sel4 |
| `run/sponge-vct-help.run` | `vct help` prints the command summary | ✅ |
| `run/sponge-vct-version.run` | `vct version` prints the version | ✅ |
| `run/sponge-vct-status.run` | `vct status` reads live init state through a sub-init + `report_rom` relay | ✅ |
| `run/sponge-vct-component-list.run` | `vct component list` lists the live component tree | ✅ |
| `run/sponge-de.run` | Sponge DE single-window demo (nitpicker + fb_sdl + sponge-de) | 🟡 (Phase 3 in progress) |
| `run/sponge-de-sel4-interactive.run` | base-sel4 interactive-PC driver set (vesa_fb/ps2/usb_hid/event_filter/platform/acpi/pci_decode) under QEMU with usb-tablet absolute pointer; Phase 10 extended it with QMP-driven real input + click-to-launch over the real driver chain + panel popup toggle | ✅ Phase 10 closed (criteria 1, 3, 4 — see §4.4 and `docs/evidence/phase10-index.md`); the underlying Qt6-on-sel4 capability-exhaustion root cause in §3.8 stays resolved |
| `run/sponge-wm-qmp.run` | **Phase 10 criterion 2**: real QMP PS/2 Mouse window drag on the upstream wm/layouter/decorator stack + sponge_pkgd-launched `pkg_gui_demo` | ✅ base-sel4 only (driver stack + wm stack + QMP) |
| `run/sponge-terminal-qmp.run` | **Phase 10 criterion 5a**: real QMP `send-key` keyboard input to a focused terminal session (gems terminal + noux bash) | ✅ base-sel4 only |
| `run/sponge-textedit-qmp.run` | **Phase 10 criterion 5b**: real QMP `send-key` keyboard input to a focused Qt6 textedit session | ✅ base-sel4 only |
| `run/qmp.inc` | shared Tcl QMP helper (sourced via `[file dirname [info script]]`); provides `qmp_connect`, `qmp_click`, `qmp_ps2_click`, `qmp_drag`, `qmp_send_key`, `qmp_type`, `qmp_exec_target`, etc. — see §4.4 for the API | n/a |
| `run/sponge-boot.run` | **Phase 8 P1**: Tier-0 storage-chain smoke (mount GENODE ext2, serve a ROM from disk via `cached_fs_rom`, read back `/system/marker.txt`; gates on `boot-probe: PASS`). base-sel4 only; `RUN_OPT='--include image/disk'`. NVMe variant via `SPONGE_BOOT_NVME=1`. | ✅ (commit `d3473f61f6`) |
| `run/sponge-desktop-disk.run` | **Phase 8 P2**: the FULL Alpha desktop booted FROM DISK (image.elf ≤ 12 MiB; Qt6 desktop served from `/system` via `cached_fs_rom`); gates on `alpha-probe: PASS`. The disk-served half of the product `.img`. base-sel4 only; `RUN_OPT='--include image/disk'`. | ✅ (commit `e7f8b9a458`) |
| `run/sponge-desktop-disk.run` + `SPONGE_GROUPED_UI=on` | **Phase 16 P2.4 D16R.11 grouped-runtime UI acceptance arm** (opt-in, default off): `test/grouped_ui_probe` emits the `run/qmp.inc` `QMP-TARGET` choreography — real usb-tablet clicks through the Sponge DE panel toggle + launcher menu entry, then `send-key` typing — and asserts per runtime class (window label bound to `pkg_runtime_simple` / `pkg_runtime_terminal`) the D16R.11 shape: first paint (Capture pixels) + measurable typed input, plus a separate terminal command-execution delta below the prompt row. Simple-class target is textedit when its launcher entry is reachable, calculator otherwise (the launcher domain is 532px so textedit is visible on the `desktop` profile — see the probe header); terminal-class target is terminal. The P2.6 browser leg (falkon, `pkg_runtime_browser -> falkon`) is wired after terminal and asserted with the same first-paint predicate and typed-delta thresholds (only the first-paint poll budget is longer); it currently fails on the prebuilt-closure ABI blocker recorded in `docs/evidence/phase16-p26-browser-staging.md`. All product media default to `fbcon=off`; `SPONGE_FBCON=on` is the diagnostic on-panel logging escape hatch and leaves serial output unchanged. Gates on `grouped-ui-probe: PASS`. Command: `SPONGE_GROUPED_UI=on make -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | ✅ two consecutive grouped passes + default BIOS regression; receipt `docs/evidence/phase16-grouped-runtime-ui.md` |
| `run/sponge-persist-disk.run` | **Phase 8 P3**: persistence on SPONGE-DATA — `sponge_pkgd`'s installed-set store survives a reboot when backed by a writable ext2 on P4 (added by `tool/mkdata`); two-boot adversarial proof. base-sel4 only; `RUN_OPT='--include image/disk'`. | ✅ (commit `f25a81dcbe`) |
| `run/sponge-falkon-disk.run` | **Phase 8 P4**: Falkon's 509 MiB WebEngine payload booted FROM DISK (architecture PROVEN — image.elf stays at ~12 MiB; ldso resolves the 237 MiB WebEngine lib from disk; lwIP DHCPs to 10.0.2.15). First paint is blocked by a base-sel4 capability-space issue (NOT a boot/storage issue); see `docs/14` §12.4 and `docs/evidence/p4-cspace-fix.log`. base-sel4 only; `RUN_OPT='--include image/disk'`. | 🟡 architecture proven; first paint blocked (commit `bae5423d1b`) |
| `run/sponge-boot-i440fx.run` | **Phase 12 — i440fx/PIIX4 IDE smoke** (`-machine pc`, explicit `-cpu Skylake-Client`, one disk, no AHCI, no product image, no DE). Tier-0 only; gates on `boot-probe: PASS` from the PIIX4 IDE marker. `smoke-only`, not product-verified. ≤60 s target. Evidence `docs/evidence/phase12-boot-i440fx.log`. | ✅ Phase 12 (criterion 1) |
| `run/sponge-boot-multidisk.run` | **Phase 12 — q35/AHCI multi-disk order check**. Two disks; expected marker is on P3 of the SECOND disk; drive order is swapped so success cannot come from "first disk". Tier-0 only; gates on `boot-probe: PASS` from the second disk's P3 marker. ≤60 s target. Evidence `docs/evidence/phase12-boot-multidisk.log`. | ✅ Phase 12 (criterion 1) |
| `run/sponge-desktop-disk-nvme.run` | **Phase 12 — q35/NVMe desktop-from-disk** (one namespace on a `pcie-root-port`, NVMe `caps: 5000 | ram: 64M`, `<partition number="3"/>` semantics preserved, P3 `Number: 3` report/byte check before `alpha-probe: PASS`). Opt-in via `./tool/dist --storage nvme`. 600 s+ full desktop reality, bounded at 900 s. Evidence `docs/evidence/phase12-desktop-nvme.log`. | ✅ Phase 12 (criterion 1) |
| `run/sponge-pc-nic.run` | **Phase 12 — pc_nic/e1000 + nic_router DHCP** (Linux-backed `pc_nic` server, `caps: 1000 | ram: 32M`, upstream `label_prefix: pc_nic | domain: uplink` policy, QEMU user/slirp backend). Gates in order: `pc_nic: bound device` then `nic_router: uplink DHCP acquired` (300 s cold DDE-Linux gate). Evidence `docs/evidence/phase12-pc-nic.log`. | ✅ Phase 12 (criterion 2) |
| `run/sponge-usb-boot.run` | **Phase 12 — BIOS-side USB storage attachment** (ISO attached via `-device usb-storage`; BIOS side only; gates on `BIOS-side USB boot verified` + `alpha-probe: PASS`). Evidence `docs/evidence/phase12-usb-boot.log`. | ✅ Phase 12 (criterion 1) |
| `run/sponge-usb-kbd-via-qmp.run` | **Phase 12 — USB HID keyboard via QMP hotplug** (`pc_usb_host` → `usb_hid` → `event_filter`; ordered QMP chain: `device_add usb-kbd` → `usb_hid: KEYBOARD detected` → `device_del` → `send-key` → final PASS). The glyph-delta secondary gate is a documented Phase-12 gap (probe-focus ROM quirk). 600 s+ seL4 desktop reality, bounded at 900 s. Evidence `docs/evidence/phase12-usb-kbd.log`. | ✅ Phase 12 (criterion 2) |
| `run/sponge-pkg-admission.run` | Resource admission control: does `sponge_pkgd` refuse a launch whose declared quota does not fit `pkg_runtime`'s remaining budget (answer `insufficient-resources` with the four figures, package kept out of the running set, running witness unaffected, and the same launch succeeding once the budget is freed)? Gates on `admission-probe: PASS`. Kernel-agnostic. Evidence `docs/evidence/pkg-admission-control.md`. | ✅ |
| `run/sponge-supervisor-takeover.run` | Top-level fault containment: a child of the supervised `system` sandbox wedges, the Tier-0 `sponge_supervisor` detects it from that sandbox's own state report, kills the subtree by rewriting every `<start>` version, arms `sponge_console` when the fault survives the grace window, and hands the display back a bounded hold later. The takeover is asserted by a Capture-side **pixel oracle**, and a `SPONGE_TAKE_NO_FAULT=1` control run asserts with a negative check that a healthy system is never touched. Gates on `supervisor-takeover: PASS`. base-sel4 only; boots the **disk** image because the flat-ISO HPET blocker denies `timer` its IO_MEM. Evidence `docs/evidence/top-level-supervision.md`. | ✅ |
| `run/sponge-resource-request.run` | Dynamic quota top-up verdict: does Genode 26.08's `init` grant or forward a child's `resource_request()` upward? Three nested sandboxes measure immediate-grant-from-slack, park-forever-with-no-upward-forwarding, and revival-only-by-a-config-change-to-the-hosting-sandbox. Gates on `resq-c: PASS`. Kernel-agnostic. Evidence `docs/evidence/dynamic-resource-topup.md`. | ✅ |

The source-of-truth `.run` files live in `run/`. The same files are
discoverable through `repos/sponge/run/` via **committed relative
symlinks** (one per scenario) so the Genode repo discovery picks them
up regardless of which directory the build was launched from.

### 4.0.1 Phase 12 — automated and manual equivalents (control escape hatch)

The Phase-12 workstream introduces four new automated host checks and
their manual equivalents. AGENTS.md §3.5 requires the manual
equivalent for every automated step; the table below collects them in
one place. None of the automated steps mutate the repository —
`tool/hw_compat.mojo` is `assert`-only, `tool/patches verify` is
read-only, and `tool/dist --storage` only chooses the per-scenario
storage mode.

| Automated step | Manual equivalent (control escape hatch) |
|---|---|
| `./tool/dist --storage ahci` (default — preserves current product-media behavior) | `make -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` (the AHCI product path; q35 + `Skylake-Client` is now the explicit pin from W1) |
| `./tool/dist --storage nvme` (opt-in — one-namespace NVMe product path) | `make -C genode/build/x86_64 run/sponge-desktop-disk-nvme KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` (q35 + `Skylake-Client` + explicit pcie-root-port + `-device nvme` per the W2 storefront block) |
| `./tool/build verify` (the Phase-12 host gate) | `./tool/patches verify && ./tool/hw_compat assert` — two separate commands so a failure stops at the first one (the plan's "stop on first failure" rule) |
| `./tool/patches verify` (the W1 patch pre-flight, also reachable from `tool/build run`) | `git show -s <sha>` (commit exists) + `git merge-base --is-ancestor <sha> HEAD` (ancestor) + `git show --stat <sha>` (touched paths) — per ledger row. The tool is the convenience layer; the git log is the source of truth. |
| `./tool/hw_compat assert` (the W5 compatibility validator) | Reading `docs/15-hardware-compatibility.md` by hand and verifying each `scenario`/`evidence` path resolves and each `verified`/`smoke-only` cell carries the exact marker. The validator fails fast on missing/renamed paths; the manual check is the long-form audit. |
| Serialized Phase-12 regression sweep (W6 §7 + §8) | One of the new Phase-12 scenarios: `make -j1 -C genode/build/x86_64 run/<scenario> KERNEL=sel4 BOARD=pc [RUN_OPT="..."]`. The W6 regression evidence log records every command; the manual equivalent is the same `make` invocation per scenario. Scenarios must run **one at a time** with `make -j1` and **no concurrent make** in `genode/build/x86_64`. |

The CLI flags `--storage` / `--help` / `--json` follow the vct
convention; `--storage invalid` returns exit 2 with a concise English
usage line and never starts a build. The full help text is in
`tool/dist --help` and the storage-mode research / acceptance is in
`docs/evidence/task-2-phase12-storage.md`.

### 4.1 Anatomy of a working run script

The minimum reliable pattern, learned from `hello_tutorial` and
applied to `sponge-minimal.run`:

```tcl
# 1. Build list MUST include lib/ld, or the dynamic linker is missing
#    from the boot image and every component fails to load with
#    "environment ROM session denied (label=ld.lib.so)".
build { core lib/ld init <your-component> }

create_boot_directory

# 2. install_config accepts either raw XML or the '+' shorthand form.
install_config {
config
+ parent-provides
  + service LOG
  + service PD
  + service CPU
  + service ROM
+ default-route
  + any-service
    + parent
    + any-child
+ default | caps: 200 | ram: 8M
+ start <your-component>
  + config
    + args
      + arg status
-
}

# 3. [build_artifacts] is a run-tool helper that automatically stages
#    every binary the build step produced, including init.xsd (needed
#    for config validation) and ld.lib.so. Do NOT list them manually.
build_boot_image [build_artifacts]

append qemu_args " -nographic -m 1G "

# 4. run_genode_until matches a regex in the boot log within N seconds.
#    (sponge-minimal.run matches the vct codename line.)
run_genode_until {.*Sponge OS codename:.*Archaeocyte.*} 30
```

For how to write run scripts in general, see the
[Genode run documentation](https://genode.org/documentation/developer-resources/run).

### 4.2 Running a scenario

```bash
./tool/build run <scenario>
# Manual equivalent:
make -C genode/build/x86_64 run/<scenario>
```

The run tool builds the requested binaries, stages the boot directory
under `genode/build/x86_64/var/run/<scenario>/`, runs the kernel
(`./core` for base-linux, QEMU for base-sel4 / base-hw), and watches
the log for the success regex. On success it prints
`Run script execution successful.` and exits 0.

### 4.3 Kernel matrix

The same `sponge-minimal.run` boots on both kernels:

| Kernel | Driver | RAM minimum | Notes |
|---|---|---|---|
| `linux` | `./core` directly (no QEMU) | 8 MiB for the default route | Fastest iteration. `core` runs as a Linux ELF process; no QEMU, no kernel build. |
| `sel4`  | QEMU (`-nographic -m 1G`) | 1 GiB guest RAM | Production target. Requires `prepare_port sel4 sel4_tools grub2` plus the seven Python modules (`§3.6`). |
| `hw`    | QEMU | 128 MiB | Genode's own microkernel. Not yet wired into Sponge OS scenarios. |
| `nova`  | QEMU | 256 MiB | NOVA hypervisor. Not yet wired into Sponge OS scenarios. |

### 4.4 Host-driven QMP input (`run/qmp.inc`)

Phase 10 (`docs/plans/phase10-interactive-desktop.md`) delivers the
fully-interactive desktop by driving guest input from the host through
QEMU QMP instead of the probe's synthetic `Event`-session injection.
The shared helper `run/qmp.inc` (sourced by
`sponge-de-sel4-interactive.run`, `sponge-wm-qmp.run`,
`sponge-terminal-qmp.run`, and `sponge-textedit-qmp.run`) is a Tcl 8.x
source file with no external dependencies — only Tcl's built-in
`socket` (TCP, matches `-qmp tcp:`) and `expect` (already used by the
Genode run tool).

Source convention (from the symlink-resolved run directory):

```tcl
source [file join [file dirname [file normalize [info script]]] qmp.inc]
```

Enable the QMP listener in QEMU args:

```tcl
set qmp_port [qmp_pick_port]               ;# or [expr {20000 + ([pid] % 20000)}]
append qemu_args " -qmp tcp:127.0.0.1:${qmp_port},server=on,wait=off "
```

Then, after `run_genode_until` confirms the guest is ready:

```tcl
set qmp_chan [qmp_connect $qmp_port]
qmp_exec_target $qmp_chan 120              ;# bounded expect for QMP-TARGET marker
run_genode_until {.*<scenario>: PASS.*} 120 $qemu_spawn_id
qmp_disconnect $qmp_chan
```

#### `qmp.inc` API (full list)

| Proc | Args | Purpose |
|---|---|---|
| `qmp_pick_port` | — | Free TCP port via `socket -server 0`; kernel-assigned; avoids TIME_WAIT collisions on sequential QEMU respawns. |
| `qmp_connect` | `port` | Bounded retry (100 × 50 ms), read QMP greeting, send `qmp_capabilities`, return channel. |
| `qmp_cmd` | `chan json` | Send one JSON command, skip async `{"event":...}` lines, die loud (exit 1) on `{"error":...}` or 10 s read timeout. |
| `qmp_abs` | `x y` | Scale guest (1024×768) coords to QEMU usb-tablet axes (0..32767): `ax=(x*32767+512)/1024`, `ay=(y*32767+384)/768`. |
| `qmp_pointer_move` | `chan x y` | Emit `input-send-event` abs events (per-axis, two-event batch). |
| `qmp_button` | `chan btn {down\|up}` | Emit `input-send-event` btn event (`button` key, "left"/"right"/"middle"). |
| `qmp_click` | `chan x y` | `qmp_pointer_move` + `qmp_button left down` + `qmp_button left up`. Under QEMU `-nographic` the click is NOT precisely targeted — see below. |
| `qmp_ps2_click` | `chan x y` | The W3-calibrated PS/2 Mouse click: clamp to (0,0) → 5× rel-50 + 4× rel-50 → fine rel-1 walk → hover jiggle (+1/-1) → 200 ms press hold → release. ±1 px precision with the vendored `event_filter` `<accelerate>` chain (or 1:1 rel-to-px with the custom config used by `sponge-de-sel4-interactive.run`). |
| `qmp_drag` | `chan x1 y1 x2 y2` | Move to start, hover jiggle, press, walk to end (50/1 split), release. |
| `qmp_send_key` | `chan keyname` | Emit a single QEMU key down/up via `send-key` with the **qcode-object form** (NOT the rejected string form — see QEMU 11 lesson below). |
| `qmp_type` | `chan string` | Char→keyname map (`a-z`, `0-9`, `space→spc`, `\n→ret`, `-→minus`, `.→dot`) + `qmp_send_key` per char; unmapped chars fail loud. |
| `qmp_exec_target` | `chan timeout_s` | Bounded `expect` on global `qemu_spawn_id` for the `QMP-TARGET` marker, dispatches to the matching proc (click/drag/type/absmove/move/press/sync), raises `match_max -i $qemu_spawn_id 200000` first. FAIL + exit 1 on timeout. Returns `move`, `press`, `absmove`, or `sync` for those marker types so variable-length choreography can stop at the required barrier. |
| `qmp_move_rel` | `chan axis value` | Emit one per-axis relative motion event (PS/2 path). Used by the calibrated click/drag recipes. |
| `qmp_move_rel_2d` | `chan x y` | Emit both relative axes in one `input-send-event`; closed-loop moves call this with rel-1 steps to remain in the event_filter accelerator's identity band. |
| `qmp_disconnect` | `chan` | Close the QMP TCP channel. |

#### `QMP-TARGET` marker contract (probe → host)

Probes in observe mode log these lines on the serial console; the run
script's `qmp_exec_target` consumes and dispatches them:

```
QMP-TARGET click <gx> <gy>           # PS/2 REL recipe by default
QMP-TARGET tablet <gx> <gy>          # usb-tablet-abs recipe (W4 terminal scenario style)
QMP-TARGET drag <x1> <y1> <x2> <y2>  # window drag (motion in two phases)
QMP-TARGET type <string>             # keyboard input
QMP-TARGET key <keyspec>             # QEMU key or combination
QMP-TARGET absmove <gx> <gy>         # absolute materialization event
QMP-TARGET move <dx> <dy>            # closed-loop, combined-axis rel-1 steps
QMP-TARGET press <gx> <gy>           # press/release at the current pointer
QMP-TARGET sync                      # drain barrier; no guest input
```

Patterns anchor on `\r*\n` (NOT `\r?\n`) because QEMU's `-nographic`
serial emits `CR CR LF` line endings (verified by `od -c` on a
captured line). The dispatch arms are ordered click, press/rightclick,
tablet, drag, type/key, sync, absmove, then move. Probes serialize their
markers; scenarios with a variable number of moves use the dispatcher's
return value to stop at `sync` or `press` rather than guessing a count.

Closed-loop movement must not send a raw pixel residual as one large
relative event. The vendored event_filter accelerator is nonlinear
outside its identity band, so such requests can oscillate or overshoot.
`qmp_exec_target` therefore walks every `move` as combined-axis rel-1
events with the existing 5 ms fine-walk cadence. A probe that consumes
the pointer ROM must wait for five consecutive in-tolerance samples
before emitting its next marker. `sync` is the explicit barrier between
an earlier click sequence and pointer materialization.

The dispatch contract is verified by
`docs/evidence/task-2-phase10-interactive-dispatch-test.tcl`
(plain `tclsh`, no deps — runs in <1 s).

#### Three hard-won QEMU 11.0.3 lessons

(a) **`match_max` must be raised under log floods.** Tcl/expect's
default per-spawn-id buffer for regex matching is 2000 bytes. The
launch-phase green-pixel poll in `sponge_de_probe` spams
`sponge-de-probe: launch green poll N frac_per_mille=0` every 200 ms;
2000 iters × ~70 B = ~140 KB of poll spam pushed into
`qemu_spawn_id`. Under that flood, expect's match window keeps only
the TAIL of accumulated output — between markers, the buffer tail is
the poll-spam line, not the QMP-TARGET marker; the expect arms find
nothing to match and time out at 120 s. The fix (commit `b5bd9b0307`):
`qmp_exec_target` raises `match_max -i $qemu_spawn_id 200000` (200 KB)
immediately before the `expect` block; the run script re-raises it
after `global qemu_spawn_id` is set and before each `run_genode_until`
gate consumes data. The 200 KB cover any single inter-marker window
plus the green-poll flood. See the comment block at the
`match_max` raise in `qmp.inc` for the rationale. (`sponge_de_probe`
also reduces source-cadence: log only poll 0, every 50th iteration,
and the final iteration.)

(b) **`run_genode_until` gate patterns must NOT have a trailing `.*`
when markers follow the gate.** Example:

```
{.*sponge-de-probe: phase input PASS.*}    ;# trailing .* is GREEDY
```

The trailing `.*` consumes the entire rest of the buffer after the
match — swallowing the next QMP-TARGET marker that arrives in the same
read chunk. Discovered via the W2 panel-phase "off-by-one" cascade:
the input-PASS `run_genode_until` swallowed the panel S-click marker,
so the 2nd `qmp_exec_target` arm matched the panel close click (which
arrived AFTER the "waiting" message) and the launch markers cascaded
forward. Fix (commit `a9ca5ffd9e`): drop the trailing `.*`, keep the
leading `.*`. The leading `.*` only consumes output BEFORE the marker
(bounded); the trailing wildcards are the smell.

(c) **Event pacing (`after`) is required for PS/2 REL bursts.** QEMU
11's emulated PS/2 controller drops unsynchronized bursts of
`input-send-event` REL events into a single accumulated delta
(observed: 50× rapid `rel(-50)` collapses into 2× effective motion on
this host). The QMP `mouse_set` + `input-send-event` style used by the
window-drag recipe needs an `after 5` (or so) between consecutive
per-axis REL events for the controller to deliver them as discrete
motions. The qmp.inc `qmp_ps2_click` proc paces per-axis events with
`after 5`; this is the empirically-tuned value for QEMU 11.0.3 on
Linux. Bumping it slower (`after 20`) is safe; faster (`after 0`)
silently loses events.

#### Two known QEMU 11 quirks (the beyond-`qmp.inc` caveats)

- **`send-key` strings are rejected.** QEMU 11 (and earlier — this
  is stable across versions) wants the `keys` argument as an array of
  objects `{"type":"qcode","data":"<name>"}`, NOT the older string
  form `["<name>"]`. The string form fails with `Invalid parameter
  type for 'keys[0]', expected: object` (see
  `docs/evidence/task-4-phase10-interactive.md` §Root cause 3). All
  scenarios in `run/qmp.inc` use the qcode-object form.

- **`input-send-event` BTN events are NOT delivered to untargeted
  devices.** QEMU 11 broadcasts untargeted abs/btn events across all
  pointer devices; if the PS/2 mouse is the current device (default
  under `-nographic`, `query-mice` shows `current:true`), the abs
  events are reinterpreted as PS/2 *relative* deltas (a slam to a
  corner — see the W4 evidence §Root cause 5). Fix: dispatch
  `mouse_set <tablet-index>` via HMP to make the usb-tablet the
  current device BEFORE the abs/btn events. `qmp_tablet_click` /
  `qmp_hmp` in `qmp.inc` implement the device-targeted recipe
  (used by `sponge-terminal-qmp.run`, criterion 5a).

#### Pointer model — usb-tablet-abs vs PS/2-REL tradeoffs

Under QEMU 11.0.3 `-nographic`:

- **usb-tablet absolute motion:** the click DOES reach sponge-de
  through the real usb-tablet → pc_usb_host → usb_hid → event_filter →
  nitpicker → sponge-de chain (criterion 1's proof is intact), but
  nitpicker's pointer sanitizer
  (`genode/repos/os/src/server/nitpicker/main.cc:780-810`) treats
  `Input::Absolute_motion` coordinates as raw pixels. Every abs value
  in 0..32767 lands off-screen on a 1024x768 display and is then
  **clamped to screen center (~512, 384)** — independent of the
  divisor you choose for `qmp_abs`. This is the
  `~29 px y-drift (384 - 412 = -28)` mystery from W1: it is
  `screen-center-y - intended-y`, not a per-event QEMU translation
  offset. W1's "click worked" because the demo domain
  (192,172,640,480) is large enough that the clamp point (512, 384)
  falls inside it.

- **PS/2 Mouse relative motion:** avoids the abs clamping entirely.
  The pointer walks the path explicitly — `qmp_ps2_click` and
  `qmp_drag` use event_filter's `<accelerate>` chain
  (`sensitivity_percent=1000, max=50, curve=127`, the standard
  recipe in
  `genode/repos/os/recipes/raw/drivers_interactive-pc/event_filter.config`)
  which gives a non-linear LUT: `rel-1` → 1 px, `rel-50` → ~100 px,
  `rel-100` → ~150 px. The clamp-to-(0,0) + coarse + fine sequence
  lands the pointer ±1 px of any absolute target.

  The PS/2 path exercises a strictly-equivalent hardware input chain
  (QMP → emulated PS/2 controller → ps2 driver → event_filter →
  nitpicker → sponge-de / wm / decorator) — just the PS/2 branch of
  `event_filter`'s input handling instead of the usb branch.

The interactive scenario (`sponge-de-sel4-interactive.run`) uses the
**PS/2 RECIPE for every click**, including the criterion-1 input click.
The criterion-1 chain is still proven (the click reaches sponge-de's
`input` report), and the precise targeting makes all panel/launch
markers land inside their respective rects.

#### Recipe-matches-`event_filter.config` caveat

`qmp_ps2_click`'s coarse walk (`5 × rel-50` ≈ 100 px per event)
assumes the vendored event_filter `<accelerate>` chain
(`rel-50 → ~100 px`). The custom staged `event_filter.config` used
by `sponge-de-sel4-interactive.run` *removes* the `<accelerate>`
wrapper (see the run script's `event_filter.config` block), so
`rel-50 → 50 px` (1:1). With the 1:1 mapping, the recipe's coarse
walk covers half the intended distance — the `FIRST_ENTRY` target in
`sponge_de_probe` is doubled (target (170, 73) → target (340, 170))
so the recipe's halved walk lands on the geometric-correct center.
Future users reusing `qmp_ps2_click` from `sponge-de-sel4-
interactive.run`'s event_filter.config (1:1) MUST match the recipe to
their `event_filter.config` — either restore `<accelerate>` to get
the standard 100-px coarse walk, or compensate the recipe's halved
walk by doubling the target coordinates as the scenario does.

#### Manual interactive viewing (control escape hatch)

For a human to drive the running guest from the host instead of the
script-driven QMP choreography, append the QEMU flags to spawn QEMU
with the SDL display and replace the final `run_genode_until` with
`run_genode_until forever`:

```bash
# Manual equivalent (host-side mouse + keyboard reach the guest):
make -C genode/build/x86_64 run/sponge-de-sel4-interactive \
  KERNEL=sel4 BOARD=pc QEMU_OPT="-display sdl"
```

(The QEMU args are placed BEFORE the run script's own
`append qemu_args "-qmp tcp:..."` — the `-qmp tcp:` is still added,
but on QEMU 11 the SDL display and the QMP listener both work in the
same guest.) Human input from the SDL window reaches the same driver
chain (event_filter is enabled in the boot config), so this proves the
script-driven QMP path is equivalent to a real user's mouse.

### 4.5 Phase-11 DE customization (configd keys, themes, window chrome)

Phase 11 (`docs/plans/phase11-de-customization.md`) makes the desktop
customizable through `sponge_configd` without touching Genode
internals. The live data path:

```
vct config <key> <value>
  -> sponge_configd (validated, broadcast on the "config" report)
  -> sponge-de's ConfigController (<config source="configd"/> gate)
  -> panel / launcher re-styled live (Qt GUI thread, marshalled)
```

New keys (validation in `sponge_configd/main.cc`, full table in
`repos/sponge/src/sponge_configd/README.md`):

| Key | Type | Default | Effect |
|---|---|---|---|
| `panel.height` | uint 16–128 | `28` | Panel thickness; launcher toggle tracks it |
| `panel.visible_widgets` | enum-list of `clock,launcher` | `clock,launcher` | Hide/show panel widgets live |
| `clock.format` | printable-ASCII format string | `HH:mm` | Qt time format; invalid values fall back to `HH:mm` with a warning on the panel side |
| `launcher.sort_by` | enum `alpha`,`manual` | `alpha` | Launcher entry ordering (manual = pkgd broadcast order) |
| `panel.position` | enum `top`,`bottom`,`left`,`right` | `bottom` | **Boot-time only** (nitpicker domain owns placement; change the run script domain and reboot) |

Automation is the default (`vct config panel.height 40` just works);
the control escape hatch is the same channel (`vct config` per key)
plus the theme files themselves.

Themes: four shipped (`default`, `light`, `dark`, `compact` under
`repos/sponge-de/src/sponge-de/themes/`); `vct theme apply <name>`
switches live. The unknown-theme path is hardened: a
`label_suffix=".theme"` catch-all route hands `sponge_themed` an
empty ROM, so a typo keeps the previous theme and the daemon stays
alive (proven by the 5-step probe in `run/sponge-theme.run`). The
transport cap is 8192 bytes — guard it with
`./tool/test_theme_payload_size` after editing a theme.

Window chrome: `run/sponge-de-themed-chrome.run` replaces the stock
decorator with upstream `themed_decorator`. The chrome assets live in
`decor.tar`, authored by `./tool/decor_assets` (metadata + PNGs +
byte-vendored `font.tff` under `tool/decor_assets_data/` — edit
`metadata.txt` and re-run the tool to redesign the frame geometry;
that is the documented manual step). The title-bar tint follows the
active theme live: `sponge_decorator_bridge` watches the theme ROM
and republishes the decorator's whole config (libc + vfs + policy
color) through report_rom. Note that the tar *assets* are boot-time
(static in upstream); only the policy color is live (roadmap §11.3).

---
