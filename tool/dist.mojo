# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS distribution media builder.
#
# Produces the two installable Alpha media artifacts in var/dist/:
#
#   sponge-os-0.1.0-alpha-x86_64-sel4.img   (GPT disk image, image/disk)
#   sponge-os-0.1.0-alpha-x86_64-sel4.iso   (El Torito ISO,   image/iso)
#
# Each artifact ships with a `<name>.sha256` sidecar (`<hash>  <name>`
# format, compatible with `sha256sum -c`).
#
# What this tool does:
#   (a) Pre-flight check: verifies every host tool the Genode run
#       framework's image plugins invoke (xorriso, sgdisk, mcopy,
#       e2cp, e2mkdir, mkfs.ext2, mkfs.vfat, resize2fs) is on PATH,
#       printing the exact apt install line for any missing one and
#       exiting non-zero BEFORE any build runs.
#   (b) Runs the disk-image build, then the ISO build, sequentially,
#       streaming each `make` invocation's output. The exit code
#       propagates: a non-zero make exit fails this tool with the
#       same code.
#   (c) Cleans var/run/sponge-alpha* between the two modes so the
#       second mode is not polluted by the first mode's staged boot
#       directory (stale_state guard).
#   (d) Copies the artifacts to var/dist/ with the release names and
#       writes the .sha256 sidecars.
#   (e) Prints a summary table with artifact sizes and sha256
#       prefixes.
#
# What this tool does NOT do:
#   - It does NOT boot-verify the media. The run/sponge-alpha.run
#     scenario gates on `alpha-probe: PASS` via the Genode run
#     framework's run_genode_until during the make invocation; the
#     media artifacts produced here are already boot-proven by that
#     probe (todos 5+6, see docs/09-roadmap.md §9).
#   - It does NOT re-implement any of the Genode run tool's image
#     plugins. It shells out to `make` exactly as a contributor
#     would by hand (AGENTS.md §3.5 — no re-implementation).
#   - It does NOT touch anything outside this repository. The only
#     paths mutated are genode/build/x86_64/var/run/ (cleaned between
#     modes), var/dist/ (artifacts written), and .omo/evidence/ (only
#     when the caller redirects output there).
#
# The manual equivalent of every step this tool performs is documented
# in docs/08-development.md §11 (control escape hatch, AGENTS.md §3.5).
#
# Usage:
#   mojo tool/dist.mojo           # build both media + summary
#   mojo tool/dist.mojo help      # show this usage

from std.sys import argv, exit
from std.python import Python, PythonObject

# Alpha release identity (kept in sync with include/sponge/version.h
# and docs/09-roadmap.md). The release artifact names embed this.
comptime RELEASE_VERSION = "0.1.0-alpha"
comptime RELEASE_ARCH = "x86_64"
comptime RELEASE_KERNEL = "sel4"
comptime RELEASE_NAME_PREFIX = "sponge-os-" + RELEASE_VERSION + "-"
    + RELEASE_ARCH + "-" + RELEASE_KERNEL

comptime SCENARIO = "sponge-alpha"

# Each host tool the media build path can invoke, mapped to its
# Debian/Ubuntu package. Multiple tools can come from one package; the
# summary print de-duplicates the install line. The mappings match
# docs/11-environment.md §7.3 exactly.
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
    table so the missing-tool output reads in the documented order."""
    return [
        HostTool("xorriso", "xorriso", "image/iso"),
        HostTool("sgdisk", "gptfdisk", "image/disk"),
        HostTool("mcopy", "mtools", "image/disk"),
        HostTool("e2cp", "e2tools", "image/disk"),
        HostTool("e2mkdir", "e2tools", "image/disk"),
        HostTool("mkfs.ext2", "e2fsprogs", "image/disk"),
        HostTool("mkfs.vfat", "dosfstools", "EFI partition fallback"),
        HostTool("resize2fs", "e2fsprogs", "image/disk"),
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
    `prefix`. Used to clean var/run/sponge-alpha* between the disk
    and ISO modes so neither mode's run_dir pollutes the other."""
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
    print("Usage:")
    print("  mojo tool/dist.mojo           Build both .img and .iso media")
    print("  mojo tool/dist.mojo help      Show this help")
    print()
    print("Produces in var/dist/:")
    print("  " + RELEASE_NAME_PREFIX + ".img")
    print("  " + RELEASE_NAME_PREFIX + ".img.sha256")
    print("  " + RELEASE_NAME_PREFIX + ".iso")
    print("  " + RELEASE_NAME_PREFIX + ".iso.sha256")
    print()
    print("The tool checks the host tools listed in docs/11-environment.md")
    print("§7.3 (xorriso, gptfdisk/sgdisk, mtools/mcopy, e2tools, dosfstools,")
    print("e2fsprogs) up front, runs the disk and ISO media builds of the")
    print(SCENARIO + " scenario via make, and copies the artifacts to var/dist/.")
    print()
    print("The tool never boot-verifies the media itself; the run scenario")
    print("already gates on `alpha-probe: PASS` during the make invocation")
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


def run_media_build(mode: String, root: String) raises -> Int:
    """Invoke the make target for one media mode (`image/disk` or
    `image/iso`) on the configured scenario. Streams make's output.
    Cleans var/run/<scenario>* first so this mode's image plugin sees
    a clean run_dir (stale_state guard)."""
    var build_dir = root + "/genode/build/x86_64"
    var run_root = build_dir + "/var/run"
    var scenario_prefix = SCENARIO

    print("[sponge-dist] cleaning stale " + run_root + "/"
          + scenario_prefix + "* before " + mode + " build")
    rm_glob(run_root, scenario_prefix)
    print()

    var make_args: List[String] = [
        "make",
        "-C",
        build_dir,
        "run/" + SCENARIO,
        "KERNEL=sel4",
        "BOARD=pc",
        "RUN_OPT=--include " + mode,
    ]

    print("[sponge-dist] starting " + mode + " media build")
    print("  cmd: " + join_with_space(make_args))
    print()
    var rc = run_argv_streaming(make_args, cwd="")
    print()
    print("[sponge-dist] " + mode + " make exit code: " + String(rc))
    return rc


def stage_artifact(mode: String, root: String) raises -> Bool:
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
        + SCENARIO + "." + ext)
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

    if len(args) >= 2:
        var sub = String(args[1])
        if sub == "help" or sub == "--help" or sub == "-h":
            cmd_help()
            return
        if sub != "" and sub != "build":
            # 'build' is the implicit default; anything else is an
            # error. Keeps the door open for future subcommands.
            print("error: unknown argument '" + sub + "'")
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

    print("[sponge-dist] Sponge OS distribution media builder")
    print("  scenario:     " + SCENARIO)
    print("  release name: " + RELEASE_NAME_PREFIX + ".{img,iso}")
    print("  repo root:    " + root)
    print()

    # (a) Pre-flight host-tool check. Exits BEFORE any build if any
    # required tool is missing (the failure must be loud and early).
    if not check_host_tools():
        exit(1)

    # (b) Disk-image build first.
    var disk_rc = run_media_build("image/disk", root)
    if disk_rc != 0:
        print()
        print("[sponge-dist] error: image/disk build failed (exit code "
              + String(disk_rc) + "). Aborting; ISO build not attempted.")
        exit(disk_rc)

    # (d.1) Stage the disk artifact + sha256.
    if not stage_artifact("image/disk", root):
        exit(1)

    print()
    # (b) ISO build second. The stale_state guard inside
    # run_media_build cleans var/run/sponge-alpha* between modes.
    var iso_rc = run_media_build("image/iso", root)
    if iso_rc != 0:
        print()
        print("[sponge-dist] error: image/iso build failed (exit code "
              + String(iso_rc) + "). Aborting.")
        exit(iso_rc)

    # (d.2) Stage the ISO artifact + sha256.
    if not stage_artifact("image/iso", root):
        exit(1)

    # (e) Summary table.
    print_summary(root)

    print()
    print("[sponge-dist] done: both media built and staged in "
          + root + "/var/dist/")
