# Driver packages in `driver_runtime` — bring-up evidence

Date: 2026-10-18. Branch: the tree at the commits listed in §6.

## 1. What was asked

Bring Sculpt OS's PC driver set over as **installable packages** with the
same `vct install <driver>` flow as applications, hosted in a dedicated
`driver_runtime` sandbox ("the most microkernel-like way": each driver in
its own protection domain, hardware capabilities explicitly routed,
drivers loaded on demand).

The driver's set, and the split that was applied:

| Sculpt inventory entry | Decision | Why |
|---|---|---|
| `acpi`, `pci_decode`, `pc_platform`, `pc_usb_host`, `usb_block`, `usb_hid`, `ps2`, `ahci`/`nvme`, `vesa_fb`/`boot_fb` | **stay static** | They own boot/storage/input hardware on the product media; they are the *manual* side of the escape hatch. |
| `pc_nic` | **package, autostart** | QEMU-emulable; verified end to end. |
| `usb_net`, `pc_wifi`, `intel_gpu`, `pc_intel_fb` | **package, NOT autostart** | They build, but no QEMU model exists for their hardware; starting them silently on install would ship a claim we cannot back. |
| `pc_discover`, `rom_reporter` | **dropped** | Not drivers. Both are diagnostics that open no hardware session, so putting them in `driver_runtime` would grant it capabilities neither has any use for (AGENTS.md §1.2). Verified buildable: `app/pc_discover` → `pc_discover`, `app/rom_reporter` → `rom_reporter`. |

## 2. Build verification — every target exists in the fork

All seven Sculpt inventory build targets were checked against the pinned
`genode/` submodule (`sponge-os-genode`, Genode 26.08 + patches). No
vendored patch was needed or taken; the patch ledger is unchanged
(`./tool/patches verify` → `OK (all 23 ledger rows match git reality)`).

| Target | Binary | Result |
|---|---|---|
| `driver/nic/pc` (`repos/pc/src/driver/nic/pc`) | `pc_nic` | builds |
| `driver/usb_net` (`repos/dde_linux/src/driver/usb_net`) | `usb_net` | builds |
| `driver/wifi` (`repos/dde_linux/src/driver/wifi`) | `wifi` | builds (needs `libnl`, `openssl`, `wpa_supplicant` — fetched, see §3) |
| `driver/gpu/intel` (`repos/os/src/driver/gpu/intel`) | `intel_gpu` | builds |
| `driver/framebuffer/intel/pc/spec/x86_64` | `pc_intel_fb` | builds |
| `app/pc_discover` | `pc_discover` | builds (dropped, see §1) |
| `app/rom_reporter` | `rom_reporter` | builds (dropped, see §1) |

Note on target naming: `pc_intel_fb` has no `target.mk` at
`repos/pc/src/driver/framebuffer/intel/pc/` — only a `target.inc`. The
buildable unit is the arch leaf `.../spec/x86_64`, which is what
`run/driver_runtime.inc` names.

## 3. New ports (docs/11 §5)

Three ports were not previously in `tool/build.mojo`'s port list and are
now:

| Port | Fingerprint | Origin | Licence |
|---|---|---|---|
| `libnl` | 3.2.25 | `github.com/thom311/libnl` | LGPL-2.1 |
| `wpa_supplicant` | 2.11 | `w1.fi` | BSD-3-Clause |
| `openssl` | 1.1.1w | `openssl.org` | OpenSSL |

All three are SHA-256-verified by Genode's `prepare_port`; the `.port`
and `.hash` files already lived in the vendored tree
(`repos/dde_linux/ports/{libnl,wpa_supplicant}`, `repos/libports/ports/openssl`).

**Firmware is deliberately NOT bundled.** `driver/wifi` loads firmware
from `/firmware` inside its VFS; the `linux-firmware` archive is
GPL-2.0 redistribution with its own distribution terms. The acquisition
procedure is documented in `docs/15-hardware-compatibility.md` §8.1.

## 4. QEMU verification — `run/sponge-pkg-driver.run` (red → green)

The scenario boots the product-shaped topology (Tier-0 hardware +
a `system` sandbox holding `report_rom`, `sponge_pkgd` and **two sibling
nested runtimes**, `driver_runtime` and `pkg_runtime`) and drives
`sponge_pkgd` through the ordinary install channel.

Gate order (the run tool consumes the stream sequentially, so each gate
can only match a marker that follows the previous one):

```
sponge-pkg-driver: PASS (pc_nic installed as a package into driver_runtime, bound the e1000, leased 10.0.2.15)
```

with the underlying log markers, in order:

```
[init -> system -> sponge_pkgd] sponge_pkgd: resource admission armed (pkg_runtime state report)
[init -> system -> sponge_pkgd] sponge_pkgd: driver resource admission armed (driver_runtime state report)
[init -> system -> sponge_pkgd] pkgd-dbg-equivalent: op=install pkg=pc_nic      <- (instrumentation removed after diagnosis)
[init -> system -> driver_probe] driver-probe: install answered status=ok
[init -> system -> driver_probe] driver-probe: driver_runtime lists child 'pc_nic'
[init -> system -> driver_probe] driver-probe: PASS
[init -> system -> driver_runtime -> pc_nic] netdev: Intel(R) PRO/1000 Network Driver
[init -> system -> driver_runtime -> pc_nic] e1000 00:02.0 eth0: Intel(R) PRO/1000 Network Connection
[init -> nic_router] [uplink] NIC sessions: 1
[init -> nic_router] [uplink] dhcp offer from 10.0.2.2, offering 10.0.2.15
```

What each gate can and cannot prove:

- `install answered status=ok` is produced only after the admission
  pre-flight (pc_nic's declared 32M/1000 quota fits `driver_runtime`'s
  **own** live budget) and after the driver_runtime config was
  regenerated with pc_nic's `<start>` node.
- `driver_runtime lists child 'pc_nic'` is the load-bearing one. That
  report is generated by `driver_runtime`'s **own init** from its own
  child table, so the name can only appear if the pkgd-generated config
  was actually delivered and the component was actually created. A
  config that was never delivered, a binary that was never staged, or a
  quota that clamped to nothing all leave no `<child>` node — the probe
  fails loudly instead of passing on the "ok" alone.
- The e1000e module-init line only exists if the driver attached to the
  PCI device, and the slirp DHCP offer is a literal the guest cannot
  invent. Those two are the hardware and network-stack proof.

### 4.1 Defects found and fixed while bringing this up

All found by the gates above, all fixed (the scenario is green):

| Defect | Symptom | Fix |
|---|---|---|
| `driver_runtime` is not a sibling of the `system` children that serve it | `platform denied Platform-session` | added the hardware services to `system`'s `<parent-provides>`; the real boundary is pkgd's generated `pkg_runtime` list, which is unchanged |
| The `platform` device policy is matched against the **scoped** session label | `Invalid session request, no matching policy for 'system -> driver_runtime -> pc_nic -> '` | policy written as `label_prefix: system -> driver_runtime -> pc_nic` |
| `nic_router`'s Uplink policy has the same scoping issue | uplink never connected, no DHCP | `label_prefix: system -> driver_runtime` (the whole sandbox, so a second Uplink driver needs no policy change) |
| `pc_nic` declares no `Timer` session | `stop because parent denied Timer-session` | the DDE-Linux kernel clock needs one; added to `pkg/pc_nic/metadata.xml` |
| `sponge_pkgd`'s config substitute map was a braced literal | the `[procs]` were substituted as literal text, so `caps:` became empty and `system` booted with **zero** caps (`Capability preservation exceeds available capabilities`) | build the map with `list`, which evaluates the substitutions |
| A substituted HID fragment carried its own indentation | the first line landed 4 columns deeper and nested the relay policy *inside* the preceding `alpha_probe -> sponge_de_launcher` policy; `report_rom: no valid policy for ROM request 'driver_runtime -> config'` | fragments now start unindented; the `@PLACEHOLDER@` supplies the column |
| `|` inside a `#` comment is not a comment in Genode's HID | the comment became a node and the config failed schema validation at a line that had nothing to do with the change | documented in `run/README.md`; avoided in the new files |

Two further findings worth recording because they cost time and would
cost the next person more:

- **Genode's run tool redefines `read` and `regsub`.** `read` there is
  `read <channel> <count>`; the two-argument `read $fh var` form of the
  language's `read` does not exist. `regsub` is likewise a three-
  argument run-tool command. Use `[read $fh]` and `string map`.
- **`run/sponge-desktop-disk-uefi-usb.run` uses
  `install_config [subst [supervisor::tier0 {...}]]`** — it is the one
  product scenario with no `tier0_config` variable, so `@PLACEHOLDER@`
  substitution never runs there. It uses `[driver_runtime::…]` calls
  directly instead. Sourcing `driver_runtime.inc` without checking which
  of the two shapes a variant uses silently ships `caps: 0`.

## 5. Regression receipts

All on base-sel4 / QEMU, run after the wiring landed:

| Scenario | Result | Note |
|---|---|---|
| `sponge-pkg-driver` | **PASS** | §4 |
| `sponge-desktop-disk` | **PASS** (`alpha-probe: PASS`) | product BIOS media |
| `sponge-desktop-disk-uefi` | **PASS** | product UEFI media |
| `sponge-desktop-disk-uefi-usb` | **PASS** | product UEFI-on-USB media |
| `sponge-desktop-disk-uefi-nvme` | **PASS** | product UEFI + NVMe media |
| `sponge-pkg-lifecycle` | **PASS** (`lifecycle-probe: PASS`) | the pkgd lifecycle, unchanged |
| `sponge-minimal` | **PASS** | lean topology, unchanged |
| `sponge-desktop-disk-nvme` | **FAIL — pre-existing, not a regression** | see below |

`sponge-desktop-disk-nvme` carries `+ bake` in pkgd's config but never
calls `bake::stage`, so `rom_sys` reports `/bake_manifest.json not found`
and pkgd is denied that ROM at every boot. The identical failure is in
the earlier `docs/evidence/product-supervision-sponge-desktop-disk-nvme.log`,
so it predates this work. A control run confirmed it: with
`run/sponge-desktop-disk-uefi-usb.run` reverted to `HEAD`, the scenario
passed, and with the driver wiring applied it passed too — the two were
never conflated. The driver's wiring in the nvme variant itself is
verified structurally (builds, stages, expands) but its PASS gate cannot
be earned until the bake staging is added there. Recorded in
`docs/13-installation.md` §6.

### Quota receipts

`driver_runtime | caps: 9000 | ram: 320M` — the exact sum of the five
driver quotas (7700 caps / 320M) plus 16.9% caps headroom. RAM is the
exact sum deliberately: a RAM shortfall is a visible `init` warning plus
a **refused install** (`insufficient-resources`), while a caps shortfall
*hangs* on seL4 (`docs/09-roadmap.md` §11.1). `driver_runtime` is placed
**before** `pkg_runtime` in the config and both dimensions are explicit:
`pkg_runtime` carries `ram: 512G` as the deliberate remainder clamp, and
that is only "everything left" while nothing is created after it.

`system`'s own caps grant rises by the same 9000 via
`driver_runtime::caps_for_system`, which each media variant feeds with
its own pre-driver figure (263000 on the BIOS/NVMe media, 280000 on the
UEFI media). Getting this wrong is silent in one direction and loud in
the other: shrinking a media variant's grant to the BIOS figure produced
exactly the §4.1 caps failure.

## 6. Honest gaps

- **Four of five drivers are build-verified only.** QEMU emulates no USB
  Ethernet adapter, no Wi-Fi radio and no Intel GPU. The per-driver state
  table, the Wi-Fi firmware acquisition procedure and the VGA
  policy-collision write-up are in `docs/15-hardware-compatibility.md`
  §8.
- **The declared session sets of the four unverified drivers are derived
  from reading their sources, not from a run.** They are declared
  explicitly so the contract is reviewable, and the packages are
  deliberately not autostarted so an incorrect guess cannot break a
  boot. `pc_nic`'s set is the only one measured.
- **Two driver packages cannot start on the shipped media without an
  operator step**: `usb_net` needs a `Terminal` provider routed into
  `driver_runtime`; `intel_gpu` / `pc_intel_fb` need the Tier-0 PCI `VGA`
  policy re-pointed away from `vesa_fb`. Both steps are Tier-0 boot
  wiring, deliberately left to the user (AGENTS.md §1.1).
- **`vct status` does not yet render driver state specially.** The
  generic flow needed no `vct` change at all: `vct install pc_nic`,
  `vct list` (the broadcast now carries `kind="driver"`), `vct launch`
  and `vct remove` all work through the existing channels. Rendering
  the driver's own health in `vct status` is a presentation change on
  top of data that already exists (`driver_runtime_state`).
- **`run/driver_runtime.inc` is wired into the five desktop-disk product
  media only.** The ISO media (`sponge-alpha.run`) and the standalone
  component scenarios keep their previous composition: they have no
  `driver_runtime` child, and `sponge_pkgd` simply publishes a report
  nothing reads. Driver packages are therefore not installable on the
  ISO media — an honest limitation of the media, not of the flow.
- **No vendored patch.** Zero rows added to `docs/11-environment.md` §4.