# Phase 15 — Real-Hardware Boot (Work Plan)

> Status: awaiting execution. Created 2026-08-17.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 15* (lines 708–725).
> Pre-planning consultation: Metis (2026-08-17); user decisions D15.1–D15.3
> recorded in this document. Prior phase plan: `docs/plans/phase14-daily-desktop.md`.
> Completing Phase 15 releases **0.2.0-alpha**.

## Goal Restatement (docs/09-roadmap.md §10, Phase 15, verbatim)

Sponge OS boots successfully on at least one physical machine — the
QEMU-only limitation is retired. **Completing this phase releases
0.2.0-alpha.**

Four completion criteria (verbatim):

1. Boot verified on physical hardware from USB or SSD media (target
   machine recorded in the compatibility document).
2. Input, display, and storage functional on that machine; the desktop
   reaches the same verified state as the QEMU scenarios.
3. `docs/13-installation.md` updated: real-hardware install path
   documented, QEMU-only limitation removed or rescoped.
4. Known hardware-specific issues recorded with reproduction notes.

## Phase Decomposition (user-directed)

The user decomposed Phase 15 into three sequential sub-phases. This plan
is binding for 15-1 and frames 15-2/15-3 at milestone level; 15-2 gets
its own scenario-evidence contracts as W-tasks land.

| Sub-phase | Owner | Content | Exit gate |
|---|---|---|---|
| **15-1** | agent | Bake-customizable media: selectable package/setting profiles baked at image-build time; UEFI-bootable product media; first-boot apply semantics | `tool/dist` produces `minimal` + `desktop` BIOS **and** UEFI media; bake scenarios PASS in QEMU (BIOS and OVMF) |
| **15-2** | agent | QEMU hardware-diversity matrix exercising every real-hardware-relevant path on the baked media | Every matrix cell has scenario + evidence; `tool/hw_compat.mojo assert` green with the new real-hardware-row policy |
| **15-3** | **user** (agent prepares) | Physical boot on the LG gram 17ZD90N-VX7BK from USB | Roadmap criteria 1, 2, 4 closed; `docs/13` updated (criterion 3 closes across 15-1..15-3 doc passes) |

## Binding Decisions (do not re-litigate)

### User decisions (2026-08-17)

| # | Decision | Outcome |
|---|---|---|
| D15.1 | Target machine is the **LG gram 17 (2020), 17ZD90N-VX7BK** | All of 15-1/15-2/15-3 anchors to this one machine; other hardware stays in the gap ledger. Specs: see *Verified Ground Truth* §A. |
| D15.2 | **UEFI-bootable media is in scope for 15-1** | The target machine is UEFI-only (no CSM per LG support docs), so UEFI is a hard requirement, not an option. Implementation route per D15.13. |
| D15.3 | **Two bake profiles: `minimal` and `desktop`** | The profile mechanism is the point of the bake feature. `minimal` = smallest usable set; `desktop` = sensible everyday defaults (includes Falkon — D14.10's opt-in rationale, the boot-module ceiling, was closed in Phase 9; the override is recorded here explicitly). |

### Agent decisions (documented, challenge only with new evidence)

| # | Decision | Rationale |
|---|---|---|
| D15.4 | Bake profiles live at `pkg/bake/<name>.profile` (INI: `[packages]`, `[config]`, `[theme]` sections, `config_version=1`) | INI is trivially parseable in Mojo without new deps; committed to git so profiles are reviewable and reproducible. `tool/bake.mojo` validates and emits a JSON `bake_manifest.json` staged into the image for `vct` to read back. |
| D15.5 | Image size budget enforced by the bake stage: `minimal` ≤ 1 GiB, `desktop` ≤ 2 GiB total `.img` | Fits cheap USB sticks; gates the Falkon inclusion honestly. Overrun = non-zero exit before mkdata runs. |
| D15.6 | Factory-reset affordance: `vct bake reset` re-seeds P4 user config from P3 baked defaults | Three-philosophy mandate: automation (defaults), control (reset door), convenience (no re-flash). |
| D15.7 | Split: `tool/bake.mojo` (host-side, Mojo) + `vct bake` (in-system, C++) | AGENTS.md §3.5 host/target boundary. `vct bake` implements §3.3's full surface: `--json`, `--lang ko`, `--manual`, concise+detail help. |
| D15.8 | Primary bake mechanism is **staging-time**: `run/bake.inc` shared include, driven by `SPONGE_BAKE_PROFILE` (default `desktop`), consumed by every product-media run script before `build_boot_image`. Secondary: `tool/bake.mojo --img <file>` post-build P3 injector for fast `.img` re-bakes (runs before `tool/mkdata`, idempotent, sgdisk-verified) | Staging-time is the only mechanism that works uniformly for `.img`, `.iso`, and UEFI media (base-sel4 packs boot modules at `build_boot_image`; `/system` content enters images from `[run_dir]`). The post-build injector is the fast-iteration door for disk images only, modeled on `tool/mkdata.mojo`'s idempotency and misleading-success-output defense. |
| D15.9 | First-boot apply semantics: baked defaults live read-only under `/system/bake/` on P3 (profile name, `bake_manifest.json`, default configd keys, default theme); `sponge_configd` seeds P4 user config from them only when the P4 sentinel `.bake-seed` is absent (empty/invalid = seed, present = respect user state) | P3 is read-only product state; P4 is mutable user state. The sentinel makes seeding idempotent across reboots and makes `vct bake reset` trivial (delete sentinel + re-seed). |
| D15.10 | Bake manifest carries `bake.config_version = 1`; bumps are breaking-only | Forward-evolution guard (T13). |
| D15.11 | `tool/hw_compat.mojo assert` policy change: `target: real-hardware` rows become admissible **only** with a `qemu-envelope: run/<scenario>.run` link; status may be at most `gap` until 15-3 evidence lands, when the single 17ZD90N row flips to `verified` | Lifting the Phase-12 gate is a *consequence* of 15-2, not a precondition (T7). The validator stays read-only. |
| D15.12 | `vct bake` subcommands: `list`, `show`, `reset`, `--profile`, plus `--json`/`--lang ko`/`--manual` | AGENTS.md §3.3 contract. |
| D15.13 | UEFI implementation route: **Sponge-side UEFI disk recipe**, not `genode/tool/run/image/uefi`. New `run/sponge-desktop-disk-uefi.run` handcrafts the GPT (P1 ESP FAT32 with `/efi/boot/bootx64.efi` from the prebuilt `grub2_64.efi`, P2 GENODE ext2 with `/boot/*` + `/system/*`, P3 SPONGE-DATA via `tool/mkdata`) using the proven `sponge-boot-i440fx`/`sponge-boot-multidisk` handcrafted-image pattern. Display driver swaps `vesa_fb` → `boot_fb` | Genode's `image/uefi` is unexercised in the entire vendored tree (zero run scripts use it), copies only top-level run_dir entries (non-recursive `mcopy` — the `/system` tree would silently not land), creates a single FAT partition (no ext2 GENODE/SPONGE-DATA layout from docs/14), injects `-net none` into QEMU args, and hardcodes `/usr/share/ovmf/OVMF.fd`. A Sponge-side recipe avoids all four traps and keeps the vendored tree untouched (AGENTS.md §5.2). `boot_fb` consumes the GOP framebuffer passed through GRUB2 `gfxterm` → multiboot2 FB tag → seL4 bootinfo → `platform_info/boot/framebuffer` — the full chain exists in-tree today. |
| D15.14 | Secure Boot is handled by **user-disabled firmware setting** for 15-3, documented in the 15-3 protocol. Shim/MOK enrolment is not Phase 15 scope | The prebuilt `grub2_64.efi` is signed with Genode-Labs vendor keys absent from any OEM db; a Microsoft-db chain is not implementable in this tree. Disabling Secure Boot is a one-time documented firmware step, consistent with every Linux distro's bring-up notes for this machine. |
| D15.15 | 15-2 acceptance is **matrix-cell coverage**, not scenario count | Every non-gap cell of the updated `docs/15-hardware-compatibility.md` must map to at least one scenario + evidence (T14). |
| D15.16 | **UEFI strategy pivot (user, 2026-08-18):** 15-1 W2 proceeds on the BIOS path (fully independent of UEFI); the W4 UEFI product media is built **QEMU-unverified** (the OVMF core-init hang, see W1 OUTCOME, is recorded as an honest `gap` in the matrix with reproduction notes); **15-3 is pulled forward** as the real-hardware UEFI diagnostic on the 17ZD90N (2020 Insyde firmware, pre-dating the OVMF W^X/fragmentation era; Sculpt boots this vendored chain on real machines). If the target boots: Phase 15 criteria (real-hardware-based) close, the QEMU UEFI cell stays a documented gap. If it hangs identically: the hang is not OVMF-specific and the vendored-chain fix (ledger candidates: bender rebuild, g2fg grub213) becomes mandatory with real evidence | Avoids burning the phase on test-environment-only pathology; preserves honesty (no fabricated QEMU verification); the user's decomposition (15-1→15-2→15-3) is unchanged in content, only interleaved. |

## Scope Guards

- **No edits to the vendored `genode/` tree.** The UEFI recipe is
  Sponge-side (run script + `tool/`). A discovered patch need becomes at
  most one `docs/11 §4.2` candidate with evidence.
- **No `git subtree pull`.** Genode stays pinned at 26.05.
- **No shim/MOK/Secure-Boot signing work.** D15.14.
- **No new OS components beyond the bake/UEFI scope.** `i2c_hid`,
  Wi-Fi (AX201/CNVio2), USB-Ethernet, multi-namespace NVMe, and audio
  remain gaps, recorded honestly — they are not Phase 15 criteria.
- **No BIOS media regression.** BIOS/GRUB2 `.img`/`.iso` remain the
  default `tool/dist` output; UEFI is additive (`tool/dist --firmware
  {bios,uefi}` default `bios`... see W2 for the final flag shape; the
  escape-hatch principle requires the BIOS path to stay reachable
  forever, since it is today's verified path).
- **No scenario-count padding.** D15.15.
- **No host mutations outside the repo.** The one new host package
  (`ovmf`) is *requested from the user* per AGENTS.md §5.5; the agent
  never runs `sudo`.
- **No concurrent scenario builds** in `genode/build/x86_64`; serial
  `make -j1` for shared build steps (Phase 12 row-28 discipline).
- **Concurrent QEMU guard:** UEFI scenarios need OVMF; if `/usr/share/
  ovmf/OVMF_CODE.fd` is absent, the scenario fails its pre-flight
  loudly with the apt/dnf package name, never skips silently.

## Verified Ground Truth (consultation-validated — trust, do not re-derive)

### A. Target machine (D15.1) — LG gram 17ZD90N-VX7BK

Source: librarian research 2026-08-17 (Danawa, LG press/support, FCC,
Notebookcheck, Linux community bring-up notes). Confidence HIGH unless
marked.

| Subsystem | Fact | Bring-up consequence |
|---|---|---|
| CPU | Intel Core i7-1065G7 (Ice Lake, 4C/8T, VT-x/VT-d) | seL4 x86_64 reference-class target |
| GPU | Iris Plus G7 (Gen11, 64 EU) | **No native Genode driver for Gen11** → display via UEFI GOP framebuffer (`boot_fb`) |
| RAM | 8 GiB DDR4-3200 (+1 free SO-DIMM slot) | comfortable for the desktop media |
| SSD | 256 GB NVMe (PM981a or PM991; UNVERIFIED which), **2nd M.2 slot free** | Genode `nvme` driver path (QEMU-verified Phase 12); real-controller first contact in 15-3; single-namespace expectation |
| Wi-Fi | Intel AX201 (**CNVio2**, not AX200) | Genode wifi support unverified → **gap**; not a Phase 15 criterion |
| Ethernet | none (in-box dongle is 100 Mbit only) | networking on the target = Wi-Fi only → recorded gap |
| Display | 17" 2560×1600 IPS (LP170WQ1-SPA1), non-touch, eDP | GOP exposes native mode; `boot_fb` requires bpp=32/type=RGB — GOP default satisfies |
| Keyboard | internal PS/2 (i8042) | `ps2` driver path works (Phase 12 verified) |
| Trackpad | I2C-HID Precision (chip UNVERIFIED: Synaptics or ELAN) | **Genode has no `i2c_hid`** → trackpad expected dead in 15-3; protocol mandates a USB mouse; gap recorded |
| Firmware | Insyde H2O, **UEFI-only, no CSM** (LG dropped Legacy OS Boot on 2019+), Secure Boot default ON, F2 setup / F10 boot menu | UEFI media mandatory (D15.2); user disables Secure Boot once (D15.14) |
| Firmware quirk | Thunderbolt (iTBT) causes shutdown/reboot hangs on Linux <5.13; BIOS has `Integrated Thunderbolt Support` toggle | 15-3 protocol: disable iTBT before first boot; acpica shutdown already racy (Phase 14 W8 note) |
| USB | 3× USB-A (PCH), 1× USB-C/TB3 | USB stick boot via F10 menu; USB-A ports preferred |
| OS shipped | FreeDOS (Korean SKU ships without Windows) | cleanest possible bring-up target |

### B. UEFI boot path in the vendored tree (explore-validated 2026-08-17)

- **`genode/tool/run/image/uefi` is toolchain-only**: zero run scripts
  in the vendored tree or `run/` use it. The first UEFI scenario in the
  combined tree is ours. Its traps (all verified in source):
  `-net none` injection (`tool/run/power_on/qemu:125`), hardcoded
  `/usr/share/ovmf/OVMF.fd` (`:120`), non-recursive top-level-only
  `mcopy` (`image/uefi:35–37` vs `image/disk:79–90`), single FAT
  partition (no docs/14 4-partition layout), and the odd
  `--image-disk_shim` flag name (`image/uefi:12`).
- **Prebuilt GRUB2 EFI binaries ship in-tree**:
  `genode/contrib/grub2-*/boot/grub2/grub2_64.efi` (PE32+ x86-64) and
  `boot/font.pf2`; port `grub2.port` unpacks from `g2fg.git` — **no GRUB
  rebuild needed**. UEFI-relevant modules included: `multiboot2`,
  `efi_gop`, `gfxterm_background`, `png`, `fat`, `ext2` (GRUB can read
  the ext2 GENODE partition).
- **`vesa_fb` cannot work under UEFI**: it calls real-mode VBE via
  x86emu (`framebuffer.cc:238–249`); UEFI has no INT 10h; the seL4
  multiboot2 path hard-wires `vbeMode = -1`
  (`boot_sys.c:623`). The README's `preinit` attribute has no
  implementation.
- **`boot_fb` is the correct UEFI display driver and exists today**:
  `repos/os/src/driver/framebuffer/boot/main.cc` reads
  `platform_info/boot/framebuffer` (phys/width/height/bpp/pitch/type),
  requiring bpp=32 + type=RGB. Chain: OVMF GOP → GRUB2
  `insmod gfxterm` + `set gfxpayload=auto` (`grub2.inc:35–43`) →
  multiboot2 FB tag (type 8) → bender → seL4
  (`MULTIBOOT2_TAG_FB`, `boot_sys.c:683–687`) →
  `SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER` → base-sel4 core
  (`platform.cc:463–490`) → `boot_fb`. `drivers_interactive-pc` currently
  starts `vesa_fb`; the UEFI drivers config swaps in `boot_fb`.
- **`boot_dir/sel4` UEFI deltas are tiny**: copies
  `grub2_{32,64}.efi` → `efi/boot/boot{ia32,x64}.efi`, adds
  `serial_fallback` to bender args (`boot_dir/sel4:75–78`), and
  `grub2.inc` adds `gfxterm`/`png`/`background_image` lines
  (`grub2.inc:37–43`). The multiboot2 menuentry is identical to BIOS.
- **Handcrafted-image precedent**: `run/sponge-boot-i440fx.run` and
  `run/sponge-boot-multidisk.run` already synthesize `.img` files with
  `sgdisk + mkfs.ext2 + e2cp + dd` outside the Genode image plugins —
  the exact pattern D15.13 extends to the UEFI layout.
- **Handcrafted QEMU invocation precedent**: run scripts append their
  own `qemu_args`; an OVMF scenario passes
  `-drive if=pflash,format=raw,readonly=on,file=<OVMF_CODE.fd>` itself,
  sidestepping the `power_on/qemu` hardcoded-path trap.
- **Secure Boot**: `shim.inc` exists (Fedora-style `pesign`), but the
  prebuilt GRUB is Genode-keyed (`--disable-shim-lock`, SBAT from
  `genode.csv`); no OEM-db chain possible in-tree. D15.14 stands.

### C. Bake pipeline facts (explore-validated 2026-08-17)

- Product media today: `tool/dist.mojo` →
  `run/sponge-desktop-disk.run` (`image/disk`, stages
  `[run_dir]/system/{bin,lib,init,themes,pkg_*.xml}`) →
  `tool/mkdata.mojo` (P4 SPONGE-DATA, idempotent, sgdisk-verified) →
  `run/sponge-alpha.run` (`image/iso`, xorriso packs `[run_dir]`) →
  `var/dist/` + sha256.
- Package staging is hardcoded per scenario
  (`set staged_pkgs { hello }` → `bin/pkg_<name>.xml` + synthesized
  `bin/pkg_index.xml`). `run/bake.inc` replaces this with
  profile-driven staging; the `pkg/<name>/metadata.xml` format
  (`docs/12`) is unchanged.
- Env-knob precedent: `SPONGE_DISK_FAIL`, `SPONGE_BOOT_NVME`,
  `SPONGE_PERSIST_RO`, `SPONGE_FALKON_NO_FIXTURE`.
- base-sel4 packs **all boot modules into one `image.elf` at
  `build_boot_image` time** (docs/11 §10.4): anything staged after
  silently misses the image. All bake staging happens before
  `build_boot_image`; the post-build `tool/bake.mojo` injector only
  touches P3's `/system` tree (never boot modules) and must therefore
  refuse to add packages whose binaries are not already in the image's
  `/system/bin` (T1 defense).

## Risk Register (Metis traps T* + hardware/UEFI risks R15.*)

| # | Risk | Mitigation |
|---|---|---|
| R15.1 | **First UEFI scenario in the entire tree** — no upstream example to copy; GRUB2-EFI config details (prefix, `gfxpayload`, module set) are unproven end-to-end | W1 contains a Tier-0 `run/sponge-boot-uefi.run` smoke (boot → marker via serial/log) that gates all later UEFI work; debug on OVMF before any desktop content |
| R15.2 | `boot_fb` rejects the GOP mode (bpp≠32 or type≠RGB) on OVMF or the real panel | W1 smoke asserts `platform_info` framebuffer node contents and logs them; fallback documented (force GRUB `gfxpayload=1024x768x32`) |
| R15.3 | Bake "looks staged" but binaries missing from the image (T1) | Bake stage post-verifies every declared package's binary exists in `[run_dir]/system/bin` (staging-time) or P3 (post-build); missing artifact = non-zero exit |
| R15.4 | mkdata idempotency broken by bake ordering (T2) | `tool/bake.mojo` runs strictly before `tool/mkdata.mojo`; re-running `tool/dist` reproduces byte-identical images per profile (sha256 gate) |
| R15.5 | Falkon in `desktop` blows the size budget (T3) | D15.5 enforcement: staged-size computation before image assembly; 2 GiB hard gate |
| R15.6 | First-boot seed never fires → defaults invisible (T4) | W3 boot scenario asserts sentinel creation + seeded configd keys on P4; second-boot scenario asserts user changes survive (no re-seed) |
| R15.7 | hw_compat gate lifted too early (T7) | D15.11 sequencing: policy change lands in 15-2 W5, `verified` status only in 15-3 |
| R15.8 | Secure Boot left ON → USB refuses to boot with a firmware error screen | 15-3 protocol step 0: F2 → Security → Secure Boot = Disabled; the protocol includes the exact Insyde H2O menu path |
| R15.9 | iTBT shutdown/reboot hang on the target | 15-3 protocol: disable Integrated Thunderbolt Support in BIOS before first boot; recorded as a known machine-specific issue (criterion 4) |
| R15.10 | Trackpad dead (I2C-HID, no `i2c_hid` in Genode) | Expected and pre-registered as a gap; 15-3 protocol mandates a USB mouse; criterion 2 "input functional" = keyboard + USB HID mouse on this machine, honestly recorded |
| R15.11 | AX201 (CNVio2) unsupported by Genode wifi stack | Pre-registered gap; networking is not a Phase 15 criterion; no Ethernet exists on the machine |
| R15.12 | Real NVMe controller (PM981a/PM991) diverges from QEMU `nvme` | 15-2 keeps the NVMe QEMU envelope green; 15-3 records the exact controller via boot log; failure → gap with reproduction notes (criterion 4), not papered over |
| R15.13 | OVMF not installed on the build host | W0 requests the user install the `ovmf` package (§5.5 — agent never sudos); UEFI scenarios pre-flight the firmware path and fail loudly with the package name |
| R15.14 | `desktop` profile becomes the implicit default everywhere, silently regressing minimal-media users | `SPONGE_BAKE_PROFILE` default is explicit in `run/bake.inc`; `tool/dist` prints the active profile in its summary table; `--bake-profile` overrides |
| R15.15 | Phase 14 re-scoped follow-ups rot (T15) | W0 disposition pass: every `docs/evidence/phase14-index.md` §5 row assigned to 15-1/15-2/deferred with a recorded reason |
| R15.16 | **Vendored GRUB2 EFI page-faults under new OVMF** (discovered W1, 2026-08-17): `grub2_64.efi` (g2fg prebuilt, 2025-03) hits deterministic #PF (W:1 P:1) in its multiboot2 load path under host OVMF dated 2026-05-22 — RIP/CR2 ~0x1A4xxx past ImageBase, beyond the image file size (0xF6000), i.e. the faulting code/data is GRUB's runtime-allocated multiboot2 loader state, which new edk2 memory-protection maps read-only. Reproducible across 14+ variations. The vendored `image/uefi` path is therefore broken on this host's firmware | Options (no vendored edit): (a) pin a known-good older OVMF in `var/` (gitignored, SHA-recorded) for QEMU UEFI cells — the 15-3 target's 2020 Insyde firmware predates this protection class entirely; (b) rebuild GRUB via host `grub-mkimage` (needs user-installed package; may carry the same relocator code); (c) record a `docs/11 §4.2` patch-ledger candidate (rebuild g2fg at a newer commit). Decision pending librarian research; evidence: `docs/evidence/phase15-uefi-boot-smoke.log` |
| R15.17 | **Artifact-name collisions across rebuilds** (surfaced 15-3, 2026-08-19): `tool/dist` always writes `var/dist/sponge-os-<ver>-x86_64-sel4.{img,iso}`, so a USB stick burned from an earlier build is indistinguishable by name — the user burned a stale image during 15-3 bring-up and only the sha256 could discriminate | Follow-up (post-15-3): include profile/firmware/storage in the artifact name (e.g. `...-desktop-uefi-usb.img`) and print the full name in the dist summary; interim rule: always verify the stick with `head -c <size> /dev/sdX \| sha256sum` against the sidecar before booting |

## Hardware Matrix Contract (15-2 preview — the constraint 15-1 designs against)

Axes (each cell needs scenario + evidence, or an honest `gap` row):

| Axis | Values in scope | Mapping to target machine |
|---|---|---|
| Firmware | BIOS/GRUB2 (today), UEFI/OVMF (new) | target = UEFI |
| Machine | q35 (default), i440fx (smoke, existing) | q35 ≈ ICH-class; i440fx kept as legacy envelope |
| Storage | AHCI (existing), NVMe (existing), USB-stick attach (existing BIOS-side), NVMe+UEFI combo (new) | target = NVMe behind UEFI |
| Display | vesa_fb/BIOS (existing), boot_fb/UEFI-GOP (new) | target = boot_fb |
| Input | ps2 + usb-tablet (existing), usb-kbd HID (existing), usb-mouse HID (new cell) | target = PS/2 kbd + USB mouse |
| NIC | e1000 + slirp (existing), `-net none` honesty row (UEFI trap) | target = no functional NIC (gap) |

New scenarios the matrix requires (all base-sel4):
`sponge-boot-uefi.run` (Tier-0 UEFI smoke),
`sponge-desktop-disk-uefi.run` (product UEFI media, D15.13),
`sponge-usb-hid-mouse.run` (USB mouse HID envelope),
`sponge-bake-{minimal,desktop}.run` (profile boots + baked markers),
`sponge-bake-firstboot.run` (sentinel + seed + second-boot persistence),
`sponge-bake-reset.run` (`vct bake reset` path),
`sponge-desktop-disk-uefi-nvme.run` (the target-machine envelope).

## Task Graph

```text
Wave 0: W0 (baseline + dispositions + user installs ovmf) — no code
Wave 1: W1 (UEFI Tier-0 smoke) ───────────────┐
Wave 2: W2 (bake profiles + run/bake.inc + tool/bake.mojo + tool/dist wiring)
        W3 (first-boot semantics + vct bake) — parallel with W2, disjoint files
Wave 3: W4 (product UEFI media + drivers swap + desktop on OVMF)
Wave 4: W5 (15-2 matrix scenarios + hw_compat policy)  ← 15-2 proper
Wave 5: W6 (docs sync, dist integration, regression sweep, 15-3 protocol doc)
Wave 6: 15-3 (user physical boot; agent analyzes captured evidence)
Critical path: W0 → W1 → W4 → W5 → W6 → 15-3
```

Scenario runs serialize; code edits in W2/W3 may proceed in parallel
(disjoint files). All boots use bounded `run_genode_until` gates;
full seL4 desktop gates state their 600s+ reality.

## Tasks (15-1 detail; 15-2/15-3 milestone level)

### W0: Baseline, dispositions, prerequisites

- Capture the current `tool/dist` artifacts' sha256 + `sponge-alpha` /
  `sponge-desktop-disk` load-bearing markers (regression contract).
- Disposition pass over `docs/evidence/phase14-index.md` §5 (R15.15).
- **Ask the user to install `ovmf`** (`sudo apt install ovmf`) —
  agent never sudos (AGENTS.md §5.5). Verify
  `/usr/share/ovmf/OVMF_CODE.fd` (or distro equivalent) exists.
- Draft `pkg/bake/minimal.profile` and `pkg/bake/desktop.profile`
  contents; record the exact default package lists in this plan's
  commit that introduces them.

### W1: UEFI Tier-0 smoke (`run/sponge-boot-uefi.run`)

- Handcrafted GPT: P1 ESP (FAT32; `/efi/boot/bootx64.efi` +
  `/boot/grub/grub.cfg`), P2 ext2 with `/boot/{bender,sel4,image.elf}`
  + marker; GRUB2 `multiboot2` menuentry modeled on
  `boot_dir/sel4:85–90` with `serial_fallback`.
- QEMU: `-drive if=pflash,...OVMF_CODE.fd` appended by the run script
  (not `power_on/qemu`); assert framebuffer node in `platform_info`
  (phys/width/height/bpp=32/type logged — R15.2 gate).
- Gate: `boot-probe: PASS` over the log, bounded ≤120 s, evidence log
  committed.

**W1 OUTCOME (2026-08-18) — blocked at Genode core init under OVMF;
the reference scenario and all diagnostics are landed.** Verified
chain: OVMF pflash (pinned 2024.02, `var/ovmf/`) → GRUB2 EFI →
grub.cfg → bender → seL4 → "Booting all finished" → Genode core
starts (prints the two `initial_untyped_pool`/RAM-registration
warnings — UEFI-only, absent on every BIOS log) → **hangs between
Platform construction and the banner at `base/src/core/main.cc:126`**
(no fault: QEMU `-d int,guest_errors,cpu_reset` shows zero injected
exceptions; debug kernel prints no fault). Ruled out by experiment:
console-port routing (`console_port=0x3f8 debug_port=0x3f8`),
`phys_max=256M` placement, `-m 4G` capacity, `-machine pc` MMIO
layout. Full evidence: `docs/evidence/phase15-uefi-boot-smoke.log`.
Open hypothesis (unproven, recorded per AGENTS.md §6): OVMF's
fragmented EFI memory map breaks core's untyped registration in a way
real 2020-era firmware (the 15-3 target's Insyde H2O) does not —
Sculpt boots this exact vendored chain on real UEFI machines. The
speculative `CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS` bump is NOT
adopted as a patch; it stays an open question until real-hardware
evidence arrives. The bender `check_mem` NOP was reverted; the
principled bender rebuild and the g2fg `grub213` rebuild are
`docs/11 §4.2` ledger candidates.

### W2: Bake profiles + pipeline (`run/bake.inc`, `pkg/bake/*`,
`tool/bake.mojo`, `tool/dist.mojo` wiring)

- `run/bake.inc`: reads `SPONGE_BAKE_PROFILE` (default `desktop`),
  validates the profile (`config_version`), stages
  `pkg_<name>.xml` + `pkg_index.xml` + `/system/bake/*` (manifest,
  default configd keys, default theme) into `[run_dir]`; fails loudly
  on missing artifacts (R15.3).
- `tool/bake.mojo`: `--list`, `--show <profile>`, `--img <file>
  --profile <name>` (post-build P3 injector, idempotent, sgdisk
  pre/post verify, runs before mkdata), size-budget enforcement (D15.5).
- `tool/dist.mojo`: `--bake-profile {minimal,desktop}`,
  `--firmware {bios,uefi}` flags; summary table prints profile +
  firmware; sha256 sidecars unchanged; byte-identical rebuild gate
  (R15.4).
- Adopt `run/bake.inc` in `sponge-desktop-disk.run` and
  `sponge-alpha.run` (behavior-preserving for the default profile).

### W3: First-boot semantics + `vct bake`

- `sponge_configd`: `bake.*` key group; sentinel-based P4 seeding
  (D15.9); `vct bake list/show/reset` + `--json`/`--lang ko`/`--manual`
  (D15.7, D15.12).
- First-boot UX: `vct status` shows `bake: <profile> @ v<version>`.
- Scenarios: `sponge-bake-firstboot.run` (seed fires once, user changes
  survive reboot), `sponge-bake-reset.run`.

### W4: Product UEFI media + desktop on OVMF

- `run/sponge-desktop-disk-uefi.run` (D15.13 layout: ESP + GENODE ext2
  + SPONGE-DATA via `tool/mkdata`), UEFI drivers config with `boot_fb`,
  `sponge-de` desktop boots to the themed panel under OVMF.
- `sponge-desktop-disk-uefi-nvme.run` target-machine envelope.
- `tool/dist --firmware uefi` integration; QGenodeScreen 1×1 race
  (D14.8(b)) evaluated on the `boot_fb` path per the Phase 14
  re-scope.

### W5 (15-2): Matrix scenarios + validator policy

- All new matrix scenarios from the contract table; `docs/15` updated
  per cell with evidence links; `tool/hw_compat.mojo assert` policy
  change (D15.11); full serial regression sweep (BIOS + UEFI).

### W6 (15-1/15-2 close): Docs sync + 15-3 protocol

- `docs/13-installation.md`: bake profiles, `--firmware`, real-hardware
  path (USB write + firmware settings); QEMU-only limitation rescoped.
- `docs/08-development.md` §11 + `docs/11-environment.md` §7.3/§12:
  bake + UEFI tool surface; `ovmf` in host-tool table.
- `docs/14-boot-storage-architecture.md`: UEFI partition variant.
- `docs/plans/phase15-hardware-boot-protocol.md`: 15-3 user protocol —
  BIOS checklist (Secure Boot off, iTBT off, F10 boot menu), the exact
  `dd` command (user-run), capture checklist (GRUB menu photo, boot log
  photo, `vct status` output if reached), known-gap expectations
  (trackpad, Wi-Fi), success/failure decision tree.
- README status update; roadmap checkboxes 3 (partially, across W5–W6).

### 15-3 (user-executed)

- User flashes `desktop` UEFI `.img` to USB, follows the protocol,
  captures evidence; agent analyzes, closes criteria 1/2/4 (or records
  gaps with reproduction notes), flips the single
  `target: real-hardware` row to `verified`, releases 0.2.0-alpha.

## Must NOT Have (binding)

- No vendored-tree edits; no subtree pull; no shim/MOK signing work.
- No `i2c_hid`, Wi-Fi, USB-Ethernet, multi-namespace NVMe, audio, or
  ARM claims. Trackpad/Wi-Fi stay gaps with honest prose.
- No use of `genode/tool/run/image/uefi` (D15.13) — and no claim that
  avoiding it diminishes UEFI support; the Sponge recipe is the
  supported path.
- No removal of the BIOS media path (escape-hatch principle).
- No `sudo` by the agent, ever; the `dd` and firmware steps are
  presented to the user.
- No silent profile default changes; no scenario-count acceptance; no
  concurrent builds in the shared build dir; no `.omo/` references in
  durable docs.

## Commit Strategy

One logical change per commit, dependency order, conventional messages:

1. `docs(roadmap): add phase 15 work plan`
2. `test(evidence): capture phase 15 media baseline`
3. `feat(run): add uefi tier-0 boot smoke (OVMF, handcrafted GPT)`
4. `feat(bake): add bake profiles and run/bake.inc staging`
5. `feat(tool): add tool/bake profile inspector and img injector`
6. `feat(tool): wire bake-profile and firmware selectors into dist`
7. `feat(configd): seed user config from baked defaults on first boot`
8. `feat(vct): add bake list/show/reset subcommand`
9. `feat(run): add product uefi disk media scenario`
10. `feat(run): add uefi nvme desktop envelope scenario`
11. `feat(run): add usb hid mouse envelope scenario`
12. `feat(tool): admit real-hardware rows with qemu-envelope links`
13. `docs(hardware): update compatibility matrix for phase 15`
14. `docs(install): document bake profiles and real-hardware path`
15. `docs(roadmap): close 15-1/15-2 with serialized regression evidence`

15-3 commits follow the user's physical run: evidence, matrix flip,
release notes.

## Phase 14 Handoff Disposition (W0, R15.15 — 2026-08-17)

Every row of `docs/evidence/phase14-index.md` §5, assigned:

| # | Item | Disposition |
|---|---|---|
| 1 / 45 | QPA misroutes tablet absolute-motion (multi-domain Qt) | **Deferred (upstream QPA).** 15-3 input path is PS/2 keyboard + USB mouse; usb-tablet is QEMU-only. Gap prose kept. |
| 2 | Nitpicker pointer ROM updates only on `absolute_motion` | **Gap kept.** USB HID mouse is the 15-3 pointing device; the PS/2-relative path is not exercised on the target. Relevant to 15-2's `sponge-usb-hid-mouse.run` cell. |
| 6 / 14 | `panel.position` boot-time-only | **Deferred to Phase 16+.** Not hardware-boot critical; recorded with target phase. |
| 12 | Cursor invisible under PS/2-only input | **Gap kept; 15-3 protocol note.** Target uses PS/2 keyboard + USB mouse, so a visible cursor is expected; the PS/2-only case is documented in the 15-3 protocol's known-gap list. |
| 24 | Multi-namespace NVMe | **Gap kept.** Target SSD is expected single-namespace; 15-3 records the actual namespace count from the boot log. |
| 25 / 27 | Non-e1000 NICs (rtl8169/Wi-Fi/USB-Ethernet) | **Gap kept.** Target machine has AX201 (CNVio2) Wi-Fi and no Ethernet; networking is not a Phase 15 criterion. |
| 26 | USB HID keyboard glyph-delta (probe-focus quirk) | **15-2.** Re-checked on the usb-kbd envelope against baked media; if it reproduces, stays a gap with the new evidence pointer. |
| 28 | `i2c_hid` not implemented | **Gap kept (R15.10).** Target trackpad is I2C-HID; 15-3 protocol mandates a USB mouse. |
| 30 | `pkg_import` broader coverage (D13.5) | **Deferred.** Not needed for the bake profiles' package set. |
| 36 | Terminal toolset tars not pre-staged | **Absorbed into 15-1 W2.** The `desktop` profile stages the terminal CLI toolset; `minimal` stages the terminal core. Profile contents record this explicitly. |
| 46 | QGenodeScreen 1×1 race | **15-1 W4.** Evaluated on the `boot_fb` UEFI display path per the D14.8(b) re-scope; Phase-11 width-floor guard retained meanwhile. |
| W8 step 5 | Layouter hover-state timing race (workflow tasklist click) | **15-2 (optional re-check).** Re-run `sponge-de-workflow.run` on baked `desktop` media; result recorded either way. |
| W5 | textedit Ctrl-C keyboard dropout | **15-2 (optional re-check).** Re-checked on the usb-kbd path. |

## Success Criteria

1. **15-1 C1 — profiles:** `tool/dist --bake-profile {minimal,desktop}`
   produces both media on BIOS and UEFI firmware, each within its size
   budget, each booting to its declared package set in QEMU; sha256
   reproducible per profile.
2. **15-1 C2 — first boot:** baked defaults appear exactly once on
   first boot; user edits persist across reboots; `vct bake reset`
   restores baked defaults; all three paths have scenario evidence.
3. **15-1 C3 — UEFI:** `sponge-boot-uefi` and
   `sponge-desktop-disk-uefi` reach their bounded markers under OVMF;
   framebuffer provenance (`platform_info`) logged; BIOS media
   regression-free.
4. **15-2 C4 — matrix:** every non-gap cell of the updated
   compatibility matrix maps to scenario + evidence;
   `tool/hw_compat.mojo assert` and `./tool/build verify` exit 0.
5. **15-3 C5 — physical:** the 17ZD90N-VX7BK boots the `desktop` UEFI
   media from USB to the same verified desktop state as QEMU (keyboard
   + USB mouse input, GOP display, NVMe storage), with machine-specific
   issues recorded per criterion 4.
6. **Honesty:** timing budgets stated per scenario; every `verified`
   claim has a durable evidence log; every unreachable cell is a `gap`
   with a reason, never an omission.
