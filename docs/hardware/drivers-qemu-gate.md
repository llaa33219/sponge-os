# 15 - Hardware Compatibility: Drivers & QEMU Gate (§7-§8)

> **Breadcrumb:** parent [15 - Hardware Compatibility](../15-hardware-compatibility.md); this file holds its §7 (QEMU hardware-matrix gate) and §8 (Driver packages), moved verbatim.
> Siblings in `docs/hardware/`: [surface-matrix.md](surface-matrix.md) (§1.1, §1.4-§1.5), [cell-contract.md](cell-contract.md) (§3), [known-gaps.md](known-gaps.md) (§4).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

---

## 7. QEMU hardware-matrix gate (Phase 15 pre-real-hw hardening, 2026-09-13)

The headless QEMU hardware-matrix gate
(`tool/hwtest` driving `run/sponge-hw-matrix.run`) verifies the
boot + input-interaction chain across emulated-hardware variation
before any real-hardware attempt. It is NOT part of the
`tool/hw_compat assert` cell contract above; it is a separate
regression suite whose receipts are the scenario's PASS markers and
the tool's summary table (see `docs/08` §16 for the manual
equivalent).

Full-matrix receipt (2026-09-13, seL4 16.0.0, KVM): **20/20
variants PASS** — baseline, cpu-max, cpu-haswell,
cpu-cascadelake-server, cpu-sandybridge, cpu-la57 (5-level
paging), smp2, smp4, smp8, topo-2s2c2t (2s×2c×2t topology),
mem4g, machine-pc, usb-ehci, usb-uhci (1st-gen controller),
input-mouse, input-kbd, input-ps2, input-multi (dual-HID
residency), iommu-on (VT-d interrupt remapping,
kernel-irqchip=split), and the cpu=max/smp=4/mem=4G/input=mouse
shakedown.

Expected-unsupported findings (matrix axis boundaries, documented in
`docs/08` §16): XSAVE-less CPU models (qemu64, Nehalem) halt at
seL4 16.0.0's `XSAVE not supported` check; `-vga cirrus` has no
VBE mode for vesa_fb in this QEMU build; TCG boots are ~50% flaky
from a guest-side Timer-session race (kept out of the default
matrix, manual invocation documented); `-no-hpet` starves the
HPET-only timer driver (no PIT fallback).

Fixed during the reproduction work (2026-09-14, ledger row 16):
firmware DMAR tables (DRHD+RMRR — present on every real Intel
board, absent from QEMU's synthetic tables) killed the acpi child
via an uncaught `Mmio::Range_violation` in the DMAR walkers,
stalling pci_decode/platform/usb — the exact on-panel signature
reported from the 17ZD90N on both stack generations. The acpi
driver now skips DMAR parsing (report-only information); the
regression gate is `./tool/hwrepro --case acpi-dmar` (injects
`run/fixtures/dmar-rmrr.aml` and requires the usb chain to boot).

## 8. Driver packages — per-driver verification state

The PC driver set is installable with the ordinary package flow
(`vct install <driver>`) and runs in the `driver_runtime` sandbox
(docs/12-package-format.md §4.5). QEMU emulates exactly one of these
drivers' hardware, so the verification state is deliberately uneven and
recorded here rather than glossed.

| Package | Upstream build target | Binary | State | What the state means |
|---|---|---|---|---|
| `pc_nic` | `driver/nic/pc` | `pc_nic` | **verified in QEMU** | Installed through `sponge_pkgd` into `driver_runtime` on QEMU `-device e1000`; the e1000e driver binds (`e1000 00:02.0 eth0: Intel(R) PRO/1000 Network Connection`) and `nic_router` leases `10.0.2.15` from QEMU's slirp DHCP. Gate: `run/sponge-pkg-driver.run`. |
| `usb_net` | `driver/usb_net` | `usb_net` | builds only | Needs a USB Ethernet adapter (QEMU's `usb-net` is untested here) **and** a `Terminal` service provider routed into `driver_runtime` for its DDE-Linux option interface. Deliberately not autostarted. |
| `pc_wifi` | `driver/wifi` | `wifi` | builds only | Needs real Wi-Fi hardware and firmware the user supplies (§8.1). Not autostarted. |
| `fb_vesa` | `driver/framebuffer/vesa` | `vesa_fb` | **verified in QEMU** | The BIOS/VESA display path, as a package. Installed into `driver_runtime` from the BIOS media's bake manifest (`<autostart/>`), programs a 1024x768 INT 10h mode (`Found: VESA BIOS version 3.0` / `using 1024x768 (1024x768)`), and the desktop's pixel gate passes on top of it. Gate: `run/sponge-desktop-disk.run` (`alpha-probe: PASS`). |
| `fb_boot` | `driver/framebuffer/boot` | `boot_fb` | **verified in QEMU** | The UEFI/GOP display path, as a package. Installed into `driver_runtime` from the UEFI media's bake manifest; maps the framebuffer the kernel reported (`using boot framebuffer: 2560x1600x32 @ 0x80000000` on OVMF, `1280x800x32 @ 0xfd000000` on the BIOS media's GRUB2 framebuffer tag). Gate: `run/sponge-desktop-disk-uefi.run` and `run/sponge-desktop-disk.run` with `SPONGE_FB_SWITCH=on`. |
| `intel_gpu` | `driver/gpu/intel` | `intel_gpu` | builds only | Competes with the ACTIVE `fb` package for the PCI VGA class (§8.2). Not autostarted. |
| `pc_intel_fb` | `driver/framebuffer/intel/pc/spec/x86_64` | `pc_intel_fb` | builds only | Same VGA conflict (§8.2). Not autostarted. |

The rule the table encodes: **`<autostart/>` means "verified end to
end"**. An unverified driver installs as STOPPED and only
`vct launch <driver>` attempts a start — the user's explicit act
(AGENTS.md §1.1). The verified set is therefore self-declaring: a
driver that regresses silently loses its autostart marker rather than
failing every boot.

**The display driver is one of them.** There is no static framebuffer
node in any product topology any more: `Tier-0` starts `nitpicker`
(the compositor server, a component) and nothing that produces pixels.
The pixel producer is whichever `fb_*` package the medium's bake
manifest names — `fb_vesa` on the BIOS media, `fb_boot` on the UEFI
one — and it is an ordinary package from then on. Both are staged on
every medium, so the other one is one `vct remove` + `vct install`
away (§8.2).

### 8.1 Wi-Fi firmware is not bundled

`driver/wifi` loads its firmware as files from `/firmware` inside the
driver's VFS (`genode/repos/dde_linux/src/driver/wifi/main.cc`, the
`Fw_path { "/firmware/", request.name }` requests). The `linux-firmware`
archive is GPL-2.0 redistribution with its own distribution terms, so it
is deliberately **not** vendored into this repository.

Acquisition, when `pc_wifi` is wanted on real hardware:

1. Fetch the kernel firmware for your adapter (Debian/Ubuntu:
   `apt-get install firmware-iwlwifi` or `firmware-atheros`; Arch:
   `linux-firmware`; Fedora: `iwl-firmware` / `atheros-firmware`).
2. Stage the matching files where the driver's VFS makes them visible
   (a `File_system` session declared by the package's `<sessions>`, and
   the `firmware` directory of the driver sandbox — the mapping is a
   bring-up step and is **not** yet wired on the product media).
3. `vct install pc_wifi && vct launch pc_wifi`, then confirm association
   in the `nic_router` domain state.

Until steps 2 and 3 are done, `pc_wifi` starts and then fails to
associate. That is the honest expected outcome, not a defect.

### 8.2 The VGA conflict — how it is resolved now, and what is left

**Status: resolved for the packaged framebuffer drivers; still open for
`intel_gpu` / `pc_intel_fb`.**

The conflict used to be structural. The product media's Tier-0
`platform` config granted the PCI `VGA` class to the static `fb` label
prefix, which fed a static `+ start fb` node:

```
    + policy | label_prefix: fb | info: yes | + pci | class: VGA
```

`intel_gpu` and `pc_intel_fb` need the same class, and a PCI device is
offered to exactly one policy, so installing either meant re-pointing
the Tier-0 policy — a deliberate, visible, reversible edit to the
system's boot wiring, and therefore left to the operator.

Two things changed:

1. **The display driver became a package** (`docs/17` §1). The grant
   no longer names a static node; it names each fb package's own label
   prefix, in `run/driver_runtime.inc::platform_policies`:

   ```
   + policy | label_prefix: system -> driver_runtime -> fb_vesa | info: yes | + pci | class: VGA
   + policy | label_prefix: system -> driver_runtime -> fb_boot | info: yes | + pci | class: VGA
   ```

   Two grants for one class is safe because the packages are mutually
   exclusive at RUN time (`docs/12` §4.6): the `platform` driver
   publishes the device under both prefixes and only the running driver
   opens a `Platform` session. What a grant cannot do is decide WHICH
   driver runs — that is the bake manifest at first boot, and
   `vct remove` / `vct install` afterwards.

2. **The exclusivity is enforced, not hoped for.** `sponge_pkgd`
   refuses a second running member of the `fb` group with
   `exclusive-conflict`, naming the incumbent.

**Switching display drivers — the exact commands:**

```
# BIOS media: VESA INT 10h  ->  UEFI GOP framebuffer
vct remove fb_vesa
vct install fb_boot          # <autostart/>: no separate launch needed

# the other direction
vct remove fb_boot
vct install fb_vesa
```

A switch attempt while the incumbent runs is refused with the remedy
in the message (`vct remove fb_vesa` first), which is the "control is
a door that is always open" half of AGENTS.md §1.1; the two-command
sequence above is the automation.

**Verified, and verified where.** `SPONGE_FB_SWITCH=on` on the BIOS
media removes `fb_vesa`, installs `fb_boot`, and requires the
composited screen to come back and SETTLE on a rendered desktop
(`test/fb_switch_probe`). Measured on that run:

```
[init -> system -> driver_runtime -> fb_vesa] using 1024x768 (1024x768)
[init -> system -> driver_runtime -> fb_boot]  using boot framebuffer: 1280x800x32 @ 0xfd000000 type=1 pitch=5120 -> 1280x800
[init -> system -> fb_switch_probe] fb-switch-probe: post-switch frame rendered 247/640 sampled pixels (settled)
[init -> system -> fb_switch_probe] fb-switch-probe: PASS
```

So the display recovery is a property of the package handover, not of
a hand-wired Tier-0 node: `nitpicker` survives its Capture client
disappearing and reappearing at a different resolution, and the
desktop repaints onto the new one.

**Honest limits of the reverse direction.** `vesa_fb` programs the BIOS
INT 10h interface through `x86emu`. OVMF provides no INT 10h, so
`vct remove fb_boot && vct install fb_vesa` on UEFI media is expected
to leave the display dark. That direction is a documented gap, not a
supported switch; the BIOS→GOP direction is the verified one.

**What is still open.** `intel_gpu` and `pc_intel_fb` want the same PCI
`VGA` class the active `fb_*` package holds. They are still not
granted: taking the class away from a working display driver is still a
Tier-0 decision, and it is recorded here as a bring-up step rather than
done. A user who wants one removes the fb package first, then makes the
policy edit, exactly as before — the difference is that the display
driver itself no longer has to be edited.

**One Genode routing subtlety this work exposed**, recorded because it
cost real time and will bite the next driver that needs a core ROM:
`platform_info` (a CORE rom module, exported by base-sel4 from the
multiboot2 boot info) is requested by a packaged driver as a
grandchild of the Tier-0 `system` node, and the route rule that
reaches it must be

```
+ service ROM | label_last: platform_info | + parent
```

placed BEFORE that node's `+ service ROM | + child rom_sys`
catch-all. Two reasons, both in
`genode/repos/os/src/lib/sandbox/route_model.h`: the query label is
scoped to the child at whichever node the request arrives at, so a
`label:` rule cannot match across a nesting level while `label_last:`
can; and every `<service>` node sets `_specific_service`, so a denial
inside the `rom_sys` catch-all STOPS the rule search instead of
falling through to `any-service -> parent`. The failure mode is a
silent `parent denied ROM-session` with no "no route to service" line.

