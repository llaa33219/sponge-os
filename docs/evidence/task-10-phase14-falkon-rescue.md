# Phase 14 W10 — Falkon rescue attempt on seL4 (D14.5 Attempt 1 — PASS)

> Date: 2026-08-14
> Phase: 14 (W10)
> Decision: D14.5
> Scenario: `run/sponge-falkon-rescue.run` (NEW — modeled on
> `run/sponge-falkon-disk.run`)
> Outcome: **PASS** — falkon reaches first paint + loads the host
> fixture URL on the current vendored tree with patches #6 + #7
> from `docs/11-environment.md` §4 already applied.
> Evidence (raw QEMU log): `docs/evidence/task-10-phase14-falkon-rescue.log`

---

## 0. Headline verdict

**D14.5 satisfied.** The Phase 14 criterion 3 "browser" requirement is
met on the current tree. Falkon is shippable on the Alpha seL4 media
via the disk-served boot/storage topology (Phase 8 P4) combined with
the Phase 9 cap-ceiling patches (#6 + #7 in `docs/11-environment.md`
§4). No additional vendored-tree patch is needed; the D14.5
amendment path is **not** taken.

The combined claim being proven:

| Component | Contribution | Ledger ref |
|---|---|---|
| Disk-served boot/storage topology (Phase 8 P4) | falkon's 508 MiB WebEngine closure is NOT in image.elf; served at runtime by a third `cached_fs_rom` instance chrooted to `/system/pkg/falkon/payload/` on the GENODE ext2 P3 | `run/sponge-falkon-disk.run` (architectural) |
| `CSPACE_SIZE_LOG2_2ND` 7 → 9 on x86_64 (Phase 9 patch #6) | per-PD CSpace is 2^(6+9) = 32768 slots (4x upstream; 4x the original 8192 that caused "out of selector") | `docs/11-environment.md` §4 row #6 (`ea8ac1343b`) |
| Lazy `vm_space` lower-level CNode growth (Phase 9 patch #7) | WebEngine page-table mappings live in vm_space; lazy growth to 2^17 = 131072 entries (vs static 2^15 = 32768 that WebEngine's ~100k frame caps exhausted) | `docs/11-environment.md` §4 row #7 (`38a19fa5d8`) |

The README's existing claim "Falkon reaches first paint on the
Alpha seL4 media (Phase 9 closed the capability-chain blocker)" is
**proven reproducible** by this scenario.

---

## 1. Boot chain claim (PROVEN — the ceiling-killer proof holds)

```
sponge-falkon-rescue: staged 64 payload files under
  /system/pkg/falkon/payload/ (508 MiB on the GENODE partition —
  NOT in image.elf)
sponge-falkon-rescue: image.elf = 12 MiB (13476800 bytes)
sponge-falkon-rescue: falkon payload on disk = 508 MiB
  (NOT in image.elf — the ceiling-killer proof)
```

  - image.elf = 12 MiB (13,476,800 bytes) — within the §4.5 80 MiB
    budget (same Tier-0 roster as the Phase 8 P2 desktop-from-disk
    scenario; nothing from the falkon payload is a Tier-0 boot
    module).
  - falkon's 508 MiB WebEngine closure (64 files: the falkon binary,
    the 237 MiB `libQt6WebEngineCore.lib.so`, Qt6 libs, WebEngine
    resource/translation tars, NSS/crypto libs, the lwip socket
    plugin) is staged under `/system/pkg/falkon/payload/` on the
    GENODE ext2 P3 and served as ROM sessions by the third
    `cached_fs_rom` instance (`rom_pkg`, chrooted there).
  - The 509 MiB travel through `cached_fs_rom`'s on-demand paging at
    runtime, never through bender or seL4's boot-module setup.

This is the Phase 8 P4 architectural claim, re-verified on the
current tree.

---

## 2. Patch #6 + #7 — per-PD CNode + vm_space sizing

  - `CSPACE_SIZE_LOG2_2ND` = 9 on x86_64 → per-PD CSpace is
    2^(6+9) = **32768 slots per PD** (4x upstream; 4x the original
    8192 ceiling that caused "out of selector" before patches).
    See `genode/repos/base-sel4/src/include/base/internal/
    capability_space_sel4.h:117`.
  - `LEAF_CNODE_SIZE_LOG2` 7 → 9 → `NUM_VM_SEL_LOG2` 15 → 17 → vm_space
    lazy-grows to **131072 entries** (vs static 32768 before patch #7).
    See `genode/repos/base-sel4/src/core/include/vm_space.h` (patch
    #7 commit `38a19fa5d8`).
  - falkon's quota cascade: `falkon caps=200000 ram=1G` →
    `pkg_runtime caps=210000 ram=1500M` → `system caps=250000
    ram=3000M`. falkon's heap is 1 GiB; the 200k cap quota covers
    the ~100k WebEngine frame caps + heap headroom.

---

## 3. First-paint proof (the W10 / D14.5 acceptance gate)

  ```
  [init -> system -> pkg_runtime -> falkon] Falkon: 1 extensions loaded
  [init -> system -> falkon_probe] falkon-probe: falkon window detected
    (100% non-bg, 46 distinct color buckets)
  [init -> system -> falkon_probe] falkon-probe: [6] launch nosuchpkg-16
    -> not-installed
  [init -> system -> falkon_probe] falkon-probe: [6] not-installed reported
  [init -> system -> falkon_probe] falkon-probe: [7] double-launch falkon
    -> already-running
  [init -> system -> falkon_probe] falkon-probe: [7] already-running reported
  [init -> system -> falkon_probe] falkon-probe: PASS
  ```

  - WebEngine first paint under softpipe Mesa on seL4 is SLOW; the
    probe polled for several minutes (poll loop is bounded at
    6000 × 100ms = 600s per `falkon_probe::_wait_for_window`).
  - Pixel check `rendered_frac=100% AND color_buckets=46`:
    - `rendered_frac` (fraction of sampled non-background pixels
      across the full 1024×768 panorama) = 1.00 (above the 0.25
      threshold; the misleading_success_output guard).
    - `color_buckets` (distinct 4-bit-per-channel color buckets
      across the sampled grid) = 46 (above the 12 floor; a
      solid-color frame cannot pass).
  - The probe's error-path assertions also pass:
    `launch nosuchpkg-16 → not-installed` and double-launch
    `falkon → already-running` both report clear statuses from
    sponge_pkgd (no crash, no silent drop).

---

## 4. Fixture GET (the network-round-trip proof)

  ```
  sponge-falkon-rescue: probe PASS — checking fixture GET...
  sponge-falkon-rescue: fixture GET detected — falkon loaded the page
    from disk-served binary over the nic stack
  sponge-falkon-rescue: host fixture killed (cleanup receipt)
  ```

  - The host `python3 -m http.server` serves
    `run/fixtures/net-probe/net-fixture.txt` on port 8765.
  - falkon's metadata `<arg>` navigates to
    `http://10.0.2.2:8765/net-fixture.txt` on startup (QEMU slirp
    host alias).
  - The GET appears in `falkon-access.log` — the definitive
    host-side signal that falkon's lwip+ipxe_nic stack round-tripped
    a real HTTP request. The exit-0 probe PASS alone is not enough;
    the bytes must round-trip.

---

## 5. Phase 8 P4 evidence log cross-reference

`docs/evidence/p4-falkon-disk.log` §1–§3 is the architectural
proof (boot chain claim; same falkon boot up to DHCP). The Phase 14
W10 rescue re-runs that scenario on the current tree and proves
that the patches #6 + #7 close the per-PD CNode gap that stopped
falkon before first paint. The D14.5 effort bound (2-day) was
not exhausted; the disk-served Attempt 1 was decisive on the
first run.

---

## 6. Files delivered

  - `run/sponge-falkon-rescue.run` (NEW — modeled on
    `run/sponge-falkon-disk.run` with the Phase 14 W10 / D14.5
    header documenting the rescue context, the post-patch sizing,
    and the outcome matrix).
  - `repos/sponge/run/sponge-falkon-rescue.run` (NEW symlink —
    mirrors the `repos/sponge/run/` ↔ `run/` pattern used by every
    other scenario; required by the Genode build repo-discovery).
  - `docs/evidence/task-10-phase14-falkon-rescue.log` (NEW — raw
    QEMU output, 2578 lines, 124 KiB; includes the full Genode
    boot, falkon init, probe PASS, fixture GET, and run-script
    PASS exit).
  - `docs/evidence/task-10-phase14-falkon-step1-reproduce.log`
    (NEW — raw QEMU output from the Step 1 reproduction of the
    boot-chain failure mode on the current tree; 4284 lines,
    191 KiB; proves the boot-chain ceiling still applies to the
    tar_rom + multiboot2 approach that the original
    `run/sponge-falkon.run` uses — orthogonal to patches #6 + #7
    which address per-PD CNode sizing, not the boot chain).

---

## 7. NOT modified (per constraints)

  - `genode/repos/base-sel4/` (the vendored tree) — patches #6 + #7
    are ALREADY applied (Phase 9). W10 Attempt 2 (the "10th
    patch-ledger row" path that would bump `CSPACE_SIZE_LOG2_2ND`
    further) is **not** taken because the disk-served topology +
    post-patch sizing is sufficient.
  - `run/sponge-falkon.run` (the tar_rom + multiboot2 tar scenario)
    — unchanged. Its failure mode (boot chain ceiling) is documented
    by the new Step 1 evidence log; that is a known failure mode
    since Phase 7 todo 16 and the disk-served topology was the
    planned resolution path.
  - `run/sponge-falkon-disk.run` — unchanged. It was the Phase 8 P4
    architectural proof; the W10 rescue re-verifies the same
    architecture on the current tree via the new
    `run/sponge-falkon-rescue.run` (modeled on the disk scenario,
    not a modification of it — the disk scenario's `image/iso`
    artifact shape is unchanged for P4 provenance).
  - `docs/09-roadmap.md` — no amendment needed; criterion 3 wording
    stands ("Everyday workflow proven end-to-end in scenario:
    boot → launch terminal/editor/files/browser → do real work →
    shut down cleanly"). The W7 tasklist + W8 workflow scenario
    + this W10 rescue together satisfy the "browser" bullet.

---

## 8. Build command

  ```
  make -C genode/build/x86_64_w10 run/sponge-falkon-rescue \
      KERNEL=sel4 BOARD=pc RUN_OPT="--include image/disk"
  ```

  Wall-clock: ~4 minutes (the bulk of the time is the seL4 kernel
  config + compile; the disk image build is incremental from the
  contrib/ prebuilt falkon payload and the in-tree Genode
  components).

---

## 9. Acceptance criteria status (D14.5)

  | # | Criterion | Status |
  |---|---|---|
  | 1 | Boot-chain claim: falkon's 508 MiB NOT in image.elf | MET (image.elf = 12 MiB) |
  | 2 | First-paint proof: falkon window pixel-verified | MET (100% non-bg, 46 color buckets) |
  | 3 | Network round-trip: falkon fetched the host fixture | MET (GET in access log) |
  | 4 | Error-handling: pkgd reports `not-installed` / `already-running` | MET (probe steps 6 + 7) |
  | 5 | Evidence durable: `docs/evidence/task-10-phase14-falkon-rescue.log` | MET (this run + log) |
  | 6 | Phase 14 criterion 3 (browser) satisfied | MET (no amendment needed) |

  All six D14.5 acceptance criteria are MET. W10 Attempt 1 is
  decisive; Attempt 2 is not attempted.