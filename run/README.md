# run/ — Genode run scenarios

Each `.run` file in this directory defines one boot scenario. The
Genode build system runs it like this (the wrapper script does the
same thing):

```bash
./tool/build run sponge-minimal
# Manual equivalent:
make -C genode/build/x86_64 run/sponge-minimal
```

A scenario defines:

- The list of binaries to build
- `init`'s boot configuration (component tree, resources, routing)
- The boot modules (files that go into the RAM disk)
- The QEMU setup (or the real-hardware run options)
- The verification logic (expected-output matching for automated tests)

## Current scenarios

- `sponge-boot.run` — Phase 8 P1: storage-chain smoke test on base-sel4.
  Proves the Tier-0 runtime disk-read chain end-to-end (docs/14 §4.4):
  `platform`/`acpi`/`pci_decode` → `ahci` (Block) → `part_block` (Block/P3,
  partition pinned BY NUMBER) → `vfs` (`<rump fs="ext2fs"/>` → File_system)
  → `cached_fs_rom` (chroot `/system`, ROM service) → `boot_probe` (opens
  ROM `marker.txt`, validates exact content, logs `boot-probe: PASS`).
  Produces a disk image via `RUN_OPT="--include image/disk"` whose GENODE
  ext2 P3 contains `/system/marker.txt`; the `image/disk` tool
  auto-e2cp's the whole run dir into P3. The `boot_probe` component
  (`repos/sponge/src/test/boot_probe/`) is a tiny plain-Genode probe
  (no libc, no Qt). AHCI variant is the default; NVMe variant is selected
  via `SPONGE_BOOT_NVME=1` (boots from the same .img via AHCI auto-attach,
  but storage reads from a separate GPT disk attached via `-device nvme`).
  base-sel4 only.

- `sponge-desktop-disk.run` — Phase 8 P2: the FULL Alpha desktop booted
  from disk (docs/14 §4.4–§4.7). Tier 0 (image.elf) contains ONLY the
  boot+storage chain (kernel/core/init/ld.lib.so/timer/report_rom/
  platform/acpi/pci_decode/ahci/part_block/vfs+rump) plus nitpicker and
  the display/input drivers sub-init (vesa_fb/ps2/usb_hid/event_filter —
  justified as boot-critical + rescue-display, §4.5/§4.7). A nested
  system init (binary `init` served from `/system/bin/init` via
  `cached_fs_rom`, config from `/system/init/system.config`) hosts the
  desktop stack: wm/window_layouter/decorator, sponge_configd,
  sponge_themed, sponge_pkgd (with `binary_prefix="bin/"` + §4.6
  ld.lib.so route in generated configs), pkg_runtime, sponge-de, and
  alpha_probe. ALL their binaries and libs (Qt6, Mesa, libc, etc.) are
  served from `/system/bin/` and `/system/lib/` via two cached_fs_rom
  instances (rom_sys chroot `/system`, rom_lib chroot `/system/lib` for
  `.lib.so` files). Every dynamically linked child carries the §4.6
  route `ROM label_last="ld.lib.so" → parent`. ld.lib.so is Tier-0 only
  (never in `/system/lib/`). `alpha_probe` (with `skip_lz="yes"`) asserts
  themed panel pixel + launcher feed + configd broadcast from disk-served
  binaries; lz_viewer (criterion d) is deferred to P5. The image.elf
  size is printed and asserted ≤80 MiB. Failure channel:
  `SPONGE_DISK_FAIL=1` stages a corrupted system.config for a bounded,
  identified failure. base-sel4 only; `RUN_OPT="--include image/disk"`.

- `sponge-persist-disk.run` — Phase 8 P3: persistence on SPONGE-DATA
  (docs/14 §6). Proves sponge_pkgd's installed-set store survives a
  reboot when backed by a writable ext2 on the install media's fourth
  GPT partition. A focused, headless scenario (no desktop/Qt6 — keeps
  the P2 regression gate pristine and the proof fast): Tier-0 chain
  (platform/acpi/pci_decode/ahci/part_block) → `vfs_data` (a second
  `vfs` instance: Vfs_block on P4 + `<rump fs="ext2fs"
  writeable="yes"/>`) → sponge_pkgd with its `<vfs>`-activated store
  (docs/12 §13) wired to vfs_data → pkg_seq_probe. After image/disk
  produces the .img, `tool/mkdata` runs the docs/14 §4.3 sequence
  (truncate + sgdisk delete/move/new/hybrid + mkfs.ext2 -E offset) to
  add P4=SPONGE-DATA; the run script pre-creates /store on P4 via
  dd+e2mkdir. Two boots over the SAME .img (QEMU attaches it
  `-drive format=raw` with NO -snapshot, so writes persist to the host
  file): boot 1 installs hello and writes the store; a host-side e2cp
  readback of /store/installed.xml off the image file corroborates the
  write (misleading_success_output defense); boot 2 boots a fresh QEMU
  against the same image and gates on `sponge_pkgd: restored N root(s)
  from store` — the adversarial restoration proof (impossible unless
  boot 1's write survived). Failure channel: `SPONGE_PERSIST_RO=1`
  makes vfs_data read-only; pkgd's _save_store refuses with a bounded
  "cannot open store for write" log line (docs/12 §13.3) and the host-
  side readback confirms no store appeared (write refused, not
  corrupted). base-sel4 only; `RUN_OPT="--include image/disk"`.

- `sponge-minimal.run` — Phase 1 goal: minimum boot (skeleton).
  Kernel-agnostic; verified on `base-linux` (no QEMU) and `base-sel4`
  (QEMU, production target).

- `sponge-vct-help.run` — Phase 2: `vct help` prints the command summary.
  Kernel-agnostic.

- `sponge-vct-version.run` — Phase 2: `vct version` prints the version.
  Kernel-agnostic.

- `sponge-vct-status.run` — Phase 2: `vct status` reads live init state
  through a sub-init + `report_rom` relay. Kernel-agnostic.

- `sponge-vct-component-list.run` — Phase 2: `vct component list` lists
  the running components from the live init state report. Kernel-agnostic.

- `sponge-de.run` — Phase 3: interactive Sponge DE demo (panel + themed
  demo window) on base-linux with fb_sdl + nitpicker. Opens an SDL
  window on the host display and accepts host mouse/keyboard. Ends on
  the `sponge-de: panel and window shown` marker; replace the final
  `run_genode_until` with `run_genode_until forever` for interactive
  viewing.

- `sponge-de-test.run` — Phase 3: automated, headless GUI verification.
  No framebuffer driver and no host display: the `sponge_de_probe`
  component reads nitpicker's composited pixels via a Capture session
  (window rendering check) and injects a synthetic click via the Event
  service (input round-trip check), then logs `sponge-de-probe: PASS`.
  Kernel-agnostic.

- `sponge-wm.run` — Sponge DE on the upstream window-management stack
  (wm + window_layouter + decorator). The panel bypasses the wm to a
  fixed nitpicker domain; app windows route through the wm and get
  decorated, movable frames. `wm_probe` verifies the decoration
  pipeline and injects a synthetic title-bar drag, confirming the
  window actually moves (`wm-probe: PASS`). Headless.

- `sponge-de-sel4-interactive.run` — base-sel4 (seL4 / QEMU) interactive
  GUI driver stack: the vendored `drivers_interactive-pc` set (vesa_fb,
  ps2, usb_hid, pc_usb_host, event_filter, platform, acpi, pci_decode)
  built from source and wired with `-device nec-usb-xhci -device
  usb-tablet` for absolute-pointer input. **Phase 10** extended the
  scenario in place: every input action is now dispatched from the host
  via QEMU QMP (Tcl `socket` over TCP, PID-derived port) using the
  shared `run/qmp.inc` helper. The probe runs in three bounded observe
  phases (FATAL — any timeout/marker failure is a hard FAIL):
  1. **input** (criterion 1) — a host QMP click on the demo domain,
     real driver chain verified end-to-end (`phase input PASS`).
  2. **panel** (criterion 4) — a host QMP click on the panel's S
     launcher toggle opens the popup; a second QMP click on the demo
     body closes it (`phase panel PASS`). The panel's only
     clickable widgets today (the launcher S toggle and launcher
     entries inside the popup) are exercised. The clock is a passive
     `QLabel` by design; additional panel widgets are Phase 11 scope.
  3. **launch** (criterion 3) — opens the launcher popup, then a
     QMP-driven click on the first installed launcher entry
     (`pkg_gui_demo`) goes through the full chain: Qt click →
     `LauncherController::request_launch` → `launcher_request`
     report → `sponge_pkgd` `_do_launch` → regenerated `pkg_runtime`
     config → `pkg_gui_demo` first paint, green-pixel verified
     (`pkg_gui_demo: window shown` + `pkg_gui_demo green pixel
     detected` + `phase launch PASS`).
  Final markers: `sponge-de-probe: PASS` + `Run script execution
  successful.`. See `docs/evidence/phase10-index.md` for the per-phase
  mapping, `docs/evidence/task-2-phase10-interactive-{run1,run2}.log`
  for two consecutive green runs, and `docs/08-development.md` §"Host-
  driven QMP input" for the qmp.inc API + QEMU-11 caveats. base-sel4
  only (`assert {[have_spec sel4]}`); the Qt6/Mesa-on-sel4 root cause
  remains in `docs/09-roadmap.md` §11.1 (capability exhaustion;
  `caps: 1000` on seL4).

- `sponge-wm-qmp.run` — **Phase 10 criterion 2**: real-pointer
  window drag on base-sel4. Same topology as `run/sponge-wm.run`
  (upstream `wm` + `window_layouter` + `decorator` stack) plus the
  vendored `drivers_interactive-pc` drivers sub-init copied from
  `run/sponge-de-sel4-interactive.run`, plus `sponge_pkgd` +
  `pkg_runtime` (with the W3 phase `+ service Gui | + child wm` route
  fix and a layouter `<assign label_prefix="pkg_runtime"
  target="screen" xpos=50 ypos=300 width=320 height=240>` rule), plus
  the staged `pkg_gui_demo` package and `wm_probe` in `inject="no"`
  observe mode. QEMU: `-m 2G`, xhci + usb-tablet, `run/qmp.inc`-driven
  PS/2 Mouse drag (proven more reliable than the usb-tablet
  absolute-axis recipe under QEMU 11.0.3 because nitpicker's pointer
  sanitizer clamps every `Absolute_motion` value to screen center;
  see the qcode-block comment at the top of `run/qmp.inc`). The probe
  launches `pkg_gui_demo` via `sponge_pkgd`'s `request` channel,
  observes its window appearing in the `window_layout` ROM, emits a
  `QMP-TARGET drag <x1> <y1> <x2> <y2>` marker, waits for the
  `window_layout` position change AND a Capture pixel check at the new
  location, then asserts `wm-probe: PASS`. Gates: fb `using 1024x768` →
  usb_hid `POINTER` → QMP drag → `wm-probe: PASS`. base-sel4 only.
  See `docs/evidence/task-3-phase10-interactive.md` and
  `docs/evidence/phase10-index.md`.

- `sponge-terminal-qmp.run` — **Phase 10 criterion 5a**: real QMP
  keyboard input to a focused terminal. Topology: the
  `run/sponge-terminal.run` setup (terminal package, bash-minimal.tar,
  VeraMono.ttf, vfs_ttf) plus the vendored `drivers_interactive-pc`
  drivers sub-init, plus `run/qmp.inc` (Tcl QMP over TCP). The keyboard
  chain: QMP `send-key` → emulated PS/2 keyboard → `ps2` driver →
  `event_filter` (`en_us.chargen` + `special.chargen`) → nitpicker →
  focused terminal Gui session (`pkg_runtime -> terminal -> terminal`)
  → gems terminal server read buffer → `/dev/terminal` (vfs
  `<terminal/>` plugin) → noux bash echo → terminal re-render,
  observed as a glyph-count increase. The `terminal_probe` `qmp="yes"`
  mode emits `QMP-TARGET click <gx> <gy>` at the terminal window
  center (focus), then `QMP-TARGET type echo ok`; the run script
  connects QMP and dispatches via `qmp_send_key` (the
  qcode-object form, NOT the rejected string form — see
  `docs/08-development.md` §"Host-driven QMP input"). All probe waits
  bounded; the click + type are driven by host QMP, not synthetic
  Event injection. Gates: fb → usb_hid → render → focus click → type
  → `terminal-probe: PASS`. base-sel4 only. See
  `docs/evidence/task-4-phase10-interactive.md`.

- `sponge-textedit-qmp.run` — **Phase 10 criterion 5b**: real QMP
  keyboard input to a focused text editor. Topology: the
  `run/sponge-textedit.run` setup (qt6_textedit, full-screen
  `edit` domain at (0,0) with `focus: click`, staged textedit payload
  from `pkg/textedit/payload/`) plus the `drivers_interactive-pc`
  drivers sub-init, plus `run/qmp.inc`. The keyboard chain runs end-to-
  end; the `textedit_probe` `qmp="yes"` mode emits `QMP-TARGET click`
  for focus, then `QMP-TARGET type hello`, and verifies the typed
  delta's Capture sample exceeds 2× the cursor-blink baseline
  (`typed_delta >= TYPED_FLOOR && typed_delta > 2*baseline`, the
  `misleading_success_output` defense: a lone cursor blink cannot
  pass). Gates: fb → usb_hid → render → focus click → type →
  `textedit-probe: PASS`. base-sel4 only. See
  `docs/evidence/task-5-phase10-interactive.md`.

- `run/qmp.inc` — shared Tcl helper for the four Phase 10 QMP
  scenarios above. Sourced by a run script via
  `source [file join [file dirname [file normalize [info script]]] qmp.inc]`
  (Tcl 8.x, no external dependencies — only `socket` and `expect`,
  already used by the run tool). Provides: `qmp_pick_port` (free TCP
  port via Tcl `socket -server 0`), `qmp_connect` (bounded retry +
  QMP greeting read + `qmp_capabilities` handshake), `qmp_cmd` (send
  one JSON, skip async events, die loud on `{"error":...}`),
  `qmp_abs`, `qmp_pointer_move`, `qmp_button`, `qmp_click`,
  `qmp_ps2_click` (W3 calibrated relative-recipe with clamp-to-(0,0)
  + coarse rel-50 + fine rel-1 + hover jiggle), `qmp_drag`,
  `qmp_send_key` (qcode-object form on QEMU 11; the string form is
  rejected with `Invalid parameter type for 'keys[0]', expected:
  object`), `qmp_type` (char → QEMU keyname map), `qmp_exec_target`
  (bounded `expect` on global `qemu_spawn_id` for QMP-TARGET markers
  emitted by probes; FAIL + exit 1 on timeout), `qmp_disconnect`.
  Patterns anchor on `\r*\n` because QEMU's `-nographic` serial emits
  CR CR LF (verified). The `match_max -i $qemu_spawn_id 200000`
  raise inside `qmp_exec_target` is required under log floods (see
  the comment at the call site: the launch-phase green-pixel poll
  spams ~140 KB at 200 ms cadence; the default 2000-byte `match_max`
  window then keeps only the tail of accumulated output and the
  expect arms time out). See `docs/08-development.md` §"Host-driven
  QMP input" for the API summary + the hard-won QEMU 11 lessons.
  Mirrored at `repos/sponge/run/qmp.inc` via a committed relative
  symlink so the Genode build repo-discovery picks it up regardless
  of which directory the build was launched from.

- `sponge-pkg-explain.run` — Phase 4a: end-to-end package-explain flow.
  `vct install nano --explain` writes a request report that `report_rom`
  relays to `sponge_pkgd`, which resolves the package + its `ncurses`
  dependency from staged `pkg_*.xml` metadata ROMs and returns the
  4-step install plan (docs/06-vct.md §5.2). Kernel-agnostic.

- `sponge-pkg-install.run` — Phase 4b: `vct install hello` actually
  starts the `hello` component under a nested `pkg_runtime` init.
  `sponge_pkgd` regenerates pkg_runtime's config (relayed as its `config`
  ROM by `report_rom`); pkg_runtime starts hello, verified by the
  payload's boot marker. Kernel-agnostic.

- `sponge-pkg-remove.run` — Phase 4b: full install→remove lifecycle.
  The `pkg_seq_probe` component drives the channel (vct is short-lived),
  installs hello, removes it, and confirms both results; verified by a
  PASS marker. Kernel-agnostic.

- `sponge-pkg-list.run` — Phase 4c: the `pkg_seq_probe` installs hello,
  then issues a `list` request and asserts hello appears in the result,
  verifying the installed-set inspection path. Kernel-agnostic.

- `sponge-pkg-manual.run` — Phase 4c: `vct install hello --manual`
  renders the install step-by-step (explain→steps→execute) and still
  starts hello under pkg_runtime; verified by hello's marker. Kernel-
  agnostic.

- `sponge-pkg-persist.run` — Phase 4 follow-up #2: installed-set
  persistence across a reboot. Two boots over the same `lx_fs`-backed
  host directory (modelled on upstream `depot_remove.run`): boot 1
  installs hello and writes the store; boot 2 boots a fresh `core`/init
  against the same directory and issues a `list`-only request, asserting
  hello is present — which can only be the restored store, since no
  install was issued this boot. `base-linux` only (lx_fs is a Linux-host
  wrapper); the component code itself is portable.

- `sponge-media-smoke.run` — Phase 7 todo 3: minimal seL4 media smoke
  test. Same payload as `sponge-minimal.run` (core, lib/ld, init, vct)
  but the boot image is materialised as a real El Torito ISO or GPT disk
  image and QEMU boots from that media file (not a direct kernel boot).
  Run with `RUN_OPT="--include image/iso"` or `RUN_OPT="--include
  image/disk"`; gates on vct's banner. Proves the grub2→bender→seL4
  boot chain from removable media before the full desktop exists.
  base-sel4 only.

- `sponge-alpha.run` — Phase 7 todo 4: the unified Alpha desktop
  scenario on base-sel4. Merges five proven component sets into ONE
  boot: the interactive-PC driver stack (vesa_fb/ps2/usb_hid/xHCI), the
  upstream `wm`+`window_layouter`+`decorator` window-management stack,
  the live `sponge_configd`→`sponge_themed`→sponge-de theme pipeline,
  the `sponge_pkgd`+`pkg_runtime` launcher feed (with pre-staged
  `hello`), and the Leitzentrale subsystem (`lz_runtime`/`lz_bridge`/
  `lz_watch`/`lz_viewer`). QEMU RAM bumped to 2G. The composite
  `alpha_probe` asserts all four Alpha criteria in bounded iterations
  (themed panel pixel, launcher report carries `hello`, configd
  broadcast ROM live, lz_viewer Leitzentrale window pixel) and logs
  `alpha-probe: PASS`; the run gates on that marker (fail-loud on
  timeout). base-sel4 only.

- `sponge-pkg-gui.run` — Phase 7 todo 8: GUI-package runtime-config
  generator verification. Installs a minimal Qt6 colored-window package
  (`pkg_gui_demo`) through `sponge_pkgd` and pixel-verifies its
  distinctive green (#00ff00) window through a Capture session, proving
  the generator correctly emits `<binary>` (when it differs from
  `<name>`), the inline `<config>` verbatim (Qt6/libc wiring), the
  `<parent/>` route for `default-route="nitpicker"` Gui sessions, the
  extended `parent-provides` (Gui/Input/Report/File_system/NIC), and
  the GUI-safe `<default>` caps floor (1000, per the §11.1 lesson).
  Kernel-agnostic topology (base-linux native or base-sel4 QEMU); the
  acceptance run uses `KERNEL=sel4 BOARD=pc`. Gates on
  `pkg-gui-probe: PASS`. Updated in todo 9 to issue a `launch`
  after install (pkg_gui_demo has no `<autostart/>`, so install alone
  no longer starts the component).

- `sponge-pkg-lifecycle.run` — Phase 7 todo 9: installed-vs-running
  lifecycle verification (docs/12-package-format.md §9.2.1). The
  `pkg_lifecycle_probe` (modeled on `pkg_seq_probe`) drives a six-step
  sequence against `sponge_pkgd` AND reads the `installed` broadcast
  ROM to assert per-package `running="yes"|"no"` after every
  transition: (1) install `hello` → broadcast shows `hello/running=yes`
  (its metadata declares `<autostart/>`); (2) install `pkg_gui_demo`
  → `running=no` (no autostart — STOPPED); (3) `launch pkg_gui_demo`
  → `running=yes` AND pkg_gui_demo's `window shown` marker (the run
  gates on the marker to prove the launched `<start>` node actually
  booted the component); (4) launch again → `already-running`
  (idempotent); (5) `launch nosuchpkg` → `not-installed` (no implicit
  install); (6) `remove pkg_gui_demo` → broadcast drops it (`<start>`
  node dropped from `pkg_runtime`). Kernel-agnostic; the acceptance
  run uses `KERNEL=linux BOARD=pc` (fast iteration — Qt6 first paint
  under softpipe is the slow part on seL4). Gates on
  `lifecycle-probe: PASS`.

- `sponge-launch.run` — Phase 7 todo 10: click-to-launch + `vct launch`
  verification. Proves BOTH launch paths share the same `sponge_pkgd`
  backend (AGENTS.md §3.3 rule 5 — two interfaces, one backend). The
  `launch_probe` (1) sends `launch pkg_gui_demo` over the same
  `request` channel `vct launch` uses (VCT PATH — pkg_gui_demo boots
  and logs its `window shown` marker); (2) removes + reinstalls the
  package; (3) injects a synthetic click on sponge-de's launcher button
  to open the popup, then on the first menu entry — sponge-de's
  `LauncherController` writes `launch pkg_gui_demo` to the dedicated
  `launcher_request` channel (report_rom is single-writer per label,
  so the long-lived launcher cannot share vct's `request` label);
  pkgd processes it via the SAME `_do_launch` and answers on
  `launcher_result`; the probe pixel-verifies the green (#00ff00)
  window (CLICK PATH); (4) verifies `launch nosuchpkg` →
  `not-installed` and double-launch → `already-running`, emitting
  vct-equivalent JSON lines the run script asserts. Kernel-agnostic
  topology (base-linux native or base-sel4 QEMU); the acceptance run
  uses `KERNEL=sel4 BOARD=pc`. Gates on `launch-probe: PASS` (two Qt6
  first paints under softpipe Mesa on seL4; generous 600s timeout).

- `sponge-net-probe.run` — Phase 7 todo 12: networking probe on
  base-sel4, proving the stack BEFORE Falkon arrives (todo 16). The
  dde_ipxe e1000 NIC driver (`ipxe_nic`, an Uplink client) is bridged
  to a Nic session by `nic_uplink` (the no-`nic_router` pattern from
  upstream `os/run/nic_uplink.run` — `nic_router` is explicitly out of
  Alpha scope). `fetchurl` (libcurl + libc) loads the `lwip` vfs
  socket plugin at `/socket`, DHCP-configures via lwip, and GETs
  `http://10.0.2.2:8765/net-fixture.txt` from a host-side
  `python3 -m http.server` started by the run script (lifecycle:
  spawned before `build_boot_image`, killed by PID + pkill fallback at
  end-of-run). The fixture's first line is read off the file BEFORE the
  run and the run gates on that exact byte string appearing in the
  boot log (the `misleading_success_output` adversarial class — exit 0
  alone is not enough; the bytes must round-trip). Also starts the
  `system_clock` subsystem (`pc_rtc` + `system_rtc`) so the Rtc
  session TLS will need later is proven to come up on seL4. Bounded
  `run_genode_until` timeouts everywhere (the `hung_or_long_commands`
  class). base-sel4 only (`assert {[have_spec sel4]}`).

- `sponge-terminal.run` — Phase 7 todo 13: the terminal package
  (`pkg/terminal`), source-built from in-tree components (no depot
  import). The `terminal_probe` drives `sponge_pkgd` to install and
  launch `terminal`, then verifies the running stack: (a) the
  `installed` broadcast carries `terminal` (the `vct list`-equivalent
  check), (b) the terminal window + bash prompt render on nitpicker
  (Capture glyph-pixel check), and (c) a synthetic keystroke round-trips
  (focus click + `Press_char` events via nitpicker's Event service →
  gems terminal server read buffer → `/dev/terminal` via the vfs
  `<terminal/>` plugin → noux bash echo → terminal re-render, observed
  as a glyph-count increase). The package is a nested sub-init
  (`binary: init`) hosting the gems graphical `terminal` server, a `vfs`
  (with the `<terminal/>` plugin that bridges the terminal session to
  `/dev/terminal`), a `cached_fs_rom` (serves `/bin/bash` out of
  `bash-minimal.tar`), and the noux `/bin/bash` child — the
  source-built system_shell composition. Stages `VeraMono.ttf` (the
  terminal's monospace font, via the `vfs_ttf` plugin). Kernel-agnostic
  topology (base-linux native or base-sel4 QEMU); the acceptance run
  uses `KERNEL=sel4 BOARD=pc`. Gates on `terminal-probe: PASS`.

- `sponge-textedit.run` — Phase 7 todo 14: the text-editor package
  (`pkg/textedit`), the depot-repackaged `cproc/pkg/qt6_textedit/
  2025-10-27` (todo 11). The `textedit_probe` drives `sponge_pkgd` to
  install and launch `textedit`, then verifies: (a) the `installed`
  broadcast carries `textedit` with `running="no"` (`vct list`-
  equivalent + lifecycle), (b) after launch the broadcast flips to
  `running="yes"`, (c) the Qt6 text-editor window actually renders into
  nitpicker (Capture non-background-fraction check across the domain —
  the qt6_textedit widget paints a menu bar / toolbar / rich-text area
  everywhere distinct from the `#1e1e2e` nitpicker background; the
  `misleading_success_output` class — a bare exit 0 is not enough, the
  rendered scene must really appear), and (d) pkgd's launch error paths
  return clear statuses (`not-installed`, `already-running`), never a
  crash. The prebuilt `textedit` binary is NOT a Sponge build target
  (no in-tree `src/app/qt6_textedit`); it is staged from
  `pkg/textedit/payload/` (extracted from `cproc/bin/x86_64/qt6_textedit/
  2025-10-12` by `tool/pkg_import`). The separate missing-binary
  failure channel is verified by the scratch scenario
  `run/sponge-textedit-fail.run` (evidence log retained by the Phase 7
  reviewer — see the Phase 7 evidence index).
  Kernel-agnostic topology (base-linux native or base-sel4 QEMU); the
  acceptance run uses `KERNEL=sel4 BOARD=pc`. Gates on
  `textedit-probe: PASS`.

- `sponge-files.run` — Phase 7 todo 15: the file-manager package
  (`pkg/files`), source-built from the in-tree Qt6 component
  `repos/sponge/src/sponge_files` (a single Qt6 Widgets binary, not a
  nested sub-init). The `files_probe` drives `sponge_pkgd` to install
  and launch `files`, then verifies: (a) the installed broadcast
  carries `files` (`vct list`-equivalent) AND the Qt6 file-manager
  window actually renders into nitpicker (Capture non-background-
  fraction AND color-diversity check across the 800x600 "files" domain —
  themed list + preview + accent buttons; the
  `misleading_success_output` class — bare exit 0 is not enough); (b)
  a synthetic double-click on the list's first row navigates into
  `/demo` (asserted via the component's structural `files` report path
  transitioning `/` -> `/demo` — NOT by parsing pixels); (c) copy
  `/demo/notes.txt` -> `/writable/notes.txt` then delete
  `/writable/notes.txt` driven via the request channel (the GUI is the
  manual escape hatch; the request channel is the automation default,
  AGENTS.md §3.3 rule 2); (d) attempt delete in the read-only area
  (`/demo/notes.txt`) is refused — the tar-backed `/demo` is read-only
  by nature so `unlink(2)` returns `EROFS`, and the refusal surfaces
  three ways at once (UI status label + `Genode::log` line + the
  `files` report's `last_action result="refused"`). The fixture area
  (`/demo`) is staged from `run/fixtures/files-demo/` and packed into
  `files_demo.tar` at scenario time. Kernel-agnostic topology
  (base-linux native or base-sel4 QEMU); the acceptance run uses
  `KERNEL=sel4 BOARD=pc`. Gates on `files-probe: PASS`.

- `sponge-falkon.run` — Phase 7 todo 16: the Falkon web browser package
  (`pkg/falkon`), the depot-repackaged `cproc/pkg/falkon_qt6-jemalloc/
  2026-04-22` (todo 11). Falkon is a full Qt6 WebEngine browser (~500MB
  payload, ~1G RAM) — the heaviest Alpha component. The scenario merges
  the textedit GUI topology + the net-probe networking stack (ipxe_nic +
  nic_uplink + pc_rtc + system_rtc). Because falkon's 64-file closure
  exceeds the seL4 boot chain's module-size ceiling (~256MB; Bender's
  relocation limit + seL4's untyped cnode exhaustion), the payload is
  packed into `falkon_payload.tar` and added as a SEPARATE multiboot2
  module (not inside image.elf); a `tar_rom` server reads it and serves
  individual files as ROM sessions (pkg_runtime routes falkon's ROM
  requests there). The `falkon_probe` drives `sponge_pkgd` to install
  and launch falkon, then pixel-verifies the browser window via Capture
  (rendered-fraction + color-diversity). The run script separately
  verifies the host fixture GET (falkon's config arg navigates to
  `http://10.0.2.2:8765/net-fixture.txt` on startup). **KNOWN
  LIMITATION:** the seL4 boot chain cannot currently handle falkon's
  500MB+ payload — even with the tar_rom approach, seL4 resets during
  boot module setup (untyped cnode exhaustion). See
  the Phase 7 todo-16 evidence §2.2 for the full diagnosis
  and the resolution path (disk-based payload). base-sel4 only. Bounded
  timeouts (900s probe + 180s GET check).

- `sponge-falkon-disk.run` — Phase 8 P4: Falkon FROM DISK on base-sel4
  (the ceiling-killer proof, docs/14-boot-storage-architecture.md §4.4
  key property "Falkon boots"). Applies the P2 desktop-from-disk pattern
  to falkon's 509 MiB WebEngine closure — the payload Phase 7 packaged
  (`pkg/falkon/`, 64 ROMs) but could NOT boot because the seL4 boot
  chain has a ~256 MiB boot-module ceiling (Bender relocation + seL4
  untyped cnode, docs/14 §2.1). The payload is staged under
  `/system/pkg/falkon/payload/` on the GENODE ext2 P3 and served as ROM
  sessions by a THIRD `cached_fs_rom` instance (`rom_pkg`, chrooted
  there) — NONE of it is a Tier-0 boot module, so `image.elf` stays at
  ~12 MiB (the P2 Tier-0 roster, unchanged). **This architectural claim
  is PROVEN** (`docs/evidence/p4-falkon-disk.log`): falkon's process
  starts, the dynamic linker resolves the 237 MiB
  `libQt6WebEngineCore.lib.so` from disk via rom_pkg, and lwIP DHCPs
  over the ipxe_nic+nic_uplink stack (address=10.0.2.15). The Tier-0
  `system`-child route rule `label_prefix: pkg_runtime → child rom_pkg`
  catches falkon's payload ROM requests (binary + WebEngine libs + Qt
  tars), forwarded up through the nested init, and serves them from
  disk. `sponge_pkgd` runs WITHOUT `binary_prefix` so falkon's binary
  resolves via `rom_pkg`. The scenario adds the nic stack (`ipxe_nic` +
  `nic_uplink` from todo 12 / `sponge-net-probe.run`) and `pc_rtc` +
  `system_rtc`. The desktop CHROME (sponge-de/wm/decorator/themed) is
  deliberately omitted so falkon is the ONLY Qt renderer — the
  `falkon_probe` pixel check is then unambiguous (no desktop chrome to
  false-positive on, the misleading_success_output guard); the desktop
  is separately proven in P2. **Remaining blocker (orthogonal to the
  boot-chain claim):** base-sel4 gives every child PD a FIXED 8192-slot
  capability CNode (`CSPACE_SIZE_LOG2=13`, platform_pd.cc:199-216),
  independent of the init `caps` quota (30000/80000/200000 all fail
  identically). falkon's WebEngine runtime (render buffers / shared
  memory) exceeds 8192 caps and is stopped ("out of selector" → "denied
  RM-session") after DHCP, before first paint. The fix is a vendored
  base-sel4 patch to `CSPACE_SIZE_LOG2_2ND` (forbidden by the P4
  "no vendored-tree patches" constraint — documented for P5 in the
  evidence log §11). falkon_probe pixel check therefore does not pass
  on this base-sel4 build; the bounded 900s `run_genode_until` timeout
  governs (never a silent hang). The P4 run also delivered a real
  `sponge_pkgd` fix: the generated `parent-provides` now carries `Nic` +
  `Rtc` in the canonical casing (the prior `NIC` was wrong-cased and
  `Rtc` was absent — falkon was stopped at "denied Rtc-session"). Sizing:
  `pkg/falkon/` `caps=200000` (documented as moot on seL4 due to the
  CNode, but correct for a patched/hw kernel); `falkon ram=1G`;
  `pkg_runtime caps=210000 ram=1500M`; `system caps=250000 ram=3000M`;
  `rom_pkg ram=768M`; QEMU `-m 6G`. Failure channel:
  `SPONGE_FALKON_NO_FIXTURE=1` skips the host fixture (bounded GET-check
  failure). base-sel4 only; `RUN_OPT="--include image/disk"`.

- `sponge-power.run` — Phase 7 todo 17 (SUCCESS path): verifies
  `vct shutdown` actually powers the guest off through the real ACPI
  stack. There is NO `System` RPC session in Genode 26.05; the verified
  power path is the `system` ROM report consumed by the `acpica`
  component (`genode/repos/libports/src/app/acpica/os.cc`), the exact
  route upstream Sculpt uses. vct publishes `<system state="poweroff"/>`
  via a Report session; `report_rom` relays it as the `system` ROM;
  acpica calls `AcpiEnterSleepState(5)` (S5 → QEMU exits). The flat
  driver set (acpi/pci_decode/platform as top-level children, the
  `libports/run/acpica.run` pattern) provides the Platform device acpica
  needs. Verification (fail-loud): match the audit line
  `vct: shutdown: requesting poweroff`, then a bounded `expect`
  disambiguates — QEMU exit (`eof` = acpica acted = PASS) vs the
  unavailable line / hard timeout (FAIL). base-sel4 only. Reboot
  (`vct reboot`, state="reset") shares the same code path; change the
  vct config arg to verify, optionally with `-no-reboot`.

- `sponge-power-fail.run` — Phase 7 todo 17 (FAILURE channel): the
  companion to `sponge-power.run`. Same vct code path WITHOUT acpica.
  vct publishes the `system` report, runs its bounded 4s wait, observes
  the guest is still alive, and logs `System service (acpica)
  unavailable - guest did not power off within 4000ms` (audit line +
  QEMU-monitor escape-hatch hint, `--json` emits structured status)
  exit 1. The guest (init + report_rom + timer + vct) keeps running —
  the control-philosophy door (AGENTS.md §1.1): never a silent hang or
  a misleading success when the automation's backend is absent. Gates
  on the unavailable line. base-sel4 only.

- `sponge-pkg-meta.run` — Phase 7 todo 18: search/update assertion
  matrix. vct is short-lived, so the `pkg_meta_probe` drives the SAME
  ROM reads + comparisons that vct's `SearchCommand`/`UpdateCommand`
  perform (pkg_index.xml + pkg_<name>.xml + the `installed` broadcast),
  covering six steps in one boot: (1) install hello via sponge_pkgd,
  (2) search "hello" → hit, (3) search "zzznomatch" → honest empty
  result, (4) update hello → "already current" (broadcast == repo),
  (5) update hello delta → "repo carries 1.0, installed 0.9 — effective
  after next image build" (synthetic `installed_delta` broadcast with a
  pinned old version, faithful to the cross-image-rebuild semantic of
  docs/12 §9.2.2), (6) update nosuchpkg → "not installed" error
  (non-zero path). Gates on `pkg-meta-probe: PASS`. Kernel-agnostic
  (no GUI/drivers); runs on base-linux for speed.

- `sponge-vct-search.run` — Phase 7 todo 18: proves the REAL vct
  binary's `SearchCommand` works end-to-end (the probe mirrors vct's
  logic; this scenario runs vct itself). Stages hello's metadata +
  pkg_index.xml, boots vct with `search hello`, gates on the human
  output line `hello  1.0  <description>`. Kernel-agnostic.

- `sponge-vct-search-json.run` — Phase 7 todo 18: the `--json`
  companion to `sponge-vct-search.run`. Same topology, vct config arg
  `search hello --json`; gates on the structured JSON match line
  (`"command":"search","status":"success","matches":[{"name":"hello",...}]`).

- `sponge-panel-config.run` — **Phase 11 criterion 1** (base-linux):
  the four new configd keys applied live to the panel. sponge-de runs
  with `<config source="configd"/>` (the W2 ConfigController);
  `sponge_de_probe`'s `panel-config` phase drives configd through the
  real `config_request`/`config_result` channel and asserts 7
  subphases (P1/P2 launcher-toggle height tracks `panel.height`
  28↔64, P3–P5 `panel.visible_widgets` hides/shows clock and toggle,
  P6 `clock.format` HH:mm→HH:mm:ss glyph growth, P7 a validator-
  rejected write leaves the broadcast unchanged). Gates on
  `sponge-de-probe: phase panel-config PASS`.

- `sponge-panel-config-sel4.run` — Phase 11 criterion 1 on the
  production kernel: the same 7-subphase panel-config flow on
  base-sel4 with the interactive driver stack. Configd-write driven
  (no QMP choreography). Gates on
  `sponge-de-probe: phase panel-config PASS`.

- `sponge-de-themed-chrome.run` — **Phase 11 criterion 3**
  (base-sel4 + QMP): the upstream `themed_decorator` drop-in replaces
  the stock decorator in the WM stack (child named `decorator`,
  binary `themed_decorator`, so the Phase-10 report_rom/wm policies
  apply verbatim). The theme tar comes from `./tool/decor_assets`
  (upstream geometry metadata + byte-vendored font.tff); the
  decorator's whole config (libc + vfs + policy color from the active
  theme's panel_bg) is delivered live by `sponge_decorator_bridge`
  via report_rom — there is no inline `<config>` (init reserves that
  ROM label and would shadow the route). Gates: nonzero
  `decorator_margins` (20/8/1/1), the title bar verifiably tinted by
  the theme palette (wm_probe observe-3b, measured RGB(91,91,100) vs
  untinted (180,180,191)), a usb-tablet QMP drag moving pkg_gui_demo
  through the themed chrome, and `wm-probe: PASS`. The Phase-10
  `sponge-wm-qmp.run` drag regression must stay green alongside.

- `sponge-boot-i440fx.run` — **Phase 12 — Phase-12 criterion 1 storage
  variant (i440fx smoke)**. Storage-only i440fx/PIIX4 IDE smoke on
  base-sel4: explicit `-machine pc -cpu Skylake-Client`, exactly one
  boot disk, **does not start AHCI**, carries **no product `.img`**,
  no Sponge DE, and no NVMe toggle. Reuses the Tier-0 `boot_probe`
  pipeline (`platform`/`acpi`/`pci_decode` → IDE → `part_block` → VFS
  → `cached_fs_rom` → `boot_probe`) and gates on `boot-probe: PASS`
  from an IDE-backed P3 marker. `smoke-only` (not product-verified;
  the product stays on q35). Tier-0 target ≤60 seconds. Evidence
  `docs/evidence/phase12-boot-i440fx.log`. **Honest claim:** Phase 12
  verified QEMU `-machine pc` PIIX4 IDE path; this is **not** a real
  i440fx machine.

- `sponge-boot-multidisk.run` — **Phase 12 — Phase-12 criterion 1
  storage variant (q35/AHCI multi-disk order check)**. Two-disk smoke
  on q35+Skylake-Client with q35/AHCI; the expected marker is placed
  **only** on P3 of the **second** disk, and the QEMU drive order is
  deliberately swapped so the success cannot come from "first disk"
  semantics. The partition-number pin is preserved (`part_block`
  reads by number, never auto-probes). Gates on `boot-probe: PASS`
  reading the marker from the second disk's P3. Tier-0 target
  ≤60 seconds. Evidence `docs/evidence/phase12-boot-multidisk.log`.

- `sponge-desktop-disk-nvme.run` — **Phase 12 — Phase-12 criterion 1
  storage variant (q35/NVMe desktop-from-disk)**. Phase 12's
  product-media NVMe variant on q35+Skylake-Client: one namespace on
  a `pcie-root-port`, NVMe `caps: 5000 | ram: 64M` (the sizing
  proven by W2's quota-exhaustion canary), `<partition number="3"/>`
  semantics preserved, plus a P3 `Number: 3` report/byte assertion
  before the existing `alpha-probe: PASS`. NVMe is **not** the
  default product media (the `tool/dist --storage {ahci,nvme}`
  selector defaults to AHCI); `nvme` is opt-in. Tier-0 check target
  near 60 seconds; full seL4 desktop is 600 s+ reality, bounded at
  900 seconds. Evidence `docs/evidence/phase12-desktop-nvme.log`.
  **Honest claim:** one namespace QEMU-verified; multi-namespace NVMe
  is recorded as a Phase-12 gap in `docs/15-hardware-compatibility.md`.

- `sponge-pc-nic.run` — **Phase 12 — Phase-12 criterion 2 expanded
  driver set (pc_nic/e1000 + nic_router DHCP)**. Additive
  Linux-backed NIC driver smoke on q35+Skylake-Client: the in-tree
  `pc_nic` server (`genode/repos/pc/src/driver/nic/pc/`) is built from
  the Phase-12-managed `pc` repository (added to `REPOSITORIES` in
  W1, idempotent), QEMU exposes `-device e1000`, and the upstream
  `nic_router` policy `label_prefix: pc_nic | domain: uplink` is
  copied verbatim. `pc_nic` is sized `caps: 1000 | ram: 32M` (the
  Phase-12 small-box; the upstream default of ~140 caps / 16 MiB
  silently hangs on seL4). QEMU's user-mode slirp backend is preserved
  (no tap/bridge). Gates in order: `pc_nic: bound device` then
  `nic_router: uplink DHCP acquired` (300 s cold DDE-Linux gate;
  the bounded timeout is a loud failure, not a silent hang). The
  existing `run/sponge-net-probe.run` (iPXE/fetchurl round-trip) is
  unchanged. Evidence `docs/evidence/phase12-pc-nic.log`. **Honest
  claim text** (per docs/15 row):
  "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."

- `sponge-usb-boot.run` — **Phase 12 — Phase-12 criterion 1 storage
  variant (BIOS-side USB stick)**. BIOS-side USB-storage attachment
  of the existing product ISO on q35+Skylake-Client: the ISO is
  attached via
  `-boot menu=on -device usb-ehci -device usb-storage,drive=stick -drive id=stick,format=raw,file=<iso>,if=none`.
  Gates on `BIOS-side USB boot verified` when the bootloader/media
  handoff succeeds and the existing `alpha-probe: PASS` as the
  end-to-end Alpha corroboration. **Honest claim** (scenario header
  comment, verbatim from plan risk 1):
  "USB boot = product media bootable as a USB stick on QEMU via
  `-device usb-storage` (BIOS side). Genode-side `usb_block` reads
  USB block devices AFTER `image.elf` is loaded; not a boot-path
  claim." BIOS handoff target ≤60 seconds; full Alpha corroboration
  is 600 s+ reality, bounded at 900 seconds. Evidence
  `docs/evidence/phase12-usb-boot.log`.

- `sponge-usb-kbd-via-qmp.run` — **Phase 12 — Phase-12 criterion 2
  expanded driver set (USB HID keyboard via QMP)**. Reuses the
  `run/sponge-de-sel4-interactive.run` driver stack (q35+Skylake-
  Client, `pc_usb_host`, `usb_hid`, `event_filter`) plus the
  Phase-12 W3b QMP choreography fix. Boots without a static
  usb-kbd pass condition, then QMP `device_add`s a named `usb-kbd`
  device, observes `usb_hid` devices report `KEYBOARD` and emits
  `usb_hid: KEYBOARD detected`, QMP `device_del`s the keyboard
  and captures the removal event, then dispatches `send-key` (the
  QEMU-11 qcode-object form) via the QMP helper. The
  primary required gate is the audit chain
  `usb_hid: KEYBOARD detected` → `usb_hid: KEYBOARD removed`
  → `sponge-usb-kbd-via-qmp: PASS` + `Run script execution
  successful.`. The glyph-delta secondary gate is a documented
  Phase-12 gap (probe-focus ROM quirk; the post-device_del
  send-key travels the PS/2 path already covered by
  `sponge-terminal-qmp.run`). 600 s+ seL4 desktop reality, bounded at
  900 seconds. Evidence `docs/evidence/phase12-usb-kbd.log`. No
  usb-mouse, no `i2c_hid`, no new USB controller class.

- `tool/dist --storage {ahci,nvme}` — **Phase 12 — product-media
  selector**. `tool/dist` accepts a `--storage {ahci,nvme}` option
  (added in W2). The default is `ahci` (preserves current behavior
  and artifact naming); `nvme` selects a one-namespace NVMe product
  path (the `run/sponge-desktop-disk-nvme.run` topology). Invalid
  values are rejected before a build starts with a concise English
  error and usage line. The ISO / live media path is unchanged. See
  `docs/evidence/task-2-phase12-storage.md` for the receipts.

- `docs/15-hardware-compatibility.md` — **Phase 12 — compatibility
  contract**. The hand-curated 5×5 surface matrix + 16-cell tuple
  ledger (4 verified, 1 smoke-only, 11 gap) plus the six Phase-12
  known gaps. Validated by the read-only `tool/hw_compat.mojo
  assert` (no `generate` / `update` / repo write); reachable from
  `./tool/build verify`.

- `sponge-calculator.run` — **Phase 13 W3**: the calculator package
  (`pkg/calculator`), a source-built Qt6 app (the upstream
  `qt6_calculatorform` Designer example, built in-tree as
  `app/qt6/examples/calculatorform` from the `qt6_tools` port — no
  depot import). The `calculator_probe` drives `sponge_pkgd` to
  install and launch `calculator`, verifies the `installed` broadcast,
  pixel-verifies the rendered window via Capture (rendered-fraction +
  color-diversity), and exercises pkgd's `not-installed` /
  `already-running` error paths. Kernel-agnostic; gates on
  `calculator-probe: PASS`.

- `sponge-pdf-view.run` — **Phase 13 W4**: the PDF viewer package
  (`pkg/pdf_view`), the source-built mupdf `app/pdf_view` with a
  bundled one-page `pkg/pdf_view/payload/sample.pdf` staged as a boot
  module. `pdf_view` locates the document by `scandir("/")` for the
  first `*.pdf` name in its vfs, so the metadata's
  `<rom name="sample.pdf"/>` mount and the scenario's staged boot
  module must carry the SAME filename (docs/16 §3.2). The
  `pdf_view_probe` installs + launches via `sponge_pkgd` and
  pixel-verifies the rendered page via Capture (a rendered PDF page is
  bright-on-dark against the `#1e1e2e` background). Kernel-agnostic;
  gates on `pdf-view-probe: PASS`.

- `sponge-terminal.run` toolset extension — **Phase 13 W2**: the
  terminal package now mounts the CLI toolset tars
  (`coreutils-minimal`, `grep`, `sed`, `tar`, `less`, `findutils`,
  `diffutils`, `which`) in its vfs, and the `terminal_probe` asserts
  them end-to-end by typing `ls -l /bin` through the synthetic
  Press_char path and requiring the rendered listing's glyph jump
  (docs/plans/phase13-package-ecosystem.md D13.1/D13.2). The same fix
  repaired a latent Phase-7 bug: the metadata's `<env>` nodes used the
  `name`+`value`-attribute form, which libc's
  `populate_args_and_env` parses as the content form with empty
  content, so every variable (including `PATH`) was silently empty —
  external commands could never have run from the Alpha terminal.
  `pkg/terminal/metadata.xml` now uses the content form
  (`<env name="PATH">/bin</env>`); see docs/16 §5 pitfall 1.

## Planned additions

- Spin a usable Sponge OS install workflow through Leitzentrale (the
  P5 stream — currently the install path is `./tool/dist` only).
- More per-package run scripts (`sponge-files.run`-style) for any new
  default-app addition.

For how to write a run script, see the
[Genode run framework](https://genode.org/documentation/developer-resources/run)
documentation.
