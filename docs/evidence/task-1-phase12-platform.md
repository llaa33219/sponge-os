# Phase 12 W1 — Explicit platform pin + build pre-flight

- **Date:** 2026-08-08
- **Plan reference:** `docs/plans/phase12-hardware.md` §"W1: Explicit platform pin + build pre-flight" (lines 311–369)
- **W0 baseline:** GREEN (8/8 PASS — `docs/evidence/task-0-phase12-baseline.md`; markers are the trusted reference set)
- **Outcome:** **GREEN** — five disk-touching run scripts now carry an explicit q35 + `Skylake-Client` pin immediately before `-nographic -m …`; `tool/build.mojo` runs the read-only `./tool/patches verify` gate before any build-related command proceeds; the W0 storage markers remain byte-for-byte equivalent after the pin change.
- **W1 acceptance:** all four W1 acceptance criteria satisfied; no edits under `genode/`; no commits made; W2/W3/W3b/W4 work is **not** started.

## 1. `./tool/patches verify` receipt (W1 first gate)

`./tool/patches verify` was run as W1's first gate. It exited 0 with all nine ledger rows green. The receipt below is the full, unedited output (after the Mojo runtime's Crashpad init banner, which is noise from the Mojo SDK on this host — it does not affect exit codes).

```text
[sponge-patches] verify: ledger vs git reality

OK   #1 7255374d0d: exists, ancestor of HEAD
     touches:
       genode/repos/libports/ports/stdcxx.hash
       genode/repos/libports/ports/stdcxx.port
OK   #2 07c0fe3138: exists, ancestor of HEAD
     touches:
       genode/tool/tool_chain_qt6
OK   #3 a42e2d28f4: exists, ancestor of HEAD
     touches:
       genode/repos/libports/lib/import/import-qt6.inc
OK   #4 2909e8b57b: exists, ancestor of HEAD
     touches:
       genode/repos/libports/src/qt6/base/target.mk
OK   #5 4e91a4a602: exists, ancestor of HEAD
     touches:
       genode/repos/sponge
       repos/sponge/tool/genode-x86-g++-wrapper
       repos/sponge/tool/genode-x86-gcc-wrapper
OK   #6 ea8ac1343b: exists, ancestor of HEAD
     touches:
       genode/repos/base-sel4/src/core/include/cnode.h
       genode/repos/base-sel4/src/core/include/untyped_memory.h
       genode/repos/base-sel4/src/core/spec/arm/platform.cc
       genode/repos/base-sel4/src/core/spec/arm_v8a/platform.cc
       genode/repos/base-sel4/src/core/spec/x86_64/platform.cc
       genode/repos/base-sel4/src/include/base/internal/capability_space_sel4.h
OK   #7 38a19fa5d8: exists, ancestor of HEAD
     touches:
       genode/repos/base-sel4/src/core/include/vm_space.h
       genode/repos/base-sel4/src/core/platform.cc
       genode/repos/base-sel4/src/core/spec/x86_64/platform.cc
OK   #8 dc4a9f342f: exists, ancestor of HEAD
     touches:
       genode/repos/libports/ports/ncurses.hash
       genode/repos/libports/ports/ncurses.port
       genode/repos/ports/ports/bash.hash
       genode/repos/ports/ports/bash.port
OK   #9 b4aa1d774f: exists, ancestor of HEAD
     touches:
       genode/repos/os/src/server/nitpicker/user_state.cc

verify: OK (all 9 ledger rows match git reality)
```

**Verdict:** **9/9 OK, exit 0.** Risk 19 (patch-ledger rows 6/7 lost even though `pc_nic` sizing relies on them) is mitigated for W1 — the build pre-flight refuses to proceed unless all nine rows are present.

## 2. Files changed (W1 scope only)

| File | One-line change | Lines |
|---|---|---:|
| `run/sponge-boot.run` | Added `append qemu_args " -machine q35 -cpu Skylake-Client "` immediately before the existing `append qemu_args " -nographic -m 1G "` line. | +8 |
| `run/sponge-desktop-disk.run` | Added the same pin immediately before the existing `append qemu_args " -nographic -m 4G "` line (NEC xHCI / usb-tablet preserved). | +7 |
| `run/sponge-persist-disk.run` | Added the same pin immediately before the existing `append qemu_args " -nographic -m 1G "` line (no -snapshot preserved). | +7 |
| `run/sponge-falkon-disk.run` | Added the same pin immediately before the existing `append qemu_args " -nographic -m 6G "` line (NIC + xHCI + usb-tablet preserved). | +7 |
| `run/sponge-alpha.run` | Added the same pin immediately before the existing `append qemu_args " -nographic -m 2G "` line (xHCI + usb-tablet preserved). | +7 |
| `tool/build.mojo` | Added a `verify_patches_or_exit()` helper that invokes the existing read-only `./tool/patches verify` and propagates non-zero; added a `run_argv_env()` helper to set `VIRTUAL_ENV` + `PYTHONPATH` so the venv's python can find the `mojo` package when the venv python is a symlink to `/usr/bin/python3`; wired the gate as the first action of `cmd_prepare()` and (non-`--manual`) `cmd_run()`. | +111 |
| `docs/evidence/task-1-phase12-platform.md` | **NEW** — this file. | (created) |

Total: **147 insertions across 6 files** (`git diff --stat` against the W0 HEAD); no deletions; no file under `genode/` was touched; no commit was created.

### 2.1 REPOSITORIES block — `repos/pc` was already present (no edit needed)

Per the prior reviewer's note in the W1 brief: `REPOSITORIES += $(GENODE_DIR)/repos/pc` is **already** present in the marker-delimited managed block in `genode/build/x86_64/etc/build.conf` (line 158, inside the `# >>> sponge-os managed block >>>` / `# <<< sponge-os managed block <<<` range 143–169), and is positioned with the other PC driver repositories (`pc`, `dde_linux`, `dde_ipxe`, `dde_rump`). The block is wired into `tool/build.mojo`'s `ensure_build_conf()` and remains idempotent on both fresh and already-managed `build.conf`.

**No edit to `tool/build.mojo` was made for §3 of the W1 plan** — the existing block already satisfies the W1 risk-9 mitigation. The proof below demonstrates idempotency end-to-end.

#### 2.1.1 Fresh-vs-managed idempotency proof

The `genode/build/x86_64/etc/build.conf` file was SHA-256-hashed before and after a re-run of `./tool/build prepare`:

```text
SHA-256 (before prepare re-run): 0a244256a65f507f85ed054cbe2e5b86649691b53e660a655f309a97c57fcd6d
SHA-256 (after  prepare re-run): 0a244256a65f507f85ed054cbe2e5b86649691b53e660a655f309a97c57fcd6d
```

The hash is **byte-for-byte identical** after the re-run; `prepare` reported `ok: build.conf already configured (no changes needed)`. The managed block is therefore idempotent: a second invocation is a no-op on the already-managed `build.conf`. The fresh-path proof (template → managed) was exercised in earlier phases and is the path that adds the marker-delimited block once when `BLOCK_BEGIN` is absent; this proof shows the run-twice path does not duplicate.

#### 2.1.2 `repos/pc` placement check (post-pin)

```text
$ sed -n '/sponge-os managed block/,/sponge-os managed block/p' genode/build/x86_64/etc/build.conf
# >>> sponge-os managed block >>>
# Appended by 'tool/build prepare'. Delete this whole block to undo.
# KERNEL/BOARD live near the top of this file because the generated
# ifdef blocks below them read those variables early.
REPOSITORIES += $(GENODE_DIR)/repos/sponge
REPOSITORIES += $(GENODE_DIR)/repos/libports
# ports: noux packages (bash-minimal, vim-minimal) for the terminal
# package (pkg/terminal). Depends on libports (ncurses). Source-built
# only — no depot import for these (Phase 7 todo 13).
REPOSITORIES += $(GENODE_DIR)/repos/ports
REPOSITORIES += $(GENODE_DIR)/repos/gems
# pc + dde_linux: PC hardware drivers (platform/pc, vesa via libports,
# ps2, acpi, pci_decode, event_filter via os) and the DDE-Linux USB
# stack (pc_usb_host, usb_hid) used by the base-sel4 interactive GUI
# scenario (run/sponge-de-sel4-interactive.run). Harmless on base-linux.
REPOSITORIES += $(GENODE_DIR)/repos/pc
REPOSITORIES += $(GENODE_DIR)/repos/dde_linux
# dde_ipxe: iPXE-based NIC driver (ipxe_nic) for the base-sel4
# networking scenarios (run/sponge-net-probe.run). Harmless on
# base-linux (REQUIRES=x86 target built only on demand).
REPOSITORIES += $(GENODE_DIR)/repos/dde_ipxe
# dde_rump: NetBSD rump kernel for ext2/ffs/msdos/cd9660/ntfs/udf
# filesystems via the vfs_rump plugin. Used by the base-sel4 storage
# chain (run/sponge-boot.run, docs/14 §5). Harmless on base-linux.
REPOSITORIES += $(GENODE_DIR)/repos/dde_rump
MAKE += -j80
# <<< sponge-os managed block <<<
```

`repos/pc` is on the line after the `pc + dde_linux` comment and before `repos/dde_linux`, i.e. exactly where the W1 risk-9 mitigation says it should sit. The W6 fresh-build proof is the next task that needs to exercise it; W1 only needed the proof that the marker-delimited managed block is correct and idempotent, which is satisfied.

### 2.2 Run-script diff summary (one pin per file, immediately before `-nographic -m`)

```diff
# In each of the five disk-touching run scripts:
# run/sponge-boot.run, run/sponge-desktop-disk.run,
# run/sponge-persist-disk.run, run/sponge-falkon-disk.run,
# run/sponge-alpha.run

+# Phase 12 W1 (docs/plans/phase12-hardware.md W1): pin the platform
+# explicitly. PC board default already supplies -machine q35
+# -cpu Skylake-Client; this makes the contract local and guards
+# against a silent board-default regression. Existing storage/device
+# arguments and the <scenario-specific timeout> gate are preserved.
+#
+append qemu_args " -machine q35 -cpu Skylake-Client "
 append qemu_args " -nographic -m <N>G "
 # ... other storage/device args unchanged ...
```

Per-file line counts (added only, no removals):

| Run script | Pin line | Existing `-nographic -m` line | Storage/device args after pin |
|---|---|---|---|
| `run/sponge-boot.run` | `:364` | `:365` `append qemu_args " -nographic -m 1G "` | NVMe variant QEMU wiring (lines 367–371) preserved; 120 s `boot-probe` gate preserved |
| `run/sponge-desktop-disk.run` | `:922` | `:923` `append qemu_args " -nographic -m 4G "` | `append qemu_args " -device nec-usb-xhci,id=xhci -device usb-tablet "` (line 924) preserved; 900 s `alpha-probe` gate preserved |
| `run/sponge-persist-disk.run` | `:557` | `:558` `append qemu_args " -nographic -m 1G "` | image/disk auto-attach (no `-snapshot`, so P4 writes persist between boots) preserved |
| `run/sponge-falkon-disk.run` | `:916` | `:917` `append qemu_args " -nographic -m 6G "` | `append qemu_args " -device nec-usb-xhci,id=xhci -device usb-tablet "` + `append_qemu_nic_args` (lines 918–919) preserved; 900 s `falkon_probe` gate preserved |
| `run/sponge-alpha.run` | `:768` | `:769` `append qemu_args " -nographic -m 2G "` | `append qemu_args " -device nec-usb-xhci,id=xhci -device usb-tablet "` (line 770) preserved; `alpha-probe` gate preserved |

**Verification (grep-level):** `grep -n -E 'append qemu_args.*-machine q35|append qemu_args.*-nographic' run/sponge-{boot,desktop-disk,persist-disk,falkon-disk,alpha}.run` returns one `-machine q35 -cpu Skylake-Client ` line and one `-nographic -m <N>G` line per script, with the pin line always immediately preceding the `-nographic -m` line.

### 2.3 `tool/build.mojo` patch pre-flight

The new `verify_patches_or_exit(root)` helper is invoked as the first action of `cmd_prepare()` and (for the non-`--manual` path) `cmd_run()`. The helper:

1. Builds an env dict that mirrors the parent env and sets `VIRTUAL_ENV=<root>/.venv` + `PYTHONPATH=<root>/.venv/lib/python3.14/site-packages` so the child `python` (a symlink to `/usr/bin/python3`) can resolve the `mojo` package. `PYTHONHOME` is cleared to avoid stale overrides.
2. Calls `run_argv_env(["./tool/patches", "verify"], cwd=root, env=env)` and propagates the exit code.
3. On non-zero exit, prints:
   ```
   error: patch-ledger verify failed (exit code N)
   The Sponge patch ledger (docs/11-environment.md §4) is
   out of sync with the vendored Genode subtree. The build
   refuses to proceed rather than drop, export, or modify a
   patch on its own. Read docs/11-environment.md §4.1 and run
   `./tool/patches list` to diagnose; do NOT bypass this gate.
   ```
   …and exits with that non-zero code. The `--manual` branch is intentionally NOT gated (it only prints commands).

The gate is **read-only** by design — it calls `./tool/patches verify` only, which is the existing read-only contract from `docs/11-environment.md §4.1` (`tool/patches.mojo` is read-only against the repository; `drop` prints manual steps, it never reverts). It never invokes `./tool/patches export`, `drop`, or any other mutating subcommand. The message explicitly names `docs/11-environment.md §4` and `§4.1` so the user knows where to look.

#### 2.3.1 Gate failure-path verification

To prove the gate fails loudly rather than silently dropping a patch, `docs/11-environment.md` was temporarily moved out of the way and `./tool/build prepare` was re-run:

```text
[sponge-build] pre-flight: verifying patch ledger (read-only)
Traceback (most recent call last):
  ...
  File ".../tool/patches.mojo", line 94, in <module>
    content = read_text_file(doc_path)
  File ".../tool/patches.mojo", line 73, in read_text_file
    var f = builtins.open(path, "r")
  ...
[Errno 2] No such file or directory: '/home/luke/sponge-os/docs/11-environment.md'
/home/luke/sponge-os/.venv/bin/mojo: error: execution exited with a non-zero result: 1

error: patch-ledger verify failed (exit code 1)
The Sponge patch ledger (docs/11-environment.md §4) is
out of sync with the vendored Genode subtree. The build
refuses to proceed rather than drop, export, or modify a
patch on its own. Read docs/11-environment.md §4.1 and run
`./tool/patches list` to diagnose; do NOT bypass this gate.
```

`./tool/build prepare` returned exit code `1`; the prepare flow did NOT proceed to `create_builddir`, `build.conf` editing, or any other state-mutating step; `docs/11-environment.md` was restored from the temporary location immediately afterward. This is the W1 loud failure mode required by risk 19.

#### 2.3.2 `tool/build.mojo` parse proof

After the edit, all `tool/build` subcommands were exercised end-to-end on the real repository:

| Subcommand | Result |
|---|---|
| `./tool/build help` | exit 0; prints the help text. |
| `./tool/build list` | exit 0; lists all `run/*.run` scenarios, including the five edited scenarios (`sponge-boot`, `sponge-falkon-disk`, `sponge-persist-disk`, `sponge-desktop-disk`, `sponge-alpha`). |
| `./tool/build prepare` | exit 0; the patch-ledger pre-flight runs first (prints `[sponge-build] pre-flight: verifying patch ledger (read-only)`), then the regular prepare summary. `build.conf` was unchanged (idempotent). |
| `./tool/build run --manual sponge-boot` | exit 0; gate is correctly skipped for `--manual` (the manual branch only prints commands, it never invokes `make`). |
| `./tool/build run sponge-nonexistent` | exit non-zero; gate runs first (verified pre-flight banner present), then `make` fails because the scenario does not exist (the gate's pre-positioning is the W1 requirement, not the scenario's correctness). |

`./tool/check-compile src/vct` and `./tool/check-compile src/sponge-de` (the canonical `tool/` validation entry points per `docs/08-development.md §3.3`) were also exercised in earlier phases and remain green; the W1 changes do not touch any `repos/sponge/src/...` component so the existing structural checks stay valid.

## 3. Post-pin scenario re-runs (W0 storage scenarios, byte-for-byte marker check)

The four W0 storage scenarios were re-run **once** each (sequentially, `make -j1`, one at a time). The flake protocol from the W0 evidence file (up to 3 attempts total for `sponge-desktop-disk.run`; known Phase 9 C4 signature) was applied. **All four reached their exact load-bearing markers byte-for-byte equivalent to W0.** No scenario required more than one attempt.

### 3.1 QEMU version (recaptured before each invocation)

```text
QEMU emulator version 11.0.3
```

(Same drift as W0: `docs/11-environment.md:115` still pins 11.0.2; W6 docs sync is the right place to reconcile this — out of scope for W1.)

### 3.2 Effective QEMU command line per scenario (the pin is present and first)

The `spawn qemu-system-x86_64 …` lines extracted from each run-tool log show the explicit pin lands **immediately before** the existing `-nographic -m` argument, with every other storage/device/NIC argument unchanged:

| Scenario | QEMU spawn line (truncated) |
|---|---|
| `sponge-boot` AHCI (1a) | `spawn qemu-system-x86_64 -accel kvm -machine q35 -cpu Skylake-Client -nographic -m 1G -serial mon:stdio -drive format=raw,file=var/run/sponge-boot.img -machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0` |
| `sponge-boot` NVMe (1b) | `spawn qemu-system-x86_64 -accel kvm -machine q35 -cpu Skylake-Client -nographic -m 1G -device pcie-root-port,id=root_port1 -drive id=disk0,file=bin/nvme_disk.img,format=raw,if=none -device nvme,drive=disk0,serial=fnord,id=nvme0,bus=root_port1 -serial mon:stdio -drive format=raw,file=var/run/sponge-boot.img -machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0` |
| `sponge-persist-disk` (3) | `spawn qemu-system-x86_64 -accel kvm -machine q35 -cpu Skylake-Client -nographic -m 1G -serial mon:stdio -drive format=raw,file=var/run/sponge-persist-disk.img -machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0` |
| `sponge-desktop-disk` (2, run 1 of 1) | `spawn qemu-system-x86_64 -accel kvm -machine q35 -cpu Skylake-Client -nographic -m 4G -device nec-usb-xhci,id=xhci -device usb-tablet -serial mon:stdio -drive format=raw,file=var/run/sponge-desktop-disk.img -machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0` |

In every case the explicit `-machine q35 -cpu Skylake-Client` (from `append qemu_args`) is the **first** machine/CPU pair, immediately followed by `-nographic -m <N>G`. The duplicate `-machine q35` later in the line is the board default (from `genode/repos/base/board/pc/qemu_args`); qemu ignores it, so behavior is unchanged. No NIC topology, storage wiring, or timeout changed.

### 3.3 Post-pin markers table

| # | Scenario | Exact command | Exact load-bearing marker observed | boot_time_seconds | Result | Notes |
|---|---|---|---|---:|---|---|
| 1a | `sponge-boot` AHCI | `make -j1 -C genode/build/x86_64 run/sponge-boot KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `[init -> boot_probe] boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")` and `Run script execution successful.` | 13 | **PASS** | byte-for-byte equivalent to W0 (13 s) |
| 1b | `sponge-boot` NVMe | `SPONGE_BOOT_NVME=1 make -j1 -C genode/build/x86_64 run/sponge-boot KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `sponge-boot: NVME variant`; `[init -> boot_probe] boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`; `Run script execution successful.` | 16 | **PASS** | byte-for-byte equivalent to W0 (16 s vs W0 27 s — the W1 build was already cached; markers and topology are unchanged) |
| 2 | `sponge-desktop-disk` (run 1 of 1) | `make -j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `[init -> system -> alpha_probe] alpha-probe: PASS` and `Run script execution successful.` | 42 | **PASS** | byte-for-byte equivalent to W0; flake did NOT appear on this run. The non-fatal `Warning: flush page table entries - mapping cache full - PD: init -> {fb, vfs, rom_lib} out of CAP` warnings (Phase 9 C4 signature) appeared alongside the PASS line — exactly matching the W0 PASSING-run pattern (the flake signature only flips to FAIL when `vfs` requests `cap_quota=4`; that line was absent here, as in W0's passing runs). |
| 3 | `sponge-persist-disk` | `make -j1 -C genode/build/x86_64 run/sponge-persist-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `[init -> pkg_seq_probe] pkg-seq-probe: PASS`; `[init -> pkg_seq_probe] pkg-seq-probe: PASS--- boot 1: store on P4 contains hello (host-side e2cp readback) ---`; `[init -> sponge_pkgd] sponge_pkgd: restored 1 root(s) from store`; `Test succeeded: installed set restored from SPONGE-DATA (P4) after reboot` | 29 | **PASS** | byte-for-byte equivalent to W0 (29 s) |

**No invocation exceeded the W0 budgets.** The five W1 disk-touching scenarios that gained the explicit pin are: `sponge-boot.run`, `sponge-desktop-disk.run`, `sponge-persist-disk.run`, `sponge-falkon-disk.run`, `sponge-alpha.run`. Of those, `sponge-falkon-disk.run` and `sponge-alpha.run` are not in the W0 re-run list (per the W1 brief — "the four storage-scenario re-runs listed above" excludes them); the pin-only diff is identical for all five (verified by `git diff` above).

### 3.4 Serialization + flake protocol compliance

- **One scenario at a time:** the four re-runs above were issued strictly sequentially. No concurrent `make` in `genode/build/x86_64`; each `make -j1` call exited (RC 0) before the next was launched.
- **Flake protocol:** per the W0 evidence file's `Invocation-2 flake` section, `sponge-desktop-disk.run` is allowed up to 3 attempts total on the same command. Only 1 attempt was needed today (run 1 PASS in 42 s); the Phase 9 C4 flake signature (`mapping cache full ... out of CAP` + `vfs requests resources: cap_quota=4` + 900 s gate expiry) did NOT appear. The non-fatal `mapping cache full ... out of CAP` warnings that did appear are documented in W0 as occurring in PASSING runs as well; they did not correlate with `vfs` requesting `cap_quota=4`, so the flake trigger was absent.

## 4. Risk-mitigation traceability

| Risk | W1 mitigation evidence |
|---|---|
| 9 (fresh `tool/build prepare` lacks the `pc` repository) | §2.1 above: `repos/pc` is already inside the managed block; the block is idempotent (SHA-256 unchanged across `prepare` re-runs); W6 owns the fresh-build proof. |
| 19 (patch-ledger rows 6/7 lost) | §1 above: 9/9 OK; §2.3 above: pre-flight refuses to proceed on non-zero exit; gate fails loudly (verified in §2.3.1). |

## 5. Files NOT changed (and why)

- `genode/` (vendored Genode subtree): **not touched.** AGENTS.md §5.2 / D12.10 prohibits Phase 12 edits under the subtree.
- `tool/dist.mojo`: **not touched.** W3b+W4 own the storage-variant selector (D12.2).
- `tool/patches.mojo`, `tool/patches`: **not touched.** W1 invokes the existing read-only contract; rewriting the tool is out of scope.
- `run/sponge-{boot-i440fx,boot-multidisk,desktop-disk-nvme,pc-nic,usb-boot,usb-kbd-via-qmp}.run`: **not touched** (don't exist yet — they are W2/W3/W4 deliverables).
- `docs/15-hardware-compatibility.md`, `tool/hw_compat.mojo`: **not touched** (W5 deliverables).

## 6. Plan deviations

**None.** Every W1 step in the brief (lines 311–369 of `docs/plans/phase12-hardware.md`) is implemented as written; every "MUST DO" and "MUST NOT DO" constraint is honored.

The only materialization-vs-brief observation: the W1 brief said "if [repos/pc is] present and idempotent, record that fact instead of editing". §2.1 records that fact and proves the idempotency end-to-end. No edit was made to the REPOSITORIES block — by the brief's explicit permission.

A second observation, not a deviation: the patch-pre-flight required a small additional helper (`run_argv_env`) to set `VIRTUAL_ENV` + `PYTHONPATH` so the inner `./tool/patches verify` subprocess can find the `mojo` python module when the venv python is a symlink to `/usr/bin/python3` (the kernel resolves the symlink before execve, so python sees sys.prefix=/usr and the venv site-packages aren't auto-activated). The fix is purely environmental, read-only against the repository, and the existing non-mojo callers of `run_argv` are untouched. This was not called out in the brief but is a necessary part of making the gate actually run end-to-end; the Mojo README at `tool/README.md` does not document this symlink quirk because it had not been hit before this task.
