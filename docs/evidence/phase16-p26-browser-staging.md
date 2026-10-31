# Phase 16 P2.6 — Browser Payload Serving & Real-Launcher Acceptance

Date: 2026-10-31. All results are QEMU/seL4 (`KERNEL=sel4 BOARD=pc`)
receipts; no real-hardware claim is made. No commits were made.

## 1. Outcome summary

| P2.6 deliverable | status |
|---|---|
| rom_pkg payload serving wired into all five desktop-disk variants | **delivered** (shared `run/runtime-browser.inc` fragments; uefi-usb Tier-0 output byte-identical) |
| structural assertions for the wiring | **delivered** (falkon rules before `.lib.so` resolution; rom_pkg node after rom_lib; vfs policy present) |
| grouped probe browser leg (real launcher, first paint + typed input) | **delivered and armed** (red without wiring, wired, then blocked — see §4) |
| staged closure + new-media P3 size/margin measured | **delivered** (§5) |
| Nic/Rtc provisioning for falkon startup | **delivered** (§6, BIOS acceptance scenario only) |
| BIOS `SPONGE_GROUPED_UI=on` reaches falkon first paint + input | **resolved later** — the §4 ABI blocker was lifted by `phase16-p26-payload-rebuild.md` + the `vfs_qtwebengine_shm` source port (`phase16-p27-qtwebengine-shm-port.md`), and the remaining placement/input items are closed in `phase16-p26-browser-acceptance.md` (first paint + typed input GREEN). The original record follows: **BLOCKED** — precise evidence-backed blocker outside P2.6's lane (§4): the prebuilt 2026-04 depot closure references a `Genode::Timeout` API that upstream removed from the ld.so export map, so falkon aborts at its first lazy PLT bind. The ROM-create stall did **not** reproduce on BIOS: the rom_pkg serving path works. |

## 2. What landed (files)

- `repos/sponge/src/test/grouped_ui_probe/main.cc` — `st_browser`
  class-state ROM + precondition check, the browser leg after terminal
  (`pkg_runtime_browser -> falkon`, same first-paint predicate and the
  same `TYPED_FLOOR` / `2x baseline` thresholds; only the first-paint
  poll budget is longer: `PAINT_ITERS_BROWSER` 3000 vs 1500 iterations
  of 200 ms). The simple and terminal legs and every threshold are
  unchanged. Compiles clean (`make ... test/grouped_ui_probe`).
- `run/sponge-desktop-disk.run` — grouped arm gains the
  `grouped_ui_probe -> st_browser` relay policy + ROM route and the
  browser host gates (first paint 600 s, typed delta 180 s); the
  Tier-0 config gains the `[runtime_grouped::falkon_rom_rules]` /
  `[runtime_browser::vfs_policy]` / `[runtime_browser::rom_pkg_node
  vfs]` wiring (subst idiom), the `runtime_grouped::assert_tier0` +
  `runtime_browser::assert_tier0` calls, and the falkon-startup
  Nic/Rtc stack (§6).
- `run/sponge-desktop-disk-{nvme,uefi,uefi-nvme}.run` — the same
  wiring + assertions (same shared fragments).
- `run/sponge-desktop-disk-uefi-usb.run` — its two inline literals
  replaced by the shared calls; **the built Tier-0 config is
  byte-identical to the pre-edit build (19,416 bytes)** — proven by
  `tclsh /tmp/opencode/p26-uefiusb-identity.tcl` (builds old (git HEAD)
  and new literals through `supervisor::tier0 -> subst ->
  driver_runtime::expand` and diffs).
- `run/runtime-browser.inc` — shared `vfs_policy` /
  `rom_pkg_node <fs-child>` fragments (byte-for-byte the former
  uefi-usb literal: `root: /system/pkg/falkon/payload`,
  `writeable: no`, `caps: 20000 | ram: 768M`) + `assert_tier0`.
  Byte-identity checked by `tclsh /tmp/opencode/p26-frag-check.tcl`.
- `run/runtime-grouped.inc` — `assert_tier0` now also asserts the
  falkon rules precede the `label_suffix: .lib.so -> rom_lib` rule.
- `docs/08-development.md` — stale "browser first paint is P2.6 scope,
  not asserted" clause synced to the code.

Structural verification of all five variants (each variant's exact
config built through the exact scenario pipeline, then the two
assertions + `tool/hid format` schema check):
`tclsh /tmp/opencode/p26-four-variants.tcl` — all five pass.

## 3. Red receipt (the browser leg detects the missing wiring)

Run: `SPONGE_GROUPED_UI=on ... run/sponge-desktop-disk` without
rom_pkg wiring. Log: `/tmp/opencode/p26-runA2-red.log`.

- real launcher click reached falkon:
  `sponge-de: launcher click-to-launch 'falkon'` →
  `sponge_pkgd: launch result falkon -> ok (channel=launcher)`;
- the binary request fell through to rom_sys and failed fast:
  `[init -> rom_sys] Error: /bin/falkon not found` (the falkon binary
  lives only in the payload area by design);
- `grouped-ui-probe: diag st_browser child falkon bin=bin/falkon` and
  `diag first-paint layer=never-registered` — the leg fails loudly at
  the browser first-paint gate.

This is the expected negative control for the wiring.

## 4. Wired run: rom_pkg works; falkon blocked by a prebuilt-closure ABI drift

Run: `SPONGE_GROUPED_UI=on ... run/sponge-desktop-disk` with the
wiring + Nic/Rtc. Log: `/tmp/opencode/p26-runB.log`.

**The P2.6 serving path is proven working on BIOS** — no ROM-create
stall reproduced:

- the structural assertions print at scenario setup
  (`runtime_grouped: falkon rom_pkg routing verified (pkg_runtime_browser
  scoped, before .lib.so resolution)` +
  `runtime_browser: rom_pkg payload serving verified (root
  /system/pkg/falkon/payload, File_system child 'vfs')`);
- falkon's start node is created in the browser class runtime
  (`diag st_browser child falkon bin=bin/falkon`);
- ldso resolves and loads the payload closure from rom_pkg — the
  eight `Reloc_non_plt(libQt6WebEngineCore.lib.so): symbol not found:
  __gcov_dump / OPENSSL_memory_* / ZSTD_trace_* / sdallocx` lines are
  the known-benign optional-symbol warnings also present in every
  successful falkon proof, and they prove the 237 MiB
  `libQt6WebEngineCore.lib.so` was **served by rom_pkg and relocated**;
- every ROM create was answered (no "create of ROM ... provider did
  not answer" line anywhere in the log).

**Then falkon aborts before first paint:**

```
[init -> system -> pkg_runtime_browser -> falkon] Error: LD: jump slot
    relocation failed for symbol: 'Genode::Timeout::Timeout(Timer::Connection&)'
[init -> system -> pkg_runtime_browser -> falkon] Error: Uncaught exception
    of type 'Linker::Not_found'
[init -> system -> pkg_runtime_browser -> falkon] Warning: abort called
```

The probe's browser leg then reports `first-paint layer=never-registered`
(its bounded poll; the run's outer budget ends before the FAIL line —
the abort makes any later first paint impossible).

### 4.1 Root cause (fully evidenced)

1. The falkon closure is the prebuilt depot package
   `cproc/pkg/falkon_qt6-jemalloc/2026-04-22`
   (`pkg/falkon/payload/`, mtime 2026-04-21). Its `libc.lib.so` and
   `vfs_lwip.lib.so` carry `R_X86_64_JUMP_SLOT` relocations against
   **`Genode::Timeout::Timeout(Timer::Connection&)`** (the pre-refactor
   one-arg constructor) plus the two-arg
   `Timeout::schedule_one_shot/periodic(Microseconds, Timeout_handler&)`
   — verified with `readelf -rW` / `nm -D` on the payload files.
2. Genode's ldso binds jump slots **lazily** (`Ld::jmp_slot`,
   `genode/repos/base/src/lib/ldso/main.cc:301-321`); the first call
   (inside falkon's `Component::construct`, at the vfs/socket init
   where `Timer::Connection` members construct `Timeout`s) performs the
   lookup and fails with `Linker::Not_found`.
3. Upstream commit `54e0137e00` ("timeout: set Timeout::_handler on
   construction", 2026-06-26) replaced the one-arg constructors with
   two-arg ones and **removed the one-arg exports from
   `base/lib/symbols/ld`** (the ld.so export map) — the failing symbol
   is absent from the current map.
4. Every falkon proof that reached first paint ran on the pre-split
   vendored tree that still exported it: at the rescue run's date
   (2026-08-14, commit `5d82ae54d2`) `genode/repos/base/lib/symbols/ld`
   still carried all four one-arg `Timeout` constructors (verified:
   `git show 5d82ae54d2:genode/repos/base/lib/symbols/ld`). The
   2026-10-17 `genode/` re-import onto the canonical upstream 26.08
   lineage (which contains `54e0137e00`) is what removed them.
5. The same-name survivors (`~Timeout`, `~Timeout_scheduler`,
   `discard`, `scheduled`) now bind to **new-layout** implementations
   while the depot objects embed the **old** `Timeout` layout
   (old: `Mutex + ref + period + deadline + list + handler ptrs +
   blockade`; new: `ref + period + handler ref + Constructible<Alarm>`),
   so even a symbol shim for the missing names would leave latent
   layout corruption. A shim cannot shadow the exported names anyway:
   the lookup walks the referencing object's dependency list with
   `ld.lib.so` first (`main.cc:566-610`).

### 4.2 Why this is outside P2.6's lane (and why no fix was attempted)

- `genode/` is vendored and must not be modified (AGENTS.md §5.2) —
  restoring the export map would be a base patch.
- A compat shim cannot shadow ld.so's exports and would leave the
  layout-mismatch corruption above (§4.1.5) — that would be inventing
  semantics, which the task forbids.
- Swapping the payload's `libc.lib.so`/`vfs_lwip.lib.so` for current
  tree builds is a **package-content re-import decision** (Phase 7/8
  `tool/pkg_import` maintenance of the depot closure), not P2.6's
  staging/wiring ownership, and it would change the measured closure
  of §5.
- The anticipated ROM-create stall (the known-red uefi-usb Falkon
  stage) did **not** reproduce on BIOS; the create machinery answers
  here. Nothing about this outcome calls for Genode async-semantics
  changes.

Recommended follow-up (not done here): re-import the falkon depot
closure against the 26.08-era headers (Phase 7/8 lane), or carry a
depot rebuild of `libc`/`vfs_lwip`/`vfs_oss` in the package update
flow; then the browser leg's URL-bar input geometry can be calibrated
(§7).

## 5. Measurements (fresh artifacts only; nothing resized)

Source closure `pkg/falkon/payload` and the staged copy on the fresh
RUN B media are byte-identical in apparent size:

| measurement | value |
|---|---|
| closure file count | **64 files** |
| closure size (source and staged) | **532,932,275 bytes** |
| staged P3 content total (`var/run/sponge-desktop-disk/{boot,system}`) | 718,797,698 bytes |
| fresh BIOS P3 (GENODE, sectors 4952–1440639) | **735,072,256 bytes (701.0 MiB)** |
| fresh P3 margin over staged content | **16,274,558 bytes (≈15.5 MiB)** |
| fresh disk image total (`var/run/sponge-desktop-disk.img`) | 737,628,160 bytes (P1 BIOSBOOT 50.5 KiB, P2 GRUB2 2.2 MiB) |

Tooling: `du`/`find` on the staged tree and `sgdisk -p` on the fresh
RUN B image (`genode/build/x86_64/var/run/`). The new-media P3 size is
content-sized by the run's `image/disk` (`resize2fs -M`) and
`tool/dist` reuses that image, so the fresh P3 figure **is** the
new-media P3 size — the Phase 16 criterion-13 (c) worry ("P3 has
~150 MiB free vs the WebEngine footprint") is structurally resolved:
the payload lands in a P3 sized to fit it. No installed medium was
resized or migrated.

## 6. Nic/Rtc provisioning (falkon startup needs, BIOS scenario only)

Falkon's metadata mounts `<rtc/>` (an `Rtc::Connection` at vfs mount)
and `<lwip dhcp="yes"/>` (the socket fs blocks in
`genode_socket_config_address` until DHCP completes), and the
sponge-falkon-disk evidence records falkon stopping at
"denied Rtc-session" without them — so provisioning is required for
falkon startup, and was added to the BIOS acceptance scenario only
(the other media carry the structural rom_pkg wiring; provisioning
them is a follow-up):

- Tier-0 `nic_uplink` + `nic` (binary `ipxe_nic`, dde_ipxe e1000) +
  `pc_rtc` + `system_rtc` — the `run/sponge-falkon-disk.run` proven
  stack; `-device e1000` added to the QEMU args;
- explicit system-route pins `Nic -> nic_uplink`, `Rtc -> system_rtc`,
  `Uplink -> nic_router` (nic_router also provides Nic+Uplink, and an
  `any-child` query with two providers is denied as "ambiguous routes
  to service" — `sandbox/child.cc:637-648`);
- observed up in RUN B: `[init -> nic] --- iPXE NIC driver started ---`
  and `[init -> system_rtc] Set RTC base from RTC driver to
  2026-10-31 5:19:47`. (Page-load/DHCP/HTTP assertions deliberately
  stay out of the gate.)

Known boundary: ipxe_nic and a future `vct install pc_nic` would both
claim the e1000; the Uplink pin keeps the pc_nic path deterministic
today, but two NIC stacks on one medium is a product question for a
later phase.

## 7. Terminal-leg flake root cause (RUN A; legs untouched)

Run `/tmp/opencode/p26-runA-red.log` failed at the **terminal** typed
delta (`changed=30 baseline=17` — `30 !> 2*17`) before the browser leg
ran. Root-caused, not retried blindly:

- the vendored gems terminal renders content **5 seconds** after a
  change: `genode/repos/gems/src/server/terminal/main.cc:163`
  (`_flush_delay = 5` with the comment "Time in milliseconds") fed to
  `_timer.trigger_once(1000 * _flush_delay)` (`:208`) — the factor
  makes it seconds, not milliseconds;
- the probe's terminal recipe samples its typed-delta baseline in a
  700 ms window 2.0 s after the focus click, which sits inside that
  flush jitter: whichever run-luck phase the prompt/cursor flush lands
  in, it lands in `click_changed` (P2.4 runs: baseline 0) or in the
  baseline window (this run: baseline 17) — a pre-existing fragility
  of the P2.4 recipe, quantified at roughly a 1-in-7 failure per run;
- the timeline diff against the green P2.4 log is line-for-line
  identical up to the failure (same prompt/vfs-warning positions), and
  the same code passed the terminal leg on the next run
  (`/tmp/opencode/p26-runA2-red.log`: `changed=17 baseline=0`);
- per the task constraint the simple/terminal legs and thresholds were
  **not** touched. A future fix belongs in the terminal's flush
  latency (vendored) or a longer settle window (a recipe change
  requiring a P2.4 re-acceptance).

## 8. Regressions

| regression | run | result |
|---|---|---|
| P2.4 default BIOS path (grouped off) | `/tmp/opencode/p26-runE-default.log` | `alpha-probe: PASS` + `Run script execution successful.` |
| P2.4 grouped legs (simple + terminal) | inside runA2/runB logs | green every run the terminal flake spared (typed delta + terminal execution delta pass; thresholds unchanged) |
| P2.5 `sponge-de-pkgd-failure` | `/tmp/opencode/p26-runF4-pkgdfailure.log` | `de-survival-probe: PASS` + `de-survival-probe: scenario GREEN (P2.5 mechanisms present)` (the scenario's ISO media path; `image/disk` re-runs fail in the run tool's respawn/write-lock harness race and are not a product result) |

The grouped scenario's single `grouped-ui-probe: PASS` marker cannot
print while the browser leg is blocked — that is the arm's fail-loud
design, not a regression of the P2.4 legs.

## 9. Honest boundaries

- QEMU/seL4 receipts only.
- The four non-BIOS desktop-disk variants are verified **structurally**
  (tclsh harness: config construction through the exact scenario
  pipeline + assertions + HID schema); they were not booted here.
- The uefi-usb Falkon stage known-red (ROM-create timeout,
  warning-only) was not re-run; on BIOS the same create machinery
  demonstrably answers (§4).
- The terminal flake of §7 remains in the recipe (quantified, root
  caused, arms untouched).
- The probe's browser typed-input leg is spec'd (URL-bar focus click +
  "sponge" typing over the window rect with the shared thresholds);
  its empirical geometry calibration landed in
  `phase16-p26-browser-acceptance.md` (client-pixel URL-bar
  calibration, measured from runtime evidence).

## 10. Artifacts

- logs: `/tmp/opencode/p26-runA-red.log`, `p26-runA2-red.log`,
  `p26-runB.log`, `p26-runE-default.log`, `p26-runF4-pkgdfailure.log`
- harnesses: `/tmp/opencode/p26-frag-check.tcl`,
  `p26-uefiusb-identity.tcl`, `p26-four-variants.tcl`
- fresh media: `genode/build/x86_64/var/run/sponge-desktop-disk.img`
  (737,628,160 bytes)
