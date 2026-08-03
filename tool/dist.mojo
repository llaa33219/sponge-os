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
comptime DISK_SCENARIO = "sponge-desktop-disk"
comptime ISO_SCENARIO = "sponge-alpha"

# Default SPONGE-DATA P4 size (MiB). Matches tool/mkdata's default and
# docs/14 §4.3 ("P4 = 1 GiB default, configurable via tool/dist
# --data-size").
comptime DEFAULT_DATA_MIB = 1024

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
    print("  .img  the real product — 4-partition disk-served desktop")
    print("        (run/" + DISK_SCENARIO + ") + SPONGE-DATA partition")
    print("        (tool/mkdata). Installs persist across reboots.")
    print("  .iso  live/eval mode — alpha boot-modules composition")
    print("        (run/" + ISO_SCENARIO + ") on a RAM filesystem.")
    print("        Boots the same desktop; nothing persists.")
    print()
    print("Usage:")
    print("  mojo tool/dist.mojo                  Build product .img + .iso")
    print("  mojo tool/dist.mojo --no-data        Skip the SPONGE-DATA P4 step")
    print("                                       (control door; .img has 3 partitions)")
    print("  mojo tool/dist.mojo --data-size <N>  N MiB SPONGE-DATA P4 (default "
          + String(DEFAULT_DATA_MIB) + ")")
    print("  mojo tool/dist.mojo help             Show this help")
    print()
    print("Produces in var/dist/:")
    print("  " + RELEASE_NAME_PREFIX + ".img")
    print("  " + RELEASE_NAME_PREFIX + ".img.sha256")
    print("  " + RELEASE_NAME_PREFIX + ".iso")
    print("  " + RELEASE_NAME_PREFIX + ".iso.sha256")
    print()
    print("The tool checks the host tools listed in docs/11-environment.md")
    print("§7.3 (xorriso, gptfdisk/sgdisk, mtools/mcopy, e2tools, dosfstools,")
    print("e2fsprogs, coreutils/truncate) up front, runs the disk and ISO media")
    print("builds via make, grows the SPONGE-DATA P4 via tool/mkdata (docs/14")
    print("§4.3), and copies the artifacts to var/dist/.")
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


def run_media_build(mode: String, scenario: String, root: String) raises -> Int:
    """Invoke the make target for one media mode (`image/disk` or
    `image/iso`) on the given scenario. Streams make's output.
    Cleans var/run/<scenario>* first so this mode's image plugin sees
    a clean run_dir (stale_state guard)."""
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
    print()
    var rc = run_argv_streaming(make_args, cwd="")
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


def verify_four_partitions(img_path: String) raises -> Bool:
    """Run sgdisk -p on the produced .img and assert it shows >= 4
    partitions with P4 named SPONGE-DATA. This is the misleading_
    success_output defense — the build's exit code alone is not enough;
    the actual partition table must show the 4 partitions we claim.
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
    print("[sponge-dist] sgdisk -p verification of " + img_path)
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
    if part_count < 4:
        print("[sponge-dist] ERROR: expected >= 4 partitions, sgdisk -p shows "
              + String(part_count))
        return False
    if not saw_sponge_data:
        print("[sponge-dist] ERROR: no partition named SPONGE-DATA in table")
        return False
    print("[sponge-dist] OK — " + String(part_count) + " partitions present, "
          + "P4 = SPONGE-DATA")
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


def print_summary(root: String) raises:
    var os_path = Python.import_module("os.path")
    var builtins = Python.import_module("builtins")
    var dist_dir = root + "/var/dist"

    print()
    print("[sponge-dist] summary")
    print()
    print("  artifact                                            size       sha256 (prefix)")
    print("  --------                                            ----       ---------------")

    var exts: List[String] = ["img", "iso"]
    for ext in exts:
        var name = RELEASE_NAME_PREFIX + "." + ext
        var path = dist_dir + "/" + name
        if not os_path.isfile(path):
            print("  " + name + "    MISSING")
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

    print()
    print("  Full hashes: <artifact>.sha256 sidecars in " + dist_dir)
    print("  Verify:      (cd " + dist_dir + " && sha256sum -c *.sha256)")


def main() raises:
    var args = argv()

    # Parse flags that affect the build BEFORE dispatching to help.
    # This lets `--no-data help` still print help, while `--no-data`
    # alone runs the build without the SPONGE-DATA step.
    var add_data = True
    var data_mib = DEFAULT_DATA_MIB
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

    var root = repo_root()
    var os_path = Python.import_module("os.path")

    var build_dir = root + "/genode/build/x86_64"
    if not os_path.isdir(build_dir):
        print("error: build directory not found at " + build_dir)
        print("Run './tool/build prepare' first (see docs/08-development.md §3).")
        exit(1)

    print("[sponge-dist] Sponge OS distribution media builder")
    print("  product .img: " + DISK_SCENARIO + " (image/disk)")
    if add_data:
        print("               + tool/mkdata SPONGE-DATA P4 ("
              + String(data_mib) + " MiB)")
    else:
        print("               (--no-data: SPONGE-DATA P4 step SKIPPED)")
    print("  live/eval .iso: " + ISO_SCENARIO + " (image/iso)")
    print("  release name: " + RELEASE_NAME_PREFIX + ".{img,iso}")
    print("  repo root:    " + root)
    print()

    # (a) Pre-flight host-tool check. Exits BEFORE any build if any
    # required tool is missing (the failure must be loud and early).
    if not check_host_tools():
        exit(1)

    # (b) Disk-image build first (the new product media).
    var disk_rc = run_media_build("image/disk", DISK_SCENARIO, root)
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
        + DISK_SCENARIO + ".img")
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
        # is not enough — the .img must really show 4 partitions.
        if not verify_four_partitions(disk_src):
            print()
            print("[sponge-dist] error: post-mkdata partition verification")
            print("  failed. The .img does not carry the expected 4 partitions.")
            exit(1)
    else:
        print()
        print("[sponge-dist] --no-data: SPONGE-DATA P4 step skipped; the .img")
        print("  carries the 3 image/disk partitions only (P1 BIOS-boot +")
        print("  P2 ESP + P3 GENODE). Persistence will not work on this media.")

    # (d.1) Stage the disk artifact + sha256 (in-place modified by
    # mkdata if --no-data was not passed).
    if not stage_artifact("image/disk", DISK_SCENARIO, root):
        exit(1)

    print()
    # (b.4) ISO build second (live/eval mode). The stale_state guard
    # inside run_media_build cleans var/run/<scenario>* — but note the
    # two scenarios differ (sponge-desktop-disk vs sponge-alpha), so
    # the prefix-clean is per-scenario and the disk build's run_dir is
    # not touched by the ISO clean.
    var iso_rc = run_media_build("image/iso", ISO_SCENARIO, root)
    if iso_rc != 0:
        print()
        print("[sponge-dist] error: image/iso build failed (exit code "
              + String(iso_rc) + "). Aborting.")
        exit(iso_rc)

    # (d.2) Stage the ISO artifact + sha256.
    if not stage_artifact("image/iso", ISO_SCENARIO, root):
        exit(1)

    # (e) Summary table.
    print_summary(root)

    print()
    print("[sponge-dist] done: product media built and staged in "
          + root + "/var/dist/")
    if add_data:
        print("  .img: 4 partitions (BIOSBOOT/ESP/GENODE/SPONGE-DATA)")
        print("        — installs persist across reboots (P3).")
    else:
        print("  .img: 3 partitions (BIOSBOOT/ESP/GENODE) — no SPONGE-DATA.")
    print("  .iso: live/eval mode (RAM filesystem; nothing persists).")
