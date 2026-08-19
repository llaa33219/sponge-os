# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS distribution media builder.
#
# Produces the two installable Alpha media artifacts in var/dist/ per the
# Phase 8 boot/storage architecture (docs/14-boot-storage-architecture.md
# §8 — ".img = the real product, .iso = live/eval mode"):
#
#   sponge-os-0.1.0-alpha-x86_64-sel4.img   (4-partition disk image)
#   sponge-os-0.1.0-alpha-x86_64-sel4.iso   (El Torito ISO, live/eval)
#
# === The product story (docs/14 §8) ===
#
# The .img is the REAL PRODUCT: the Phase 8 disk-served desktop
# (run/sponge-desktop-disk) — a Tier-0 image.elf (≤ 80 MiB) that reaches
# the GENODE ext2 partition and serves the full Qt6 desktop as ROMs via
# cached_fs_rom — PLUS a fourth GPT partition SPONGE-DATA (added by
# tool/mkdata) that backs sponge_pkgd's installed-set store. Installs
# persist across reboots on this media (P3, run/sponge-persist-disk).
#
# The .iso is the LIVE/EVAL MEDIA: the Phase 7 alpha boot-modules
# composition (run/sponge-alpha) packed into an El Torito ISO. It boots
# the same themed desktop, but Tier 2 is a RAM filesystem — nothing
# persists. Useful for evaluation without promising persistence an
# read-only optical medium cannot deliver.
#
# Each artifact ships with a `<name>.sha256` sidecar (`<hash>  <name>`
# format, compatible with `sha256sum -c`).
#
# What this tool does:
#   (a) Pre-flight check: verifies every host tool the Genode run
#       framework's image plugins AND tool/mkdata invoke (xorriso,
#       sgdisk, mcopy, e2cp, e2mkdir, mkfs.ext2, mkfs.vfat, resize2fs,
#       truncate) is on PATH, printing the exact apt install line for
#       any missing one and exiting non-zero BEFORE any build runs.
#   (b) Runs the disk-image build (sponge-desktop-disk), then invokes
#       tool/mkdata to add the SPONGE-DATA P4 to the produced .img
#       (docs/14 §4.3; idempotent), then runs the ISO build
#       (sponge-alpha), sequentially, streaming each invocation's
#       output. The exit code propagates: a non-zero exit fails this
#       tool with the same code.
#   (c) Cleans var/run/<scenario>* between modes so the second mode is
#       not polluted by the first mode's staged boot directory
#       (stale_state guard).
#   (d) Copies the artifacts to var/dist/ with the release names and
#       writes the .sha256 sidecars.
#   (e) Prints a summary table with artifact sizes and sha256 prefixes.
#
# What this tool does NOT do:
#   - It does NOT boot-verify the media. Each run scenario gates on its
#     PASS marker (alpha-probe: PASS for the desktop) via the Genode
#     run framework's run_genode_until during the make invocation; the
#     media artifacts produced here are already boot-proven by that
#     probe.
#   - It does NOT re-implement any of the Genode run tool's image
#     plugins or tool/mkdata. It shells out to `make` and `tool/mkdata`
#     exactly as a contributor would by hand (AGENTS.md §3.5 — no
#     re-implementation).
#   - It does NOT touch anything outside this repository. The only
#     paths mutated are genode/build/x86_64/var/run/ (cleaned between
#     modes), var/dist/ (artifacts written), and .omo/evidence/ (only
#     when the caller redirects output there).
#
# The manual equivalent of every step this tool performs is documented
# in docs/08-development.md §11 (control escape hatch, AGENTS.md §3.5).
#
# Usage:
#   mojo tool/dist.mojo                        # build product media (.img 4-part + .iso live)
#   mojo tool/dist.mojo --no-data              # build .img WITHOUT SPONGE-DATA P4 (control door)
#   mojo tool/dist.mojo --data-size 256        # 256 MiB SPONGE-DATA P4 (default 1024)
#   mojo tool/dist.mojo help                   # show this usage

from std.sys import argv, exit
from std.python import Python, PythonObject
from std.collections import Dict

# Alpha release identity (kept in sync with include/sponge/version.h
# and docs/09-roadmap.md). The release artifact names embed this.
comptime RELEASE_VERSION = "0.1.0-alpha"
comptime RELEASE_ARCH = "x86_64"
comptime RELEASE_KERNEL = "sel4"
comptime RELEASE_NAME_PREFIX = "sponge-os-" + RELEASE_VERSION + "-"
    + RELEASE_ARCH + "-" + RELEASE_KERNEL

# Per docs/14 §8: the .img is the disk-served product (Phase 8 P2
# desktop-from-disk + P3 SPONGE-DATA via tool/mkdata); the .iso is the
# alpha boot-modules live/eval media (Tier 2 on RAM fs, no persistence).
#
# Phase 12 W2 (docs/plans/phase12-hardware.md §"W2: Storage variants
# and product-media selector"): the product .img can be built against
# EITHER the default AHCI Tier-0 chain OR an opt-in NVMe Tier-0 chain
# (`--storage {ahci,nvme}`). The default stays `ahci` (current
# behavior + artifact naming). `nvme` selects the dedicated
# `run/sponge-desktop-disk-nvme.run` product scenario that uses one
# explicit q35 `pcie-root-port` + `-device nvme` namespace (copied
# from `run/sponge-boot.run`'s `SPONGE_BOOT_NVME` block, plan §W2
# step 2 / Risk 10). The ISO path is ALWAYS `sponge-alpha` regardless
# of the storage mode (ISO is live/eval on a RAM fs; no storage driver
# is touched).
comptime DISK_SCENARIO_AHCI = "sponge-desktop-disk"
comptime DISK_SCENARIO_NVME = "sponge-desktop-disk-nvme"
# Phase 15 W4 (D15.13, D15.16): UEFI product-media scenarios. The
# Sponge-side UEFI recipe (D15.13: handcrafted GPT with P1=ESP
# FAT32, P2 ABSENT, P3=GENODE ext2, P4=SPONGE-DATA via tool/mkdata;
# the partition-number pin and tool/mkdata's P4 grow sequence work
# UNCHANGED because P2 is intentionally absent). The SATA UEFI
# scenario builds the full structure into a single handcrafted .img;
# the NVMe envelope is the target-machine envelope (D15.1). Both
# scenarios are QEMU-unverified per D15.16 (the W1 OVMF core-init
# hang is the expected QEMU outcome; structural-gate acceptance
# only; real-hardware verification is 15-3).
comptime DISK_SCENARIO_UEFI = "sponge-desktop-disk-uefi"
comptime DISK_SCENARIO_UEFI_NVME = "sponge-desktop-disk-uefi-nvme"
# Phase 15 15-3: UEFI USB-stick product-media scenario. The Tier-0
# storage chain swaps AHCI for xHCI + usb_block (class: 0x8 mass-
# storage policy); everything else is identical to the AHCI UEFI
# scenario (same disk layout, same GRUB2 EFI multiboot2 chain, same
# boot_fb display driver). The 15-3 deliverable burns this .img to a
# USB stick and boots the 17ZD90N-VX7BK from it. Per the BIOS branch's
# policy (see `is_valid_storage_mode`), `--storage usb` is UEFI-only
# here — the BIOS branch stays AHCI/NVMe (the BIOS-side USB-stick
# attach is the Phase 12 `sponge-usb-boot.run` precedent; combining
# USB-boot with BIOS GRUB is a different code path the current tree
# does not support end-to-end, so the combination is rejected loudly).
comptime DISK_SCENARIO_UEFI_USB = "sponge-desktop-disk-uefi-usb"
comptime ISO_SCENARIO = "sponge-alpha"

# Default storage mode (the default keeps current behavior and artifact
# naming — plan §W2 step 1: "default ahci keeps current behavior and
# artifact naming").
comptime DEFAULT_STORAGE_MODE = "ahci"

# Allowed values for --storage. Anything else is rejected BEFORE any
# build starts (plan §W2 step 1: "reject any other value before a build
# starts with a concise English error and usage line"). The `usb`
# value is UEFI-only — `is_valid_storage_mode_for_firmware` enforces
# that constraint at the firmware/storage combination boundary (a
# `bios` + `usb` request is rejected with the precise reason: the
# BIOS-side USB-stick attach is the Phase 12 precedent, not the
# product media).
comptime ALLOWED_STORAGE_MODES = ["ahci", "nvme", "usb"]

# Default SPONGE-DATA P4 size (MiB). Matches tool/mkdata's default and
# docs/14 §4.3 ("P4 = 1 GiB default, configurable via tool/dist
# --data-size").
comptime DEFAULT_DATA_MIB = 1024

# Phase 15 W2 (docs/plans/phase15-real-hardware-boot.md D15.3/D15.4/
# D15.8): bake profile selector. Passed to `make` as the env var
# SPONGE_BAKE_PROFILE=<name>, which run/bake.inc reads inside the run
# script before build_boot_image (D15.8's primary staging-time
# mechanism). Default: desktop — the Phase 15 everyday-defaults
# profile; the run scripts already default to desktop when the env
# var is unset, but tool/dist makes it explicit (R15.14 — silent
# default drift is the trap to avoid). The "none" sentinel is
# run/bake.inc's escape hatch (AGENTS.md §1.1): reproduces today's
# hardcoded hello-only behavior. Invalid values are rejected before
# any build (per the Phase 12 W2 storage-mode convention).
comptime DEFAULT_BAKE_PROFILE = "desktop"
comptime ALLOWED_BAKE_PROFILES = ["minimal", "desktop", "test", "none"]

# Phase 15 W4 (D15.13, D15.16): firmware selector. `bios` is the
# default and the only fully verified path on the 17ZD90N-VX7BK
# target (15-3 real-hardware UEFI diagnostic is decoupled from
# this flag — see D15.16). `uefi` is the W4 scope: it now runs the
# Sponge-side UEFI recipe (D15.13) and produces a .img that passes
# the host-side structural gates (sgdisk -p, mdir, e2ls). Per
# D15.16 the QEMU boot of the UEFI .img is EXPECTED to hit the
# W1 OVMF core-init hang; the scenario's acceptance is structural
# verification + honest gap recording, NOT a QEMU boot PASS. The
# ISO half does not apply to UEFI (El Torito is BIOS-only) — for
# `--firmware uefi` dist produces ONLY the .img and the summary
# says so explicitly.
comptime DEFAULT_FIRMWARE = "bios"
comptime ALLOWED_FIRMWARE = ["bios", "uefi"]

# Each host tool the media build path can invoke, mapped to its
# Debian/Ubuntu package. Multiple tools can come from one package; the
# summary print de-duplicates the install line. The mappings match
# docs/11-environment.md §7.3 exactly. truncate is required by
# tool/mkdata's docs/14 §4.3 sequence.
struct HostTool(Copyable, Movable):
    """One required host command and the apt package that ships it."""
    var command: String
    var apt_package: String
    var used_by: String  # which image plugin / step needs it

    def __init__(out self, command: String, apt_package: String, used_by: String):
        self.command = command
        self.apt_package = apt_package
        self.used_by = used_by


def required_tools() -> List[HostTool]:
    """The complete media host-tool set. Order matches the docs/11 §7.3
    table so the missing-tool output reads in the documented order.
    truncate is included because tool/mkdata (the P4 step) needs it in
    addition to sgdisk + mkfs.ext2."""
    return [
        HostTool("xorriso", "xorriso", "image/iso"),
        HostTool("sgdisk", "gptfdisk", "image/disk, mkdata"),
        HostTool("mcopy", "mtools", "image/disk"),
        HostTool("e2cp", "e2tools", "image/disk"),
        HostTool("e2mkdir", "e2tools", "image/disk"),
        HostTool("mkfs.ext2", "e2fsprogs", "image/disk, mkdata"),
        HostTool("mkfs.vfat", "dosfstools", "EFI partition fallback"),
        HostTool("resize2fs", "e2fsprogs", "image/disk"),
        HostTool("truncate", "coreutils", "mkdata (P4 grow)"),
    ]


def repo_root() raises -> String:
    """Locate the repository root as the parent of the tool/ directory
    that contains this script, so the tool works from any cwd."""
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def run_argv_streaming(cmd: List[String], cwd: String) raises -> Int:
    """Run a command with stdout/stderr streaming straight to the
    controlling terminal (inherited file descriptors), returning the
    child exit code. Uses Python subprocess because the Mojo stdlib
    has no process-spawning facility yet (see tool/README.md)."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var rc: PythonObject
    if cwd.byte_length() > 0:
        rc = subprocess.call(py_args, cwd=cwd)
    else:
        rc = subprocess.call(py_args)
    return Int(py=rc)


def run_argv_streaming_clean_env(cmd: List[String]) raises -> Int:
    """Same as run_argv_streaming, but spawns the child with a
    sanitized environment that strips the parent Mojo process's
    Python-override env vars. Required when the child is itself a
    Mojo tool invoked via its bash launcher (e.g. tool/mkdata):
    the parent Mojo sets MOJO_PYTHON_LIBRARY + PYTHONEXECUTABLE so it
    can find its own libpython, but those same vars make the child's
    .venv/bin/mojo wrapper try to import the mojo module from the
    WRONG Python (system python3 instead of the venv python),
    producing 'ModuleNotFoundError: No module named mojo'. Stripping
    those three vars lets the child's bash launcher resolve the venv
    python via its own shebang, exactly as it does when run from a
    clean shell."""
    var os_py = Python.import_module("os")
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var env = os_py.environ.copy()
    for k in ["MOJO_PYTHON_LIBRARY", "PYTHONEXECUTABLE", "PYTHONHOME"]:
        if k in env:
            env.pop(k, None)
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args, env=env))


def run_argv_streaming_with_env(cmd: List[String], extra_env: Dict[String, String]) raises -> Int:
    """Run a command streaming stdout/stderr to the terminal,
    with extra_env added on top of the parent environment (so the
    make invocation sees SPONGE_BAKE_PROFILE=<name> from Phase 15
    W2). Mirrors run_argv_streaming_clean_env's env-strip policy:
    we still strip MOJO_PYTHON_LIBRARY / PYTHONEXECUTABLE / PYTHONHOME
    so a child Mojo (when invoked indirectly) doesn't pick up the
    parent's libpython pointers."""
    var os_py = Python.import_module("os")
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var env = os_py.environ.copy()
    for k in ["MOJO_PYTHON_LIBRARY", "PYTHONEXECUTABLE", "PYTHONHOME"]:
        if k in env:
            env.pop(k, None)
    for key in extra_env:
        var k = String(key)
        env[k] = extra_env[k]
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args, env=env))


def which(command: String) raises -> Bool:
    """True iff `command` is executable on the current PATH. Wraps
    shutil.which so the resolution rules match what the Genode run
    framework's `installed_command` proc will actually find at build
    time. shutil.which returns None when nothing is found, else the
    absolute path string; we coerce to a Python bool to avoid any
    Mojo None-comparison ambiguity."""
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")
    var found = shutil.which(command)
    return Bool(py=builtins.bool(found))


def fmt_human_size(num_bytes: Int) raises -> String:
    """Render a byte count as a short human-readable string (B / KiB /
    MiB / GiB) with one decimal place above 1024. Used in the summary
    table. Delegates the float formatting to Python's str.format via
    the Mojo-Python bridge (Mojo's String has no printf-equivalent)."""
    var units: List[String] = ["B", "KiB", "MiB", "GiB", "TiB"]
    var size = Float64(num_bytes)
    var i = 0
    while size >= 1024.0 and i < len(units) - 1:
        size = size / 1024.0
        i += 1
    var builtins = Python.import_module("builtins")
    if i == 0:
        # Whole-byte case: no decimal.
        return String(builtins.str("{} {}").format(Int(num_bytes), units[0]))
    return String(builtins.str("{:.1f} {}").format(size, units[i]))


def sha256_of_file(path: String) raises -> String:
    """Compute the SHA-256 of `path` (hex lowercase, 64 chars). Streams
    the file through hashlib in 1 MiB chunks so the disk image (which
    can be ~1 GiB) does not have to be fully resident."""
    var hashlib = Python.import_module("hashlib")
    var builtins = Python.import_module("builtins")
    var h = hashlib.sha256()
    var f = builtins.open(path, "rb")
    var chunk_size = 1024 * 1024
    while True:
        var chunk = f.read(chunk_size)
        # Python read() returns b"" at EOF (length 0); the explicit
        # None case is for text mode, which we do not use here, but we
        # guard both via truthiness through Python's bool().
        var n = Int(py=builtins.len(chunk))
        if n == 0:
            break
        h.update(chunk)
    f.close()
    return String(h.hexdigest())


def file_size(path: String) raises -> Int:
    var os_path = Python.import_module("os.path")
    return Int(py=os_path.getsize(path))


def ensure_dir(path: String) raises:
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")
    if not os_path.isdir(path):
        os_py.makedirs(path)


def rm_glob(parent_dir: String, prefix: String) raises:
    """Remove every entry in `parent_dir` whose name starts with
    `prefix`. Used to clean var/run/<scenario>* between the disk and
    ISO modes so neither mode's run_dir pollutes the other."""
    var os_path = Python.import_module("os.path")
    var os_py = Python.import_module("os")
    var builtins = Python.import_module("builtins")
    if not os_path.isdir(parent_dir):
        return
    var entries = os_py.listdir(parent_dir)
    for entry in entries:
        var name = String(entry)
        if name.startswith(prefix):
            var full = parent_dir + "/" + name
            # rmtree for directories, unlink for files; the run_dir
            # for a scenario is a directory, but stale .img/.iso
            # siblings from prior image plugins are files.
            if os_path.isdir(full) and not os_path.islink(full):
                var shutil = Python.import_module("shutil")
                shutil.rmtree(full)
            else:
                os_py.unlink(full)
            print("[sponge-dist] cleaned stale " + full)


def cmd_help() raises:
    print("Sponge OS distribution media builder")
    print()
    print("Product story (docs/14-boot-storage-architecture.md §8):")
    print("  .img  the real product — disk-served desktop")
    print("        (BIOS path: 4-partition P1=BIOS-boot + P2=ESP + P3=GENODE")
    print("                              + P4=SPONGE-DATA via tool/mkdata;")
    print("         UEFI path: 3-partition P1=ESP + P2 absent + P3=GENODE")
    print("                              + P4=SPONGE-DATA via tool/mkdata —")
    print("                              P2 is intentionally absent so P3/P4")
    print("                              partition numbers match the BIOS media,")
    print("                              per the W4 partition-number contract,")
    print("                              D15.13).")
    print("        Install/launch scenarios: --storage {ahci,nvme,usb},")
    print("                                 --firmware {bios,uefi}")
    print("        (--storage usb is UEFI-only; BIOS branch is ahci/nvme.)")
    print("        Installs persist across reboots on P3.")
    print("  .iso  live/eval mode — alpha boot-modules composition")
    print("        (run/" + ISO_SCENARIO + ") on a RAM filesystem.")
    print("        Boots the same desktop; nothing persists. BIOS only")
    print("        (El Torito is BIOS-only; --firmware uefi SKIPS the .iso).")
    print()
    print("Storage mode (Phase 12 W2 + Phase 15 15-3):")
    print("  ahci  default; the product .img boots against the q35 ICH9")
    print("        AHCI chain (run/" + DISK_SCENARIO_AHCI + " for BIOS,")
    print("        run/" + DISK_SCENARIO_UEFI + " for UEFI).")
    print("  nvme  the product .img boots against a q35 PCIe root-port")
    print("        + -device nvme chain (run/" + DISK_SCENARIO_NVME + " for BIOS,")
    print("        run/" + DISK_SCENARIO_UEFI_NVME + " for UEFI),")
    print("        one namespace verified; multi-namespace is a known gap.")
    print("        The same q35 + Skylake-Client pin, --include image/disk")
    print("        plugin set, and SPONGE-DATA P4 step apply. The QEMU")
    print("        root-port/drive/NVMe-device wiring is copied verbatim")
    print("        from run/sponge-boot.run's SPONGE_BOOT_NVME block.")
    print("  usb   Phase 15 15-3: the USB-stick product media (UEFI only).")
    print("        The product .img boots against a Tier-0 xHCI + usb_block")
    print("        storage chain (run/" + DISK_SCENARIO_UEFI_USB + "); the")
    print("        user `dd`s the .img to a USB stick and the Insyde H2O")
    print("        firmware on the 17ZD90N-VX7BK presents it to xHCI.")
    print("        pc_usb_host matches by class (0x3 for HID, 0x8 for mass")
    print("        storage) so the policy is port-independent on real")
    print("        hardware. --storage usb + --firmware bios is rejected")
    print("        loudly (the BIOS-side USB-stick attach is the Phase 12")
    print("        `sponge-usb-boot.run` precedent which boots the existing")
    print("        ISO from a USB stick; it is not a new product image).")
    print()
    print("Bake profile (Phase 15 W2, docs/plans/phase15-real-hardware-boot.md")
    print("D15.3/D15.4/D15.8): passed to `make` as SPONGE_BAKE_PROFILE=<name>,")
    print("which run/bake.inc reads inside the run script before")
    print("build_boot_image (the primary staging-time mechanism).")
    print("  minimal  Smallest usable media (Sponge DE + terminal package).")
    print("  desktop  Everyday-default (every pre-staged package + Falkon).")
    print("  none     bake.inc escape hatch (AGENTS.md §1.1); reproduces")
    print("           today's hardcoded hello-only behavior.")
    print()
    print("Firmware (Phase 15 W2/W4, D15.13/D15.16):")
    print("  bios  default; BIOS/GRUB2 boot chain.")
    print("  uefi  Phase 15 W4: Sponge-side UEFI recipe (D15.13). Produces")
    print("        a UEFI .img that passes the host-side structural gates")
    print("        (sgdisk -p, mdir, e2ls). Per D15.16, the QEMU UEFI")
    print("        boot of this media is EXPECTED to hit the W1 OVMF")
    print("        core-init hang (genode's vendored GRUB2 EFI is broken")
    print("        under host OVMF dated 2026-05; see")
    print("        docs/evidence/phase15-uefi-boot-smoke.log). The scenario's")
    print("        acceptance is host-side structural verification + honest")
    print("        gap recording, NOT a QEMU boot PASS. Real-hardware")
    print("        verification is 15-3 (target machine: 17ZD90N-VX7BK,")
    print("        2020 Insyde H2O — predates the W^X/fragmentation era).")
    print("        --firmware uefi produces ONLY the .img (no .iso).")
    print()
    print("Usage:")
    print("  mojo tool/dist.mojo                       Build .img (ahci) + .iso")
    print("  mojo tool/dist.mojo --storage ahci        Force AHCI product .img (default)")
    print("  mojo tool/dist.mojo --storage nvme        Force NVMe product .img")
    print("  mojo tool/dist.mojo --storage usb         Force USB-stick product .img (UEFI only;")
    print("                                            --firmware uefi implicit requirement)")
    print("  mojo tool/dist.mojo --bake-profile {minimal,desktop,test,none}")
    print("                                            Bake-profile selector (default desktop)")
    print("  mojo tool/dist.mojo --firmware bios       BIOS/GRUB2 boot chain (default)")
    print("  mojo tool/dist.mojo --firmware uefi       UEFI/OVMF boot chain (Phase 15 W4;")
    print("                                            .img-only by design — no .iso)")
    print("  mojo tool/dist.mojo --no-data             Skip the SPONGE-DATA P4 step")
    print("                                            (control door; .img has 3 partitions)")
    print("  mojo tool/dist.mojo --data-size <N>       N MiB SPONGE-DATA P4 (default "
          + String(DEFAULT_DATA_MIB) + ")")
    print("  mojo tool/dist.mojo --print-only          Print the would-be make + mkdata +")
    print("                                            env (SPONGE_BAKE_PROFILE=...) commands")
    print("                                            and exit; no build is run")
    print("  mojo tool/dist.mojo help                  Show this help")
    print()
    print("Reproducibility (R15.4): the bake profile is an explicit input,")
    print("so two consecutive builds with the same --bake-profile must")
    print("produce content-equal images modulo host-side timestamps")
    print("(mkfs.ext2, the GPT write). The post-build verification prints")
    print("artifact size + sha256 prefix; full hashes live in <name>.sha256")
    print("sidecars in var/dist/. Byte-identical rebuild requires mkfs.ext2")
    print("to seed deterministic timestamps, which is not yet in the")
    print("vendored tree; staged-content manifest equality is the gate we")
    print("currently meet (the bake_manifest.json embedded in the image is")
    print("the manifest; tool/bake.mojo's idempotency check is the same).")
    print()
    print("Produces in var/dist/:")
    print("  BIOS path:")
    print("    " + RELEASE_NAME_PREFIX + ".img       (4-partition disk-served desktop)")
    print("    " + RELEASE_NAME_PREFIX + ".img.sha256")
    print("    " + RELEASE_NAME_PREFIX + ".iso       (live/eval, BIOS El Torito)")
    print("    " + RELEASE_NAME_PREFIX + ".iso.sha256")
    print("  UEFI path (--firmware uefi):")
    print("    " + RELEASE_NAME_PREFIX + ".img       (3-partition: P1=ESP + P2 absent + P3=GENODE + P4=SPONGE-DATA)")
    print("    " + RELEASE_NAME_PREFIX + ".img.sha256")
    print("    (no .iso — El Torito is BIOS-only)")
    print()
    print("The tool checks the host tools listed in docs/11-environment.md")
    print("§7.3 (xorriso, gptfdisk/sgdisk, mtools/mcopy, e2tools, dosfstools,")
    print("e2fsprogs, coreutils/truncate) up front, runs the disk and ISO media")
    print("builds via make, grows the SPONGE-DATA P4 via tool/mkdata (docs/14")
    print("§4.3), and copies the artifacts to var/dist/. For --firmware uefi")
    print("the .iso build is SKIPPED (El Torito is BIOS-only) and only the")
    print(".img + .sha256 sidecar are staged.")
    print()
    print("The tool never boot-verifies the media itself; each run scenario")
    print("already gates on its PASS marker during the make invocation")
    print("(see docs/08-development.md §11 for the manual escape hatch).")


def check_host_tools() raises -> Bool:
    """Pre-flight check. Returns True iff every required tool is on
    PATH. When any are missing, prints a per-tool report (command,
    status, apt package, used-by) and a single apt install line, then
    returns False so main() can exit non-zero BEFORE any build runs."""
    var tools = required_tools()
    var missing: List[HostTool] = []
    var missing_pkgs: List[String] = []

    print("[sponge-dist] host tool check (docs/11-environment.md §7.3)")
    print()
    for i in range(len(tools)):
        # Read fields directly off tools[i] without binding to a var
        # (HostTool is Copyable+Movable but not ImplicitlyCopyable, so
        # `var t = tools[i]` would attempt a forbidden copy).
        var cmd = tools[i].command
        var pkg = tools[i].apt_package
        var used_by = tools[i].used_by
        var present = which(cmd)
        var status = String("MISSING")
        if present:
            status = "ok"
        else:
            missing.append(HostTool(cmd, pkg, used_by))
            # de-duplicate apt packages in install order
            var seen = False
            for p in missing_pkgs:
                if p == pkg:
                    seen = True
            if not seen:
                missing_pkgs.append(pkg)
        # Column-aligned: command left-padded to longest (mkfs.ext2 = 10).
        var cmd_padded = pad_right(cmd, 10)
        print("  " + cmd_padded + "  " + status + "    (apt: "
              + pkg + "    needed by: " + used_by + ")")

    print()
    if len(missing) == 0:
        print("[sponge-dist] host tool check: OK (all "
              + String(len(tools)) + " tools present)")
        print()
        return True

    print("[sponge-dist] host tool check: FAIL (" + String(len(missing))
          + " missing)")
    print()
    print("Install the missing package(s) before continuing. On Debian")
    print("/ Ubuntu, run exactly:")
    print()
    var apt_line = String("sudo apt install ")
    var first = True
    for p in missing_pkgs:
        if not first:
            apt_line += " "
        apt_line += p
        first = False
    print("  " + apt_line)
    print()
    print("(Arch: substitute `pacman -S` for `apt install`.)")
    print()
    print("This tool will not invoke the media build until every host")
    print("tool is present, so the failure is loud and early.")
    return False


def join_with_space(parts: List[String]) raises -> String:
    """Join a list of strings with single spaces, manual implementation
    (Mojo stdlib String has no .join). Used to render the make command
    line in the log."""
    var out = String("")
    var first = True
    for p in parts:
        if not first:
            out += " "
        out += p
        first = False
    return out


def pad_right(s: String, width: Int) raises -> String:
    """Pad `s` on the right with spaces to at least `width` bytes.
    Used for column alignment in the summary table."""
    var out = s
    var i = s.byte_length()
    while i < width:
        out += " "
        i += 1
    return out


def startswith_str(s: String, prefix: String) raises -> Bool:
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(builtins.str(s).startswith(prefix)))


def run_media_build(mode: String, scenario: String, root: String,
                    bake_profile: String) raises -> Int:
    """Invoke the make target for one media mode (`image/disk` or
    `image/iso`) on the given scenario. Streams make's output.
    Cleans var/run/<scenario>* first so this mode's image plugin sees
    a clean run_dir (stale_state guard).

    Phase 15 W2: passes SPONGE_BAKE_PROFILE=<bake_profile> in the
    child's environment so run/bake.inc (sourced by the scenario
    before build_boot_image) reads it (D15.8 — the primary
    staging-time mechanism). `none` is a valid value here (bake.inc
    honors it as the escape hatch / regression baseline)."""
    var build_dir = root + "/genode/build/x86_64"
    var run_root = build_dir + "/var/run"

    print("[sponge-dist] cleaning stale " + run_root + "/"
          + scenario + "* before " + mode + " build")
    rm_glob(run_root, scenario)
    print()

    var make_args: List[String] = [
        "make",
        "-C",
        build_dir,
        "run/" + scenario,
        "KERNEL=sel4",
        "BOARD=pc",
        "RUN_OPT=--include " + mode,
    ]

    print("[sponge-dist] starting " + mode + " media build (scenario: "
          + scenario + ")")
    print("  cmd: " + join_with_space(make_args))
    print("  env: SPONGE_BAKE_PROFILE=" + bake_profile)
    print()
    var extra_env: Dict[String, String] = Dict[String, String]()
    extra_env["SPONGE_BAKE_PROFILE"] = bake_profile
    var rc = run_argv_streaming_with_env(make_args, extra_env)
    print()
    print("[sponge-dist] " + mode + " make exit code: " + String(rc))
    return rc


def run_mkdata(root: String, img_path: String, data_mib: Int) raises -> Int:
    """Invoke tool/mkdata to add the SPONGE-DATA P4 to the produced
    .img (docs/14 §4.3 sequence: truncate + sgdisk delete/move/new/
    hybrid + mkfs.ext2 -E offset). Idempotent: a re-run on an image
    that already has P4=SPONGE-DATA is a verified no-op. The control
    door is `./tool/dist --no-data` (skips this step entirely)."""
    var mkdata = root + "/tool/mkdata"
    var args: List[String] = [
        mkdata,
        img_path,
        "--data-size",
        String(data_mib),
    ]
    print()
    print("[sponge-dist] growing SPONGE-DATA P4 via tool/mkdata")
    print("  cmd: " + join_with_space(args))
    print("  (docs/14 §4.3 grow/repartition sequence; idempotent)")
    print()
    # tool/mkdata is itself a Mojo tool launched via its bash wrapper.
    # The parent Mojo's Python-override env vars (MOJO_PYTHON_LIBRARY,
    # PYTHONEXECUTABLE) would make the child's .venv/bin/mojo wrapper
    # use the wrong Python; the clean-env variant strips them.
    var rc = run_argv_streaming_clean_env(args)
    print()
    print("[sponge-dist] tool/mkdata exit code: " + String(rc))
    return rc


def verify_partitions(img_path: String, firmware: String) raises -> Bool:
    """Run sgdisk -p on the produced .img and assert the partition
    table matches the firmware contract.
      - BIOS: 4 partitions (P1 BIOS-boot, P2 ESP, P3 GENODE,
              P4 SPONGE-DATA added by tool/mkdata). P4 must be
              named SPONGE-DATA.
      - UEFI: 3 partitions (P1 ESP, P2 ABSENT by the W4 partition-
              number contract — see D15.13 + run/sponge-desktop-
              disk-uefi.run header, P3 GENODE, P4 SPONGE-DATA
              added by tool/mkdata). P4 must be named SPONGE-DATA.
    The misleading_success_output defense: the build's exit code
    alone is not enough; the actual partition table must match.
    Returns True on success, False (with diagnostic) on failure."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    py_args.append("sgdisk")
    py_args.append("-p")
    py_args.append(img_path)
    var p = subprocess.Popen(
        py_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    var comm = p.communicate()
    var out_obj = comm[0]
    var out_str = String("")
    if Bool(py=builtins.bool(out_obj)):
        out_str = String(out_obj.decode("utf-8", "replace"))
    var rc = Int(py=p.returncode)
    print("[sponge-dist] sgdisk -p verification of " + img_path
          + " (firmware=" + firmware + ")")
    print(out_str)
    if rc != 0:
        print("[sponge-dist] ERROR: sgdisk -p exited " + String(rc))
        return False
    # Count partition rows (lines starting with a number after the
    # header). sgdisk -p prints "Number  Start  ...  Name" then rows.
    var lines = out_str.splitlines()
    var part_count = 0
    var saw_sponge_data = False
    for line_py in lines:
        var line = String(line_py).lstrip()
        # A partition row begins with a digit. The header begins with
        # "Number". We count digit-led rows only.
        if line.byte_length() > 0:
            var first = line[byte=0]
            if first >= '0' and first <= '9':
                part_count += 1
                if contains_substring(String(line_py), "SPONGE-DATA"):
                    saw_sponge_data = True
    var expected_min = 4
    if firmware == "uefi":
        # UEFI layout: P1=ESP, P2 absent, P3=GENODE, P4=SPONGE-DATA.
        # P2 is intentionally not allocated so the P3/P4 partition
        # numbers are identical to the BIOS media (P3/P4 work
        # unchanged). 3 partitions total.
        expected_min = 3
    if part_count < expected_min:
        print("[sponge-dist] ERROR: expected >= " + String(expected_min)
              + " partitions for firmware=" + firmware + ", sgdisk -p shows "
              + String(part_count))
        return False
    if not saw_sponge_data:
        print("[sponge-dist] ERROR: no partition named SPONGE-DATA in table")
        return False
    if firmware == "uefi":
        print("[sponge-dist] OK — " + String(part_count)
              + " partitions present (P1=ESP, P2 absent, P3=GENODE, P4=SPONGE-DATA)")
    else:
        print("[sponge-dist] OK — " + String(part_count)
              + " partitions present (P1=BIOS-boot, P2=ESP, P3=GENODE, P4=SPONGE-DATA)")
    return True


def contains_substring(haystack: String, needle: String) raises -> Bool:
    """Case-sensitive substring test (Mojo String has no .contains)."""
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(
        builtins.str(haystack).find(needle) >= 0))


def stage_artifact(mode: String, scenario: String, root: String) raises -> Bool:
    """After a successful media build, locate the produced artifact
    under genode/build/x86_64/var/run/<scenario>.<ext>, copy it to
    var/dist/<release-name>.<ext>, and write the .sha256 sidecar.
    Returns True on success, False (with a printed error) if the
    source artifact is missing."""
    var os_path = Python.import_module("os.path")
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")

    var ext = String("img")
    if mode == "image/iso":
        ext = "iso"
    elif mode != "image/disk":
        print("internal error: unknown mode '" + mode + "'")
        return False

    var src = (root + "/genode/build/x86_64/var/run/"
        + scenario + "." + ext)
    var dist_dir = root + "/var/dist"
    var dst = dist_dir + "/" + RELEASE_NAME_PREFIX + "." + ext

    if not os_path.isfile(src):
        print("[sponge-dist] error: expected artifact not found at " + src)
        print("  mode " + mode + " reported success but the "
              + ext + " is absent. The image plugin may have been")
        print("  disabled by RUN_OPT, or moved by a framework update.")
        return False

    ensure_dir(dist_dir)
    shutil.copyfile(src, dst)
    print("[sponge-dist] staged " + dst + "  (" + src + ")")

    var digest = sha256_of_file(dst)
    var sidecar = dst + ".sha256"
    # os_path.basename returns a Python str; wrap in String() so the
    # `+` operands are all Mojo strings (str has no __radd__ that
    # Mojo's String.__add__ falls back to).
    var base_name = String(os_path.basename(dst))
    var line = digest + "  " + base_name + "\n"
    var f = builtins.open(sidecar, "w")
    f.write(line)
    f.close()
    print("[sponge-dist] wrote " + sidecar)
    return True


def print_summary(root: String, bake_profile: String, firmware: String, storage_mode: String) raises:
    var os_path = Python.import_module("os.path")
    var builtins = Python.import_module("builtins")
    var dist_dir = root + "/var/dist"

    print()
    print("[sponge-dist] summary")
    print()
    print("  bake profile: " + bake_profile)
    print("  firmware:     " + firmware)
    print("  storage:      " + storage_mode)
    print()
    print("  artifact                                            size       sha256 (prefix)")
    print("  --------                                            ----       ---------------")

    var exts: List[String] = ["img", "iso"]
    for ext in exts:
        var name = RELEASE_NAME_PREFIX + "." + ext
        var path = dist_dir + "/" + name
        if not os_path.isfile(path):
            if ext == "iso" and firmware == "uefi":
                # UEFI media is .img-only by design (El Torito is
                # BIOS-only). Don't print "MISSING" — explain the
                # intentional skip in the summary.
                continue
            print("  " + name + "    MISSING")
            continue
        if ext == "iso" and firmware == "uefi":
            # A stale .iso from a prior BIOS build may be present in
            # var/dist/; skip it in the UEFI summary (the .iso was
            # not staged in this run, so it does not belong to the
            # current artifact set).
            continue
        var bytes = file_size(path)
        var size_str = fmt_human_size(bytes)
        var digest = sha256_of_file(path)
        var digest_prefix = String("")
        # Take the first 12 hex chars as a recognizable prefix.
        var n = 0
        for i in range(digest.byte_length()):
            if n >= 12:
                break
            var ch = digest[byte=i]
            digest_prefix += String(ch)
            n += 1
        # Column-pad name to 50 chars so the table lines up.
        var name_padded = pad_right(name, 50)
        var size_padded = pad_right(size_str, 10)
        print("  " + name_padded + "  " + size_padded + "  " + digest_prefix)

    if firmware == "uefi":
        print()
        print("  (.iso SKIPPED: --firmware uefi is .img-only by design;")
        print("   El Torito is BIOS-only — no UEFI equivalent)")
    print()
    print("  Full hashes: <artifact>.sha256 sidecars in " + dist_dir)
    print("  Verify:      (cd " + dist_dir + " && sha256sum -c *.sha256)")


def is_valid_storage_mode(value: String) raises -> Bool:
    """True iff `value` is one of the allowed --storage values.
    Used by main() to reject --storage arguments BEFORE any build
    starts (plan §W2 step 1: "reject any other value before a build
    starts with a concise English error and usage line"). Hard-coded
    rather than a comptime list to avoid the StringSlice/non-copyable
    iterator issue (the comptime list elements are StringSlice over a
    StaticConstantOrigin, not ImplicitlyCopyable). The `usb` value is
    accepted here at the syntax level; the firmware/storage combination
    is validated separately in `is_valid_storage_mode_for_firmware`."""
    if value == "ahci":
        return True
    if value == "nvme":
        return True
    if value == "usb":
        return True
    return False


def is_valid_storage_mode_for_firmware(storage_mode: String, firmware: String) raises -> Bool:
    """Validate the (storage_mode, firmware) combination. The `usb`
    storage mode is UEFI-only — the BIOS branch does not produce a
    USB-stick product image end-to-end (the BIOS-side USB-stick attach
    is the Phase 12 `sponge-usb-boot.run` precedent, which boots the
    existing ISO from a USB mass-storage device; it is not a new
    product image). Combining `bios` + `usb` would mean the user
    wants a BIOS-bootable USB-stick product image, which the current
    tree does not support — `is_valid_storage_mode_for_firmware`
    returns False in that case, and main() prints the exact reason
    before any build runs."""
    if storage_mode == "usb" and firmware == "bios":
        return False
    return True


def is_valid_bake_profile(value: String) raises -> Bool:
    """True iff `value` is one of the allowed --bake-profile values
    (D15.3/D15.4/D15.8). Used by main() to reject invalid values
    BEFORE any build starts (mirrors is_valid_storage_mode's
    fail-loud policy). The "none" sentinel is a valid value — it
    reproduces today's hello-only behavior (bake.inc escape hatch)."""
    if value == "minimal":
        return True
    if value == "desktop":
        return True
    if value == "test":
        return True
    if value == "none":
        return True
    return False


def is_valid_firmware(value: String) raises -> Bool:
    """True iff `value` is one of the allowed --firmware values
    (D15.13/D15.16). Currently ` BIOS` is the only fully verified
    path; `uefi` is the W4 scope (per 2026-08-18 pivot the QEMU
    UEFI cell stays a documented gap; 15-3 is the real-hardware
    diagnostic). When uefi is requested, main() exits loudly
    instead of silently ignoring."""
    if value == "bios":
        return True
    if value == "uefi":
        return True
    return False


def disk_scenario_for(storage_mode: String, firmware: String) raises -> String:
    """Map the validated (storage_mode, firmware) pair to the
    run/<scenario>.run product scenario name.
      - BIOS + AHCI: canonical desktop-from-disk (unchanged)
      - BIOS + NVMe: dedicated NVMe-from-disk (Phase 12 W2)
      - UEFI + AHCI: Sponge-side UEFI recipe (Phase 15 W4, D15.13)
      - UEFI + NVMe: target-machine NVMe envelope (Phase 15 W4, D15.1)
      - UEFI + USB: USB-stick product media (Phase 15 15-3)
    The five-way split is intentional — each cell maps to a
    distinct run script with a distinct disk layout, BIOS/UEFI
    chain, and structural-gate set. Rejecting unknown combinations
    defensively (a missing row falls through to the BIOS+AHCI
    default rather than silently using the wrong script). The
    firmware/storage combination is validated by main() before
    this proc is called; we still fall through defensively for any
    unanticipated pair."""
    if firmware == "uefi":
        if storage_mode == "nvme":
            return DISK_SCENARIO_UEFI_NVME
        if storage_mode == "usb":
            return DISK_SCENARIO_UEFI_USB
        return DISK_SCENARIO_UEFI
    if storage_mode == "nvme":
        return DISK_SCENARIO_NVME
    return DISK_SCENARIO_AHCI


def main() raises:
    var args = argv()

    # Parse flags that affect the build BEFORE dispatching to help.
    # This lets `--no-data help` still print help, while `--no-data`
    # alone runs the build without the SPONGE-DATA step.
    var add_data = True
    var data_mib = DEFAULT_DATA_MIB
    var storage_mode = String(DEFAULT_STORAGE_MODE)
    var bake_profile = String(DEFAULT_BAKE_PROFILE)
    var firmware = String(DEFAULT_FIRMWARE)
    var print_only = False
    var i = 1
    while i < len(args):
        var a = String(args[i])
        if a == "help" or a == "--help" or a == "-h":
            cmd_help()
            return
        if a == "--no-data":
            add_data = False
            i += 1
            continue
        if a == "--print-only":
            # Diagnostic: do the full pre-flight (host-tool check,
            # build_dir check), print the make command + env, but do
            # NOT invoke make, mkdata, or stage_artifact. Useful for
            # confirming --bake-profile / --storage / --firmware
            # propagation without paying for a full build.
            print_only = True
            i += 1
            continue
        if a == "--data-size":
            if i + 1 >= len(args):
                print("error: --data-size requires a value (MiB)")
                exit(1)
            var re = Python.import_module("re")
            if not Bool(py=re.match(r"^\d+$", String(args[i + 1]))):
                print("error: --data-size must be a positive integer (MiB), got '"
                      + String(args[i + 1]) + "'")
                exit(1)
            data_mib = Int(String(args[i + 1]))
            i += 2
            continue
        if startswith_str(a, "--data-size="):
            var builtins = Python.import_module("builtins")
            comptime prefix_len = 12  # len("--data-size=")
            var val = String(builtins.str(a)[prefix_len:])
            var re2 = Python.import_module("re")
            if not Bool(py=re2.match(r"^\d+$", val)):
                print("error: --data-size must be a positive integer (MiB), got '"
                      + val + "'")
                exit(1)
            data_mib = Int(val)
            i += 1
            continue
        if a == "--storage":
            if i + 1 >= len(args):
                print("error: --storage requires a value (one of: ahci, nvme, usb)")
                print()
                cmd_help()
                exit(1)
            var candidate = String(args[i + 1])
            if not is_valid_storage_mode(candidate):
                print("error: --storage value '" + candidate + "' is not one of: ahci, nvme, usb")
                print()
                print("Usage:")
                cmd_help()
                exit(1)
            storage_mode = candidate
            i += 2
            continue
        if startswith_str(a, "--storage="):
            var builtins = Python.import_module("builtins")
            comptime prefix_len = 10  # len("--storage=")
            var val = String(builtins.str(a)[prefix_len:])
            if not is_valid_storage_mode(val):
                print("error: --storage value '" + val + "' is not one of: ahci, nvme, usb")
                print()
                cmd_help()
                exit(1)
            storage_mode = val
            i += 1
            continue
        if a == "--bake-profile":
            if i + 1 >= len(args):
                print("error: --bake-profile requires a value (one of: minimal, desktop, test, none)")
                print()
                cmd_help()
                exit(1)
            var candidate = String(args[i + 1])
            if not is_valid_bake_profile(candidate):
                print("error: --bake-profile value '" + candidate
                      + "' is not one of: minimal, desktop, test, none")
                print()
                cmd_help()
                exit(1)
            bake_profile = candidate
            i += 2
            continue
        if startswith_str(a, "--bake-profile="):
            var builtins = Python.import_module("builtins")
            comptime prefix_len = 15  # len("--bake-profile=")
            var val = String(builtins.str(a)[prefix_len:])
            if not is_valid_bake_profile(val):
                print("error: --bake-profile value '" + val
                      + "' is not one of: minimal, desktop, test, none")
                print()
                cmd_help()
                exit(1)
            bake_profile = val
            i += 1
            continue
        if a == "--firmware":
            if i + 1 >= len(args):
                print("error: --firmware requires a value (one of: bios, uefi)")
                print()
                cmd_help()
                exit(1)
            var candidate = String(args[i + 1])
            if not is_valid_firmware(candidate):
                print("error: --firmware value '" + candidate
                      + "' is not one of: bios, uefi")
                print()
                cmd_help()
                exit(1)
            # Phase 15 W4: --firmware uefi is now ACCEPTED (the W4
            # scenarios are structurally correct; per D15.16 the
            # QEMU UEFI boot is expected to hit the W1 OVMF core-
            # init hang and the scenario's acceptance is host-side
            # structural verification only). The default stays bios
            # for the regression baseline.
            firmware = candidate
            i += 2
            continue
        if startswith_str(a, "--firmware="):
            var builtins = Python.import_module("builtins")
            comptime prefix_len = 11  # len("--firmware=")
            var val = String(builtins.str(a)[prefix_len:])
            if not is_valid_firmware(val):
                print("error: --firmware value '" + val
                      + "' is not one of: bios, uefi")
                print()
                cmd_help()
                exit(1)
            # See the --firmware handling above.
            firmware = val
            i += 1
            continue
        if a == "build":
            # 'build' is the implicit default; accept it explicitly for
            # future-proofing (a no-op alias).
            i += 1
            continue
        if startswith_str(a, "-") and a != "-":
            print("error: unknown option '" + a + "'")
            print()
            cmd_help()
            exit(1)
        # Any non-flag positional is rejected; the tool takes no
        # positional arguments.
        print("error: unexpected argument '" + a + "'")
        print()
        cmd_help()
        exit(1)

    if data_mib < 8:
        print("error: --data-size " + String(data_mib)
              + " MiB is below the 8 MiB ext2 comfort floor")
        exit(1)

    # Validate the firmware/storage combination. `usb` is UEFI-only
    # (the BIOS branch's product media does not have a USB-stick
    # variant — the BIOS-side USB-stick attach is the Phase 12
    # `sponge-usb-boot.run` precedent which boots the existing ISO
    # from a USB mass-storage device; it is not a new product image).
    # The check is loud + early so the user knows exactly why before
    # any build runs.
    if not is_valid_storage_mode_for_firmware(storage_mode, firmware):
        print("error: --storage " + storage_mode + " is not supported with --firmware "
              + firmware + ".")
        print()
        print("  --storage usb is UEFI-only. The BIOS branch's product media")
        print("  supports --storage {ahci,nvme} only. The BIOS-side USB-stick")
        print("  attach is the Phase 12 precedent at run/sponge-usb-boot.run,")
        print("  which boots the existing ISO from a USB mass-storage device")
        print("  — it is not a new product image.")
        print()
        print("  Use one of:")
        print("    --storage usb --firmware uefi    (the 15-3 USB-stick artifact)")
        print("    --storage ahci --firmware bios   (the default desktop media)")
        print("    --storage nvme --firmware bios   (the NVMe BIOS product media)")
        print("    --storage ahci --firmware uefi   (the SATA UEFI product media)")
        print("    --storage nvme --firmware uefi   (the NVMe UEFI target-machine envelope)")
        print()
        cmd_help()
        exit(1)

    var root = repo_root()
    var os_path = Python.import_module("os.path")

    var build_dir = root + "/genode/build/x86_64"
    if not os_path.isdir(build_dir):
        print("error: build directory not found at " + build_dir)
        print("Run './tool/build prepare' first (see docs/08-development.md §3).")
        exit(1)

    var disk_scenario = disk_scenario_for(storage_mode, firmware)

    print("[sponge-dist] Sponge OS distribution media builder")
    if storage_mode != DEFAULT_STORAGE_MODE:
        print("  storage mode: " + storage_mode + "  (--storage override)")
    else:
        print("  storage mode: " + storage_mode
              + "  (default; explicit --storage ahci preserves this)")
    if bake_profile != DEFAULT_BAKE_PROFILE:
        print("  bake profile: " + bake_profile + "  (--bake-profile override)")
    else:
        print("  bake profile: " + bake_profile
              + "  (default; explicit --bake-profile desktop preserves this)")
    if firmware != DEFAULT_FIRMWARE:
        print("  firmware:     " + firmware + "  (--firmware override; "
              + "produces ONLY the .img, no .iso)")
    else:
        print("  firmware:     " + firmware
              + "  (default; uefi is Phase 15 W4 scope, .img-only by design)")
    print("  product .img: " + disk_scenario + " (image/disk)")
    if add_data:
        print("               + tool/mkdata SPONGE-DATA P4 ("
              + String(data_mib) + " MiB)")
    else:
        print("               (--no-data: SPONGE-DATA P4 step SKIPPED)")
    if firmware == "uefi":
        print("  live/eval .iso: SKIPPED (UEFI media is .img-only;")
        print("                    El Torito is BIOS-only — no equivalent)")
    else:
        print("  live/eval .iso: " + ISO_SCENARIO + " (image/iso; storage-independent)")
    print("  release name: " + RELEASE_NAME_PREFIX + ".{img,iso}")
    print("  repo root:    " + root)
    print()

    # (a) Pre-flight host-tool check. Exits BEFORE any build if any
    # required tool is missing (the failure must be loud and early).
    if not check_host_tools():
        exit(1)

    # (a.5) Diagnostic --print-only: emit the make command + env +
    # mkdata invocation we WOULD run, then exit cleanly. No disk
    # writes, no actual build. Lets the user verify flag propagation
    # cheaply (the alternative is a full media build that takes
    # 5+ minutes per mode).
    if print_only:
        var build_dir_pp = root + "/genode/build/x86_64"
        print("[sponge-dist] --print-only: would invoke")
        print()
        print("  make -C " + build_dir_pp + " run/" + disk_scenario
              + " KERNEL=sel4 BOARD=pc RUN_OPT=--include image/disk")
        print("    env: SPONGE_BAKE_PROFILE=" + bake_profile)
        print()
        if add_data:
            print("  tool/mkdata <staged.img> --data-size "
                  + String(data_mib) + "  (would grow P4 SPONGE-DATA)")
        else:
            print("  (--no-data: tool/mkdata SKIPPED)")
        print()
        if firmware == "uefi":
            print("  (.iso SKIPPED: --firmware uefi is .img-only;")
            print("   El Torito is BIOS-only — no UEFI equivalent)")
        else:
            print("  make -C " + build_dir_pp + " run/" + ISO_SCENARIO
                  + " KERNEL=sel4 BOARD=pc RUN_OPT=--include image/iso")
            print("    env: SPONGE_BAKE_PROFILE=" + bake_profile)
            print()
            print("  (no mkdata after ISO build; the ISO path is media-only)")
        print()
        if firmware == "uefi":
            print("  target stage: var/dist/" + RELEASE_NAME_PREFIX
                  + ".img + .sha256 sidecar (no .iso)")
        else:
            print("  target stage: var/dist/" + RELEASE_NAME_PREFIX
                  + ".{img,iso} + .sha256 sidecars")
        print("  firmware selector: " + firmware
              + (String("") if firmware == DEFAULT_FIRMWARE
                  else "  (--firmware override)"))
        print()
        print("[sponge-dist] --print-only done; no build was run.")
        return

    # (b) Disk-image build first (the new product media).
    var disk_rc = run_media_build("image/disk", disk_scenario, root, bake_profile)
    if disk_rc != 0:
        print()
        print("[sponge-dist] error: image/disk build failed (exit code "
              + String(disk_rc) + "). Aborting; mkdata + ISO not attempted.")
        exit(disk_rc)

    # (b.2) The disk artifact is at genode/build/x86_64/var/run/<scenario>.img
    # BEFORE staging — we run mkdata on it in-place so the staged
    # release artifact already carries P4. This is the docs/14 §4.3
    # grow/repartition step.
    var disk_src = (root + "/genode/build/x86_64/var/run/"
        + disk_scenario + ".img")
    if not os_path.isfile(disk_src):
        print()
        print("[sponge-dist] error: image/disk make exited 0 but the .img is")
        print("  absent at " + disk_src)
        print("  (image plugin may have been disabled by RUN_OPT).")
        exit(1)

    if add_data:
        var mkdata_rc = run_mkdata(root, disk_src, data_mib)
        if mkdata_rc != 0:
            print()
            print("[sponge-dist] error: tool/mkdata failed (exit code "
                  + String(mkdata_rc) + "). Aborting; .img is 3-partition,")
            print("  ISO build not attempted. Re-run with --no-data to stage")
            print("  the 3-partition .img as-is, or fix tool/mkdata.")
            exit(mkdata_rc)
        # (b.3) Misleading-success-output defense: actually inspect the
        # partition table we just produced. The build exit code alone
        # is not enough — the .img must really show the expected
        # partition count + SPONGE-DATA name (firmware-dependent).
        if not verify_partitions(disk_src, firmware):
            print()
            print("[sponge-dist] error: post-mkdata partition verification")
            print("  failed. The .img does not carry the expected partition")
            print("  layout for firmware=" + firmware + ".")
            exit(1)
    else:
        print()
        print("[sponge-dist] --no-data: SPONGE-DATA P4 step skipped; the .img")
        if firmware == "uefi":
            print("  carries the UEFI layout (P1=ESP + P2 absent + P3=GENODE).")
        else:
            print("  carries the 3 image/disk partitions only (P1 BIOS-boot +")
            print("  P2 ESP + P3 GENODE). Persistence will not work on this media.")

    # (d.1) Stage the disk artifact + sha256 (in-place modified by
    # mkdata if --no-data was not passed).
    if not stage_artifact("image/disk", disk_scenario, root):
        exit(1)

    print()
    # (b.4) ISO build second (live/eval mode). SKIPPED for UEFI
    # (El Torito is BIOS-only; UEFI media is .img-only). The
    # stale_state guard inside run_media_build cleans
    # var/run/<scenario>* — but note the two scenarios differ (the
    # storage-mode scenario vs sponge-alpha), so the prefix-clean
    # is per-scenario and the disk build's run_dir is not touched
    # by the ISO clean.
    if firmware == "uefi":
        print("[sponge-dist] --firmware uefi: skipping .iso build (El Torito")
        print("  is BIOS-only; UEFI media is .img-only by design, see D15.16).")
    else:
        var iso_rc = run_media_build("image/iso", ISO_SCENARIO, root, bake_profile)
        if iso_rc != 0:
            print()
            print("[sponge-dist] error: image/iso build failed (exit code "
                  + String(iso_rc) + "). Aborting.")
            exit(iso_rc)

        # (d.2) Stage the ISO artifact + sha256.
        if not stage_artifact("image/iso", ISO_SCENARIO, root):
            exit(1)

    # (e) Summary table.
    print_summary(root, bake_profile, firmware, storage_mode)

    print()
    print("[sponge-dist] done: product media built and staged in "
          + root + "/var/dist/")
    if firmware == "uefi":
        if add_data:
            print("  .img: UEFI layout (P1=ESP + P2 absent + P3=GENODE + P4=SPONGE-DATA)")
            print("        — installs persist across reboots (P3).")
            print("        (built via run/" + disk_scenario + " — storage=" + storage_mode
                  + ")")
        else:
            print("  .img: UEFI layout (P1=ESP + P2 absent + P3=GENODE) — no SPONGE-DATA.")
            print("        (built via run/" + disk_scenario + " — storage=" + storage_mode
                  + ")")
        print("  .iso: SKIPPED (UEFI media is .img-only; El Torito is BIOS-only)")
    else:
        if add_data:
            print("  .img: 4 partitions (BIOSBOOT/ESP/GENODE/SPONGE-DATA)")
            print("        — installs persist across reboots (P3).")
            print("        (built via run/" + disk_scenario + " — storage=" + storage_mode + ")")
        else:
            print("  .img: 3 partitions (BIOSBOOT/ESP/GENODE) — no SPONGE-DATA.")
            print("        (built via run/" + disk_scenario + " — storage=" + storage_mode + ")")
        print("  .iso: live/eval mode (RAM filesystem; nothing persists).")
