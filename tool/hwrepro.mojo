# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# tool/hwrepro — real-hardware stall reproduction harness (QEMU/OVMF).
#
# Boots a product .img under the MINIMAL QEMU configuration that
# deterministically reproduces the LG gram 17ZD90N real-hardware
# stall on BOTH stack generations:
#
#   -cpu host -m size=4G,slots=4,maxmem=16G
#
# `-cpu host` (Ice Lake host, 52 phys-bits) plus the hotplug-window
# declaration makes OVMF place the xHCI controller's 16 KiB MMIO BAR
# at 0x380000000000 (54 TiB, the 64-bit PCI window). Neither stack
# can actually use a BAR that high, and each dies its own way —
# same user-visible signature:
#
#   current stack (26.08 + seL4 16.0.0), 4/4:
#     Error: I/O memory [0000380000000000,0000380000004000) not available
#     Error: unable to access MMIO mapping: base=0x380000000000 ...
#     [init -> usb] ... abort called - thread: ep
#     (core's io_mem allocator refuses the unknown range; the
#      platform failure cascades into the usb driver aborting)
#
#   old stack (26.05 + seL4 13.0.0, media sponge-test-uefi-usb-
#   20260824.img), 3/3:
#     [init -> usb] xhci_hcd 00:03.0: xHCI HW did not halt ... status = 0x0
#     [init -> usb] xhci_hcd 00:03.0: Host halt failed, -110
#     [init -> usb] Will sleep forever...
#     (the mapping silently reads zeroes; the xHCI probe times out
#      and the DDE driver deadlocks)
#
# In both cases usb dies → usb_block never serves P3 → Tier-0 alone
# survives (background + pointer cursor — both Tier-0), no storage
# chain, no system init, no desktop panel, no mouse — the exact
# on-panel state reported from the 17ZD90N on BOTH the 26.05/13.0.0
# era media and the 26.08/16.0.0 media.
#
# Real-machine mapping: the gram's firmware places its xHCI BAR at
# 0x601d140000 (384 GiB — also a high 64-bit window). The QEMU
# twin places the emulated BAR at 54 TiB. Same class: high device
# MMIO the stack cannot map.
#
# Bisection notes (2026-09-14): VT-d/intremap/irqchip-split and
# storage throttling are NOT required (the earlier attribution to
# the DMAR MMIO was a coincidence — the 0x380000000000 range is
# the xHCI BAR itself; V4 had no intel-iommu at all and died the
# same way). Cascadelake-Server (46 phys-bits) boots CLEAN under
# the identical config — the trigger is host-CPU phys-bits + maxmem.
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
#   REPRODUCED  either stall signature found (A: MMIO refusal,
#               B: xHCI halt timeout — see above)
#   CLEAN       boot reached alpha-probe: PASS
#   OTHER       neither signature — inspect the log tail
# The full serial log is always saved to var/hwrepro/<img>.log.
#
# Manual equivalent (AGENTS §3.5 escape hatch): the exact QEMU
# command is printed by --dry-run.

from std.sys import argv, exit
from std.python import Python


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

    if not Bool(py=pathlib.Path(img).exists()):
        print("hwrepro: media not found: " + img)
        print("        build one: ./tool/dist --storage usb --firmware uefi"
              + " --bake-profile desktop")
        exit(1)
        return

    if not Bool(py=pathlib.Path("var/ovmf/OVMF_CODE.fd").exists()):
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

    # GRUB-menu media (the August diag images) wait for Enter —
    # select the default entry via QMP at t+100 s. Production media
    # auto-boot and the keypress is harmless.
    var helper = "var/hwrepro/_grub_enter.py"
    var builtins0 = Python.import_module("builtins")
    var hfh = builtins0.open(helper, "w")
    hfh.write(
        "import socket, time\n"
        "s = socket.create_connection((\"127.0.0.1\", 44460), timeout=10)\n"
        "f = s.makefile(\"rw\")\n"
        "f.readline()\n"
        "f.write('{\"execute\":\"qmp_capabilities\"}\\n'); f.flush(); f.readline()\n"
        "for n in range(2):\n"
        "    s.sendall(b'{\"execute\":\"input-send-event\",\"arguments\":"
        "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":true,"
        "\"key\":{\"type\":\"qcode\",\"data\":\"ret\"}}},"
        "{\"type\":\"key\",\"data\":{\"down\":false,"
        "\"key\":{\"type\":\"qcode\",\"data\":\"ret\"}}}]}}\\n')\n"
        "    time.sleep(4)\n"
    )
    hfh.close()

    # The minimal reproduction config (deterministic on both stack
    # generations; see the header). Run under `timeout` via bash so
    # the expected never-exits QEMU is killed cleanly and bash
    # returns 124 instead of raising.
    var cmd = "timeout 500 qemu-system-x86_64"
    cmd += " -accel kvm"
    cmd += " -machine q35"
    cmd += " -cpu host -smp 8"
    cmd += " -m size=4G,slots=4,maxmem=16G"
    cmd += " -device nec-usb-xhci,id=xhci"
    cmd += " -device usb-mouse,bus=xhci.0"
    cmd += " -device usb-storage,drive=stick,bus=xhci.0"
    cmd += " -drive id=stick,format=raw,file=" + img + ",if=none"
    cmd += (" -drive if=pflash,format=raw,readonly=on,"
            + "file=var/ovmf/OVMF_CODE.fd")
    cmd += " -drive if=pflash,format=raw,file=" + varscopy
    cmd += " -fw_cfg name=opt/org.tianocore/UninstallMemAttrProtocol,string=yes"
    cmd += " -nographic -snapshot -serial mon:stdio"
    cmd += " -qmp tcp:127.0.0.1:44460,server=on,wait=off"
    cmd += " -drive format=raw,file=" + img
    cmd += " > " + logpath + " 2>&1"
    # the GRUB-menu keypress sender runs alongside QEMU
    cmd = "( sleep 100 && python3 " + helper + " > /dev/null 2>&1 ) &\n" + cmd

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

    # signature A (current stack): core refuses the high MMIO range
    var mmio_refused = log.find("I/O memory") >= 0 \
        and log.find("not available") >= 0 \
        and log.find("abort called") >= 0
    # signature B (old stack): xHCI MMIO reads zeroes, probe times out
    var halt_timeout = log.find("halt failed, -110") >= 0 \
        or log.find("Will sleep forever") >= 0

    if mmio_refused or halt_timeout:
        var tag = String("REPRODUCED — real-hw stall signature")
        if mmio_refused:
            tag += " [A: MMIO refusal]"
        if halt_timeout:
            tag += " [B: xHCI halt timeout]"
        print(tag + ":")
        var lines = log.split("\n")
        for ln in lines:
            var s = String(ln)
            if s.find("I/O memory") >= 0 or s.find("MMIO mapping") >= 0 \
                    or s.find("abort called") >= 0 \
                    or s.find("halt failed") >= 0 \
                    or s.find("did not halt") >= 0 \
                    or s.find("Will sleep forever") >= 0:
                print("  | " + s.rstrip())
        print("  (high device MMIO unusable by this stack; usb died →")
        print("   no storage chain, no panel, no mouse — Tier-0 only)")
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
