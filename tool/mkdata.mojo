# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS SPONGE-DATA (P4) partition creator.
#
# Implements the docs/14-boot-storage-architecture.md §4.3 P4 creation
# sequence: grows a 3-partition image/disk .img by a data area and adds a
# fourth GPT partition named SPONGE-DATA, formatted ext2, ready to back
# the writable user-area stores (pkgd installed-set, future configd
# store, user files) — docs/14 §6.
#
# The tool is a Mojo host-side helper (AGENTS.md §3.5). It shells out to
# the same host tools the Genode run framework's image/disk plugin
# already uses (sgdisk, mkfs.ext2, truncate) — no vendored-tree patch,
# no re-implementation (AGENTS.md §5.2). The manual equivalent of every
# step is documented in docs/11-environment.md §7.3 and §12 (control
# escape hatch, AGENTS.md §3.5).
#
# === The sequence (docs/14 §4.3) ===
#
# After image/disk produces <img> (P1 BIOS-boot, P2 ESP, P3 GENODE ext2,
# packed as header+P3+backup-GPT-gap with NO free sectors), this tool:
#
#   1. truncate -s +<data_bytes> <img>          # grow the file
#   2. sgdisk --delete=3 <img>                  # drop P3 entry (bytes stay)
#   3. sgdisk --move-second-header <img>        # backup GPT -> new end
#   4. sgdisk --new=3:<first>:<last> <img>      # re-pin P3 at its old offset
#      sgdisk --change-name=3:GENODE <img>      # re-apply the P3 name
#   5. sgdisk --new=4:<p4_first>:<p4_last> <img> # create SPONGE-DATA
#      sgdisk --change-name=4:SPONGE-DATA <img>
#   6. mkfs.ext2 -E offset=<p4_byte_off> -L SPONGE-DATA -F <img> <p4_kib>
#   7. sgdisk --hybrid <img>                    # rebuild hybrid MBR (P1-P3)
#
# Why delete+recreate P3 instead of just creating P4 in the new free
# space: image/disk packs P3 flush against the (old) backup GPT, so the
# delete+recreate cycle is the docs/14-spec'd defensive sequence that
# guarantees a clean table after --move-second-header. The P3 byte range
# is unchanged (same first/last sector captured BEFORE step 1), so the
# GENODE ext2 content is untouched on disk.
#
# === Hybrid MBR 3-entry limit (docs/14 §4.3 note) ===
#
# sgdisk --hybrid builds the hybrid MBR from the FIRST THREE GPT
# partitions (P1-P3). P4 therefore exists in the GPT only — visible to
# `sgdisk -l`, GRUB, and any Linux host, but NOT listed by MBR-only
# tools. This is fine for Sponge OS (GRUB boots from P1/P2; Linux hosts
# inspect with sgdisk). Documented in docs/11.
#
# === Idempotency ===
#
# The destructive sequence is run ONCE. A second invocation detects
# that P4 already exists with name SPONGE-DATA and exits 0 after a
# verification print (no-op). If P4 exists with a DIFFERENT name the
# tool errors loudly rather than overwriting — fail-loud, never silent
# corruption (AGENTS.md §1.4).
#
# === What this tool does NOT do ===
#
#   - It does NOT boot-verify the partition. The run scenario
#     (run/sponge-persist-disk.run) does that.
#   - It does NOT touch anything outside <img>. The only mutated path
#     is the image file passed on the command line.
#   - It does NOT depend on a build directory — it operates on an
#     already-produced .img, so it can be run by hand or by a run
#     script's [exec] between build_boot_image and run_genode_until.
#
# Usage:
#   mojo tool/mkdata.mojo <img>                  # add a 1024 MiB SPONGE-DATA P4
#   mojo tool/mkdata.mojo <img> --data-size 256  # 256 MiB P4
#   mojo tool/mkdata.mojo help                   # show this usage
#
# Exit codes: 0 success (created or already-present), 1 usage/host-tool/
#   sgdisk/mkfs failure, 2 P4 exists with an unexpected name.

from std.sys import argv, exit
from std.python import Python, PythonObject

comptime SECTOR_SIZE = 512
# GPT backup header (1 sector) + partition entry array (32 sectors by
# default) = 33 sectors at the very end of the image that must NOT be
# covered by P4. We reserve 34 to leave a one-sector gap (matches
# sgdisk's own default alignment-friendly behaviour).
comptime BACKUP_GPT_SECTORS = 34

comptime DEFAULT_DATA_MIB = 1024
comptime P4_LABEL = "SPONGE-DATA"
comptime P3_LABEL = "GENODE"


# Result of running an external command with captured stdout+stderr.
struct Captured(Copyable, Movable):
    var rc: Int
    var output: String

    def __init__(out self):
        self.rc = 0
        self.output = String("")


# Result of parsing `sgdisk -i <N> <img>`. `present` is False when sgdisk
# reports the partition does not exist. The explicit no-arg __init__
# gives the zero/empty defaults for a "missing partition".
struct PartInfo(Copyable, Movable):
    var present: Bool
    var first_sector: Int
    var last_sector: Int
    var name: String

    def __init__(out self):
        self.present = False
        self.first_sector = -1
        self.last_sector = -1
        self.name = String("")


def run_cmd_capture(cmd: List[String]) raises -> Captured:
    """Run a command, capture stdout+stderr as a single string, return
    a Captured (rc + output). Uses Python subprocess because the Mojo
    stdlib has no process facility yet (see tool/dist.mojo's
    run_argv_streaming for the same pattern). Captured (not streamed)
    because this tool parses sgdisk text output."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var p = subprocess.Popen(
        py_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    var comm = p.communicate()
    # communicate() returns (stdout_bytes, stderr_bytes). stderr is None
    # here because we merged it into stdout via STDOUT. Read index 0.
    var out_obj = comm[0]
    var out_str = String("")
    if Bool(py=builtins.bool(out_obj)):
        out_str = String(out_obj.decode("utf-8", "replace"))
    var c = Captured()
    c.rc = Int(py=p.returncode)
    c.output = out_str
    return c^


def run_cmd_stream(cmd: List[String]) raises -> Int:
    """Run a command streaming stdout/stderr to the terminal (inherited
    fds), return the exit code. Used for the destructive sgdisk/mkfs
    steps so the operator sees live progress."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args))


def which(command: String) raises -> Bool:
    """True iff `command` is on PATH (same helper as tool/dist.mojo)."""
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")
    var found = shutil.which(command)
    return Bool(py=builtins.bool(found))


def file_size(path: String) raises -> Int:
    var os_path = Python.import_module("os.path")
    return Int(py=os_path.getsize(path))


def join_with_space(parts: List[String]) raises -> String:
    var out = String("")
    var first = True
    for p in parts:
        if not first:
            out += " "
        out += p
        first = False
    return out


def check_host_tools() raises -> Bool:
    """Pre-flight: sgdisk, mkfs.ext2, truncate must all be on PATH (the
    same tools image/disk uses, docs/11 §7.3). Prints the exact apt line
    for any missing one and returns False."""
    var cmds: List[String] = ["sgdisk", "mkfs.ext2", "truncate"]
    var pkgs: List[String] = ["gptfdisk", "e2fsprogs", "coreutils"]
    var missing: List[String] = []
    var all_present = True
    print("[sponge-mkdata] host tool check")
    for i in range(len(cmds)):
        var present = which(cmds[i])
        if present:
            print("  " + cmds[i] + "  ok")
        else:
            print("  " + cmds[i] + "  MISSING  (apt: " + pkgs[i] + ")")
            missing.append(pkgs[i])
            all_present = False
    if not all_present:
        print()
        print("[sponge-mkdata] missing host tools — install before continuing:")
        print("  sudo apt install " + join_with_space(missing))
    return all_present


def cmd_help() raises:
    print("Sponge OS SPONGE-DATA (P4) partition creator")
    print()
    print("Implements the docs/14 §4.3 P4 creation sequence on an existing")
    print("image/disk .img (P1 BIOS-boot + P2 ESP + P3 GENODE). After running,")
    print("the image has a fourth GPT partition named SPONGE-DATA, formatted")
    print("ext2, ready to back the writable user-area stores (docs/14 §6).")
    print()
    print("Usage:")
    print("  mojo tool/mkdata.mojo <img>                  create a "
          + String(DEFAULT_DATA_MIB) + " MiB SPONGE-DATA P4")
    print("  mojo tool/mkdata.mojo <img> --data-size <N>  create an N MiB P4")
    print("  mojo tool/mkdata.mojo help                   show this help")
    print()
    print("The tool is idempotent: a second run on an image that already has a")
    print("P4 named SPONGE-DATA is a verified no-op. If P4 exists with a")
    print("different name the tool errors loudly (exit 2).")
    print()
    print("Host tools required (docs/11-environment.md §7.3): sgdisk,")
    print("mkfs.ext2, truncate — the same set image/disk already uses.")
    print()
    print("Manual escape hatch: every step this tool performs is a plain")
    print("sgdisk/truncate/mkfs.ext2 invocation; see docs/11 §12 for the")
    print("documented sequence (AGENTS.md §3.5 control door).")


def contains_substring(haystack: String, needle: String) raises -> Bool:
    """Case-sensitive substring test (Mojo String has no .contains)."""
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(
        builtins.str(haystack).find(needle) >= 0))


def startswith_str(s: String, prefix: String) raises -> Bool:
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(builtins.str(s).startswith(prefix)))


def parse_first_int_after(line: String, marker: String) raises -> Int:
    """Extract the first integer that appears after `marker` in `line`.
    sgdisk prints e.g. 'First sector: 40960 (at 0.00 GiB)'; we want 40960."""
    var re = Python.import_module("re")
    var builtins = Python.import_module("builtins")
    var m = re.search(
        re.escape(marker) + String(r"\s*(\d+)"),
        builtins.str(line),
    )
    if not Bool(py=builtins.bool(m)):
        return -1
    return Int(py=m.group(1))


def parse_name_after(line: String, marker: String) raises -> String:
    """Extract the partition name following `marker` (strip whitespace
    and the surrounding single quotes sgdisk wraps the name in).

    sgdisk prints 'Partition name: 'SPONGE-DATA'' (note the single
    quotes around the value). We take the suffix after the marker, trim
    whitespace, then strip a matching pair of surrounding quotes (single
    or double) if present."""
    var builtins = Python.import_module("builtins")
    var pys = builtins.str(line)
    var idx = Int(py=pys.find(marker))
    if idx < 0:
        return String("")
    var marker_len = marker.byte_length()
    var suffix = pys[idx + marker_len:]
    var trimmed = String(builtins.str(suffix).strip())
    # sgdisk wraps the name in single quotes; strip one matching pair.
    var n = trimmed.byte_length()
    if n >= 2:
        var first = trimmed[byte=0]
        var last = trimmed[byte=n - 1]
        if (first == "'" or first == "\"") and last == first:
            return String(builtins.str(trimmed)[1:n - 1])
    return trimmed


def parse_partition_info(sgdisk_i_output: String) raises -> PartInfo:
    """Parse `sgdisk -i <part_num> <img>` output into a PartInfo.

    The relevant lines look like:
      Partition GUID code: ...
      First sector: 40960 (at 0.00 GiB)
      Last sector: 12345 (at 0.00 GiB)
      Partition name: GENODE

    When the partition does not exist, sgdisk prints a line containing
    'does not exist' and returns 0; in that case present=False.
    """
    var info = PartInfo()
    info.present = False
    info.first_sector = -1
    info.last_sector = -1
    info.name = String("")

    var builtins = Python.import_module("builtins")
    var pyout = builtins.str(sgdisk_i_output)
    var lines = pyout.splitlines()
    for line_py in lines:
        var line = String(line_py)
        var lower = String(builtins.str(line_py).lower())
        if contains_substring(lower, "does not exist"):
            return info^  # present stays False
        if contains_substring(lower, "first sector:"):
            info.present = True
            info.first_sector = parse_first_int_after(line, "First sector:")
        if contains_substring(lower, "last sector:"):
            info.last_sector = parse_first_int_after(line, "Last sector:")
        if contains_substring(lower, "partition name:"):
            info.name = parse_name_after(line, "Partition name:")

    return info^


def query_partition(img: String, part_num: Int) raises -> PartInfo:
    """Run `sgdisk -i <part_num> <img>` and parse the result. sgdisk -i
    returns 0 whether or not the partition exists (it prints a 'does not
    exist' line in the missing case), so we parse output regardless of
    rc."""
    var cap = run_cmd_capture(
        ["sgdisk", "-i", String(part_num), img])
    return parse_partition_info(cap.output)


def verify_layout(img: String) raises -> Bool:
    """Assert: P3=GENODE present, P4=SPONGE-DATA present. Returns True on
    a clean verification, False (with a printed diagnostic) otherwise.
    This is the acceptance gate (1) from the P3 task."""
    var p3 = query_partition(img, 3)
    var p4 = query_partition(img, 4)

    if not p3.present:
        print("[sponge-mkdata] ERROR: P3 (GENODE) missing after P4 creation")
        return False
    if not p4.present:
        print("[sponge-mkdata] ERROR: P4 missing after creation")
        return False
    if p3.name != P3_LABEL:
        print("[sponge-mkdata] ERROR: P3 name is '" + p3.name
              + "', expected '" + P3_LABEL + "'")
        return False
    if p4.name != P4_LABEL:
        print("[sponge-mkdata] ERROR: P4 name is '" + p4.name
              + "', expected '" + P4_LABEL + "'")
        return False

    print("[sponge-mkdata] verified: P3=" + P3_LABEL + " ["
          + String(p3.first_sector) + ".." + String(p3.last_sector)
          + "], P4=" + P4_LABEL + " ["
          + String(p4.first_sector) + ".." + String(p4.last_sector) + "]")
    return True


def create_p4(img: String, data_mib: Int) raises -> Int:
    """Run the docs/14 §4.3 sequence on <img>. Returns 0 on success
    (created or already-present), non-zero on failure."""
    var os_path = Python.import_module("os.path")
    if not Bool(py=os_path.isfile(img)):
        print("[sponge-mkdata] ERROR: image not found: " + img)
        return 1

    # ---------- idempotency guard ----------
    # Inspect P4 BEFORE any mutation. If it already exists as
    # SPONGE-DATA, this is a re-run -> verified no-op. If it exists under
    # a different name, refuse (fail-loud).
    var p4_before = query_partition(img, 4)
    if p4_before.present:
        if p4_before.name == P4_LABEL:
            print("[sponge-mkdata] P4 already exists as " + P4_LABEL
                  + " — idempotent no-op (re-run detected)")
            if verify_layout(img):
                print("[sponge-mkdata] done: P4 present, no changes made")
                return 0
            print("[sponge-mkdata] ERROR: existing P4 failed verification")
            return 1
        print("[sponge-mkdata] ERROR: P4 exists with name '" + p4_before.name
              + "' (expected absent, or '" + P4_LABEL + "'). Refusing to "
              + "overwrite — resolve manually (sgdisk --delete=4) if intended.")
        return 2

    # ---------- capture P3 offset BEFORE growing ----------
    var p3 = query_partition(img, 3)
    if not p3.present:
        print("[sponge-mkdata] ERROR: P3 (GENODE) not found in " + img)
        print("  This tool expects a fresh image/disk output (P1+P2+P3).")
        print("  Run the scenario with RUN_OPT=--include image/disk first.")
        return 1
    if p3.first_sector < 0 or p3.last_sector < 0:
        print("[sponge-mkdata] ERROR: could not parse P3 sector range")
        return 1
    print("[sponge-mkdata] P3 captured: first=" + String(p3.first_sector)
          + " last=" + String(p3.last_sector))

    # ---------- 1. grow the image ----------
    var data_bytes = data_mib * 1024 * 1024
    var size_before = file_size(img)
    print("[sponge-mkdata] growing image by " + String(data_mib)
          + " MiB (" + String(data_bytes) + " bytes)")
    var rc = run_cmd_stream(
        ["truncate", "-s", "+" + String(data_bytes), img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: truncate failed (rc=" + String(rc) + ")")
        return 1
    var size_after = file_size(img)
    if size_after != size_before + data_bytes:
        print("[sponge-mkdata] ERROR: size mismatch after truncate: expected "
              + String(size_before + data_bytes) + " got "
              + String(size_after))
        return 1
    print("[sponge-mkdata] image grew: " + String(size_before) + " -> "
          + String(size_after) + " bytes")

    # ---------- 2. delete P3 entry (bytes stay on disk) ----------
    rc = run_cmd_stream(["sgdisk", "--delete=3", img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --delete=3 failed (rc="
              + String(rc) + ")")
        return 1

    # ---------- 3. move backup GPT to the new end ----------
    rc = run_cmd_stream(["sgdisk", "--move-second-header", img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --move-second-header failed (rc="
              + String(rc) + ")")
        return 1

    # ---------- 4. re-pin P3 at its old offset + re-apply name ----------
    rc = run_cmd_stream(
        ["sgdisk", "--new=3:" + String(p3.first_sector) + ":"
         + String(p3.last_sector), img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --new=3 failed (rc="
              + String(rc) + ")")
        return 1
    rc = run_cmd_stream(["sgdisk", "--change-name=3:" + P3_LABEL, img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --change-name=3 failed (rc="
              + String(rc) + ")")
        return 1

    # ---------- 5. create P4 in the new free space ----------
    var total_sectors = size_after // SECTOR_SIZE
    var p4_first_req = p3.last_sector + 1
    var p4_last_req = total_sectors - BACKUP_GPT_SECTORS
    if p4_last_req <= p4_first_req:
        print("[sponge-mkdata] ERROR: no room for P4 (p4_first="
              + String(p4_first_req) + " p4_last=" + String(p4_last_req)
              + "); data-size " + String(data_mib)
              + " MiB too small for this image")
        return 1
    print("[sponge-mkdata] creating P4: first=" + String(p4_first_req)
          + " last=" + String(p4_last_req))
    rc = run_cmd_stream(
        ["sgdisk", "--new=4:" + String(p4_first_req) + ":" + String(p4_last_req),
         img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --new=4 failed (rc="
              + String(rc) + ")")
        return 1
    rc = run_cmd_stream(["sgdisk", "--change-name=4:" + P4_LABEL, img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --change-name=4 failed (rc="
              + String(rc) + ")")
        return 1

    # ---------- 6. format P4 as ext2 at its ACTUAL byte offset ----------
    # sgdisk auto-aligns partition starts to 2048-sector boundaries, so
    # the partition may land a few sectors past the requested p4_first.
    # We RE-QUERY the actual P4 first/last sectors and use those for the
    # mkfs offset and size — otherwise the ext2 superblock would be
    # written at the pre-alignment offset and the partition would not
    # match its filesystem (silent corruption). mkfs.ext2 -E offset
    # seeks into the image file and writes the ext2 structures there,
    # WITHOUT touching the GPT. The trailing <p4_kib> limits the fs to
    # the partition extent so mkfs does not run into the backup GPT.
    # -F forces creation on a regular file (not a block device); -q
    # suppresses the multi-line banner.
    var p4_actual = query_partition(img, 4)
    if not p4_actual.present or p4_actual.first_sector < 0:
        print("[sponge-mkdata] ERROR: P4 vanished after creation (sgdisk "
              + "alignment moved it out of range?)")
        return 1
    var p4_byte_off = p4_actual.first_sector * SECTOR_SIZE
    var p4_bytes = (p4_actual.last_sector - p4_actual.first_sector + 1)
        * SECTOR_SIZE
    var p4_kib = p4_bytes // 1024
    if p4_actual.first_sector != p4_first_req:
        print("[sponge-mkdata] note: sgdisk aligned P4 first sector "
              + String(p4_first_req) + " -> " + String(p4_actual.first_sector)
              + " (2048-sector boundary); mkfs offset adjusted")
    print("[sponge-mkdata] formatting P4 ext2: offset=" + String(p4_byte_off)
          + " size=" + String(p4_kib) + " KiB label=" + P4_LABEL)
    rc = run_cmd_stream(
        ["mkfs.ext2", "-E", "offset=" + String(p4_byte_off),
         "-L", P4_LABEL, "-F", "-q", img, String(p4_kib) + "K"])
    if rc != 0:
        print("[sponge-mkdata] ERROR: mkfs.ext2 failed (rc=" + String(rc) + ")")
        print("  The image GPT was already updated; the ext2 format is the")
        print("  only failed step. Re-run THIS tool after investigating — it")
        print("  will detect P4 exists and skip the GPT steps, but mkfs needs")
        print("  a manual: mkfs.ext2 -E offset=" + String(p4_byte_off)
              + " -L " + P4_LABEL + " -F " + img + " " + String(p4_kib) + "K")
        return 1

    # ---------- 7. rebuild hybrid MBR (P1-P3; P4 is GPT-only) ----------
    # docs/14 §4.3 note: hybrid MBR holds at most 3 entries, so P4 is
    # absent from the MBR. sgdisk --hybrid with no args derives the MBR
    # from the first 3 GPT partitions. Documented in docs/11.
    rc = run_cmd_stream(["sgdisk", "--hybrid", img])
    if rc != 0:
        print("[sponge-mkdata] ERROR: sgdisk --hybrid failed (rc="
              + String(rc) + ")")
        return 1

    print()
    print("[sponge-mkdata] P4 creation complete. Verifying layout ...")
    if not verify_layout(img):
        print("[sponge-mkdata] ERROR: post-creation verification failed")
        return 1
    print()
    print("[sponge-mkdata] done: 4-partition image ready (P4=" + P4_LABEL
          + ", " + String(p4_kib) + " KiB ext2)")
    return 0


def main() raises:
    var args = argv()

    if len(args) >= 2:
        var sub = String(args[1])
        if sub == "help" or sub == "--help" or sub == "-h":
            cmd_help()
            return

    # Parse positional <img> + optional --data-size <N>.
    var img = String("")
    var data_mib = DEFAULT_DATA_MIB
    var re = Python.import_module("re")
    var i = 1
    while i < len(args):
        var a = String(args[i])
        if a == "--data-size":
            if i + 1 >= len(args):
                print("error: --data-size requires a value (MiB)")
                exit(1)
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
            if not Bool(py=re.match(r"^\d+$", val)):
                print("error: --data-size must be a positive integer (MiB), got '"
                      + val + "'")
                exit(1)
            data_mib = Int(val)
            i += 1
            continue
        if startswith_str(a, "-") and a != "-":
            print("error: unknown option '" + a + "'")
            print()
            cmd_help()
            exit(1)
        # Positional: the image path.
        if img.byte_length() == 0:
            img = a
        else:
            print("error: unexpected extra argument '" + a + "'")
            exit(1)
        i += 1

    if img.byte_length() == 0:
        print("error: missing <img> argument")
        print()
        cmd_help()
        exit(1)

    if data_mib < 8:
        print("error: --data-size " + String(data_mib)
              + " MiB is below the 8 MiB ext2 comfort floor")
        exit(1)

    print("[sponge-mkdata] Sponge OS SPONGE-DATA (P4) creator")
    print("  image:        " + img)
    print("  data-size:    " + String(data_mib) + " MiB")
    print()

    if not check_host_tools():
        exit(1)
    print()

    var rc = create_p4(img, data_mib)
    exit(rc)
