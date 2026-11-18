# Runtime component topology: the tree and the data chains

> **Breadcrumb:** parent [17 - Runtime Component Topology](../17-runtime-component-topology.md); this file holds its §1 (The tree), §2 (The three data chains) and §2a (The storage providers), moved verbatim.
> Siblings in `docs/topology/`: [incident-analyses.md](incident-analyses.md) (§3, §4, §4a), [design-gap-register.md](design-gap-register.md) (§5 part 1), [design-gap-register-2.md](design-gap-register-2.md) (§5 part 2), [quota-ledger.md](quota-ledger.md) (§6).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 1. The tree

```
seL4 kernel
└─ core            (root server: caps, PDs, threads, IO port/memory)
   └─ init         (root task: quota broker + session router)
      │
      │ ── hardware / plumbing (all Tier-0 boot modules) ──
      ├─ timer          periodic timeouts
      ├─ acpi           ACPI table reports
      ├─ pci_decode     PCI device inventory report
      ├─ platform       platform-driver service
      ├─ usb            pc_usb_host — the xHCI controller driver
      │                 (DDE Linux; claims the physical controller)
      ├─ usb_block      USB mass-storage class driver → the boot stick
      ├─ usb_hid        USB HID class driver → the mouse (Input svc)
      ├─ ps2            PS/2 keyboard/mouse (Input svc)
      ├─ event_filter   consumes Input, applies rules, provides Event
      │                 → routes Event to nitpicker
      ├─ part_block     GPT partitioning of the stick (rows for the
      │                 P3 sys, P4 store and P5 home providers)
      ├─ part_block_log a SECOND part_block instance for the P4 log
      │                 row (one Block session per partition — §2a)
      ├─ vfs_sys + rump ext2 on P3 (GENODE, read-only) → File_system
      ├─ vfs_pkg + rump ext2 on P4 (SPONGE-VAR, /pkgd) → File_system
      ├─ vfs_log + rump ext2 on P4 (SPONGE-VAR, /log) → File_system
      ├─ vfs_home + rump ext2 on P5 (SPONGE-HOME, /user) → File_system
      ├─ rom_sys        cached_fs_rom chroot /system  → ROM (binaries,
      │                 configs of every system child)
      ├─ rom_lib        cached_fs_rom chroot /system/lib → ROM (.lib.so)
      ├─ rom_pkg        cached_fs_rom chroot /system/pkg/falkon/
      │                 payload → ROM (the browser payload area)
      ├─ nitpicker      the GUI server: domains, input focus,
      │                 compositing — a COMPONENT, not a driver
      ├─ pointer        draws the mouse cursor (Gui client of
      │                 nitpicker's pointer domain); emits the hover
      │                 Report that the layouter's hover-state
      │                 re-evaluation consumes
      ├─ report_rom     top-level Report/ROM relay (window_list, focus,
      │                 hover, … — the whole wm-stack nervous system)
      │
      └─ system        (nested init #1 — caps 280000, RAM = the remainder)
         ├─ report_rom   #2 — internal channels (request/result,
         │               launcher_request/result, installed, rules, …)
         ├─ wm           window manager: virtualizes Gui for apps
         ├─ layouter     window_layouter: window placement (rules ROM)
         ├─ decorator    window chrome renderer
         ├─ sponge_configd   key/value config backend (persistent store)
         ├─ sponge_themed    theme server
         ├─ sponge_pkgd      package backend; regenerates the class
         │                   runtime configs (and driver_runtime's)
         ├─ vct_tty          Terminal bridge for the in-terminal vct
         ├─ sponge-de        the desktop (panel, launcher, tasklist,
         │                   background, settings, shortcuts)
         ├─ alpha_probe / cfg_probe   boot-time verifiers
         ├─ driver_runtime  (nested init #2 — caps 36000, ram 448M, both
         │                   EXPLICIT: it precedes the class runtimes,
         │                   so it cannot use the remainder clamp without
         │                   taking its RAM out of their pool.
         │                   Hosts <kind>driver</kind> packages.
         │                   Exactly ONE of them is present from the
         │                   first boot: the media's bake manifest
         │                   seeds the display driver (fb_vesa on the
         │                   BIOS media, fb_boot on the UEFI one) and
         │                   its <autostart/> puts the <start> node
         │                   here. The rest appear on `vct install
         │                   <driver>`, and `vct remove` takes any of
         │                   them away again — including the display
         │                   driver: the runtime fb switch is a
         │                   package operation (test/fb_switch_probe).
         ├─ pkg_runtime_simple    (nested init #3a — caps 6000,
         │                        ram 1488M, both EXPLICIT: the
         │                        simple class — hello, textedit,
         │                        files, calculator, pdf_view, debug)
         ├─ pkg_runtime_terminal  (nested init #3b — caps 2000,
         │                        ram 247M, both EXPLICIT: the
         │                        terminal class)
         └─ pkg_runtime_browser   (nested init #3c — caps 220000,
                                  RAM = the remainder: the browser
                                   class (falkon). LAST `+ start` node
                                   of the config — the clamp holder.)
```

G1 addition (2026-11-18): `sponge_compositor` is an alternative Tier-0
only in the dedicated opaque/differential scenarios. It is selected solely
by `SPONGE_COMPOSITOR=native`; the product tree above and every shipped
medium continue to use `nitpicker`. WM and DE consume only the public
Gui/Capture/Event surface and require no native-compositor dependency
([`../evidence/phase16-compositor-index.md`](../evidence/phase16-compositor-index.md)).

`driver_runtime` exists so that the capability boundary is structural

rather than conventional
([`12-package-format.md`](../12-package-format.md) §4.5): `sponge_pkgd`
generates per-target `<parent-provides>` lists from the package kinds
and runtime classes, and only the driver list carries `Platform` /
`Dma` / `Irq` / `Io_mem` / `Io_port`. An application package cannot
name its way to a hardware session even though `system` would happily
route one. See
[`04-components.md`](../04-components.md) §1.2.1.

Only the `system` and `pkg_runtime_browser` RAM grants read "the
remainder", not a fixed number — see §6.1. The two explicit class
runtimes carry fixed RAM; caps stay fixed and hand-derived everywhere.
The class-runtime grants are media-independent (identical on all five
desktop-disk variants). The `caps` figures above are the **uefi-usb**
values for `system`, which are the largest of the five desktop-disk
variants (they carry `vct_tty`). The two plain/ahci and nvme disk
variants that omit `vct_tty` grant `system` 263000 caps. Per-scenario
numbers are in §6.

## 2. The three data chains

**Storage** (how anything loads at all):
`xHCI → pc_usb_host → usb_block → part_block → vfs_sys/rump →
cached_fs_rom (rom_sys, rom_lib, rom_pkg) → every system child's
binary/config`
One chain, shared by all of `system`. A stall here starves every
system child — this is the boot-critical path. The writable-area
providers (`vfs_pkg`, `vfs_log`, `vfs_home`) sit **off** this chain on
purpose — see §2a.

**Input** (how the mouse reaches the screen):
`USB mouse → pc_usb_host → usb_hid → event_filter → nitpicker →
(pointer domain) → pointer component → cursor redraw`
Entirely top-level. **No node of this chain lives inside `system`.**

**Graphics** (how pixels reach the panel):
`sponge-de / apps → wm → nitpicker → fb_<pkg> → framebuffer`
Apps are three broker hops from the hardware, and the last hop is a
PACKAGE inside `driver_runtime`, not a Tier-0 node. `nitpicker` sits in
Tier 0 because it is the compositor server the whole desktop shares —
not a driver, and not something a package install should be able to
replace underneath every client at once.

**Display** (why the driver is a package): the framebuffer driver is
`pkg/fb_vesa` or `pkg/fb_boot`, both `<kind>driver</kind>`. The media
chooses one in its bake manifest, `sponge_pkgd` emits its `<start>`
node into `driver_runtime`, and the Tier-0 `platform` policy grants the
PCI `VGA` class under that package's own label prefix. Switching is
`vct remove <old> && vct install <new>` (docs/15 §8.2) and is refused
while the incumbent runs (docs/12 §4.6).

## 2a. The storage providers — three-area layout and the `vfs_pkg` / `vfs_log` split

**Naming authority (binding).** The data partitions are partition P3
`GENODE`, partition P4 `SPONGE-VAR`, partition P5 `SPONGE-HOME` (plus
the firmware partitions); there is no P6+. This is the naming
authority (plan D16R.3); `docs/14` §4.3 states it in full. Historical
note: before the P3.2 provider split the writable area was a single
P4 named `SPONGE-DATA` (the Phase-8 persistence lineage —
`tool/mkdata`, `run/sponge-persist-disk`, and older scenario headers
and logs still carry that name). `SPONGE-DATA` is history; no
documentation reintroduces it as current.

Roles on the shipped media (`run/sponge-desktop-disk-uefi-usb.run`):

| partition | name | content | served by |
|---|---|---|---|
| P3 | `GENODE` | `/boot`, `/system` — system-immutable; read-only at **both** the part_block policy and the rump mount | `vfs_sys` (labels `rom_sys`, `rom_lib`, `rom_pkg`) |
| P4 | `SPONGE-VAR` (64M) | `/pkgd` (package-store region) + `/log` (log-sink region) — system-mutable | **two** providers: `vfs_pkg` + `vfs_log` |
| P5 | `SPONGE-HOME` (1024M) | `/user` — user files | `vfs_home` |

**The provider split (P3.2, D16R.8, failure domain FD4).** Since P3.2
the two P4 regions are served by two separate File_system providers —
`vfs_pkg` (root `/pkgd`, the package-store region backing
`sponge_pkgd`'s persistent installed-roots store, docs/12 §13) and
`vfs_log` (root `/log`, the `sponge_logdisk` sink) — instead of the
pre-split shared `vfs_var` instance. The pre-split instance carried
both regions on one session path, so a stall or denial on the log path
took the package-store path down with it. The product wiring is the
one `run/sponge-provider-stall.run` proves:
`provider-stall: PASS — the log-path stall stayed on its own provider
path (the vfs_pkg/vfs_log split holds): system ROM and the package
store kept working while the log sink degraded`. Session-create paths
perform no storage I/O at all since P3.3 (dispatch parses package
metadata from bounded memory cached at bootstrap — 16 KiB per document
— store flushes run on the pkgd Bootstrap worker, and the store wait
is timer-paced off the entrypoint; `docs/evidence/phase16-runtime-p33.md`).

**Why `part_block_log` exists.** Upstream `part_block` allows only ONE
Block session per partition — the second create is refused
`partition 4 already in use` (`os/src/server/part_block/main.cc:286`).
So each P4 provider gets its OWN `part_block` instance (same binary,
same row semantics; `usb_block` serves both sessions — its
`Session_map` allocates one index per session): `vfs_pkg`'s row lives
in the Tier-0 `part_block`, `vfs_log`'s identical row (partition 4,
writeable) lives in `part_block_log`. That is the whole rationale: a
wiring constraint of the upstream partitioning server, not a storage-
layout change — both rows pin the same partition.

**The coherence caveat (honest).** Both P4 regions share one ext2
partition behind **two independent rump mounts** (D16R.8's separation
is wiring-level only). Concurrent metadata writes from the two mounts
are a known ext2 coherence hazard; today only the log sink writes
(pkgd's store is not yet wired to `vfs_pkg` in the product), so the
hazard is not exercised. If both regions gain writers, revisit — the
alternatives are one provider per partition-pair or a shared Block
layer, both larger than the P3.2 wiring
(`docs/evidence/phase16-runtime-provider-stall.md` §5.5).

**Provider quotas (uefi-usb Tier-0, `run/sponge-desktop-disk-uefi-usb.run`).**
The three-area layout adds these nodes on top of the two 256M `rom_*`
servers; the figures are what the §6.1 uefi-usb Tier-0 accounting
charges:

| node | caps | RAM | note |
|---|---:|---:|---|
| `vfs_sys` | 16000 | 128M | P3 read-only; rump 48M |
| `vfs_pkg` | 2000 | 32M | P4 `/pkgd`; rump 16M |
| `vfs_log` | 2000 | 32M | P4 `/log`; rump 16M |
| `vfs_home` | 2000 | 32M | P5 `/user`; rump 16M |
| `part_block_log` | 200 | 8M | the log path's own part_block |
| `rom_pkg` | 20000 | 768M | falkon's ~509 MiB payload (§6) |

Scope: these are QEMU/base-sel4 receipts (D16R.12 labelling); the
`{uefi,uefi-nvme}` disk variants remain structure-only gates in QEMU
(D15.16) — runs that do not exercise the desktop gate (`alpha-probe`),
which is the D15.16 limitation, not a boot block (QEMU/OVMF boots
reach Genode; `docs/11-environment.md` §4 ledger rows 10 + 11 retired
the old W^X/bender-NOP cause).
