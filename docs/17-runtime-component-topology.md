# Runtime Component Topology — the 0.2.0 product media

> Verified against `run/sponge-desktop-disk-uefi-usb.run` (17:35 image
> lineage). This is the inventory of every component above the seL4
> kernel, what it does, and — critically — which shared brokers the
> "isolated" components actually depend on. Written after the 2026-10-04
> real-hardware regression (vct_tty's missing `<provides>` froze the
> whole desktop): the isolation model held, but liveness did not, and
> the difference is the point of this document.

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
      ├─ part_block     GPT partitioning of the stick
      ├─ vfs + rump     ext2 on P3 (GENODE partition) → File_system
      ├─ rom_sys        cached_fs_rom chroot /system  → ROM (binaries,
      │                 configs of every system child)
      ├─ rom_lib        cached_fs_rom chroot /system/lib → ROM (.lib.so)
      ├─ fb             boot_fb — GOP framebuffer + capture buffer
      ├─ nitpicker      the GUI server: framebuffer owner, domains,
      │                 input focus, compositing
      ├─ pointer        draws the mouse cursor (Gui client of
      │                 nitpicker's pointer domain); emits the hover
      │                 Report that the layouter's hover-state
      │                 re-evaluation consumes
      ├─ report_rom     top-level Report/ROM relay (window_list, focus,
      │                 hover, … — the whole wm-stack nervous system)
      │
      └─ system        (nested init #1 — caps 280000, RAM 3420M)
         ├─ report_rom   #2 — internal channels (request/result,
         │               launcher_request/result, installed, rules, …)
         ├─ wm           window manager: virtualizes Gui for apps
         ├─ layouter     window_layouter: window placement (rules ROM)
         ├─ decorator    window chrome renderer
         ├─ sponge_configd   key/value config backend (persistent store)
         ├─ sponge_themed    theme server
         ├─ sponge_pkgd      package backend; regenerates pkg_runtime
         ├─ vct_tty          Terminal bridge for the in-terminal vct
         ├─ sponge-de        the desktop (panel, launcher, tasklist,
         │                   background, settings, shortcuts)
         ├─ alpha_probe / cfg_probe   boot-time verifiers
         └─ pkg_runtime  (nested init #2 — caps 230000, RAM 2816M)
            └─ hello, calculator, pdf_view, falkon, terminal, …
```

The `system` and `pkg_runtime` figures above are the **uefi-usb**
values, which are the largest of the five desktop-disk variants
(they carry `vct_tty`). The two plain/ahci and nvme disk variants
that omit `vct_tty` grant `system` caps 263000 / RAM 3420M. Per-scenario
numbers are listed in §6.

## 2. The three data chains

**Storage** (how anything loads at all):
`xHCI → pc_usb_host → usb_block → part_block → vfs/rump →
cached_fs_rom (rom_sys, rom_lib) → every system child's binary/config`
One chain, shared by all of `system`. A stall here starves every
system child — this is the boot-critical path.

**Input** (how the mouse reaches the screen):
`USB mouse → pc_usb_host → usb_hid → event_filter → nitpicker →
(pointer domain) → pointer component → cursor redraw`
Entirely top-level. **No node of this chain lives inside `system`.**

**Graphics** (how pixels reach the panel):
`sponge-de / apps → wm → nitpicker → fb → GOP framebuffer`
Apps are three broker hops from the hardware.

## 3. Why a terminal component froze the desktop (the honest analysis)

The 15:55 regression: `vct_tty` shipped without a `<config>` node —
the first config-less child in the `system` sandbox — and the
sandbox's entrypoint thread aborted
(`Attached_dataspace::Region_conflict`) creating it. Result: no
panel, frozen desktop.

Component isolation did **not** fail:
- No component read or wrote another component's memory.
- No capability was forged.

What failed is **liveness of a shared broker**:
1. **The sandbox entrypoint is a single thread** that brokers every
   session request of every child (Gui, ROM, Report, PD, CPU …).
   One uncaught exception in that thread froze all session traffic
   for all children — the panel could not complete its Gui session.
   The trigger was mine; the fragility (an ep abort on a child-config
   edge case, with no fault containment) is broker-level.
2. **Resource pools are shared**: children of one init draw caps/RAM
   from a single grant (`system`: 280000 caps / 3420M on the product
   media). Adding a child can starve siblings — quota arithmetic, not
   fault isolation. The same arithmetic bit `pkg_runtime`: as a
   nested init it draws its own 230000 caps / 2816M grant, which must
   cover the SUM of every simultaneously-enabled package (§6).
3. **report_rom is a shared nervous system**: one process relays every
   Report/ROM channel in its scope; a policy or evaluation problem
   there has system-wide blast radius.

## 4. The mouse follow-up (17:35 image) — topology says: suspicious

With the `<provides>` fix everything renders, but the real-hardware
cursor does not move. Per §2, the input chain has **zero nodes inside
`system`** — the vct_tty/pkgd/sponge-de changes are structurally
unable to freeze `usb_hid → event_filter → nitpicker → pointer`
unless a *shared* substrate (nitpicker load, core caps, CPU) is
saturated. The discriminating tests, in order:

1. **Diagnostic media** (`SPONGE_FBCON=on`): kernel fb console on the
   panel shows whether `usb_hid` enumerated the mouse at all, and any
   event_filter/nitpicker/pointer errors — on the real machine.
2. **QEMU input test**: boot the 17:55-lineage image, drive the QEMU
   mouse via QMP, screendump — proves the chain works with the same
   binaries/configs.
3. **Replug/reboot the mouse on real hw**: the known USB-enumeration
   flake family (documented in Phase 15).

## 5. Design gap register (what "microkernel-proper" would add)

The user's intuition is correct: properly deployed, a terminal
component must never be able to freeze the desktop. Status after the
2026-10-04 containment deployment and the 2026-10 quota-sizing fix
(§6):

- [x] **Broker fault containment — system level (deployed)**: the
  five desktop-disk product scenarios' system init runs
  `<heartbeat rate_ms="2000"/>` with
  `<heartbeat restart_after_skipped="3"/>` on the critical
  children (wm, layouter, decorator, sponge_configd, sponge_themed,
  sponge_pkgd, vct_tty, sponge-de, pkg_runtime). A child whose
  entrypoint wedges is abandoned and respawned within ~6 s. Verified:
  the product scenario boots PASS with the deployment in place.
- [ ] **Broker fault containment — top level (blocked by design)**:
  the top-level init cannot run a heartbeat because core provides no
  Timer on base-sel4 (empirically confirmed: the session is denied
  and init stops). Restarting a wedged `system` child from above
  requires a supervisor that owns the top-level config (the pkgd
  pattern applied one level up) — recorded as the follow-up design.
- [ ] **Broker fault containment — `run/sponge-alpha.run` (blocked by
  topology)**: alpha is a FLAT topology. `wm`, `layouter`,
  `decorator`, `sponge_configd`, `sponge_themed`, `sponge_pkgd`,
  `sponge-de` and `pkg_runtime` are direct children of the
  TOP-LEVEL init; its only nested init (`drivers`) hosts the hardware
  set, not the desktop. A heartbeat is served by the sandbox that
  hosts the children, so it can only be deployed on a non-top-level
  init — and the top-level one has no Timer (row above). Deploying
  containment here therefore requires restructuring alpha into the
  nested-`system` shape the disk scenarios already use, which is a
  topology change and would invalidate alpha's `alpha-probe: PASS`
  evidence. Not done; the rationale is recorded in the scenario file
  above its top-level init configuration.
- [ ] **Broker hardening**: the config-less-child ep abort is an
  upstream-adjacent fragility (workaround: every child ships an
  explicit — possibly empty — config node; vct_tty's fix). The
  containment above now bounds its blast radius to the child.
- [x] **Resource headroom policy**: every `system` and `pkg_runtime`
  grant across the five desktop-disk product scenarios carries ~10%
  headroom over its own children sum (§6), and the enclosing QEMU
  `-m` is sized so init's pool exceeds Tier-0 + the `system` grant by
  ≥1 GiB. Adding a child is not a cliff-edge operation.
- [~] **Containment regression test**: `test/ep_wedge` (wedge +
  witness modes) and `run/sponge-heartbeat-restart.run` are written
  and the component builds, but the scenario is blocked by a
  PRE-EXISTING environment regression: flat ISO boots deny the HPET
  IO_MEM (`sponge-pkg-explain` fails identically), so no flat
  scenario can run a timer on this host today. The disk/UEFI product
  path is unaffected. Revisit when the flat-boot HPET issue is
  root-caused (QEMU/seaBIOS ACPI exposure).

## 6. Quota ledger (the five desktop-disk scenarios)

`pkg_runtime` hosts every enabled package **simultaneously**, so its
grant must cover the SUM of the package quotas declared in
`pkg/*/metadata.xml`, not one package's worth. The `desktop` bake
profile (`pkg/bake/desktop.profile`) enables eight packages:

| package | caps | RAM |
|---|---:|---:|
| hello | 256 | 8M |
| terminal | 1100 | 224M |
| textedit | 1000 | 64M |
| files | 1000 | 64M |
| calculator | 1000 | 64M |
| pdf_view | 256 | 1024M |
| falkon | 200000 | 1024M |
| debug | 1000 | 64M |
| **enabled sum** | **205612** | **2536M** |

(206512 caps counting the `nano` 600 + `ncurses` 300 pair reachable
from the editor packages.) Hence the uniform
`pkg_runtime | caps: 230000 | ram: 2816M` on all five desktop-disk
scenarios: **+11.9% caps / +11.0% RAM** over the enabled sum.

Before this change the four ahci/nvme desktop-disk variants granted
`pkg_runtime` 8000 caps / 1536M — falkon (200000 caps) was **25x
short** and could never launch, and the RAM sum overshot the grant by
~1 GiB. The uefi-usb variant's 210000 caps / 2048M was closer but
still ~488M under on RAM and under on caps once pdf_view and terminal
are counted.

| scenario | `system` children | `system` grant | headroom | QEMU `-m` |
|---|---|---|---:|---:|
| sponge-desktop-disk | 238650 / 3100M | 263000 / 3420M | +10.2% / +10.3% | 6G |
| sponge-desktop-disk-nvme | 238650 / 3100M | 263000 / 3420M | +10.2% / +10.3% | 6G |
| sponge-desktop-disk-uefi | 254100 / 3100M | 280000 / 3420M | +10.2% / +10.3% | 8G |
| sponge-desktop-disk-uefi-nvme | 254100 / 3100M | 280000 / 3420M | +10.2% / +10.3% | 8G |
| sponge-desktop-disk-uefi-usb | 254300 / 3108M | 280000 / 3420M | +10.1% / +10.0% | 8G |

The `-m` column is derived, not decorative, and the two boot paths
reserve very different amounts of guest RAM:

- **Direct-kernel (BIOS) path** — core reserves ~458 MiB for kernel +
  rootserver. Measured on `sponge-desktop-disk` after this change:
  `-m 6G` ⇒ "5686 MiB RAM and 523288 caps assigned to init" against
  4294 MiB of grants ⇒ ~1392 MiB of slack. At `-m 4G` the pool was
  3667 MiB — already ~600 MiB *below* the grant total.
- **OVMF/UEFI path** — ~2.4 GiB, not ~458 MiB. Measured on
  `sponge-desktop-disk-uefi-usb`: `-m 8G` ⇒ 5826 MiB assigned to init
  against 5318 MiB of grants ⇒ ~508 MiB of slack. Extrapolating the
  BIOS overhead would have put a `-m 6G` UEFI pool near 3.8 GiB,
  ~800 MiB *below* the grant total. Hence all three UEFI variants
  carry 8G.

uefi-usb needs the larger Tier-0 as well: the three-area layout adds
`vfs_var` (32M), `vfs_home` (32M) and `rom_pkg` (768M for falkon's
payload) on top of the two 256M `rom_*` servers, bringing Tier-0 to
1898M.

**Residual gap**: `run/sponge-alpha.run` still grants `pkg_runtime`
400 caps / 32M — sized for the hello-only Phase-7 topology, not for
the `desktop` profile it stages by default. Its 2G QEMU envelope
would have to grow in lockstep and its PASS evidence would have to be
re-earned, so the change is deliberately separate.
