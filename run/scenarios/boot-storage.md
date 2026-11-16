# Boot, storage, and media images

Breadcrumb: [`run/README.md`](../README.md) → Boot, storage, and media images

Tier-0 storage chain, disk/ISO boot variants, and install-media
persistence. Full catalog entries follow verbatim (cautions and
limitations included); the parent index carries one-line summaries.

## Scenarios
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
