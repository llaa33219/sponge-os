# Patch classification — keep / remove / replace decision matrix for all Genode fork-local changes

**Artifact type:** design-significant engineering evidence (capability/patch-set
analysis; AGENTS.md §5.6)
**Date:** 2026-10-28
**Scope:** every Sponge-local change on top of the pinned Genode 26.08 fork
(`genode/` submodule, `sponge-os-genode`): the **31 patch-ledger rows** of
`docs/11-environment.md` §4 and the **unledgered fork-local commits** in the
fork history, including the net-zero revert pair.
**Status:** classification record only. This document pre-approves **no**
removal, runs no scenario, and changes no code. It is the durable
keep/remove/replace matrix the next patch-set audit builds on.

---

## 1. Purpose and how to read this document

The patch ledger (`docs/11-environment.md` §4) answers *"what did we change
and why."* This document answers the operational follow-up for each entry:

1. **Verdict** — keep, replace-when, superseded, or reassess-later.
2. **Drop-when** — the exact precondition that would make removal legitimate.
3. **Dependencies** — what else rides on this entry (and what this rides on).
4. **Removal verification** — the exact scenario(s) that must pass after a
   removal before it may be committed.

Two distinctions are load-bearing throughout:

- **Documentation-only correction** (ledger wording, forwarding notices,
  revert-list fixes) versus **actual code removal candidate** (reverting or
  deleting a shipped change). Only the latter changes runtime behavior; the
  former is bookkeeping and is listed separately in Part D.
- **Ledger consolidation** (merging or re-wording rows) is *not* a code
  removal. Row #21 is the standing example (Part A, Part C).

## 2. Sources and method

Claims in this document come from the following, as of fork `HEAD` =
`82f0143d1f` (ledger row #31):

- `docs/11-environment.md` §4 patch ledger (rows 1–31, including each row's
  Drop-When cell), §4.1 (`tool/patches` semantics), §4.2 (candidate intents —
  out of scope here: those are intents, not patches).
- `tool/patches.mojo` — read-only ledger tool. `verify` checks per row that
  the commit exists and is an ancestor of `HEAD`, resolving each SHA in
  `genode/` first and falling back to the main repository
  (`git_capture_both`). Recorded audit result: **`tool/patches verify` passes
  all 31 rows**. `drop <n>` prints manual revert instructions only; per
  AGENTS.md §5.2 a drop is a deliberate human commit.
- `docs/patches/` — 7 files: 3 durable patch records
  (`bender-uefi-memmap.patch`, `qt6-qpa-frame-sync.patch`,
  `themed-decorator-resize-minimize.patch`) and 4 **forwarding notices**
  (`sel4-reserved-extent-export.patch`, `sel4-early-fb-console.patch`,
  `sel4-device-untyped-coverage.patch`, `sel4-device-untyped-ladder.patch`).
- Genode fork git history and diffs (`git log`, `git show --stat` per commit)
  and the fork's `repos/base-sel4/patches/` port-patch directory
  (current `sel4.hash` stamp `28c4a439…`).
- Main-repository commit `1c2dd297aa` (row #23's ledger SHA resolves there,
  not in the fork — see Part B, `87c8ebf33f`).
- Evidence records cited per row below, notably
  `docs/evidence/sponge-upgrade-wedge.md` (net-zero revert pair),
  `docs/evidence/forwarded-close-starvation.md` and
  `docs/evidence/forwarded-create-starvation.md` (forwarded-session cluster),
  `docs/evidence/2026-10-19-sel4-patch-autoapply.md` (seL4 port-patch
  mechanism), `docs/evidence/xhci-high-bar-mapping.md` (row #27),
  `docs/evidence/sponge-quota-deadlock.md` (rows #21/#25),
  `docs/evidence/startup-grace.md` (row #26),
  `docs/evidence/sponge-debug-scrollstorm.md` (row #23).

Scenario names are `run/` scripts in this repository. Where a row's
Drop-When cell names no scenario, the removal-verification entry below says
**derived** and gives the narrowest check supported by the row text.

## 3. Policy in force

- **Preserve-first.** seL4 kernel fixes, Qt6 fixes, and upstream defect
  fixes are kept until their stated upstream absorption condition is *met*,
  not merely plausible. Absence of an upstream fix is not a reason to
  remove; it is the reason the row exists.
- **Zero removals is a valid outcome.** If no row's drop-when is met, the
  correct classification action is to remove nothing. This document reaches
  exactly that outcome today (Part D). "Cleaning up the patch set" is never
  a motivation on its own; `docs/11-environment.md` §4 states a patch is
  *never silently absorbed* and its row drops only together with a
  deliberate revert commit on `main` that says so.
- **Reassessment is not approval.** Entries marked *reassess-after-topology*
  (rows #19/#20 and #28–#31, plus unledgered `73e5859da8`) are explicitly
  **not** pre-approved removals. They may be revisited only after the
  architecture refactor's topology gates are green (Part E); until then the
  verdict stands as KEEP.
- **Scope discipline.** This document records decisions and their support.
  It proposes no new base patches (§4.2 candidates stay where they are) and
  edits neither the ledger nor the code.

## 4. Verdict legend

| Code | Meaning |
|------|---------|
| **K-U** | KEEP — replace-when-upstream. Preserve-first; remove only when the row's drop-when condition is met and the named verification passes. |
| **K-A** | KEEP — always. Sponge-owned artifact, not an upstream patch (row #5's drop-when is "Never"). |
| **K-I** | KEEP — infrastructure / bookkeeping commit with no independent drop-when; it rides with its cluster (Part C). |
| **S** | SUPERSEDED — ledger consolidation candidate. The mechanism was rewritten by a later row; **not** an independently revertable code-removal candidate. |
| **R-T** | REASSESS-AFTER-TOPOLOGY — nested/forwarded-session machinery. Keep now; reassess only after the topology gates are green. Not pre-approved for removal. |
| **R-ONLY** | RECORD-ONLY — history/record entry with a net-zero tree delta; nothing exists to remove. |

---

## 5. Summary matrix

| # | Commit | Verdict | One-line drop-when |
|---|--------|---------|--------------------|
| 1 | `252a6aa4b7` | K-U | upstream fixes the `stdcxx.port` URL itself |
| 2 | `55adeec3f0` | K-U | Sponge grows a Qt-tools dependency |
| 3 | `dd6465e014` | K-U | upstream parameterizes `QT_TOOLS_DIR` |
| 4 | `d8604ff9bf` | K-U | upstream fixes the qt6 cmake host-header leak |
| 5 | `935941ad68` | K-A | never (Sponge-owned artifacts) |
| 6 | `847483031a` | K-U | upstream resolves `platform.cc:108` XXX with dynamic CNode allocation |
| 7 | `847483031a` | K-U | upstream resolves `platform.cc:108` XXX / dynamic vm_space growth |
| 8 | `59e50cdba4` | K-U | upstream switches bash/ncurses ports to a working mirror |
| 9 | `010ad9ea0b` | K-U | upstream initializes `_pointer` for relative-only input |
| 10 | `ec3a711f7c` | K-U | upstream bender understands UEFI-reclaimable memmap, or bender leaves the chain |
| 11 | `4904d06c4b` | K-U | upstream seL4 exports the reserved extent natively |
| 12 | `ed354a4bd3` | K-U | serial-on-PCI vendor, or upstream mirrors the kernel log to the FB |
| 13 | `1bd88ee53d` | K-U | upstream grows `seL4_BootInfoFrameBits` or a `get_high_mmio_untyped_cap` API |
| 14 | `847483031a` | K-U | upstream grows a proper high-MMIO IO_MEM path |
| 15 | `847483031a` | K-U | upstream re-wires `phys_alloc_16k` metadata growth |
| 16 | `731c931373` | K-U | upstream rewrites the DMAR walkers per the VT-d spec |
| 17 | `3449ef9cbb` | K-U | upstream themed_decorator gains sizers + `<minimizer/>` |
| 18 | `46da4e43b9` | K-U | upstream fixes #5338 (robust queue wakeup) |
| 19 | `391272b3e3` | R-T | upstream makes a Blockade wake session-guaranteed (with row #20) |
| 20 | `391272b3e3` | R-T | upstream restructures `Root_proxy::_handle_session_requests` (with row #19) |
| 21 | `10d172b3dd` | S | superseded by #25 — ledger consolidation, not a code removal |
| 22 | `f16ad10cba` | K-U | upstream restores the `CONFIG_DEBUG_BUILD` gen_config append |
| 23 | `1c2dd297aa` | K-U | upstream qt6 QPA registers the framebuffer sync |
| 24 | `f44bc47ee5` | K-U | upstream adds a restart budget/backoff to the heartbeat restart path |
| 25 | `342900343b` | K-U | upstream refuses/bounds `Parent::upgrade` retries (absorbs #21's intent) |
| 26 | `dda40f3a87` | K-U | upstream distinguishes a first construct from a wedge |
| 27 | `20d0ff31d8` | K-U | upstream keeps a device untyped's free index (builds on #13) |
| 28 | `1672f3649a` | R-T | upstream grows non-blocking upgrade/close + a per-op deadline (`ea04b06868` + `1672f3649a`) |
| 29 | `0a329d4fff` | R-T | upstream stabilizes `Genode::Env`'s layout / async Parent protocol |
| 30 | `74c84f9f0a` | R-T | upstream notifies a container when a forwarded close completes |
| 31 | `82f0143d1f` | R-T | upstream notifies a container when a forwarded create completes (`e0c2706225` + `205b84fcad` + `82f0143d1f`) |
| — | `d60ebb3f92` | K-I | unledgered: seL4 13.0.0 → 16.0.0 pin override (replace when upstream pins 16+) |
| — | `aaa279e1cc` | K-I | unledgered: vm-leaf 16K-pool budget for 26.08 mapping sets (same family as #6/#7) |
| — | `a9ee2bcde3` | K-I | unledgered: 26.08 build-compat for boot_fb (not independently removable) |
| — | `e16699624d` | K-I | unledgered: seL4 port-patch auto-apply (rides with rows #11/#12/#13/#27) |
| — | `57075c4d26` | K-I | unledgered: `sel4.hash` re-stamp for the new patch set (bookkeeping) |
| — | `73e5859da8` | R-T | unledgered: forwarded-session cluster member — **missing from all revert lists** |
| — | `87c8ebf33f` | K-I | unledgered: qt6 `target.mk` auto-apply wiring for row #23 (ledger gap) |
| — | `500cb2cc8a` + `3df26cf22b` | R-ONLY | net-zero revert pair (first row-#28 attempt, reverted) |

---

## 6. Part A — ledger rows 1–31

Each entry gives the four required fields. Drop-when paraphrases the ledger
cell; the ledger cell remains the authoritative wording.

### Row 1 — `252a6aa4b7` — stdcxx port: replace dead gcc mirror with ftp.gnu.org
- **Verdict:** K-U (preserve-first, upstream defect/port fix).
- **Drop-when:** upstream updates the `stdcxx.port` URL itself (which would re-bump the hash).
- **Dependencies:** none beyond the port fetch.
- **Removal verification (derived):** `prepare_port stdcxx` re-fetches from the
  upstream URL and the stdcxx consumers still build. No run/ scenario is
  named in the row.

### Row 2 — `55adeec3f0` — disable qttools in the qt6 host-tools build
- **Verdict:** K-U (Qt6 build fix).
- **Drop-when:** Sponge OS gains a Qt tools dependency (Designer, Linguist, …).
- **Dependencies:** qt6 host-tools build (`genode/tool/tool_chain_qt6`); pairs
  with row #3 (same host-tools area).
- **Removal verification (derived):** the Qt6 host-tools build with
  `BUILD_qttools=ON` succeeds on a clean tree and the Qt6 scenarios still
  stage. No run/ scenario is named in the row.

### Row 3 — `dd6465e014` — parameterize `QT_TOOLS_DIR` with a repo-local default
- **Verdict:** K-U (Qt6 build fix).
- **Drop-when:** upstream parameterizes `QT_TOOLS_DIR`.
- **Dependencies:** row #2 family (qt6 import machinery).
- **Removal verification (derived):** Qt6 rebuild (`import-qt6.inc` consumers)
  against the upstream default path. No run/ scenario is named in the row.

### Row 4 — `d8604ff9bf` — qt6 cmake build: block host-header leakage via sponge-owned wrappers
- **Verdict:** K-U (Qt6 build fix).
- **Drop-when:** upstream fixes the cmake invocation to never see host
  headers, or Sponge OS no longer uses Qt6 (the row records the latter as
  unlikely).
- **Dependencies:** row #5 (the sponge-owned compiler wrappers are the
  mechanism) — dropping #4 while keeping #5 is fine; dropping #5 while #4
  remains is not (see Part C).
- **Removal verification (derived):** qt6 base rebuild without the wrappers
  against an upstream-fixed cmake path; `sponge-de-test` still builds/runs.

### Row 5 — `935941ad68` — sponge-owned compiler wrappers + repos/sponge wiring
- **Verdict:** K-A — **never dropped.** The drop-when cell reads "Never
  (these are Sponge-owned artifacts, not upstream patches)".
- **Drop-when:** none. Not an upstream patch.
- **Dependencies:** consumed by row #4; wires `repos/sponge` into the vendored
  tree (the symlink is load-bearing for the whole build, AGENTS.md §2).
- **Removal verification:** n/a. This entry is excluded from removal
  planning by its own drop-when.

### Row 6 — `847483031a` — sel4: size-aware on-demand CNode backing for large child-PD CSpaces
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** upstream resolves the `platform.cc:108` `XXX` ("allocate
  intermediate CNodes … here") with dynamic CNode allocation. The
  Phase-15 addendum (CSPACE_SIZE_LOG2_1ST 6→7, 65536 slots/PD) is part of
  this row and reverts with it.
- **Dependencies:** single commit shared with rows #7, #14, #15 (one revert
  drops all four rows — Part C). Phase-9 cluster evidence:
  `docs/evidence/c1-dma-safe-backing.log` (title-matching workstream of
  `docs/plans/phase9-sel4-cspace.md`) and the `c4-regression.log` closure
  set; the c1–c4 logs cover this one-commit cluster as a whole.
- **Removal verification (named + derived):** the Phase-9 cluster regression
  (`docs/evidence/c4-regression.log` closure set) and the `sponge-desktop-disk`
  family; the 16 KiB-pool / 4 GiB desktop regression the drop-when warns
  about must stay green. As a single-commit cluster, verify once for
  rows #6/#7/#14/#15 together.

### Row 7 — `847483031a` — sel4: lazy vm_space lower-level CNode growth (131072 mappings/PD)
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** upstream resolves `platform.cc:108` `XXX` / grows vm_space
  dynamically.
- **Dependencies:** same commit as rows #6/#14/#15. Evidence:
  `docs/evidence/c2-lazy-vmspace.log`.
- **Removal verification (derived):** mapping-set pressure gate (the lazy
  growth path at 131072 mappings/PD) within the c1–c4 cluster regression;
  `sponge-desktop-disk` family. Verified once for the whole commit.

### Row 8 — `59e50cdba4` — bash/ncurses ports: replace 502-ing ftpmirror.gnu.org with ftp.gnu.org
- **Verdict:** K-U (preserve-first, port fix).
- **Drop-when:** upstream switches the two ports to a working mirror (which
  would re-bump the hashes).
- **Dependencies:** none beyond the port fetches.
- **Removal verification (derived):** `prepare_port bash` and
  `prepare_port ncurses` re-fetch against the upstream port files. No run/
  scenario is named in the row.

### Row 9 — `010ad9ea0b` — nitpicker: track pointer on relative motion before the first absolute event
- **Verdict:** K-U (upstream defect fix).
- **Drop-when:** upstream initializes `_pointer` for relative-only input paths
  (roadmap §11.2 item 2 records the adjacent report-ROM gap).
- **Dependencies:** none.
- **Removal verification (derived):** the relative-motion cold-start path —
  the product USB input gates (e.g. `run/sponge-usb-hid-mouse.run` and the
  `sponge-desktop-disk-uefi-usb` mouse path). No dedicated scenario is named
  in the row.

### Row 10 — `ec3a711f7c` — bender: tolerate reclaimable UEFI memmap overlaps + union-coverage RAM check
- **Verdict:** K-U (upstream defect fix; binary rebuilt from
  `alex-ab/morbo` branch `genode_bender`).
- **Drop-when:** upstream Genode ships a bender whose `check_mem` understands
  UEFI-reclaimable regions, or the boot chain no longer needs bender.
- **Dependencies:** none in-tree; the durable source record is
  `docs/patches/bender-uefi-memmap.patch`.
- **Removal verification (derived):** the UEFI boot path —
  `run/sponge-boot-uefi.run` and `run/sponge-desktop-disk-uefi*.run` (OVMF
  boots must still place the seL4 kernel).

### Row 11 — `4904d06c4b` — sel4: reserved-extent export + core-side artifact detection (replaces the old init_freemem fix)
- **Verdict:** K-U (seL4 fix; preserve-first). Already a replacement row: it
  replaced the former subtraction-loop patch `87c58ac9ae` (deleted).
- **Drop-when:** upstream seL4 exports the reserved extent (or equivalent) in
  the bootinfo natively; then delete
  `genode/repos/base-sel4/patches/sel4-reserved-extent-export.patch` and
  re-run `genode/tool/ports/update_hash sel4` (the row spells out the
  removal steps).
- **Dependencies:** rides the port-patch auto-apply mechanism
  (`e16699624d`/`57075c4d26`, unledgered — Part B). Core-side detection in
  `initial_untyped_pool.h` is shared with rows #6/#7 (same file). The
  seL4-16 addendum (upstream `4018ad594` shrinks the leak 9→3; this row
  remains defense-in-depth) is part of the row. Evidence:
  `docs/evidence/2026-10-19-sel4-patch-autoapply.md`,
  `docs/evidence/row11-core-side-migration-attempt.md`.
- **Removal verification (named):** fresh `prepare_port sel4` + kernel
  rebuild from scratch, then `run/sponge-desktop-disk-uefi-usb` (3/3
  structural gates; artifacts detected+skipped per boot),
  `run/sponge-minimal`, `run/sponge-configd-persist`.

### Row 12 — `ed354a4bd3` — sel4 kernel: early framebuffer text console (UEFI/no-serial visibility)
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** Sponge OS acquires a graphics-capable vendor shipping
  serial-on-PCI, or the GOP framebuffer becomes the canonical kernel log
  channel everywhere and upstream seL4 mirrors the console to the
  framebuffer in its debug builds.
- **Dependencies:** row #22 (`CONFIG_DEBUG_BUILD` gates `seL4_DebugPutChar`;
  the row's own text records the dependency — never drop #22 first). Rides
  the `e16699624d`/`57075c4d26` port-patch mechanism. The `fbcon=off`
  kill-switch (production media passes it; diagnostic profile omits it,
  AGENTS.md §1.1 control escape hatch) is part of the row.
- **Removal verification (named):** `run/sponge-desktop-disk-uefi`
  (KERNEL=sel4 BOARD=pc; screendump shows `sel4 fb console: ready` → core
  log on the GOP framebuffer), `run/sponge-desktop-disk-uefi-usb`
  (production `fbcon=off` line leaves the desktop clean), `run/sponge-minimal`
  (BIOS control: console stays disabled). Patch deletion follows the row-11
  pattern (`genode/repos/base-sel4/patches/sel4-early-fb-console.patch` +
  `update_hash sel4`).

### Row 13 — `1bd88ee53d` — sel4 kernel: bound device-untyped coverage above the memory map (high GOP FB + 64-bit PCI BARs)
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** upstream seL4 grows `seL4_BootInfoFrameBits` (allowing the
  bootinfo cap past ~246 untypeds), or Sponge moves to a kernel with a
  `get_high_mmio_untyped_cap(phys, size)` API.
- **Dependencies:** rides the `e16699624d`/`57075c4d26` port-patch
  mechanism. **Row #27 builds on this row** (its 64 GiB grid covers this
  row's HIGH region; drop #27 first — Part C). Row #14's core-side high-phys
  path consumes the untypeds this row creates.
- **Removal verification (named):** `run/sponge-desktop-disk-uefi-usb`
  (high GOP framebuffer at ≥ 32 GiB class addresses maps; `alpha-probe: PASS`
  instead of black screen) with `run/sponge-minimal` as the unaffected-BIOS
  control. Patch deletion: `sel4-device-untyped-coverage.patch` +
  `update_hash sel4`.

### Row 14 — `847483031a` — base-sel4 high-phys frame-cap CNode with sequential slot allocation (IO_MEM at phys ≥ 8 GiB)
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** upstream seL4/Genode grows a proper high-MMIO IO_MEM path
  (large-page device frames or a windowed phys-CNode scheme).
- **Dependencies:** single commit with rows #6/#7/#15. Consumes row #13's
  device untypeds; row #27's watermark fix corrects the retype path this
  row's fast-forward uses (row #27's why names "core's row-14 high-phys
  path").
- **Removal verification (derived):** `run/sponge-high-phys-probe.run`
  (16 GiB in-coverage / 2 TiB out-of-coverage arms) plus the single-commit
  cluster check for rows #6/#7/#14/#15.

### Row 15 — `847483031a` — sel4: serve 16K-pool AVL metadata from a static arena (re-entrancy deadlock)
- **Verdict:** K-U (seL4 fix; preserve-first).
- **Drop-when:** upstream re-wires `phys_alloc_16k` metadata growth so it
  cannot re-enter the guarded pool (verified absent as of 2026-08-24; the
  row records the upstream staging scan and upstream's own `ff997e46d79`
  precedent).
- **Dependencies:** single commit with rows #6/#7/#14.
- **Removal verification (derived):** the c1–c4 cluster regression
  (`docs/evidence/c4-regression.log`) including the 16 KiB-pool growth path
  under load; `sponge-desktop-disk` family. Verified once for the whole
  commit.

### Row 16 — `731c931373` — acpi: skip DMAR parsing (firmware-layout misparse killed the acpi child)
- **Verdict:** K-U (upstream defect fix; preserve-first).
- **Drop-when:** upstream rewrites the DMAR walkers to the VT-d spec layout
  with bounded walks and exception-safe serialization; then the skip can be
  reverted to full parsing.
- **Dependencies:** none.
- **Removal verification (derived):** the ACPI child must survive on the
  product boots (`run/sponge-desktop-disk`, `run/sponge-minimal`) with full
  DMAR parsing restored. No dedicated scenario is named in the row.

### Row 17 — `3449ef9cbb` — themed_decorator sizer + minimizer (vendored bridge)
- **Verdict:** K-U (vendor bridge; keep until upstream).
- **Drop-when:** upstream Genode adds both the resize affordance and
  `<minimizer/>` to `themed_decorator`; then drop and re-verify the
  themed-chrome-resize scenario on the upstream-themable path.
- **Dependencies:** none in the patch graph. The durable record is
  `docs/patches/themed-decorator-resize-minimize.patch`; the Sponge-side
  `tool/decor_assets_data` entries ride the same commit. The release-media
  resize headline does **not** depend on this row (it uses the motif
  decorator).
- **Removal verification (named):** `run/sponge-de-themed-chrome-resize.run`
  on the upstream code path.

### Row 18 — `46da4e43b9` — packet_stream `tx_wakeup` re-checks queue state (self-healing wakeup)
- **Verdict:** K-U (upstream defect fix; pairs with upstream issue #5338).
- **Drop-when:** upstream fixes #5338 (release-store/acquire-load on the
  queue indices or an equivalent robust-wakeup protocol).
- **Dependencies:** none.
- **Removal verification (named):** "the mouse soak" — the ledger names no
  standalone script for it. Re-run the input/mouse soak the row was written
  for (the product-USB input chain) before committing a drop.

### Row 19 — `391272b3e3` — component.cc: treat an invalid `session_cap` as "pending" (spurious Blockade wake)
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream changes `Component_env::try_session` (or the
  Blockade protocol) so a wake guarantees a result for the waiting session.
- **Dependencies:** part 1 of 2 sharing commit `391272b3e3` with row #20 —
  one commit, two rows; reverting drops both. Verified together 6/6
  (BIOS 4/4 + UEFI 2/2 QMP click-to-launch runs). Sits in the
  nested/forwarded-session family (Part C) whose reassessment gate is the
  architecture refactor's topology gate set.
- **Removal verification (named, post-gate only):** the doubly-nested launch
  — click-to-launch runs on BIOS and UEFI USB media topologies (calculator
  window appears) plus `run/sponge-desktop-disk`,
  `run/sponge-desktop-defaults-firstboot`, `run/sponge-launch`,
  `run/sponge-alpha`.

### Row 20 — `391272b3e3` — root_proxy: re-read `session_requests` ROM before the create pass
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream restructures `Root_proxy::_handle_session_requests`
  (or the session_requester protocol) to observe creates submitted while
  upgrade/close RPCs are in flight.
- **Dependencies:** part 2 of 2 with row #19, same commit, same family and
  gate as row #19.
- **Removal verification (named, post-gate only):** same set as row #19
  (doubly-nested click-to-launch on both media topologies; the four named
  regressions).

### Row 21 — `10d172b3dd` — sandbox: answer child resource requests immediately from available slack
- **Verdict:** **S — SUPERSEDED by row #25.** A ledger consolidation
  candidate, **not** an independently revertable code-removal candidate.
- **Drop-when (historical):** upstream grants resource requests from slack
  without a config re-application (or an operator-less auto-answer mode) —
  this intent is now carried by **row #25**, which replaced the mechanism.
- **Dependencies:** `342900343b` (row #25) rewrote the same function,
  `Sandbox::Child::resource_request`, in place: row #25's cells record that
  "the immediate-grant shortcut is replaced by the same accounting the
  config-driven path uses" and that row #21's `child_pd.transfer_quota(...)`
  failed deterministically yet "reported success anyway". Reverting
  `10d172b3dd` alone is therefore not possible against current code.
- **Removal verification:** none applies to row #21 alone. If the combined
  #21+#25 mechanism is ever dropped (row #25's drop-when), the verification
  is row #25's set **plus** `run/sponge-launch-order-regression.run` (the
  regression row #21 was written for). Ledger consolidation (merging this
  row's history into #25 or marking it superseded) is a **documentation-only**
  correction — see Part D.

### Row 22 — `f16ad10cba` — kernel-sel4-pc.mk: restore the `CONFIG_DEBUG_BUILD` gen_config append
- **Verdict:** K-U (upstream build-defect fix; preserve-first).
- **Drop-when:** upstream Genode restores the debug `gen_config` append for
  seL4 16 (or documents the replacement mechanism).
- **Dependencies:** **load-bearing for row #12** and every serial diagnostic
  (`seL4_DebugPutChar` is gated on `CONFIG_DEBUG_BUILD`; the row's why cell
  records the dependency). Never drop #22 while #12 or the debug paths
  remain.
- **Removal verification (named):** from-scratch kernel configure —
  `rm -rf var/libcache/kernel-sel4-pc` then `./tool/build run
  sponge-desktop-disk-nvme`: `gen_config/kernel/gen_config.h` must carry the
  define and base-sel4 must compile.

### Row 23 — `1c2dd297aa` — qt6 QPA: coalesce frames against the compositor
- **Verdict:** K-U (Qt6 fix; preserve-first). Current revision supersedes the
  inert blocking-wait revision `d1a11e1e25` (guard inverted); the corrected
  blocking wait was a measured 24x regression and was rejected — **do not
  reintroduce a wait**.
- **Drop-when:** upstream Genode's qt6 port registers the framebuffer sync in
  the QPA platform plugin (or replaces `refresh()` with a vsync-paced
  submit).
- **Dependencies:** the fork-side build wiring `87c8ebf33f` (unledgered —
  Part B) applies `docs/patches/qt6-qpa-frame-sync.patch` via the
  `qpa_frame_sync_patched.tag` rule in
  `genode/repos/libports/src/qt6/base/target.mk`. Drop both together.
  `qt6_base` is a fetched contrib port (`ports/qt6_base.port` @
  `issue5873` / `67348e71a70`) — the patch content is not vendored-repo
  state.
- **Removal verification (named):** `run/sponge-debug-scrollstorm.run`
  (A/B arms; the rejected wait's 2657 ms gap must stay absent) and the
  non-regression set `sponge-de-test`, `sponge-launch`,
  `sponge-de-sel4-interactive`, `sponge-desktop-disk`; installed-plugin
  check `nm genode/build/x86_64/qt6/base/install/qt/plugins/platforms/libqgenode.lib.so | grep begin_frame`.
  Open half of the row (keep until measured, docs/17 §5 register): the
  real-hardware magnitude on the LG gram 17ZD90N at 2560x1600.

### Row 24 — `f44bc47ee5` — init: bound automatic heartbeat restarts with a per-child budget
- **Verdict:** K-U (upstream defect fix on top of upstream `0b36aa771d`
  / genodelabs/genode#5718, which ships no budget and no backoff).
- **Drop-when:** upstream adds an attempt budget / backoff to the heartbeat
  restart path; then drop, remove `restart_budget` from the six `run/` files
  that set it, and re-verify. If upstream makes the restart path
  non-repeating by default, drop outright.
- **Dependencies:** pairs with row #26 (same heartbeat surface); the
  product scenarios' `restart_budget="3"` attributes are consumer-side
  cleanup that belongs to the drop.
- **Removal verification (named):** `run/sponge-containment-churn.run` must
  still bound the loop under the upstream replacement (the row's red→green
  was 48 unbounded respawns → exactly 4 + exhaustion warning); regressions
  `sponge-desktop-disk`, `sponge-desktop-disk-uefi-usb`, `sponge-pkg-driver`,
  `sponge-supervisor-takeover`, `sponge-pkg-lifecycle`, `sponge-minimal`.

### Row 25 — `342900343b` — sandbox: a deferred quota request must never be answered falsely + bound the upgrade retry loops
- **Verdict:** K-U (upstream defect fix; preserve-first). Absorbs row #21's
  intent with corrected accounting.
- **Drop-when:** upstream either answers `Parent::upgrade` with an explicit
  refusal (a fifth `Upgrade_result`), or bounds the retry loops, or both,
  **and** `Sandbox::Child::resource_request` raises the assigned quota
  rather than transferring out of the child's own leftover. On upstream
  release: drop this row, revert `342900343b`, re-verify.
- **Dependencies:** supersedes row #21 (same `resource_request` function);
  `Genode::Parent::UPGRADE_ATTEMPTS` is shared by four retry loops across
  `base/`, `libports/libc`, and `os/` (all in this commit).
- **Removal verification (named):** `run/sponge-quota-deadlock.run` must
  still "refuse truthfully and boundedly" (the row's red→green was 3701
  false grants → 0); regressions `sponge-minimal`, `sponge-desktop-disk`,
  `sponge-pkg-driver`, `sponge-supervisor-takeover`
  (`docs/evidence/sponge-quota-deadlock-regressions.md`). If dropping #21
  and #25 together, add `run/sponge-launch-order-regression.run`.

### Row 26 — `dda40f3a87` — init: a child that has never answered a heartbeat is STARTING, not wedged
- **Verdict:** K-U (upstream defect fix; preserve-first).
- **Drop-when:** upstream distinguishes a first construct from a wedge (a
  heartbeat answered before the constructor returns, or a startup phase in
  the sandbox `Child`), or grows `<heartbeat>` with an explicit startup
  window; then drop, revert `dda40f3a87`, and re-verify.
- **Dependencies:** pairs with row #24 (same `<heartbeat>` surface;
  `init.hsd` documents both `restart_budget` and `startup_grace_ms`).
- **Removal verification (named):** `run/sponge-startup-grace.run` with
  `SPONGE_STARTUP_GRACE_MS=0` must still reproduce the churn it was written
  for (20 s construct → kill/respawn loop). Evidence:
  `docs/evidence/startup-grace.md`.

### Row 27 — `20d0ff31d8` — base-sel4: a device untyped keeps its free index (mid-untyped BAR mapped wrong)
- **Verdict:** K-U (seL4 fix; preserve-first). **Builds on row #13.**
- **Drop-when:** upstream seL4 keeps a device untyped's free index across the
  loss of its last child (or grows `get_high_mmio_untyped_cap(phys, size)`);
  then drop, delete `genode/repos/base-sel4/patches/sel4-device-untyped-ladder.patch`,
  re-run `genode/tool/ports/update_hash sel4`, and re-verify.
- **Dependencies:** row #13 (its 64 GiB grid covers row #13's HIGH device
  region and refactors the same `create_untypeds_for_region()` path — drop
  #27 before #13, never #13 alone) and row #14 (the core-side high-phys
  fast-forward whose throwaway frames the watermark bug defeated). Rides the
  same port-patch auto-apply mechanism; its own commit re-stamped
  `sel4.hash` (`fcfb8bdd…` → `28c4a439…`). Residual limitation recorded in
  the row: one window per device untyped, monotonic watermark.
- **Removal verification (named):** `tool/hwrepro --case midwindow`
  (before the fix it reproduced the field signature verbatim: "xHCI HW did
  not halt … status = 0x0", "Host halt failed, -110", supervisor
  kill-loop); regressions `sponge-minimal`, `sponge-desktop-disk`
  (alpha-probe PASS), `sponge-desktop-disk-uefi-usb`,
  `sponge-high-phys-probe`. Evidence:
  `docs/evidence/xhci-high-bar-mapping.md`.

### Row 28 — `1672f3649a` — a never-answered quota upgrade must not park the container's entrypoint; teardown must not free held sessions
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream grows a non-blocking upgrade/close variant for
  containers plus a per-session-operation deadline in `Genode::Child`; then
  drop, re-verify, and revert **both** commits `ea04b06868` and
  `1672f3649a`.
- **Dependencies:** the root of the forwarded-session cluster: rows #29,
  #30, #31 and unledgered `73e5859da8` all build on its non-blocking
  machinery (`Env::try_*`, the pending-operation registry,
  `pending_session_op.h`). The earlier attempt `500cb2cc8a` was reverted
  (`3df26cf22b`, net-zero — Part B).
- **Removal verification (named, post-gate only):** `run/sponge-upgrade-wedge.run`,
  `run/sponge-pkg-driver.run`, `run/sponge-supervisor-takeover.run`.
  Revert order is reverse-application (Part C).

### Row 29 — `0a329d4fff` — keep the `Genode::Env` vtable layout ABI-stable for prebuilt components
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream grows an asynchronous Parent protocol (or the
  non-blocking variants move into a separate interface) so the layout of
  `Genode::Env` never changes again; then drop, re-order the virtuals per
  upstream, and re-verify.
- **Dependencies:** exists only to repair the vtable break row #28
  introduced (`Env::close` `vptr+0x68` → `+0x70` shifted every later slot;
  the qmake build gained base-header stamp dependencies in this commit).
  **Never drop #28 while #29 stands, or #29 before #28 goes.**
- **Removal verification (named, post-gate only):** `run/sponge-desktop-disk`,
  `run/sponge-supervisor-takeover`, `run/sponge-pkg-driver`.

### Row 30 — `74c84f9f0a` — a forwarded close must complete when the parent answers
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream grows an asynchronous Parent protocol (or a
  parent-side notification for a forwarded close); then drop, re-verify, and
  revert `74c84f9f0a` (which depends on row #28's non-blocking machinery).
- **Dependencies:** row #28 (non-blocking machinery), the completion tick of
  unledgered `73e5859da8`'s family (50 ms `Pending_op_timer`; the
  heartbeat-period completion this commit replaced). Evidence:
  `docs/evidence/forwarded-close-starvation.md` (defect measured at
  `ea04b06868` → `73e5859da8`, fixed by `74c84f9f0a`).
- **Removal verification (named, post-gate only):** `run/sponge-desktop-disk`,
  `run/sponge-upgrade-wedge`, `run/sponge-supervisor-takeover`,
  `run/sponge-pkg-driver`, `run/sponge-desktop-disk-uefi-usb`.
  **Cluster warning:** the row's revert list is `74c84f9f0a` only; a real
  cluster drop must also revert `73e5859da8` (Part C).

### Row 31 — `82f0143d1f` — a refused create must be resolved; the environment sessions must recover from it
- **Verdict:** **R-T** — reassess-after-topology. Not a pre-approved removal.
- **Drop-when:** upstream grows an asynchronous Parent protocol for the
  CREATE (or a parent-side notification for a forwarded create); then drop,
  re-verify, and revert `e0c2706225`, `205b84fcad` and `82f0143d1f`.
- **Dependencies:** row #28's non-blocking machinery and the
  `complete_create()` probe/timer completion path (see the wording note in
  Part D). Quota-erosion recovery (`Local_connection_base::reissue`) is
  part of the commit set. Evidence:
  `docs/evidence/forwarded-create-starvation.md`.
- **Removal verification (named, post-gate only):** `run/sponge-desktop-disk`,
  `run/sponge-upgrade-wedge`, `run/sponge-supervisor-takeover`,
  `run/sponge-pkg-driver`, `run/sponge-desktop-disk-uefi-usb`.
  **Cluster warning:** the row's revert list omits `73e5859da8`; include it
  (Part C).

---

## 7. Part B — unledgered fork-local commits and the net-zero revert pair

These commits are fork-local (on top of upstream 26.08 in the `genode/`
submodule) but are not named in the ledger's Commit column. Two of them are
bookkeeping, one is a coverage gap in the ledger; all are recorded here so
cluster accounting is complete.

### `d60ebb3f92` — feat(genode): sel4: bump seL4 kernel 13.0.0 → 16.0.0 (upstream pin override)
- **Verdict:** K-I (preserve-first seL4 infrastructure).
- **What it is (git):** `sel4.port` pin override to 16.0.0 (`6e7c3b733`),
  `sel4.hash` re-stamp, rebase of the fork's `intel_vmx_disable_vpid.patch`
  / `intel_vmx_full_state.patch` onto 16.0.0, plus a `vm_space.cc` adjust.
  The commit message records the upstream fixes picked up (e.g.
  `4018ad594` "boot: fix init_freemem skipping reserved regions" — the
  row-11 bug class; the leak shrinks 9 → 3 artifacts/boot).
- **Drop-when:** upstream Genode pins seL4 16 (or newer) itself — the
  override hunk becomes redundant (replace-with-upstream; keep the
  rebased patches as long as they are still needed).
- **Dependencies:** **foundational for every seL4 kernel patch**: rows #11,
  #12, #13, #27 are written against seL4 16.0.0 sources. Never revert this
  bump without re-validating the whole `repos/base-sel4/patches/` set.
- **Removal verification (derived):** `prepare_port sel4` applies the full
  patch set against the upstream pin; `run/sponge-minimal` boots
  base-sel4; the seL4 cluster scenarios of rows #11/#12/#13/#27 stay green.

### `aaa279e1cc` — fix(genode): sel4: raise aggregate vm-leaf 16K-pool budget for 26.08 mapping sets
- **Verdict:** K-I (preserve-first seL4 core fix).
- **What it is (git):** `repos/base-sel4/src/core/spec/x86_64/platform.cc`,
  +10/−1 — the vm-leaf 16K-pool budget sized for the 26.08 mapping sets.
- **Drop-when:** the same family as rows #6/#7 (upstream resolves
  `platform.cc:108` `XXX` / dynamic growth so the fixed budget is
  unnecessary).
- **Dependencies:** rows #6/#7/#14/#15 cluster (same subsystem; the lazy
  growth paths this budget feeds).
- **Removal verification (derived):** the c1–c4 cluster regression
  (mapping-set pressure; `docs/evidence/c2-lazy-vmspace.log` family) and
  `sponge-desktop-disk`.

### `a9ee2bcde3` — build(genode): migrate VFS clients to the 26.08 API (boot_fb)
- **Verdict:** K-I (26.08 build-compatibility; not a defect fix).
- **What it is (git):** `repos/os/src/driver/framebuffer/boot/main.cc`,
  +7 — `boot_fb` reads its initial config ROM at startup
  (`_handle_config()` before the signal wiring), so the heartbeat flag and
  initial `period_ms` take effect at boot under the 26.08 API.
- **Drop-when:** none independent — it is required by the 26.08 vendoring
  itself and reverts only with a future upstream-version bump that changes
  the contract again.
- **Dependencies:** the boot_fb chain (row #12's console surfaces through
  boot_fb's Capture session; the early-fb evidence exercises it).
- **Removal verification (derived):** boot_fb builds and the
  `sponge-desktop-disk-uefi` first-paint path still shows the boot log.

### `e16699624d` — feat(base-sel4): auto-apply the three Sponge seL4 kernel patches on `prepare_port`
- **Verdict:** K-I (patch-set infrastructure).
- **What it is (git):** adds `sel4-reserved-extent-export.patch`,
  `sel4-early-fb-console.patch`, `sel4-device-untyped-coverage.patch` to
  `genode/repos/base-sel4/patches/`, where `sel4.port`'s
  `PATCHES := $(sort $(wildcard $(REP_DIR)/patches/*.patch))` and
  `install.mk`'s `patch -p0` apply them on every `prepare_port sel4`.
  Mechanism record: `docs/evidence/2026-10-19-sel4-patch-autoapply.md`
  (the 2026-10-18 `genode/contrib/` re-fetch dropped rows #11/#12/#13 and
  this commit made that class of loss impossible).
- **Drop-when:** when the last seL4 kernel patch (rows #11/#12/#13/#27) is
  dropped and `repos/base-sel4/patches/` holds no Sponge patches any more,
  the wildcard is inert and this commit's files go with their rows.
- **Dependencies:** rows #11/#12/#13 ride it directly; row #27's patch file
  was added into the same directory afterwards.
- **Removal verification (named):** the round-trip the auto-apply evidence
  records — fresh `prepare_port sel4` + kernel rebuild from scratch, then
  `run/sponge-desktop-disk-uefi-usb` and `run/sponge-minimal`.

### `57075c4d26` — chore(base-sel4): re-stamp the `sel4` port hash for the new patch set
- **Verdict:** K-I (bookkeeping; port fingerprint).
- **What it is (git):** `repos/base-sel4/ports/sel4.hash`, 1 line — the
  expected consequence of editing a port's patch set (the row-11 note in
  the ledger records that this is reviewable, not a fingerprint
  weakening). Later re-stamped again by `20d0ff31d8` (row #27) to
  `28c4a439…`, the current stamp.
- **Drop-when:** none — the hash tracks the patch set; it is changed only
  with the patch-set changes it measures.
- **Dependencies:** `e16699624d` (and `20d0ff31d8`).
- **Removal verification (derived):** `prepare_port sel4` hash check passes
  for the current patch set.

### `73e5859da8` — fix(base): a forwarded session request must not park the container either
- **Verdict:** **R-T** — reassess-after-topology. **Unledgered cluster
  member of the forwarded-session family (rows #28–#31).**
- **What it is (git):** `base/include/base/service.h`, `base/src/lib/base/child.cc`,
  `base/src/lib/base/component.cc` — +75/−3; it extends the row-#28
  non-blocking forwarding to the forwarded *session request* path (between
  row #29's `0a329d4fff` and row #30's `74c84f9f0a` in history).
  Measured contexts name it: `docs/evidence/forwarded-close-starvation.md`
  ("Fork commits: `ea04b06868` → `73e5859da8` (defect), `74c84f9f0a` (fix)"
  — the close defect was *measured* at this tree state) and
  `docs/evidence/sponge-upgrade-wedge.md` ("The forwarded CREATEs starved
  (fork commit `73e5859da8`)").
- **Drop-when:** the same upstream condition as rows #28/#30/#31
  (asynchronous Parent protocol for forwarded operations).
- **Dependencies:** row #28 (its machinery) and the row #30/#31 completion
  path. **It is named in no ledger row's revert list — see Part C; any
  cluster drop must include it.**
- **Removal verification (named, post-gate only):** the row #28–#31 set —
  `run/sponge-upgrade-wedge`, `run/sponge-pkg-driver`,
  `run/sponge-supervisor-takeover`, `run/sponge-desktop-disk`,
  `run/sponge-desktop-disk-uefi-usb` — plus the forwarded-close/create
  measurements rerun at the pre-cluster tree.

### `87c8ebf33f` — vendor(qt6): pace QPA genode frames against the compositor ("ledger row #23 code")
- **Verdict:** K-I (Qt6 build wiring; ledger coverage gap).
- **What it is (git):** `genode/repos/libports/src/qt6/base/target.mk`,
  +48/−1 — the `qpa_frame_sync_patched.tag` rule (idempotent via a reverse
  dry-run; partial application fails loudly) that applies
  `docs/patches/qt6-qpa-frame-sync.patch` at build time. The ledger's row
  #23 Commit column names the **main-repository** commit `1c2dd297aa`
  ("coalesce QPA genode frames instead of waiting for them", which carries
  the patch content, `sponge_debug` telemetry and the scrollstorm scenario
  arms) — `tool/patches` resolves it there via its main-repo fallback. The
  fork-side wiring commit is unnamed in the ledger.
- **Drop-when:** exactly row #23's (upstream registers the sync) — the tag
  rule and the patch file drop together.
- **Dependencies:** row #23 (content) / `1c2dd297aa` (durable record).
- **Removal verification (named):** row #23's set —
  `run/sponge-debug-scrollstorm.run` + the non-regression quartet + the
  `nm … libqgenode.lib.so | grep begin_frame` installed-plugin check.

### `500cb2cc8a` + `3df26cf22b` — the net-zero revert pair (first row-#28 attempt)
- **Verdict:** **R-ONLY** — record-only; there is nothing to remove.
- **What it is (git + evidence):** `500cb2cc8a` was the first attempt at the
  row-#28 fix (non-blocking upgrade/close; `env.h`, `service.h`,
  `component.cc`, libc `env.h`). It fixed the wedge but **broke the e1000
  path** — measured in `docs/evidence/sponge-upgrade-wedge.md` §3: with the
  rejected patch `sponge-pkg-driver` and `sponge-supervisor-takeover` FAIL
  (no e1000 banner, gates time out) while `sponge-upgrade-wedge` goes
  ALIVE. It was reverted the same day by `3df26cf22b`, which restores the
  tree byte-for-byte (the 63-insertion commit and its 63-deletion revert —
  **net zero**).
  The evidence record already states the convention: "No vendored patch is
  shipped for this defect, and there is no ledger row for it (AGENTS.md
  5.2: a reverted patch loses its row)." The shipped fix is row #28
  (`ea04b06868` + `1672f3649a`).
- **Drop-when:** none — a no-op pair stays in history as the negative
  result it is; do not squash or drop it (it is the proof that the naive
  non-blocking close regresses the driver chain).
- **Dependencies:** none in the tree; historically the rejected precursor
  of row #28.
- **Removal verification:** n/a. If ever questioned:
  `git diff 500cb2cc8a^ 3df26cf22b` is empty and
  `run/sponge-upgrade-wedge.run` on that tree reproduces the parked
  entrypoint (0 witness rounds) exactly as §3 of the evidence record shows.

---

## 8. Part C — cluster accounting and dependency / revert-order warnings

### C1. The forwarded-session cluster (rows #28–#31 + unledgered `73e5859da8`)

Application order in the fork (verified in `git log`):

```
500cb2cc8a  (attempt 1, later reverted)      ─┐ net-zero pair
3df26cf22b  (revert of attempt 1)            ─┘
ea04b06868  row 28 part 1 (non-blocking upgrade/close)
1672f3649a  row 28 part 2 (teardown invariant)
0a329d4fff  row 29 (Env vtable ABI stability)
73e5859da8  UNLEDGERED (forwarded session request)
74c84f9f0a  row 30 (forwarded close completion)
e0c2706225  row 31 part 1 (non-blocking create + complete_create hook)
205b84fcad  row 31 part 2 (refused-create resolution, quota erosion)
82f0143d1f  row 31 part 3 (re-issued env session lifetime)
```

**Revert-order warning.** If the cluster is ever dropped (only after the
topology gates, and only when the upstream drop-when is met), revert in
**reverse order** and **include `73e5859da8`**:

```
82f0143d1f → 205b84fcad → e0c2706225 → 74c84f9f0a → 73e5859da8
→ 0a329d4fff → 1672f3649a → ea04b06868
```

The union of the revert lists written in rows #28, #30 and #31 names
`ea04b06868`, `1672f3649a`, `74c84f9f0a`, `e0c2706225`, `205b84fcad`,
`82f0143d1f` and `0a329d4fff` (implicitly, as its own row) — **but not
`73e5859da8`**. Omitting it leaves a half-reverted non-blocking session
path in `service.h`/`component.cc` with no supporting registry semantics.
This gap is the reason Part D lists a revert-list correction as a pending
documentation-only fix.

Further order rules for this cluster:
- Row #29 exists only to repair the vtable break row #28 introduced. Drop
  #28 only together with #29, and never leave #29 standing alone.
- Rows #30 and #31 both state a dependency on row #28's non-blocking
  machinery; row #31's drop-when additionally names the `complete_create()`
  hook (attribution note in Part D).
- The cluster's runtime evidence (`forwarded-close-starvation.md`,
  `forwarded-create-starvation.md`, `sponge-upgrade-wedge.md`) is the
  measurement basis for any future drop — the five named scenarios in
  rows #28–#31 are the acceptance set.

### C2. The seL4 kernel-patch cluster (rows #11, #12, #13, #27 + unledgered `d60ebb3f92`, `e16699624d`, `57075c4d26`)

- All four kernel patches live as port patches under
  `genode/repos/base-sel4/patches/` and are auto-applied by
  `prepare_port sel4` (`e16699624d`); each removal is: delete the row's
  `.patch` file, re-run `genode/tool/ports/update_hash sel4`
  (`57075c4d26`'s successor stamp), and pass the row's named scenarios.
- **Order:** drop row #27 before row #13 (the #27 grid covers #13's HIGH
  region and refactors the same `create_untypeds_for_region()` path —
  #27 builds on #13). Never drop #13 while #27 remains.
- Row #22 (`CONFIG_DEBUG_BUILD`) is a *precondition* of row #12 and every
  serial diagnostic; drop #12 before #22, never the reverse.
- `d60ebb3f92` (seL4 16 pin) underpins the whole set: the patch files are
  written against 16.0.0 and the fork's `intel_vmx_*` patches were rebased
  in that commit. Reverting the pin invalidates the set.
- When the last of the four drops, `e16699624d`'s files drop with it and
  the forwarding notices in `docs/patches/` become historical (Part D).

### C3. The Qt6 cluster (rows #2, #3, #4, #23 + unledgered `87c8ebf33f`)

- Row #23's content (`docs/patches/qt6-qpa-frame-sync.patch`, main-repo
  commit `1c2dd297aa`) and `87c8ebf33f`'s `target.mk` wiring drop together;
  dropping only one leaves either a build that applies a missing patch
  (fail-loud, but broken) or a patch nothing applies.
- Row #4's host-header wrappers depend on row #5's compiler wrappers;
  rows #2/#3 are independent of #23 but share the host-tools build.
- Do not reintroduce the rejected blocking wait (`d1a11e1e25` revision) —
  measured 24x regression (row #23 cells; `sponge-debug-scrollstorm.md`).

### C4. The capability-space / memory-backing cluster (rows #6, #7, #14, #15 + unledgered `aaa279e1cc`)

- All four rows are one commit (`847483031a`): **one revert drops four
  rows** — all-or-nothing, and all four ledger rows must go in the same
  ledger edit. `aaa279e1cc` sits in the same subsystem and would be
  reviewed together.
- Row #14 consumes row #13's device untypeds and is corrected by row #27's
  watermark fix; treat #13/#14/#27 as a dependency chain in any partial
  rework (see C2).

### C5. Supersession and pairing

- **Row #21 → row #25:** #25 rewrote `Sandbox::Child::resource_request`
  in place. #21 is a ledger consolidation candidate; it is **not**
  independently revertable. Any code-level removal is a combined #21+#25
  action verified by rows #25's set **plus**
  `run/sponge-launch-order-regression.run`.
- **Rows #19/#20:** one commit (`391272b3e3`), two rows — revert/re-drop
  together; both are R-T.
- **Rows #24/#26:** paired heartbeat surface (`<heartbeat>` node;
  `init.hsd` documents both attributes). A drop of either should review the
  other; row #24's drop also removes `restart_budget` from the six `run/`
  files that set it.
- **Row #28:** two commits (`ea04b06868`, `1672f3649a`) — the row says so
  explicitly: reverting the row means reverting both.
- **Row #31:** three commits (`e0c2706225`, `205b84fcad`, `82f0143d1f`) —
  the row says so explicitly.

### C6. Generic removal protocol (control escape hatch)

Per `docs/11-environment.md` §4/§4.1 and AGENTS.md §5.2: `tool/patches drop
<n>` prints the `git revert <full-sha>` command and the reminder to remove
ledger row `#n`; the removal is its **own commit on `main`**, and the commit
message says so. The manual equivalent (`git revert`, ledger edit, one
commit) is documented in §4.1. Nothing auto-reverts; this document does not
change that.

---

## 9. Part D — documentation-only corrections vs actual code removal candidates

### D1. Documentation-only corrections (no runtime effect)

Each item below is ledger/doc bookkeeping. None removes code. They are the
only "removal-adjacent" actions currently justified, and each is a normal
docs commit when someone chooses to make it.

1. **Row #21 consolidation.** Mark row #21 superseded by row #25 in the
   ledger (or merge its history into #25's cells). Ledger edit only; the
   code was already rewritten by `342900343b`.
2. **Revert-list gap: `73e5859da8`.** Rows #30 and #31 name their own
   commits but the cluster's revert lists omit `73e5859da8`. Correct the
   drop-when cells (or add a cluster note) so a future drop cannot miss it.
   Ledger edit only.
3. **Row #31 `complete_create()` attribution.** Row #31's drop-when
   attributes the `complete_create()` hook to row #30; git inspection shows
   the hook introduced by `e0c2706225` (row #31's own part-1 commit — the
   symbol appears in that commit's `service.h` diff and in none of
   `74c84f9f0a`'s). Wording correction only; the dependency direction
   (rows #30/#31 both sit on row #28's machinery) is unaffected.
4. **Row #23 commit-column coverage.** Row #23's Commit column names the
   main-repo commit `1c2dd297aa`; the fork-side wiring `87c8ebf33f` is
   unnamed in the ledger. Either add it to the row's cells or note the
   two-repo scope. Ledger edit only.
5. **`docs/patches/` forwarding notices.** The four `sel4-*.patch` files
   are already forwarding notices pointing at the fork's port patches; they
   exist so the pre-2026-10-19 locations remain findable. Keeping their
   text accurate is doc-only; deleting them would also be doc-only but is
   not recommended while the history they forward from is referenced.
6. **§4 preamble wording.** The "dropped by re-running `git subtree pull`"
   phrase predates the 2026-10-17 submodule split (AGENTS.md §5.2 history
   note). A future ledger edit can modernize it to the submodule/pin-bump
   flow. Doc-only.

### D2. Actual code removal candidates

**None.** No entry's drop-when condition is met by current upstream state
(the ledger's drop-when cells are all conditional on upstream absorptions
that have not landed; row #5 is "Never"; the R-T set is gated on the
topology reassessment). Per the zero-removals policy (Part 3), the correct
and expected outcome of this classification is **remove nothing**.

Every future code removal must satisfy, in order:
1. the row's drop-when condition is **met** (upstream absorption or the
   row's named Sponge-side condition),
2. the row's exact removal-verification scenario(s) pass at the post-drop
   tree,
3. cluster rules of Part C are respected (ordering, multi-commit rows,
   `73e5859da8` in the forwarded-session cluster),
4. the drop is its own commit on `main` that removes the ledger row and
   says so (AGENTS.md §5.2).

---

## 10. Part E — reassessment gates and open items

- **Nested/forwarded-session rows #19, #20, #28, #29, #30, #31 and
  unledgered `73e5859da8` are R-T.** The architecture refactor will
  reassess the nested-session patches **only after the topology gates are
  green** — the product-topology structural gates of the runtime component
  topology (`docs/17-runtime-component-topology.md`; the class of gate the
  row-#11 verification records as "3/3 structural gates" on
  `sponge-desktop-disk-uefi-usb`). Until that gate set is green on the
  refactor, these entries are **not** pre-approved removals and their
  verdict is KEEP (R-T). This document is the durable record of that
  decision.
- **Row #23 open half:** the real-hardware coalescing magnitude on the LG
  gram 17ZD90N at 2560x1600 remains unmeasured (registered in
  `docs/17-runtime-component-topology.md` §5). The row is kept regardless;
  the gap does not authorize removal.
- **Row #13 open item:** the TODO-19 honest-register row continues to track
  the real-hardware diagnostic (row #13 cells).
- **Row #27 residual limitation:** one device window per untyped
  (monotonic watermark) is *not* removed by the row and remains a known
  bound (row #27 cells).

---

## 11. Bottom line

- 31 ledger rows and 8 unledgered fork-local history entries (7 commits +
  the net-zero revert pair) are classified above with verdict, drop-when,
  dependencies, and exact removal verification.
- **Preserved first:** every seL4 fix (rows #6/#7/#11–#15/#22/#27 and
  `d60ebb3f92`/`aaa279e1cc`/`e16699624d`/`57075c4d26`), every Qt6 fix
  (rows #2/#3/#4/#23 and `87c8ebf33f`), and every upstream defect fix
  (rows #1/#8/#9/#10/#16/#18/#24/#25/#26) remain KEEP until their stated
  upstream drop-when is met.
- **Row #21 is superseded by #25** — a ledger consolidation candidate, not
  a code-removal candidate. **Row #27 builds on #13** — drop order matters.
  **`73e5859da8` is in the cluster accounting** and in the revert order of
  C1, even though no ledger row names it.
- **Rows #19/#20/#28–#31 are reassess-after-topology**, not pre-approved
  removals.
- **Documentation-only corrections (Part D1) are distinct from code
  removal candidates (Part D2).** The code-removal candidate set is
  currently **empty**: zero removals is the valid outcome.
