# Runtime component topology: quota ledger

> **Breadcrumb:** parent [17 - Runtime Component Topology](../17-runtime-component-topology.md); this file holds its §6 (Quota ledger, including §6.0 and §6.1), moved verbatim.
> Siblings in `docs/topology/`: [tree-and-chains.md](tree-and-chains.md) (§1, §2, §2a), [incident-analyses.md](incident-analyses.md) (§3, §4, §4a), [design-gap-register.md](design-gap-register.md) (§5 part 1), [design-gap-register-2.md](design-gap-register-2.md) (§5 part 2).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 6. Quota ledger (the five desktop-disk scenarios)

Each class runtime hosts its enabled packages **simultaneously**, so
its grant must cover the SUM of its class's package quotas declared in
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

Phase 16 P2.4 splits `pkg_runtime` into three class runtimes
(`pkg_runtime_simple`, `pkg_runtime_terminal`, `pkg_runtime_browser`,
docs/12 §4.5 `<runtime_class>`), each hosting exactly the packages of
its class **simultaneously**. The class rows below are the
machine-checked source of truth (the RAW package-quota sums — they do
**NOT** include admission margins):

| runtime class | caps | RAM | owning include |
|---|---:|---:|---|
| simple (hello, textedit, files, calculator, pdf_view, debug) | 4512 | 1288M | `run/runtime-simple.inc` |
| terminal | 1100 | 224M | `run/runtime-terminal.inc` |
| browser (falkon) | 200000 | 1024M | `run/runtime-browser.inc` |

The SHIPPED grants are the rows plus the measured headroom
`run/runtime-quota.inc` derives — `(row + closure) x 1.10`, rounded
up (caps to 1000, RAM to 1 MiB), the editor closure (`nano` 600 +
`ncurses` 300 / 64M, installable into the simple class) charged to
simple:

| runtime node | caps | RAM | note |
|---|---:|---:|---|
| `pkg_runtime_simple` | 6000 | 1488M | explicit |
| `pkg_runtime_terminal` | 2000 | 247M | explicit |
| `pkg_runtime_browser` | 220000 | **512G** | RAM = the remainder clamp (§6.1), LAST `+ start` node |

The raw rows cannot be the grants: `sponge_pkgd`'s admission gate
admits a package only against `need + margin` (8 MiB / 64 caps,
`docs/evidence/pkg-admission-control.md`) of the class's REPORTED
availability, which is the grant minus the nested init's own live
usage (~70 caps / ~1.1 MiB measured at idle). A single-package class
granted exactly its row can never admit its own package — the
terminal launch would be refused before the package ever starts. The
derivation, the margin arithmetic and the fail-loud drift check live
in `run/runtime-quota.inc`; each scenario writes the literals and
calls `runtime_quota::verify 6000 1488M 2000 247M 220000 512G`.
The class grants are uniform across all five desktop-disk scenarios
(media-independent), while `system`'s grant is per media (280000 caps
on the uefi/usb variants, 263000 on the plain/ahci and nvme ones).
The app/driver capability split is preserved: `Platform`, `Dma`,
`Irq`, `Io_mem`, and `Io_port` remain driver-runtime-only. The class-row
receipt is `docs/evidence/phase16-runtime-targets.md` (P2.2, single
`pkg_runtime`); the grouped-grant evidence is the static
`runtime_grouped::assert_wiring` harness plus the runtime boots. The
grouped topology is runtime-verified on
`sponge-desktop-disk-uefi-usb` (two boots, `alpha-probe: PASS` and
the terminal launch gate) and acceptance-verified on
`sponge-desktop-disk` (P5.1's three consecutive all-green suite
passes, including the grouped app matrix with real launcher first
paint + input per-app witnesses — hello is GUI-free autostart
(paint/input N/A by design) and the calculator relaunch-era
typed-delta witness is context-blocked);
`sponge-desktop-disk-uefi`/`-uefi-nvme` remain structurally asserted
(D15.16 structure-only QEMU gates — runs that do not exercise the
desktop gate, not a QEMU boot block; QEMU/OVMF boots reach Genode).
The P2.4 interactive receipt is
`docs/evidence/phase16-grouped-runtime-ui.md`: two consecutive BIOS
`SPONGE_GROUPED_UI=on` runs with real launcher first paint and typed
input for both textedit and terminal, plus a default-BIOS
`alpha-probe: PASS` regression run (the same document records the
staging, placement, and host-QMP marker-preservation fixes required
to make that evidence reliable). Legacy single-runtime
admission was verified on `sponge-desktop-disk` (`resource admission
armed` and `alpha-probe: PASS` in the same boot). On
`sponge-desktop-disk-nvme` the gate is wired but NOT yet reachable:
pkgd stops earlier on a pre-existing `bake_manifest` denial (§5 row
below). Receipts: `run/sponge-pkg-admission.run`,
`docs/evidence/pkg-admission-control.md`,
`docs/evidence/phase16-runtime-acceptance.md`.

### 6.0 `driver_runtime`'s grant

`driver_runtime` is sized from the driver set, not from a bake profile
(no driver is in a profile — they are opt-in), and both dimensions are
explicit:

| package | caps | RAM | autostart? |
|---|---:|---:|---|
| pc_nic | 1000 | 32M | yes (QEMU-verified end to end) |
| usb_net | 1500 | 64M | no |
| pc_wifi | 2500 | 128M | no |
| intel_gpu | 1200 | 32M | no |
| pc_intel_fb | 1500 | 64M | no |
| fb_vesa | 1000 | 32M | yes (BIOS media, QEMU-verified) |
| fb_boot | 24000 | 96M | yes (UEFI media, QEMU-verified) |
| **sum** | **31700** | **448M** | |

`driver_runtime | caps: 36000 | ram: 448M` — **+13.6% caps** headroom
(docs/17 §5 practice, rounded up), RAM at the exact sum.

Only ONE fb driver ever runs (docs/12 §4.6 exclusive group), so the
sum over both is deliberately conservative: a grant sized to whichever
one the media happens to seed would have to be re-derived per firmware
variant, and the arithmetic would live in five scenarios instead of one
place. The two fb packages dominate the caps figure for the same
reason — `boot_fb` maps the whole GOP framebuffer and allocates a
full-size Capture buffer, and 6000 caps was still short at the OVMF
resolution.

`system`'s own
caps grant rises by the same 36000 through
`driver_runtime::caps_for_system`, which each media variant feeds with
its own pre-driver figure (263000 on the BIOS/NVMe media, 280000 on
the UEFI media) so no variant's budget is silently shrunk.

RAM is the exact sum rather than headroom because RAM has no hang
failure mode: a shortfall is a visible `init` warning and a **refused
install** (`insufficient-resources` from the admission pre-flight),
which is a far better failure than a hang. Caps would be the opposite.

Before this change the four ahci/nvme desktop-disk variants granted
`pkg_runtime` 8000 caps / 1536M — falkon (200000 caps) was **25x
short** and could never launch, and the RAM sum overshot the grant by
~1 GiB. The uefi-usb variant's 210000 caps / 2048M was closer but
still ~488M under on RAM and under on caps once pdf_view and terminal
are counted. RAM then went 2816M → the clamp; caps never moved off an
explicit number.

### 6.1 The remainder clamp (RAM only)

`system` and `pkg_runtime_browser` declare `ram: 512G` (the two
explicit class runtimes carry fixed RAM instead). That is **not** a claim
that the system needs 512 GiB — it is the mechanism. Genode's init
silently clamps a child's RAM quantum to the parent's remaining
availability at the moment the child is created:

```c++
/* genode/repos/os/src/lib/sandbox/child.cc:481-486 */
Ram_quota const avail_ram = Genode::Child::effective_quota(quota_limit.ram);

if (ram_quota.value > avail_ram.value) {
    warning(name(), ": configured RAM exceeds available RAM, proceed with ", avail_ram);
    ram_quota = avail_ram;
}
```

`quota_limit` is the sandbox PD's live `stats.ram.avail()` minus the
sandbox's `Preservation` reserve (`library.cc:96-108, 123`) — what is
genuinely still free at that instant. The caps path
(`child.cc:490-495`) is the identical clamp. Upstream behaviour, used
as-is; **no vendored patch**.

Two conditions make it sound:

1. **The clamped child must be the LAST `+ start` node in its config.**
   The remainder is only "everything left" if nothing is created
   afterwards. The runtime-last invariant (legacy `pkg_runtime`, now
   `pkg_runtime_browser`) came from `248afa12d6` (real-hardware
   config-delivery timing); `system`-last is already true. A per-file
   scan confirms both in all five scenarios — no reordering was
   needed.
2. **Caps stay explicit.** Caps exhaustion on seL4 *hangs* (a child
   that cannot get a capability waits forever — §11.1 of
   docs/09-roadmap.md), which is the worst failure mode a resource can
   have; and a cap grant that absorbed the whole remainder would starve
   any future sibling added to the config. RAM has no such failure
   mode, which is exactly why only RAM uses the clamp.

Measured on `run/sponge-desktop-disk`, same unchanged file, two boots
(historical receipt — measured on the pre-P2.4 single-grant topology;
the clamp mechanism and the arithmetic below are unchanged, but the
`pkg_runtime` row is now the `pkg_runtime_browser` one):

| | `-m 6G` | `-m 4G` |
|---|---:|---:|
| init pool | 5686 MiB | 3638 MiB |
| `system` grant | 4807.1 MiB | 2759.1 MiB |
| `pkg_runtime` grant | **4521.2 MiB** | **2473.2 MiB** |
| verdict | `alpha-probe: PASS` | `alpha-probe: PASS` |

The arithmetic closes: `system − pkg_runtime` = 285.9 MiB at *both*
pool sizes = the 284 MiB of fixed system children plus ~1.9 MiB of the
nested init's own allocation. Tier-0 for this scenario totals 874 MiB,
and 5686 − 874 ≈ 4807, the observed `system` grant. Full receipts:
`docs/evidence/remainder-clamp-ram.md`.

| scenario | `system` children (caps / fixed RAM) | `system` caps grant | class grants (simple / terminal / browser caps) | QEMU `-m` (ceiling) |
|---|---|---:|---:|---:|
| sponge-desktop-disk | 8650 / 284M | 263000 | 6000 / 2000 / 220000 | 6G |
| sponge-desktop-disk-nvme | 8650 / 284M | 263000 | 6000 / 2000 / 220000 | 6G |
| sponge-desktop-disk-uefi | 24100 / 284M | 280000 | 6000 / 2000 / 220000 | 8G |
| sponge-desktop-disk-uefi-nvme | 24100 / 284M | 280000 | 6000 / 2000 / 220000 | 8G |
| sponge-desktop-disk-uefi-usb | 24300 / 292M | 280000 | 6000 / 2000 / 220000 | 8G |

(The `system` children columns exclude the class runtimes themselves,
whose grants are media-independent — 6000 / 2000 / 220000 caps and
1488M / 247M / the RAM remainder, §6 — and likewise exclude
`driver_runtime`, whose grant is media-independent at 36000 caps /
448M (§6.0). The uefi variants' larger fixed
sums come from the 6000-cap `decorator`, 8000-cap `sponge-de` and
6000-cap `alpha_probe`, plus uefi-usb's `vct_tty` at 200 caps / 8M.
The three class runtimes replace the old single `pkg_runtime` at
230000 caps everywhere.)

The `-m` column is now a **ceiling only** — it bounds the boot and
nothing has to agree with it, because the two RAM grants follow the
pool. The values are unchanged on purpose: the existing receipts were
earned at them, and changing them would invalidate that evidence for
no functional gain. The two boot paths still reserve very different
amounts of guest RAM, which is why the ceilings differ:

- **Direct-kernel (BIOS) path** — core reserves ~458 MiB for kernel +
  rootserver. Measured: `-m 6G` ⇒ "5686 MiB RAM and 523288 caps
  assigned to init"; `-m 4G` ⇒ 3638 MiB.
- **OVMF/UEFI path** — ~2.4 GiB, not ~458 MiB. Measured on
  `sponge-desktop-disk-uefi-usb`: `-m 8G` ⇒ 5826 MiB assigned to init.
  The UEFI ceiling stays at 8G: uefi-usb alone carries ~1942 MiB of
  Tier-0 (the three-area layout (§2a) adds `vfs_pkg` 32M, `vfs_log` 32M,
  `vfs_home` 32M, `part_block_log` 8M and `rom_pkg` 768M for falkon's
  509 MiB payload on top of the two 256M `rom_*` servers — the pre-P3.2
  shared `vfs_var` was one 32M instance on one `part_block`, ~1902 MiB),
  so after the OVMF reservation a 6G guest would leave the fixed system
  services with almost nothing.

**Residual gap**: `run/sponge-alpha.run` still grants `pkg_runtime`
400 caps / 32M — sized for the hello-only Phase-7 topology, not for
the `desktop` profile it stages by default. Its 2G QEMU envelope
would have to grow in lockstep and its PASS evidence would have to be
re-earned, so the change is deliberately separate. The clamp is also
**not** applied there: alpha is a flat topology whose `pkg_runtime` is
a direct child of the *top-level* init, where "last created" would
mean nothing useful (the drivers sub-init and the desktop children are
siblings created in arbitrary order).
