> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §11. Building Distribution Media (Alpha)

## 11. Building Distribution Media (Alpha)

This section produces the two installable Alpha media artifacts in
`var/dist/` per the Phase 8 boot/storage architecture
([`docs/14`](../14-boot-storage-architecture.md) §8):

```
sponge-os-0.2.0-x86_64-sel4.img   (UEFI USB-stick product image: ESP/GENODE/SPONGE-DATA)
sponge-os-0.2.0-x86_64-sel4-legacy.iso   (legacy BIOS El Torito ISO: live/eval mode)
```

The `.img` is the **real product**: a Tier-0 `image.elf` (≤ 80 MiB)
that mounts the GENODE ext2 partition and serves the full Qt6 desktop
from `/system` via `cached_fs_rom` (P2, `run/sponge-desktop-disk.run`)
PLUS a fourth GPT partition `SPONGE-DATA` (added by `tool/mkdata`,
docs/14 §4.3) that backs `sponge_pkgd`'s installed-set store so
installs survive reboot (P3, `run/sponge-persist-disk.run`). The `.iso`
is the **live/eval media**: the Phase 7 boot-modules composition
(`run/sponge-alpha.run`) on an El Torito image; Tier 2 is a RAM
filesystem, so nothing persists.

Each artifact ships with a `<name>.sha256` sidecar in the
`<hash>  <filename>` format that `sha256sum -c` consumes directly.
Both media boot (in QEMU) to the same `alpha-probe: PASS` marker — the
media are already boot-proven by the run scenario's `run_genode_until`
call inside the make invocation; nothing re-verifies them post-copy.

### 11.1 One-command flow (default)

```bash
./tool/dist
```

The wrapper ([`tool/dist`](../../tool/dist) → [`tool/dist.mojo`](../../tool/dist.mojo)):

1. Pre-flight checks every host tool the Genode run framework's image
   plugins AND `tool/mkdata` invoke (`xorriso`, `sgdisk`, `mcopy`,
   `e2cp`, `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, `resize2fs`,
   `truncate`) and prints the exact `apt install` line for any missing
   one, exiting non-zero **before** any build runs (loud-and-early
   failure).
2. Builds the **product `.img`** from `run/sponge-desktop-disk.run`
   with `RUN_OPT='--include image/disk'`.
3. Runs `tool/mkdata` on the produced `.img` to add the SPONGE-DATA P4
   (docs/14 §4.3 — `truncate` + `sgdisk` delete/move/new/hybrid +
   `mkfs.ext2 -E offset`). This is the partition that backs pkgd's
   installed-set store. Idempotent (a re-run on an image that already
   has P4 is a verified no-op).
4. Verifies the partition table (the `misleading_success_output`
   defense — never trust the build exit code alone): `sgdisk -p` must
   show ≥ 4 partitions with P4 named SPONGE-DATA.
5. Builds the **live/eval `.iso`** from `run/sponge-alpha.run` with
   `RUN_OPT='--include image/iso'` — the Phase 7 boot-modules
   composition.
6. Copies the artifacts to `var/dist/`, writes the `.sha256` sidecars,
   and prints a summary table.

`./tool/dist` never boot-verifies the media itself (the run framework
already does that during the make), and never touches anything outside
the repository (AGENTS.md §3.5).

**Control doors (AGENTS.md §1.1 — automation is the default; a door is
always open):**

- `./tool/dist --no-data` — produce the `.img` WITHOUT the SPONGE-DATA
  P4 (3-partition image; installs will NOT persist on this media).
  Useful when iterating on the disk-served desktop alone.
- `./tool/dist --data-size 256` — produce a 256 MiB SPONGE-DATA P4
  instead of the default 1024 MiB.
- `./tool/mkdata <img>` — run only the P4 grow/repartition step on an
  existing 3-partition `.img`. Idempotent. See `tool/README.md` and
  docs/14 §4.3 for the full contract.

### 11.2 Manual flow (the canonical reference)

Per AGENTS.md §3.5 every automated step has a documented manual
equivalent. The procedure below reproduces `./tool/dist` step by
step. Run it from the repository root, after `./tool/build prepare`
and `./tool/build ports` have set up `genode/build/x86_64/`.

```bash
# 0. Install the seven media host-tool packages once (§1.3 covers the
#    base build packages; this is the extra media set, documented in
#    docs/11-environment.md §7.3). truncate is required by tool/mkdata
#    (the docs/14 §4.3 P4 grow step).
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs coreutils
# (Arch: substitute `pacman -S` for `apt install`.)

# 1. Sanity-check that every host tool the image plugins + mkdata
#    invoke is actually on PATH.
for tool in xorriso sgdisk mcopy e2cp e2mkdir mkfs.ext2 mkfs.vfat \
            resize2fs truncate; do
    command -v "$tool" >/dev/null || echo "MISSING: $tool"
done

# 2. Build the product .img — the disk-served desktop scenario.
#    sponge-desktop-disk self-loads boot_dir/sel4, power_on/qemu, and
#    log/qemu via ensure_plugin_loaded, so the bare RUN_OPT override
#    is sufficient.
make -C genode/build/x86_64 run/sponge-desktop-disk \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/disk'

# 3. Grow the SPONGE-DATA P4 onto the produced .img (docs/14 §4.3).
#    tool/mkdata is idempotent: a re-run on an image that already has
#    P4=SPONGE-DATA is a verified no-op.
./tool/mkdata genode/build/x86_64/var/run/sponge-desktop-disk.img

# 4. Verify the partition table shows 4 partitions (misleading_success_
#    output defense — the build exit code alone is not enough).
sgdisk -p genode/build/x86_64/var/run/sponge-desktop-disk.img

# 5. Copy + sha256 the disk artifact.
mkdir -p var/dist
cp genode/build/x86_64/var/run/sponge-desktop-disk.img \
   var/dist/sponge-os-0.2.0-x86_64-sel4.img
(cd var/dist && sha256sum sponge-os-0.2.0-x86_64-sel4.img \
    > sponge-os-0.2.0-x86_64-sel4.img.sha256)

# 6. Clean the previous mode's staged boot directory so the ISO build
#    does not inherit stale boot modules from the disk build
#    (stale_state guard; `./tool/dist` does the same — note the two
#    scenarios differ, so this clean is per-scenario and does not
#    touch the disk build's run_dir).
rm -rf genode/build/x86_64/var/run/sponge-alpha*

# 7. Build the live/eval .iso — the alpha boot-modules composition.
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/iso'

# 8. Copy + sha256 the ISO artifact.
cp genode/build/x86_64/var/run/sponge-alpha.iso \
   var/dist/sponge-os-0.2.0-x86_64-sel4-legacy.iso
(cd var/dist && sha256sum sponge-os-0.2.0-x86_64-sel4-legacy.iso \
    > sponge-os-0.2.0-x86_64-sel4-legacy.iso.sha256)

# 9. Verify both sidecars.
(cd var/dist && sha256sum -c *.sha256)

# 10. (Optional) Print a summary of what was produced.
ls -lh var/dist/sponge-os-0.2.0-x86_64-sel4.img / sponge-os-0.2.0-x86_64-sel4-legacy.iso
```

The release name embeds the version (`0.2.0`, kept in sync
with `include/sponge/version.h`), the architecture (`x86_64`), and
the kernel (`sel4`). Bump the version in
`include/sponge/version.h` first (`tool/version_bump.mojo`) and the
release name changes everywhere it is referenced.

### 11.3 Why two media formats

Per docs/14 §8: the disk image (`.img`) is the real product. Four
partitions (BIOS-boot + ESP + GENODE + SPONGE-DATA), full persistence —
this is what `tool/dist` optimizes and what "install to USB" means. The
ISO (`.iso`) is the live/eval mode: Tier 2 is a plain RAM filesystem
(`<ram/>` in the Tier-2 vfs), so everything works but nothing persists.
This matches every live-OS convention and keeps the ISO useful for
evaluation without promising persistence a read-only optical medium
cannot deliver. The Genode run framework produces the two via two
different image plugins (`genode/tool/run/image/disk` vs.
`genode/tool/run/image/iso`); `tool/mkdata` then grows SPONGE-DATA onto
the disk image's `--include image/disk` output (the image/disk tool
packs P1+P2+P3 with no free sectors, so P4 cannot be added with a bare
`sgdisk --new` — see docs/14 §4.3 for the grow/repartition sequence).

### 11.4 Building only one of the two media

The two scenarios are independent. To build only one:

```bash
# Only the product .img (no P4):
make -C genode/build/x86_64 run/sponge-desktop-disk \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/disk'
./tool/mkdata genode/build/x86_64/var/run/sponge-desktop-disk.img   # add P4

# Only the live/eval .iso:
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/iso'
```

The legacy boot-modules `.img` (Phase 7's `sponge-alpha` image/disk
output) is reachable by running the alpha scenario with `image/disk`:
it is the developer regression shape, not the product. It lacks the
SPONGE-DATA P4 (no persistence) and packs the whole desktop into boot
modules (subject to the ~256 MiB ceiling, docs/14 §2).
