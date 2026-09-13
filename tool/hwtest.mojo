# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# tool/hwtest — hardware-matrix test driver.
#
# Drives run/sponge-hw-matrix.run across a curated matrix of QEMU
# emulated-hardware variants (CPU, SMP, memory, machine, display,
# USB controller, input device) and reports a PASS/FAIL table.
#
# Flags:
#   (no args)       run the full default matrix (~16 boots)
#   --list          list variant names, run nothing
#   --only a,b,c    run only the named variants
#   --dry-run       print the would-be commands, run nothing
#
# Manual escape hatch (AGENTS §3.5): every variant is a plain
#   SPONGE_HW_VARIANT=<name> SPONGE_HW_CPU=<cpu> ... \
#       make -C genode/build/x86_64 run/sponge-hw-matrix \
#       KERNEL=sel4 BOARD=pc
# invocation — see docs/08 §"hardware matrix".

from std.sys import argv, exit
from std.python import Python, PythonObject


struct Variant(Copyable, Movable):
    """One hardware-matrix variant: one axis (or a named shakedown
    combination) varying against the proven baseline."""

    var name: String
    var cpu: String
    var smp: String
    var mem: String
    var machine: String
    var vga: String
    var usb: String
    var input: String

    def __init__(out self, name: String, cpu: String, smp: String,
                 mem: String, machine: String, vga: String,
                 usb: String, input: String):
        self.name = name
        self.cpu = cpu
        self.smp = smp
        self.mem = mem
        self.machine = machine
        self.vga = vga
        self.usb = usb
        self.input = input

    def env_dict(self) raises -> PythonObject:
        """Parent environment overlaid with this variant's knobs."""
        var os_py = Python.import_module("os")
        var d = os_py.environ.copy()
        d["SPONGE_HW_VARIANT"] = self.name
        d["SPONGE_HW_CPU"] = self.cpu
        d["SPONGE_HW_SMP"] = self.smp
        d["SPONGE_HW_MEM"] = self.mem
        d["SPONGE_HW_MACHINE"] = self.machine
        d["SPONGE_HW_VGA"] = self.vga
        d["SPONGE_HW_USB"] = self.usb
        d["SPONGE_HW_INPUT"] = self.input
        return d

    def cmd_line(self) -> String:
        var cmd = "SPONGE_HW_VARIANT=" + self.name
        cmd += " SPONGE_HW_CPU=" + self.cpu
        cmd += " SPONGE_HW_SMP=" + self.smp
        cmd += " SPONGE_HW_MEM=" + self.mem
        cmd += " SPONGE_HW_MACHINE=" + self.machine
        cmd += " SPONGE_HW_VGA=" + self.vga
        cmd += " SPONGE_HW_USB=" + self.usb
        cmd += " SPONGE_HW_INPUT=" + self.input
        cmd += " make -C genode/build/x86_64 run/sponge-hw-matrix"
        cmd += " KERNEL=sel4 BOARD=pc"
        return cmd


def variant(name: String, cpu: String = "Skylake-Client",
            smp: String = "1", mem: String = "2G",
            machine: String = "q35", vga: String = "std",
            usb: String = "xhci", input: String = "tablet") -> Variant:
    return Variant(name, cpu, smp, mem, machine, vga, usb, input)


def default_matrix() raises -> List[Variant]:
    """Baseline first; each other row varies ONE axis so a FAIL
    localizes the culprit. ~16 boots, 1-4 min each under KVM."""
    var v: List[Variant] = []

    # baseline (the proven interactive-stack configuration)
    v.append(variant("baseline"))

    # CPU models — Cascadelake-Server = the Ice-Lake-generation proxy
    # (this QEMU build ships no IceLake-Client; 'max' covers the feature
    # superset). NOTE: seL4 16.0.0 requires XSAVE and halts with
    # "XSAVE not supported" on XSAVE-less models (verified: qemu64,
    # Nehalem) — those are expected-unsupported, not regressions.
    v.append(variant("cpu-max", cpu="max"))
    v.append(variant("cpu-haswell", cpu="Haswell"))
    v.append(variant("cpu-cascadelake-server", cpu="Cascadelake-Server"))
    v.append(variant("cpu-sandybridge", cpu="SandyBridge"))

    # topology / memory
    v.append(variant("smp2", smp="2"))
    v.append(variant("smp4", smp="4"))
    v.append(variant("smp8", smp="8"))
    v.append(variant("mem4g", mem="4G"))

    # machine / display / USB controller
    v.append(variant("machine-pc", machine="pc"))
    v.append(variant("usb-ehci", usb="ehci"))

    # input devices
    v.append(variant("input-mouse", input="mouse"))
    v.append(variant("input-kbd", input="kbd"))
    v.append(variant("input-ps2", input="ps2"))

    # combined shakedown (the heaviest single-axis stack-up)
    v.append(variant("shakedown",
                     cpu="max", smp="4", mem="4G", input="mouse"))
    return v^


def run_variant(v: Variant, dry: Bool) raises -> Bool:
    var subprocess = Python.import_module("subprocess")

    print("== variant " + v.name)
    print("   " + v.cmd_line())

    if dry:
        return True

    var time_py = Python.import_module("time")
    var t0 = time_py.time()

    var result = subprocess.run(
        ["make", "-C", "genode/build/x86_64",
         "run/sponge-hw-matrix", "KERNEL=sel4", "BOARD=pc"],
        env=v.env_dict(),
        capture_output=True,
        text=True,
    )

    var secs = Int(py=time_py.time() - t0)
    var rc = Int(py=result.returncode)
    var out = String(result.stdout) + String(result.stderr)

    var found = out.find("sponge-hw-matrix: PASS")
    var passed = rc == 0 and found >= 0

    if passed:
        print("   -> PASS (rc=" + String(rc) + ", "
              + String(secs) + "s)")
    else:
        print("   -> FAIL (rc=" + String(rc) + ", "
              + String(secs) + "s)")
        # surface the tail of the output for quick triage
        var lines = out.split("\n")
        var shown = 0
        var idx = len(lines) - 1
        while idx >= 0 and shown < 6:
            var ln = String(lines[idx]).rstrip()
            if ln != String(""):
                print("     | " + ln)
                shown += 1
            idx -= 1
    return passed


def main() raises:
    var args = argv()

    var list_only = False
    var dry = False
    var only: List[String] = []
    var i = 0
    while i < len(args):
        var a = String(args[i])
        if a == "--list":
            list_only = True
        elif a == "--dry-run":
            dry = True
        elif a == "--only":
            i += 1
            if i < len(args):
                for name in String(args[i]).split(","):
                    only.append(String(String(name).strip()))
        i += 1

    var variants = default_matrix()

    if list_only:
        for v in variants:
            print(v.name)
        return

    var selected: List[Variant] = []
    if len(only) > 0:
        for v in variants:
            for name in only:
                if v.name == name:
                    selected.append(v.copy())
        if len(selected) == 0:
            print("no matching variants for --only")
            exit(2)
            return
    else:
        for v in variants:
            selected.append(v.copy())

    var names: List[String] = []
    var verdicts: List[Bool] = []
    for v in selected:
        var ok = run_variant(v, dry)
        names.append(v.name)
        verdicts.append(ok)

    print("\n==== hardware matrix summary ====")
    var failed = 0
    var idx = 0
    while idx < len(names):
        var verdict = "FAIL" if not verdicts[idx] else "PASS"
        print(names[idx] + ": " + verdict)
        if not verdicts[idx]:
            failed += 1
        idx += 1

    if failed > 0:
        print(String(failed) + "/" + String(len(names))
              + " variants FAILED")
        exit(1)
    else:
        print("all " + String(len(names)) + " variants PASS")
