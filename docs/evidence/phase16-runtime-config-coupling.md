# Phase 16 P0.3 — Runtime config coupling and create-path I/O audit

**Task:** P0.3 of `docs/plans/phase16-runtime-overhaul.md` ("Config coupling
audit"). **Branch:** `refactor/phase16-runtime-overhaul` (tree at
`a5f5af32c3` at audit time). **Date:** 2026-10-28. **Method:** read-only
inspection of source and run files; no scenarios were run and no
implementation was modified.

**Reading conventions.** Every section separates:

- **Observed** — what the code/run script does today, with exact
  `file:line` references verified by reading the file on this branch.
- **Recommendation** — implementation guidance for the P1/P2 tasks. Nothing
  labeled Recommendation has been implemented or measured.

Line numbers refer to the tree as of the audit commit; the P1.3 module split
will move them and is required to update this file (its gate says so).

---

## 1. Producer/consumer map — who emits which runtime config

### 1.1 Producer table (Observed)

| Config | Producer | Emission mechanism | Transport | Consumer |
|---|---|---|---|---|
| `pkg_runtime` sandbox config (app packages) | `sponge_pkgd` | `Expanding_reporter _runtime_reporter` label `"config"`, name `"runtime"`, pre-sized 32 KiB (`repos/sponge/src/sponge_pkgd/main.cc:340-342`, sizing rationale `:283-300`) | report_rom policy `pkg_runtime -> config \| report: sponge_pkgd -> runtime` (`run/sponge-desktop-disk.run:643`) | `pkg_runtime` nested init's `"config"` ROM (start node route `run/sponge-desktop-disk.run:931`) |
| `driver_runtime` sandbox config (driver packages) | `sponge_pkgd` | `Expanding_reporter _driver_runtime_reporter` label `"config"`, name `"driver_runtime"`, pre-sized 8 KiB (`repos/sponge/src/sponge_pkgd/main.cc:368-370`, rationale `:344-366`) | report_rom policy `driver_runtime -> config \| report: sponge_pkgd -> driver_runtime` (`run/driver_runtime.inc:224`) | `driver_runtime` nested init's `"config"` ROM (route `run/driver_runtime.inc:280`) |
| `system` sandbox config | `sponge_supervisor` | fixed-size `Reporter _system_reporter`, label `"config"`, name `"system_config"`, 128 KiB (`repos/sponge/src/sponge_supervisor/main.cc:198`, `:388`; never-growing rationale `:179-197`) | report_rom policy `system -> config \| report: sponge_supervisor -> system_config` (`run/supervisor.inc:464`) | `system` nested init's `"config"` ROM — replaces the rom_sys route (`supervisor.inc:159-160`: `sys_config_route` = `+ service ROM \| label: config \| + child rom_sys \| label: init/system.config`, swapped to `sys_config_relay` = `+ service ROM \| label: config \| + child report_rom`) |
| root (Tier-0) init config | run script at image build | Tcl `install_config` (proc at `genode/tool/run/run:324`) | staged `config` boot module | root `init` |
| `system` init config *as data* (supervisor-less scenarios) | run script | written to `/system/init/system.config` (`run/sponge-desktop-disk.run:515` ff., HID→XML conversion `:1051-1054`) | served by `rom_sys` (route `run/sponge-desktop-disk.run:1360`) | `system` nested init |
| fault-console state | `sponge_supervisor` | `Reporter _armed_reporter`, name `"console_state"`, 4 KiB (`repos/sponge/src/sponge_supervisor/main.cc:199`, `:389`, content `:404-407`) | policy `label_suffix: console_state \| report: sponge_supervisor -> console_state` (`run/supervisor.inc:465`) | fault console component |

Two adjacent authorities are listed for boundary clarity only (D16R.4):
`sponge_configd` owns the user-config store broadcast (`"config"` report,
`repos/sponge/src/sponge_configd/main.cc:217`) and `vct`'s power command
publishes a `"system"` report consumed by acpica
(`repos/sponge/src/vct/commands.cc:1830`) — note the label collision with
the supervisor's *name* `"system_config"`; they are different report names
on the same label and must stay so.

### 1.2 pkgd emission sites — the coupling under overhaul (Observed)

`_generate_runtime_config()` (`repos/sponge/src/sponge_pkgd/main.cc:1959`)
always emits **both** sandbox configs together (`:2078-2079`; contract
comment `:1950-1957` — "no call site can ever publish one without the
other"). Every call site regenerates both, regardless of which runtime the
change touched — the whole-regen behavior P1.4 makes dirty-scoped:

| # | Call site | Trigger | Surrounding sequence |
|---|---|---|---|
| 1 | `main.cc:1750` | `_do_install` (any install, app or driver) | `_add_root` → `_sync_installed_from_roots` → `_sync_running_state` → **`_save_store` (`:1749`)** → **`_generate_runtime_config`** → `_generate_installed_report` (`:1751`) |
| 2 | `main.cc:1798` | `_do_remove` | drop root → re-derive → **`_save_store` (`:1797`)** → **`_generate_runtime_config`** → `_generate_installed_report` |
| 3 | `main.cc:1928` | `_do_launch` (after `_add_running` `:1921`) | no `_save_store` (`_running` is not persisted, comment `:1922-1927`) → **`_generate_runtime_config`** → `_generate_installed_report` |
| 4 | `main.cc:3544` | `_bootstrap` (boot) | initial emission, before any request handler is armed (`:3539-3544`) |

Repro-harness control path (test-only, inert in production):
`main.cc:2055-2061` publishes an empty `pkg_runtime` config plus a normal
`driver_runtime` config when `<repro_empty_from: N>` fires.

App/driver split is decided in exactly one place —
`_emit_runtime_config(reporter, drivers)` at `main.cc:2092-2356`, filter
`if (c.is_driver() != drivers) continue;` (`:2265`), with the
`<kind>` attribute parsed at `main.cc:133` (`is_driver()` `:135-136`).
`<parent-provides>` differs per sandbox and IS the capability boundary
(`:2170`): `APP_BASE` = ROM/PD/RM/CPU/LOG/Timer/Gui/Input/Report/
File_system/Nic/Rtc, `DRIVER_BASE` = ROM/PD/RM/CPU/LOG, each unioned with
the session names the installed packages of that sandbox declared
(`:2213-2231`).

### 1.3 supervisor emission sites (Observed)

`_publish()` (`repos/sponge/src/sponge_supervisor/main.cc:417-469`) forwards
a named `<system>`/`<system_console>` template from the `"templates"` ROM,
substitutes `@KILL@` with a rising version counter (`:444-446` — the only
restart lever, header `:29-41`), and refuses truncated output (`:436-440`)
and buffer overflow (`:448-453`, `:457-464`) rather than publishing
garbage. Publish points:

| Site | Phase | Template |
|---|---|---|
| `main.cc:657` | BOOT | `"system"` (nominal) |
| `main.cc:724` | NOMINAL → CONTAIN (fault survived escalation) | `"system"` (fresh `@KILL@`) |
| `main.cc:781` | CONTAIN → CONSOLE | `"system_console"` (desktop-less), then `_arm_console(true)` `:786` |
| `main.cc:838` | RELEASE → NOMINAL | `"system"` (nominal, `_publish_nominal()` `:471-474`) |

Both reporters are constructed on the first Timer tick, never in the
constructor (`_construct_reporters` `:385-392`; rationale `:188-196` —
report_rom announces its service from its own constructor and a denied
Report session is fatal).

### 1.4 run-script `install_config` (Observed)

`install_config` installs **only the Tier-0 root init config**. ~108 call
sites exist in `run/*.run` (one per scenario; full list derivable via
`grep -n install_config run/*.run`). Product family anchors:

- `run/sponge-desktop-disk.run:1391` — `install_config $tier0_config`, where
  `set tier0_config [supervisor::tier0 {` at `:1062` wraps the literal
  (ends `- }]` `:1388`) and `driver_runtime::expand` fills the driver
  placeholders (`:1389-1390`).
- Same pattern: `run/sponge-desktop-disk-nvme.run:1248`,
  `run/sponge-desktop-disk-uefi.run:1432`,
  `run/sponge-desktop-disk-uefi-nvme.run:1189`,
  `run/sponge-desktop-disk-uefi-usb.run:1293`.
- `run/sponge-pkg-driver.run:102` and `run/sponge-supervisor-takeover.run:354`
  are the hand-rolled variants (inline nested `<config>`, see §4).

No `install_config` call installs pkgd's, the supervisor's, or the system
sub-init's config; those are inline nodes / ROM modules / report_rom
relays as tabulated in §1.1. The nested `system` config is data on P3
(`/system/init/system.config`, written at `run/sponge-desktop-disk.run:515`
ff., served via the route at `:1360`) or relayed through report_rom when
the supervisor is wired (`run/supervisor.inc:159-160`).

---

## 2. Shared report_rom channel inventory (Observed)

Single-writer-per-label is the reason the channels are split (pkgd header
`main.cc:28-35`, `:235-254`). Product block: `run/sponge-desktop-disk.run:619-658`
(system report_rom) + `run/driver_runtime.inc:224-225` (`@DRIVER_RELAY_POLICIES@`
placeholder at `:658`) + `run/supervisor.inc:464-465` + Tier-0 block
`:1083-1094`. Labels are the full `<consumer> -> <rom-name>` /
`<writer> -> <report-name>` pairs.

| Channel (report name) | Writer → label | Readers (ROM labels) | Writer site |
|---|---|---|---|
| `runtime` | `sponge_pkgd -> runtime` | `pkg_runtime -> config`, `cfg_probe -> config`, `sponge-de -> runtime_mirror` (`:643`, `:645`, `:654`) | `main.cc:340` |
| `driver_runtime` | `sponge_pkgd -> driver_runtime` | `driver_runtime -> config` (`driver_runtime.inc:224`) | `main.cc:368` |
| `state` (pkg_runtime's sandbox report) | `pkg_runtime -> state` | `sponge_pkgd -> runtime_state` (`:644`), `sponge-de -> runtime_state` (`:653`) | produced by init per the `<report>` node pkgd embeds in the generated config (`main.cc:2113-2128`: `delay_ms=2000`, `init_ram`, `init_caps`, `child_ram`) |
| `state` (driver_runtime) | `driver_runtime -> state` | `sponge_pkgd -> driver_runtime_state` (`driver_runtime.inc:225`), `driver_probe` (`run/sponge-pkg-driver.run:297`), `fb_switch_probe` when armed (`run/sponge-desktop-disk.run:979-983`, `SPONGE_FB_SWITCH=on` only) | same generated `<report>` node |
| `state` (system) | `system -> state` | `sponge_supervisor -> system_state` (Tier-0 relay `run/sponge-desktop-disk.run:1092` `label_suffix: system_state`; read at `supervisor main.cc:172`), `sponge-de -> system_state` (optional, `sponge-de/main.cc:437`, `:625`) | system init |
| `system_config` | `sponge_supervisor -> system_config` | `system -> config` (`supervisor.inc:464`) | `supervisor main.cc:388` |
| `console_state` | `sponge_supervisor -> console_state` | fault console (`supervisor.inc:465`) | `supervisor main.cc:389` |
| `installed` | `sponge_pkgd -> installed` | `sponge-de -> installed`, `pkg_runtime -> debug -> installed` (`:647`, `:650`) | `main.cc:382` |
| `request`/`result` | `vct`/`probe` ↔ `sponge_pkgd` (`main.cc:225-232`) | vct (`backend_client.h:83` defaults `"request"`/`"result"`) | `lib/src/sponge_backend_client/backend_client.cc` |
| `launcher_request`/`launcher_result` | `sponge-de` ↔ `sponge_pkgd` (`:648-649`) | `launcher_controller.cc:354-356` | `main.cc:243-244` |
| `tty_request`/`tty_result` | `vct_tty` bridge (`repos/sponge/src/vct_tty/main.cc:275`) | same `_handle_request_impl` body (`main.cc:707-714`) | `main.cc:253-254` |
| `probe_request`/`probe_result` | launch probe (`main.cc:256-264`) | same body | `main.cc:263-264` |
| `config_request`/`config_result`, `de_config_request`/`de_config_result`, `config` broadcast | vct ↔ `sponge_configd`; `sponge-de` SettingsController ↔ `sponge_configd` (`sponge_configd/main.cc:196-217`, `:1353-1362`) | configd consumers | configd |
| `system` (power) | `vct` (`commands.cc:1830`) | acpica | distinct from `system_config` |

The three pkgd transports (`request`, `launcher_request`, `tty_request`)
feed one `_handle_request_impl` (`main.cc:714`); P1 extraction must keep
that single-backend property (AGENTS.md §3.3 rule 5).

---

## 3. `binary_prefix` rewrite points (Observed)

`binary_prefix` is an attribute of **pkgd's own `<config>`** (not package
metadata), read once at construct:
`repos/sponge/src/sponge_pkgd/main.cc:3593-3595`
(`attribute_value("binary_prefix", String<32>())`, empty default =
boot-module model). Member `_binary_prefix` at `:699` (rationale
`:692-698`). All rewrite/compose sites:

| # | Site | What it does |
|---|---|---|
| 1 | `main.cc:1103-1175` (inside `_parse_package`, `:1010`) | Rewrites the **serialized inline config body** of a package: every `<binary name="B">` with slash-free `B` becomes `prefix/B` (`:1129-1141`); every slash-free `<start name="X">` lacking its own `<binary>` gains `<binary name="prefix/X"/>` after its open tag (`:1143-1168`). Path-like names (`/bin/bash`) untouched. Static buffers (`cfg_buf` `:1050`, `out_buf` `:1113`) because this runs on the small-stack ep thread. |
| 2 | `main.cc:2284-2295` (in `_emit_runtime_config`) | Composes the top-level `<binary name=...>` of every generated `<start>`: `bin_path(_binary_prefix, c.binary)` (`:2289`), emitted only when it differs from the package name (`:2291-2295`). Shared by app and driver nodes — one machinery, both config paths. |
| 3 | `main.cc:3166-3178` (`_binary_available`) | Launch pre-flight: effective path `(_binary_prefix, p.binary)` (`:3171`) must appear in the `<binaries/>` manifest; log line `:1886-1888` prints the composed path. |
| 4 | `main.cc:3131` (comment) / `:3593-3595` | Documentation + attribute read (see above). |

Run-script **setting** points (`+ config | binary_prefix: bin/` on the
sponge_pkgd start node):

- `run/sponge-desktop-disk.run:776` (+ `nvme:650`, `uefi:858`,
  `uefi-nvme:652`, `uefi-usb:875`) — all five product disk scenarios.
- `run/sponge-gaprepro.run:602` (plus repro knobs),
  `run/sponge-launch-order-regression.run:602`,
  `run/sponge-desktop-defaults-firstboot.run:374`,
  `run/sponge-persist-disk.run:347`.
- **Deliberately absent** in the falkon scenarios: pkgd runs with an empty
  prefix so falkon's binary resolves via `rom_pkg`, not `rom_sys`
  (`run/sponge-falkon-disk.run:416-420`, `run/sponge-falkon-rescue.run:460-464`,
  `run/README.md:640`). `run/sponge-pkg-driver.run:34,39` is boot-module
  style (empty prefix).

P2.3 resolution (2026-10-29): the plan's original symptom was stale. Site #1
already rewrites nested starts and explicit binaries in the serialized inline
`<config>`; the unchanged golden suite and the fully staged UEFI-USB terminal
launch gate both pass. See `phase16-runtime-rom-failures.md` §9. The remaining
boundary is staging, not rewriting: media without the terminal binary/payload
set cannot make the same runtime claim.

---

## 4. Inline package config bodies (Observed)

- Field: `Package::config_xml` `Genode::String<16384>`
  (`main.cc:124`; contract comment `:116-123` — a complete
  `<config>...</config>` block, emitted verbatim, Qt/libc fragments
  preserved).
- Parse/serialize: `main.cc:1038-1083` — rebuilt via `Xml_generator`
  with **attributes preserved** (`:1068-1072`; the vesa_fb width/height
  lesson) and `append_node_content` depth 16 (`:1076-1077`).
- Rewrite pass: `binary_prefix`, §3 site #1 (`:1100-1175`).
- Emit: `main.cc:2310` — `g.append(c.config_xml.string())` raw
  (non-sanitized) inside the generated `<start>`, after `<resource>` and
  before `<route>` (which opens at `:2312`).
- Size impact (drives `RUNTIME_CONFIG_BUFFER`): the verbatim blocks
  dominate — falkon 3386, terminal 4117, pdf_view 2975 bytes vs ~460
  bytes scaffolding per package (`main.cc:290-295`).

The same mechanism serves both app and driver packages; driver config
blocks are small (pc_nic ~1 KiB, `main.cc:358-362`).

---

## 5. Persistence and bake (Observed)

### 5.1 pkgd installed-set store

- Constants: `STORE_PATH = "/store/installed.xml"`, version 1, 4 KiB buf
  (`main.cc:2764-2766`).
- Opt-in gate: `<vfs>` node in pkgd's config → `Vfs::Root` construct
  (`_init_store` `main.cc:2778-2795`); without it `_load_store`/`_save_store`
  are no-ops (`:536-543`).
- `_load_store` `:2845` (stat/open/read), `_save_store` `:2944` (write).
  Only `_roots[]` is persisted; `_installed` is re-derived via
  `_sync_installed_from_roots` (`:1537`).
- Write-back points: `_do_install` `main.cc:1749` and `_do_remove`
  `main.cc:1797` (**both on the env.ep() request thread**), and
  `_seed_roots_from_bake` `main.cc:3109` (Bootstrap thread). `_running` is
  intentionally not persisted (`:1922-1927`).
- **Product-media observation:** the pkgd start nodes in the
  `sponge-desktop-disk*` family carry **no `<vfs>` node** (verified:
  `run/sponge-desktop-disk.run:773-780`,
  `run/sponge-desktop-disk-uefi-usb.run:872-887`), so the store path is
  inactive there; it is exercised by `run/sponge-persist-disk.run:342-352`
  (`+ vfs / + fs`; `/store/installed.xml` on P4 per `:318`, two-boot
  restore proof `:65-69`) and `run/sponge-pkg-persist.run` (lx_fs).
  Whether product-media persistence should be armed is **not decided
  here**.

### 5.2 Bake seeding (pkgd)

- Gate: `<bake/>` node → open `"bake_manifest"` ROM
  (`_init_bake` `main.cc:3029-3038`).
- `_seed_roots_from_bake` `main.cc:3049-3121`: parses the
  `"staged_packages"` JSON array, seeds roots **only when `_num_roots == 0`**
  (`:3061-3062`) so persisted removals stay authoritative; may
  `_save_store()` at `:3109`.
- Order in the bootstrap is fixed: `_init_bake`/`_seed_roots_from_bake`
  (`:3503-3504`) run **before** `_init_admission` (`:3519`) — the plan's
  P0.3 ordering question.

### 5.3 Run-script bake/persist staging

- `run/bake.inc` — stager: `_write_manifest` `:534`,
  `_stage_pkg_metadata` `:608`, `_write_pkg_index` `:637`,
  `_stage_payload` `:657`, entry `proc stage` `:969`. Call sites:
  `run/sponge-desktop-disk.run:481` (`bake::stage $sys_dir img`), also
  uefi:602, uefi-nvme:395, uefi-usb:615; `run/sponge-alpha.run:292`
  (`bake::stage bin iso`).
- Flat copy for rom_sys resolution: `run/sponge-desktop-disk.run:499-501`
  copies `bake/{bake_manifest.json,config.defaults,theme.defaults}` to
  `/system/` (rom_sys resolves `label.last_element()`; comment `:493-496`).
  Configd consumes `bake_manifest`/`bake_config_defaults` (`:753-754`);
  pkgd's `bake_manifest` route at `:787`.
- First-boot/reset proofs: `run/sponge-bake-firstboot.run` (two boots,
  override suppresses re-seed), `run/sponge-bake-reset.run` (`vct bake
  reset` path).
- configd has its own store (`sponge_configd/main.cc:284`,
  `_init_store`/`_load_store`/`_save_store` `:403-406`) — likewise
  **without** a `<vfs>` node on product media
  (`run/sponge-desktop-disk-uefi-usb.run:839-858`).

---

## 6. Storage-I/O-at-session-create inventory (Observed)

### 6.1 pkgd threads and the boot-time I/O boundary

- **Bootstrap thread** (`main.cc:552-601`): `struct Bootstrap : Thread`
  ("pkgd-store", 64 KiB stack, `:577-599`), runs `_bootstrap()` then
  `sleep_forever`. Motivation comment `:553-575`: a File_system request
  from `Component::construct()` blocks `env.ep()` for as long as the
  storage chain takes — the same defect as
  `docs/evidence/logdisk-field-diagnosis.md` §3(a); pkgd previously
  repeated it in `_init_store`, `_load_store`,
  `_seed_roots_from_bake`/`_init_bake`.
- **Constructor** (`main.cc:3587-3613`): reads the config ROM only
  (inline, init-served — cannot touch storage; comment `:3600-3604`),
  then starts the Bootstrap thread (`:3605`); on thread-creation failure
  falls back to single-threaded `_bootstrap()` on ep (`:3606-3611`).
- **Bootstrap init order** (`_bootstrap` `main.cc:3485-3584`) — the
  complete order, with I/O class:

| Step | Line | I/O |
|---|---|---|
| `_load_index()` | 3488 | ROM only (`pkg_index.xml`) |
| `_init_store()` | 3493 | **File_system session open** (via `Vfs::Root`) |
| `_load_store()` | 3494 | **stat/open/read** |
| `_init_bake()` | 3503 | ROM only |
| `_seed_roots_from_bake()` | 3504 | ROM read; may **write** store (`:3109`) |
| `_init_binary_manifest()` | 3510 | ROM only |
| `_init_admission()` / `_init_driver_admission()` | 3519-3520 | ROM opens (`runtime_state` / `driver_runtime_state`) |
| `_sync_installed_from_roots()` | 3527 | pure |
| `_sync_running_state()` | 3537 | pure |
| `_generate_runtime_config()` | 3544 | report writes |
| `_generate_installed_report()` | 3549 | report write |
| `_handle_request()` drain | 3565 | only after everything above (ordering comment `:3551-3563`) |
| arm `sigh` | 3566-3567 | ep owns requests from here |
| `_init_launcher_channel()` | 3581 | ROM/report opens, last (comment `:3569-3580`) |

### 6.2 Which threads can touch storage at session-create time (Observed)

1. **Bootstrap thread, at boot only**: the store load/write and the bake
   seed write (above). Never after the sigh is armed.
2. **env.ep() request thread, during install/remove**: `_save_store()` at
   `main.cc:1749` and `:1797` is a synchronous File_system write on the
   entrypoint inside the package-operation path. A launch does not touch
   storage (`:1922-1927`). This is the pkgd-internal create-path I/O P3.3
   targets.
3. **The ROM-serving chain, at every child create**: `pkg_runtime`/
   `driver_runtime` children resolve binaries through `rom_sys`/`rom_lib`/
   `rom_pkg` (`cached_fs_rom` → `vfs`/`rump ext2` → AHCI/NVMe block,
   topology `docs/14-boot-storage-architecture.md` §4.4 lines 306-321).
   A ROM-session open at child construction pages from the block chain;
   on a slow medium this is storage I/O reached from a session create.
   The supervisor's `heartbeat_state="starting"` exemption
   (`repos/sponge/src/sponge_supervisor/main.cc:551-566`,
   `docs/evidence/startup-grace.md`) exists because a slow first construct
   (slow storage chain) is indistinguishable from a wedge.
4. **configd's store** (only where `<vfs>` is configured) writes on key
   sets — not on the package create path.

The log-sink vs package-store provider sharing (FD4,
`vfs_log`/`vfs_pkg` separation, D16R.8) and the full forwarded-create
chain are inventoried by P0.5; this audit records only the pkgd-side and
ROM-chain sites above.

---

## 7. `rom_pkg` / `rom_sys` / `rom_lib` physical roots (Observed — P2.6 input)

All product roots are `cached_fs_rom` instances chrooted at directories on
the GENODE ext2 partition **P3**. Host-side staging directory:
`set sys_dir "[run_dir]/system"` (`run/sponge-desktop-disk.run:318`;
same in `run/sponge-falkon-disk.run:229`) — what `image/disk` copies into
P3. No depot and no build-dir `rom/` mapping on disk media (flat/ISO
scenarios use `bin/` boot modules instead).

| ROM server | Physical root (chroot) | vfs policy | Start node | Consumer routes |
|---|---|---|---|---|
| `rom_sys` | `/system` | `run/sponge-desktop-disk.run:1197` (`label_prefix: rom_sys \| root: /system`) | `:1210-1214` (`cached_fs_rom`, 500 caps / 256M) | system binary `init` (`:1358`), system config `init/system.config` (`:1360`), `.lib.so` falls through to rom_lib (`:1362`), catch-all ROM (`:1379`) |
| `rom_lib` | `/system/lib` | `:1198` | `:1220-1224` | `+ service ROM \| label_suffix: .lib.so \| + child rom_lib` (`:1362`) |
| `rom_pkg` | `/system/pkg/falkon/payload` | `run/sponge-falkon-disk.run:604` (also `run/sponge-desktop-disk-uefi-usb.run:1481`) | `run/sponge-falkon-disk.run:636-637` ("the third cached_fs_rom"); `run/sponge-desktop-disk-uefi-usb.run:1561` (`caps: 20000, ram: 768M`) | `+ service ROM \| label_prefix: pkg_runtime \| + child rom_pkg` (`run/sponge-falkon-disk.run:823`); uefi-usb `:1702-1703` (`pkg_runtime -> bin/falkon → rom_pkg label falkon`, `pkg_runtime -> falkon*`) |
| `rom_bake` (test scenarios) | `/system/bake` | `run/sponge-bake-firstboot.run:36`, `run/sponge-bake-reset.run:46` | `:39` | `bake_config_defaults`, `bake_manifest` (`:64-65`) |

Observed shape for P2.6: **`rom_pkg` today is one `cached_fs_rom` per
served closure, rooted at that package's payload directory** (falkon is
the only instance), while ordinary package binaries resolve through
`rom_sys` at `/system/pkg/<name>/payload/<binary>`
(`docs/14-boot-storage-architecture.md` §4.4:321). The three-partition
layout (D16R.2: P3 `GENODE`, P4 `SPONGE-VAR`, P5 `SPONGE-HOME`, no P6+)
is unchanged by anything in this audit; P2.6 may only grow P3 on newly
generated media.

---

## 8. pkgd module/thread map (Observed — baseline for the P1.3 split)

`sponge_pkgd` is one file: `repos/sponge/src/sponge_pkgd/main.cc` (3624
lines) + `target.mk` + `README.md`. Functional blocks as they sit today:

| Block | Lines (anchors) | Thread |
|---|---|---|
| metadata model + inline-config parse (incl. `binary_prefix` rewrite) | `:103-199`, `:1010-1230` | ep (metadata ROM reads at request time) |
| report channels (request/result ×3, runtime, driver_runtime, installed) | `:225-382` | — |
| state: `_installed`/`_roots`/`_running` | `:502-533` | shared (guarded by the bootstrap-before-arming protocol) |
| store (Vfs, load/save) | `:544-550`, `:2764-3027` | Bootstrap at boot; ep at install/remove |
| Bootstrap thread + `_bootstrap()` | `:552-601`, `:3485-3584` | Bootstrap |
| bake seeding | `:603-607`, `:3029-3121` | Bootstrap |
| binary manifest + launch pre-flight | `:609-632`, `:3131-3178` | ep |
| admission (`_read_budget`, `_admits`, margins) | `:634-690`, `:3232-3433` | ep |
| request handling (`_handle_request_impl`, `_do_*`) | `:702-719`, `:1347-1932` | ep |
| resolution & routing helpers (decls `:721-725`; `_is_parent_route` `:880`, `_session_label` `:903`, `_load_index` `:918`, `_load_package` `:947`, `_parse_package` `:1010`, `_resolve` `:1291`, `_visit` `:1303`) | `:880-1345` | ep; `_load_index` also on Bootstrap |
| runtime-config generation (`_generate_runtime_config`, `_emit_runtime_config`) | `:771-774`, `:1935-2356` | ep or Bootstrap (both call it) |
| installed broadcast + result reporters | `:2365-2762` | ep / Bootstrap |
| constructor / `Component::construct` | `:3587-3624` | ep |

---

## 9. P1 extraction checklist (Recommendation)

Per D16R.5 / P1.2: extract each target into a pure library (no I/O, no
`env()`) with tests **before** any topology change. Targets and their
current anchors:

- [x] **`binary_prefix` rewriter.** Surface: the text rewrite at
  `main.cc:1103-1175` plus the path composition at `:2289` and `:3171`.
  Landed as pure `sponge_binary_prefix` (`71b2c4359e`) with caller-owned
  buffers and a host golden self-test (`self-test: PASS (equivalence +
  golden snapshots)`).
  Pure shape: `(prefix, serialized_config_text) -> rewritten_text` and
  `(prefix, binary_name) -> effective_path`. Replace the static buffers
  (`:1050`, `:1113`) with caller-provided buffers in the library. Tests:
  slash-free name rewritten; `/bin/bash` untouched; `<start>` without
  `<binary>` gains one; `<start>` with `<binary>` does not; empty prefix
  is identity. P2.3 verified these existing semantics; no extension was
  needed (`phase16-runtime-rom-failures.md` §9).
- [x] **Launch model.** Surface: `_is_running`/`_add_running`/
  Landed as pure `sponge_pkg_launch` with the exact six-gate order,
  lazy resource evaluation, running-set helpers, and a host self-test
  (`self-test: PASS (equivalence + outcomes + ordering +
  exclusive/manifest/sync/identity invariants)`).
  `_sync_running_state` (`:1588-1634`) and the five-outcome decision
  order in `_do_launch` (`:1837-1932`: not-installed / already-running /
  exclusive-conflict / binary-missing / insufficient-resources / ok),
  including the `Start_model::matches()` name+version semantics the
  determinism contract relies on (`main.cc:1940-1948`;
  `supervisor main.cc:29-41` cites
  `genode/repos/os/src/lib/sandbox/config_model.h:171`). Pure shape:
  decision function over (installed set, running set, exclusive map,
  staged-binary set, budget) -> outcome; side effects (reports, mutation)
  stay in the shell.
- [ ] **Admission math.** Surface: `_quota_bytes` (`:3189`),
  `_read_budget` (`:3232-3268`, HID parse), `_admits` (`:3299`),
  margins `:689-690`, the two-budget install loop `:1690-1737`. Pure
  shape: `(quota_string, need, avail, margin) -> admit/refuse` + budget
  decrement; the HID decode is pure over a byte range.
- [x] **Config-emission templating.** Surface: `_emit_runtime_config`
  Landed as pure `sponge_pkg_config` with host golden self-test.
  (`:2092-2356`) — name-sorted order (`:2096-2110`), `<report>` node
  (`:2113-2128`), `<parent-provides>` union (`:2170-2231`),
  `<default-route>`/`<default>` (`:2233-2261`), `<start>` nodes
  (`:2277-2356` incl. inline config `:2310` and the `ld.lib.so`
  route `:2312-2326`). Pure shape:
  `(installed[], running[], kind filter, binary_prefix) -> config_text`.
  Keep the determinism contract (`:1940-1948`) as a property test.
- [x] **P1.3 module split.** Along the existing thread boundary:
  constructor + `_bootstrap()` + store + bake (all storage I/O) vs the
  pure core (decides, never blocks). No new threads. Update this file's
  §1.2/§8 line anchors in the same commit (gate of P1.3).
- [ ] **Result/report emission and the three transports** stay in the I/O
  shell; keep one `_handle_request_impl` (AGENTS.md §3.3 rule 5).

Extraction must not change behavior: gate is "product scenarios unchanged"
(P1.2). The test harness location follows `docs/08-development.md`
conventions (test component or host-side check).

---

## 10. P2.6 staging anchors (Recommendation, on Observed anchors)

Stage the falkon + WebEngine closure into the browser runtime's payload
area inside the existing three-partition layout (D16R.2). Observed anchor
points a P2.6 implementer edits:

1. **Payload root (the `rom_pkg` root confirmed above):**
   `/system/pkg/falkon/payload` on P3 today
   (`run/sponge-falkon-disk.run:604`). Staging writes go to
   `$sys_dir/pkg/...` (`set sys_dir "[run_dir]/system"`,
   `run/sponge-desktop-disk.run:318`); payload staging helper
   `run/bake.inc:657` (`_stage_payload`).
2. **ROM server node:** `run/sponge-falkon-disk.run:636-637` /
   `run/sponge-desktop-disk-uefi-usb.run:1561` (RAM/caps sizing of the
   third `cached_fs_rom`).
3. **Consumer routes:** `run/sponge-falkon-disk.run:823`,
   `run/sponge-desktop-disk-uefi-usb.run:1702-1703` — under the
   `system` child route.
4. **pkgd mode choice:** the falkon scenarios run pkgd with an **empty**
   `binary_prefix` so the browser binary resolves via `rom_pkg`
   (`run/sponge-falkon-disk.run:416-420`); the product scenarios use
   `bin/` + `rom_sys`. The browser runtime's payload-area design must
   state which mode it assumes (P2.2's per-runtime includes own this).
5. **Media sizing:** `tool/dist.mojo` / `tool/mkdata.mojo` P3 growth —
   **new media only**, never migrate or resize installed media
   (D16R.2); record the staged size and new-media P3 size in the P2.6
   receipt.
6. **Runtime grouping prerequisite:** the browser runtime config only
   exists after P2.2/P2.4 (`docs/plans/phase16-runtime-overhaul.md`
   P2.4/P2.6 dependencies); nothing here changes the current
   `pkg_runtime`/`driver_runtime` grouping.

---

## 11. Not established by this audit

- Whether the absence of `<vfs>` store nodes on product media (§5.1) is
  intentional — recorded, not judged.
- The full forwarded-create / provider-chain I/O map and the NIC uplink
  regression (FD1, D16R.17) — owned by P0.2/P0.5/P4.2.
- Any classification of ledger rows #25-#31 — P4.1's job.

---

## 12. P1.3/P1.4 implementation and green receipt (2026-10-28)

**Machine:** QEMU q35, `Skylake-Client`, KVM, base-sel4. This is not a
real-hardware result. **Implementation commit:** `e5ccb99a9d`
(`fix(pkgd): stop cross-runtime config republication`).

### 12.1 Landed module map

The P1.3 split is file/linkage structure only; it added no thread and kept
one `Main` class and one request backend. The landed responsibilities are:

| File | Responsibility |
|---|---|
| `pkgd.h` | package metadata model and `Main` declaration |
| `main.cc` | request/result channels, replay, runtime-report emission, bootstrap and construction |
| `metadata.cc` | index/package parsing and dependency resolution |
| `registry.cc` | roots, installed closure, running set and exclusive-peer lookup |
| `ops.cc` | install/remove/launch and launch pre-flight |
| `admission.cc` | quota parsing, budget reads and admission decisions |
| `store.cc` | opt-in persistent root store and bake seeding |

`sponge_pkg_config`, `sponge_binary_prefix`, and `sponge_pkg_launch` are
the landed pure, host-testable libraries. The launch-model behavior gates
also passed serially on QEMU: `sponge-pkg-lifecycle`, `sponge-pkg-admission`,
and `sponge-launch`. Admission math remains the only P1.2 extraction named
in §9 still open; P1.5 emission validation is the next task.

### 12.2 Dirty-scoped emission

Install marks targets from the resolved plan's package kinds before the plan
is rebuilt. Remove marks the kinds actually dropped from the installed
closure. Launch marks only the launched package's kind after every refusal
path. Bootstrap marks both targets. `_generate_runtime_config()` consumes
and clears both marks, and publishes only marked targets; mixed dependency
plans therefore still publish both, while app-only and driver-only
operations no longer republish the other runtime.

The config-authority scenario was hardened at the same time: every gate
observes one QEMU boot, each optional defect window closes on an explicit
next-operation/control landmark, a missing landmark is a harness failure,
and either cross-runtime publication direction independently produces RED.
This closes the former stacked-respawn and `A && B` false-PASS holes.

### 12.3 Commands and observed gates

All Genode scenarios ran serially with `make -j1`; no concurrent build used
`genode/build/x86_64`.

```sh
make -j1 -C genode/build/x86_64 run/sponge-config-authority KERNEL=sel4 BOARD=pc
# config-authority-probe: PASS

make -j1 -C genode/build/x86_64 run/sponge-pkg-lifecycle KERNEL=sel4 BOARD=pc
# lifecycle-probe: PASS
# Run script execution successful.

make -j1 -C genode/build/x86_64 run/sponge-pkg-admission KERNEL=sel4 BOARD=pc
# admission-probe: PASS
# Run script execution successful.
```

The unchanged pure generator also passed its host equivalence and golden
snapshot test (full reproducible build command is in `self_test.cc:20-35`):

```sh
g++ -std=c++20 -O1 \
  -I genode/repos/base/include \
  -I genode/repos/base/include/spec/64bit \
  -I genode/repos/base/include/spec/x86_64 \
  -I genode/repos/base/include/spec/x86 \
  -I genode/repos/base/src/include \
  -I repos/sponge/include \
  repos/sponge/lib/src/sponge_pkg_config/self_test.cc \
  repos/sponge/lib/src/sponge_pkg_config/pkg_config.cc \
  genode/repos/base/src/lib/base/xml_generator.cc \
  genode/repos/base/src/lib/base/output.cc \
  -o /tmp/sponge_pkg_config_self_test \
&& /tmp/sponge_pkg_config_self_test
# self-test: PASS (equivalence + golden snapshots)
```

This receipt closes P1.4 only. P1.5 emission validation and the DE/provider
failure-isolation tasks remain separate gates.

---

## 13. P1.5 emission-validation implementation and green receipt (2026-10-29)

**Implementation:** `a4d38119ea` (validator padding contract),
`22de0ec855` (publication seam), `ef92b95b4e` (authority harness), and
`374306cc74` (scenario contract). **Machine:** QEMU q35, `Skylake-Client`,
KVM, base-sel4. These are not real-hardware results. The host's 8 MiB
memlock limit required the temporary
`/tmp/opencode/libclamp_io_uring.so` QEMU shim; it is not a repository or
scenario dependency.

### 13.1 Publication contract

`sponge_pkgd` now renders and validates every dirty runtime target before
publishing any of them. A render or validation failure restores the dirty
marks, publishes neither target, and returns a bounded structured operation
error. Valid mixed-runtime operations still publish both targets; valid
single-runtime operations retain P1.4's dirty-scoped behavior. Genode's
`Expanding_reporter` was not changed.

Failure-path convergence is event-driven: only a later mutating operation
that reaches emission retries the queued dirty targets; an idempotent retry
that returns early (`already-running` or `package not installed`) does not.
The installed broadcast likewise remains at its last successful publication
until an end-to-end success. These are failure-path-only liveness limits,
not permission to publish invalid configuration.

The pure validator covers the runtime-config subset that pkgd emits. The
shape-checked `repro_padding` element remains the sole test-only validation
exception, with a deliberately unbounded `<p>` count because the reproduction
budget grows per write. The separate `repro_empty_from` positive control keeps
its explicitly scoped test-only bypass. Malformed XML and structural errors
remain hard publication failures.

The first survival run exposed an undocumented 1024-node padding cap at write
3 (1536 nodes). Removing that cap—not weakening any shape check—fixed the
regression. A host regression case now renders the exact write-3 document;
the validator self-test passes **80/80 cases**.

### 13.2 Commands and observed gates

All scenarios ran serially with `make -j1` against the same build directory.

```sh
make -j1 -C genode/build/x86_64 run/sponge-config-emission-invalid KERNEL=sel4 BOARD=pc
# config-emission-invalid-probe: PASS (structured rejection, nothing published)

make -j1 -C genode/build/x86_64 run/sponge-config-authority KERNEL=sel4 BOARD=pc
# config-authority-probe: PASS

make -j1 -C genode/build/x86_64 run/sponge-pkg-lifecycle KERNEL=sel4 BOARD=pc
# lifecycle-probe: PASS
# Run script execution successful.

make -j1 -C genode/build/x86_64 run/sponge-pkg-admission KERNEL=sel4 BOARD=pc
# admission-probe: PASS
# Run script execution successful.

make -j1 -C genode/build/x86_64 run/sponge-config-regen-survival KERNEL=sel4 BOARD=pc
# regen-probe: PASS
# Run script execution successful.

SPONGE_REPRO_CONTROL=1 make -j1 -C genode/build/x86_64 \
  run/sponge-config-regen-survival KERNEL=sel4 BOARD=pc
# regen-probe: FAIL witness was killed
# sponge-config-regen-survival: POSITIVE CONTROL — detector fired as required
# Run script execution successful.
```

The malformed scenario logs `status="error" op="install"` with the bounded
validator reason and never publishes the truncated document. The authority
harness now uses unanchored substring gates and a 200000-byte expect buffer;
its verbose publication flood no longer causes a false harness timeout.

This receipt closes P1.5. The remaining P1.2 admission-math extraction and the
DE/provider failure-isolation tasks remain separate gates.
