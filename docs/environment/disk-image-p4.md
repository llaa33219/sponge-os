# 11 - Development Environment: Disk Image P4 and References (§12-§13)

> **Breadcrumb:** parent [11 - Development Environment & Reproducibility](../11-environment.md); this file holds its §12 (Disk image P4, SPONGE-DATA creation) and §13 (References), moved verbatim.
> Siblings in `docs/environment/`: [third-party-ports.md](third-party-ports.md) (§5-§6), [bootstrap.md](bootstrap.md) (§7), [env-vars.md](env-vars.md) (§8), [base-sel4-gui.md](base-sel4-gui.md) (§10).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 12. Disk image P4 (SPONGE-DATA) creation

The Sponge OS install media carries a fourth GPT partition,
**SPONGE-DATA**, that backs the writable user-area stores (the pkgd
installed-set at `/store/installed.xml`, the future configd store, and
user files under `/home`) — `docs/14-boot-storage-architecture.md` §4.3,
§6. The Genode run framework's `image/disk` plugin
(`genode/tool/run/image/disk`) produces only P1 (BIOS boot) + P2 (ESP)
+ P3 (GENODE ext2): it packs the image as `header + P3 + backup-GPT-gap`
with **no free sectors** (it `resize2fs -M`-shrinks P3 and
`--move-second-header`s the backup GPT to the image end). P4 therefore
cannot be added by a bare `sgdisk --new=4`; the image must be grown and
repartitioned post-build.

### 12.1 tool/mkdata

`tool/mkdata` (`tool/mkdata.mojo`, AGENTS.md §3.5 Mojo host-side helper)
runs the `docs/14` §4.3 sequence on an already-produced `.img`:

```
truncate -s +<data_bytes> <img>
sgdisk --delete=3 <img>                  ;# drop P3 entry (bytes stay)
sgdisk --move-second-header <img>        ;# backup GPT -> new end
sgdisk --new=3:<old_first>:<old_last> <img>
sgdisk --change-name=3:GENODE <img>
sgdisk --new=4:<p4_first>:<p4_last> <img>
sgdisk --change-name=4:SPONGE-DATA <img>
mkfs.ext2 -E offset=<p4_byte_off> -L SPONGE-DATA -F <img> <p4_kib>K
sgdisk --hybrid <img>
```

Usage:

```
./tool/mkdata <img>                  # add a 1024 MiB SPONGE-DATA P4
./tool/mkdata <img> --data-size 256  # 256 MiB P4
```

The tool is **idempotent**: a second run on an image that already has a
P4 named `SPONGE-DATA` is a verified no-op (it detects P4 up front and
exits 0 without re-running the destructive sequence). If P4 exists under
a *different* name the tool errors loudly (exit 2) rather than
overwriting — fail-loud, never silent corruption (AGENTS.md §1.4).

Host tools required (subset of §7.3): `sgdisk` (gptfdisk),
`mkfs.ext2` (e2fsprogs), `truncate` (coreutils) — the same tools
`image/disk` already uses. The run scenario
`run/sponge-persist-disk.run` invokes `tool/mkdata` between
`build_boot_image` and `run_genode_until`.

### 12.2 `mkfs.ext2 -E offset=<bytes>` — formatting a partition inside an image

The key technique is `mkfs.ext2`'s `-E offset=<bytes>` extended option.
It makes `mkfs.ext2` `lseek()` to `<bytes>` inside the image file and
write the ext2 superblock + structures there, **without** touching the
GPT or any other partition. The trailing `<size>K` argument limits the
filesystem to the partition extent so mkfs does not run into the backup
GPT. `-F` forces creation on a regular file (mkfs.ext2 otherwise
expects a block device).

Two subtleties this tool handles:

1. **sgdisk auto-alignment.** `sgdisk --new` aligns partition starts to
   2048-sector (1 MiB) boundaries by default, so the partition may land
   a few sectors past the requested `p4_first`. `tool/mkdata` re-queries
   P4's actual first/last sectors *after* `--new=4` and computes the
   mkfs offset from the **actual** sectors — otherwise the ext2
   superblock would be written at the pre-alignment offset and the
   partition would not match its filesystem (silent corruption). The
   tool prints the alignment adjustment when it occurs.

2. **Re-applying the P3 name.** `sgdisk --delete=3` removes the P3
   entry entirely (the ext2 *bytes* stay on disk, but the GPT name is
   gone). The sequence re-applies `--change-name=3:GENODE` after
   `--new=3` so P3 keeps its label. `docs/14` §4.3 omits this step in
   its sketch; the tool adds it because the acceptance gate checks P3's
   name (`sgdisk -l`).

### 12.3 Hybrid MBR 3-entry limit

`sgdisk --hybrid` (the last step, also run by `image/disk`) builds the
hybrid MBR from the **first three** GPT partitions (P1-P3). P4
(SPONGE-DATA) therefore exists in the **GPT only** — it is visible to
`sgdisk -l`, to GRUB (which boots from P1/P2), and to any Linux host,
but **not** listed by MBR-only tools (e.g. legacy `fdt`/`fdisk -l`
modes, very old BIOSes). This is acceptable for Sponge OS: the boot
chain never reads the MBR partition entries (GRUB loads from the BIOS
boot + ESP partitions), and the Control-philosophy inspection tool is
`sgdisk -l` (ext2 is universally readable on the host). If a future
deployment needs P4 in the MBR (e.g. a legacy toolchain), the fix is a
small vendored patch to `image/disk` adding `--hybrid 1:2:3:4` semantics
— but sgdisk's hybrid MBR spec caps at 3 entries, so the real fix would
be dropping hybrid MBR entirely (pure-GPT boot), recorded in the §4
patch ledger if taken.

---

## 13. References

- Genode OS Framework: <https://genode.org>
- Genode documentation: <https://genode.org/documentation>
- Genode 26.05 source (canonical): <https://codeberg.org/genodelabs/genode>
- Genode toolchain download: <https://genode.org/download/tool-chain>
- Genode run framework:
  <https://genode.org/documentation/developer-resources/run>
- This document is referenced by:
  - `AGENTS.md` §5.2 (vendoring rules)
  - `docs/08-development.md` (the development flow, which points here
    for the environment contract)
  - `docs/09-roadmap.md` Phase 1 lessons (which now point at the
    vendored tree)
