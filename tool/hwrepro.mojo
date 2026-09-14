# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# tool/hwrepro — real-hardware stall reproduction harness (QEMU/OVMF).
#
# Boots a product .img under the QEMU configuration that
# deterministically reproduces the LG gram 17ZD90N real-hardware
# stall (2/2 verified 2026-09-14):
#
#   -machine q35,kernel-irqchip=split -device intel-iommu,intremap=on
#   -cpu host -smp 8 -m size=4G,slots=4,maxmem=16G + 3x 2G pc-dimms
#   + a throttled (10 MB/s, 100 iops) read-only usb-storage stick and
#     a fast SATA boot copy of the same media
#
# The dimms/maxmem combination makes OVMF place the emulated VT-d
# unit's MMIO at 0x380000000000 — a range that is NOT in core's
# io_mem allocator — and the boot dies exactly like the real panel:
#
#   Error: I/O memory [0000380000000000,0000380000004000) not available
#   Error: unable to access MMIO mapping: base=0x380000000000 ...
#   [init -> usb] ... abort called - thread: ep
#
# → the usb driver aborts, so usb_block never serves P3: Tier-0
# survives (background + pointer cursor — both Tier-0), but there is
# no storage chain, no system init, no desktop panel, and no mouse —
# the exact on-panel state reported from the 17ZD90N (2026-09-14).
#
# This harness exists so a fix for the real-hardware stall can be
# developed and gated in QEMU instead of on the laptop (the panel
# has no serial; every real-hw iteration is a reflash + eyeball).
#
# Usage:
#   ./tool/hwrepro                 # boot the default product media
#   ./tool/hwrepro <path/to.img>   # boot a specific media image
#
# Verdict (exit 0 both ways — this is a classifier, not a gate):
#   REPRODUCED  stall signature found (core MMIO refusal + usb abort)
#   CLEAN       boot reached alpha-probe: PASS
#   OTHER       neither signature — inspect the log tail
# The full serial log is always saved to var/hwrepro/<img>.log.
#
# Manual equivalent (AGENTS §3.5 escape hatch): the exact QEMU
# command is printed by --dry-run.

from std.sys import argv, exit
from std.python import Python, PythonObject


def main() raises:
    var args = argv()

    var img = String("var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img")
    var dry = False
    var i = 1  # args[0] is the script path (mojo argv() includes it)
    while i < len(args):
        var a = String(args[i])
        if a == "--dry-run":
            dry = True
        elif a == "--help" or a == "-h":
            print("usage: tool/hwrepro [img] [--dry-run]")
            return
        elif not a.startswith("-"):
            img = a
        i += 1

    var pathlib = Python.import_module("pathlib")
    var os_py = Python.import_module("os")

    if not Bool(py=pathlib.Path(img).exists()):
        print("hwrepro: media not found: " + img)
        print("        build one: ./tool/dist --storage usb --firmware uefi"
              + " --bake-profile desktop")
        exit(1)
        return

    var varovmf = "var/ovmf"
    if not Bool(py=pathlib.Path(varovmf + "/OVMF_CODE.fd").exists()):
        print("hwrepro: OVMF firmware missing (var/ovmf/OVMF_CODE.fd)")
        exit(1)
        return

    # per-run writable copy of the pinned OVMF vars
    var logdir = pathlib.Path("var/hwrepro")
    logdir.mkdir(parents=True, exist_ok=True)
    var stem = String(pathlib.Path(img).stem)
    var varscopy = "var/hwrepro/OVMF_VARS." + stem + ".fd"
    var sh_py = Python.import_module("shutil")
    sh_py.copyfile("var/ovmf/OVMF_VARS.fd", varscopy)

    var logpath = "var/hwrepro/" + stem + ".log"

    # The reproduction config (deterministic 2/2; see header). Run
    # under `timeout` via bash so the expected never-exits QEMU is
    # killed cleanly and bash returns 124 instead of raising.
    var cmd = "timeout 500 qemu-system-x86_64"
    cmd += " -accel kvm"
    cmd += " -machine q35,kernel-irqchip=split"
    cmd += " -device intel-iommu,intremap=on"
    cmd += " -cpu host -smp 8"
    cmd += " -m size=4G,slots=4,maxmem=16G"
    cmd += (" -object memory-backend-ram,id=m0,size=2G"
            + " -object memory-backend-ram,id=m1,size=2G"
            + " -object memory-backend-ram,id=m2,size=2G")
    cmd += (" -device pc-dimm,memdev=m0,id=d0,slot=0"
            + " -device pc-dimm,memdev=m1,id=d1,slot=1"
            + " -device pc-dimm,memdev=m2,id=d2,slot=2")
    cmd += " -object throttle-group,id=tg,x-bps-total=10000000,x-iops-total=100"
    cmd += (" -blockdev driver=file,filename=" + img
            + ",node-name=stickfile,read-only=on")
    cmd += " -blockdev driver=raw,file=stickfile,node-name=stickraw,read-only=on"
    cmd += (" -blockdev driver=throttle,file=stickraw,throttle-group=tg"
            + ",node-name=stick,read-only=on")
    cmd += " -device nec-usb-xhci,id=xhci"
    cmd += " -device usb-mouse,bus=xhci.0"
    cmd += " -device usb-storage,drive=stick,bus=xhci.0"
    cmd += (" -drive if=pflash,format=raw,readonly=on,"
            + "file=var/ovmf/OVMF_CODE.fd")
    cmd += " -drive if=pflash,format=raw,file=" + varscopy
    cmd += " -fw_cfg name=opt/org.tianocore/UninstallMemAttrProtocol,string=yes"
    cmd += " -nographic -snapshot -serial mon:stdio"
    cmd += " -drive format=raw,file=" + img
    cmd += " > " + logpath + " 2>&1"

    print("hwrepro: booting " + img + " (<=500 s, log: " + logpath + ")")
    if dry:
        print(cmd)
        return

    var subprocess = Python.import_module("subprocess")
    var _rc = subprocess.run(["bash", "-c", cmd])

    var builtins = Python.import_module("builtins")
    var fh = builtins.open(logpath, "r")
    var log = String(fh.read())
    fh.close()

    print("\n==== hwrepro verdict ====")
    if log.find("I/O memory") >= 0 and log.find("not available") >= 0 \
            and log.find("abort called") >= 0:
        print("REPRODUCED — real-hw stall signature:")
        var lines = log.split("\n")
        for ln in lines:
            if ln.find("I/O memory") >= 0 or ln.find("MMIO mapping") >= 0 \
                    or ln.find("abort called") >= 0:
                print("  | " + String(ln).rstrip())
        print("  (core refused an MMIO range absent from its io_mem")
        print("   allocator; the platform failure cascaded into the")
        print("   usb driver abort — Tier-0 only, no panel, no mouse)")
        return

    if log.find("alpha-probe: PASS") >= 0:
        print("CLEAN — boot reached alpha-probe: PASS (no stall)")
        return

    print("OTHER — no known signature; last lines:")
    var lines2 = log.split("\n")
    var shown = 0
    var idx = len(lines2) - 1
    while idx >= 0 and shown < 8:
        var ln2 = String(lines2[idx]).rstrip()
        if ln2 != String(""):
            print("  | " + ln2)
            shown += 1
        idx -= 1
