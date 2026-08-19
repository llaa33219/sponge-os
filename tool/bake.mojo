# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS bake-profile inspector + post-build P3 injector (Phase 15 W2b).
#
# Companion to run/bake.inc (the staging-time half of the bake machinery):
# this tool is the host-side, post-build half (D15.8). Where bake.inc
# runs INSIDE the run framework BEFORE build_boot_image, this tool runs
# OUTSIDE the framework on an EXISTING .img, modifying only P3 (the
# GENODE ext2) so .img re-bakes do not require a full scenario rebuild.
#
# === Three subcommands ===
#
#   --list                       Print every profile under pkg/bake/*.profile
#                                 (name + description). Read-only, fast.
#
#   --show <profile>             Parse the profile (config_version, packages,
#                                 config, theme) and print per-package
#                                 payload sizes (du of pkg/<name>/payload).
#                                 Loud error on unknown profile or
#                                 config_version != 1.
#
#   --img <file> --profile <name> [--dry-run]
#                                 Post-build P3 injector. Stages the
#                                 profile into <file>'s GENODE ext2
#                                 partition: pkg_<name>.xml metadata,
#                                 pkg_index.xml, /system/bake/{manifest,
#                                 config.defaults, theme.defaults}, and
#                                 every pkg/<name>/payload/* payload.
#                                 Implements:
#                                   * D15.5  size budget (minimal <=1GiB,
#                                            desktop <=2GiB total .img)
#                                   * R15.3  T1 defense: refuse to inject a
#                                            package whose binary is NOT
#                                            already in /system/bin
#                                   * D15.10 config_version=1 only
#                                   * Idempotency: re-run with same profile
#                                            is a no-op (exit 0).
#                                   * sgdisk pre/post verify: partition
#                                            table unchanged, P3 still
#                                            named GENODE.
#
# === The P3 access strategy (no root) ===
#
# e2cp/e2mkdir/e2ls do NOT accept a byte offset; they operate on a
# file that IS the ext2 filesystem (or on a device that is one).
# losetup -o can create a /dev/loopN pinned at P3's offset, but
# losetup needs root on this host (and AGENTS.md §5.5 forbids sudo).
# So we extract P3 to a temp file with `dd if=<img> bs=512
# skip=<p3_first> count=<p3_size>`, mutate it with the e2 tools,
# then write it back with `dd if=<tmp> bs=512 seek=<p3_first>
# count=<p3_size> conv=notrunc of=<img>` (NOTRUC so P4 survives
# the write — mkdata's P4 lives AFTER P3 in the same file).
#
# === Mirror of mkdata.mojo ===
#
# This file follows tool/mkdata.mojo's structure exactly: the
# Captured struct, query_partition(), which(), file_size(),
# check_host_tools(), run_cmd_capture/run_cmd_stream, the
# parse_* helpers, and the exit-code convention (0 ok, 1
# usage/host-tool/io, 2 profile/validation/budget refusal).
# AGENTS.md §3.5 — same host-side tool family, same plumbing.
#
# === Usage ===
#
#   mojo tool/bake.mojo --list
#   mojo tool/bake.mojo --show <profile>
#   mojo tool/bake.mojo --img <file> --profile <name> [--dry-run]
#   mojo tool/bake.mojo help
#
# Exit codes:
#   0  success (created or already-present)
#   1  usage / host-tool / sgdisk / e2cp / dd failure
#   2  profile / validation / size-budget refusal
#   (mirrors tool/mkdata.mojo's scheme)

from std.sys import argv, exit
from std.python import Python, PythonObject

# ============================================================================
# Constants
# ============================================================================

comptime SECTOR_SIZE = 512
comptime CONFIG_VERSION = 1  # D15.10: only v1 understood; bumps are breaking

# Profile filenames live at <repo>/pkg/bake/<name>.profile. The
# profile name = filename minus the .profile suffix.
comptime BAKE_PROFILES_DIR = "pkg/bake"

# D15.5 image-size budgets. minimal must fit a small USB stick,
# desktop may be larger (Falkon is staged — Phase 9 closed the
# boot-module ceiling so desktop fits within 2 GiB; a 509 MiB
# payload is the worst-case single addition).
comptime DEFAULT_PROFILE = "desktop"  # matches bake.inc default; D15.14

# Allowed --profile values. The "none" sentinel reproduces today's
# hardcoded behavior (bake.inc escape hatch, AGENTS.md §1.1). For
# --img, "none" is rejected with a clear message: --img's whole
# point is to bake a profile, not to un-bake one (bake::stage's
# none path produces nothing to inject).
comptime ALLOWED_PROFILES = ["minimal", "desktop", "test"]

# D15.5 budgets in MiB (binary MiB, not MB). 1 MiB = 1048576 B.
comptime BUDGET_MINIMAL_MIB = 1024  # <= 1 GiB
comptime BUDGET_DESKTOP_MIB = 2048  # <= 2 GiB

# D15.4 schema name baked into every manifest. The schema_version
# (1) lives alongside; bumps are breaking-only.
comptime MANIFEST_SCHEMA = "bake.config_version"
comptime MANIFEST_VERSION = 1

# Theme source: sponge-de themes live at
# repos/sponge/src/sponge-de/themes/<name>.theme (matches bake.inc).
comptime THEMES_DIR = "repos/sponge/src/sponge-de/themes"
comptime THEME_SUFFIX = ".theme"

# P3 label (must match image/disk's setting + tool/mkdata's verify).
comptime P3_LABEL = "GENODE"


# ============================================================================
# Captured struct (mirror of tool/mkdata.mojo's Captured)
# ============================================================================

# Result of running an external command with captured stdout+stderr.
struct Captured(Copyable, Movable):
    var rc: Int
    var output: String

    def __init__(out self):
        self.rc = 0
        self.output = String("")


# Result of parsing `sgdisk -i <N> <img>`.
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


# ============================================================================
# Profile struct (parsed INI profile)
# ============================================================================

# One parsed profile. Top-level keys: config_version, name, description.
# Section data: packages (list), config (dict), theme (single active).
# Mirrors run/bake.inc's active_profile_* Tcl variables exactly.
struct Profile(Copyable, Movable):
    var name: String
    var description: String
    var config_version: Int
    var packages: List[String]
    var config_keys: List[String]
    var config_values: List[String]  # parallel to config_keys (no Dict in stdlib)
    var theme_active: String
    var source_path: String

    def __init__(out self, name: String):
        self.name = name
        self.description = String("")
        self.config_version = 0
        self.packages = List[String]()
        self.config_keys = List[String]()
        self.config_values = List[String]()
        self.theme_active = String("")
        self.source_path = String("")


# ============================================================================
# Plumbing (mirror of tool/mkdata.mojo)
# ============================================================================

def run_cmd_capture(cmd: List[String]) raises -> Captured:
    """Run a command, capture stdout+stderr as a single string, return
    a Captured (rc + output). Python subprocess because Mojo stdlib
    has no process facility."""
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
    var out_obj = comm[0]
    var out_str = String("")
    if Bool(py=builtins.bool(out_obj)):
        out_str = String(out_obj.decode("utf-8", "replace"))
    var c = Captured()
    c.rc = Int(py=p.returncode)
    c.output = out_str
    return c^


def run_cmd_stream(cmd: List[String]) raises -> Int:
    """Run a command streaming stdout/stderr to the terminal, return rc."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args))


def which(command: String) raises -> Bool:
    """True iff `command` is on PATH."""
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


def contains_substring(haystack: String, needle: String) raises -> Bool:
    """Case-sensitive substring test (Mojo String has no .contains)."""
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(
        builtins.str(haystack).find(needle) >= 0))


def startswith_str(s: String, prefix: String) raises -> Bool:
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(builtins.str(s).startswith(prefix)))


# ============================================================================
# sgdisk parsing (mirror of tool/mkdata.mojo)
# ============================================================================

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
    """Extract the partition name following `marker`. sgdisk wraps
    the name in single quotes — strip a matching pair if present."""
    var builtins = Python.import_module("builtins")
    var pys = builtins.str(line)
    var idx = Int(py=pys.find(marker))
    if idx < 0:
        return String("")
    var marker_len = marker.byte_length()
    var suffix = pys[idx + marker_len:]
    var trimmed = String(builtins.str(suffix).strip())
    var n = trimmed.byte_length()
    if n >= 2:
        var first = trimmed[byte=0]
        var last = trimmed[byte=n - 1]
        if (first == "'" or first == "\"") and last == first:
            return String(builtins.str(trimmed)[1:n - 1])
    return trimmed


def parse_partition_info(sgdisk_i_output: String) raises -> PartInfo:
    """Parse `sgdisk -i <part_num> <img>` output into a PartInfo."""
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
    """Run `sgdisk -i <part_num> <img>` and parse the result."""
    var cap = run_cmd_capture(
        ["sgdisk", "-i", String(part_num), img])
    return parse_partition_info(cap.output)


# ============================================================================
# Host-tool preflight
# ============================================================================

def check_host_tools() raises -> Bool:
    """Pre-flight: e2cp/e2mkdir/e2ls/dd/sgdisk must all be on PATH.
    Prints the exact apt line for any missing one and returns False."""
    var cmds: List[String] = [
        "e2cp", "e2mkdir", "e2ls", "dd", "sgdisk", "du", "find", "awk"]
    var pkgs: List[String] = [
        "e2fsprogs", "e2fsprogs", "e2fsprogs", "coreutils",
        "gptfdisk", "coreutils", "findutils", "gawk"]
    var missing: List[String] = []
    var all_present = True
    print("[sponge-bake] host tool check")
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
        print("[sponge-bake] missing host tools — install before continuing:")
        print("  sudo apt install " + join_with_space(missing))
    return all_present


# ============================================================================
# Help
# ============================================================================

def cmd_help() raises:
    print("Sponge OS bake-profile inspector + post-build P3 injector")
    print()
    print("Companion to run/bake.inc. Subcommands:")
    print()
    print("  --list")
    print("      Print every profile under pkg/bake/*.profile (name +")
    print("      description). Read-only, fast.")
    print()
    print("  --show <profile>")
    print("      Parse the profile; print packages, config, theme, and")
    print("      per-package payload sizes (du of pkg/<name>/payload).")
    print("      Loud error on unknown profile or config_version != 1.")
    print()
    print("  --img <file> --profile <name> [--dry-run]")
    print("      Post-build P3 injector. Stages the profile into <file>'s")
    print("      GENODE ext2 partition: pkg_<name>.xml metadata,")
    print("      pkg_index.xml, /system/bake/{manifest, config.defaults,")
    print("      theme.defaults}, and pkg/<name>/payload/* payloads.")
    print("      Idempotent (re-run is a no-op). T1 defense (refuses to")
    print("      inject packages whose binary is not in /system/bin).")
    print("      D15.5 size-budget enforced (minimal <= 1 GiB,")
    print("      desktop <= 2 GiB total .img).")
    print("      --dry-run prints the plan without writing.")
    print()
    print("Exit codes: 0 ok, 1 usage/host-tool/io failure, 2")
    print("profile/validation/budget refusal (mirrors tool/mkdata.mojo).")
    print()
    print("Host tools required: e2cp/e2mkdir/e2ls (e2fsprogs), dd/sgdisk,")
    print("du (same set tool/mkdata needs). The manual escape hatch for")
    print("every step is documented in docs/08-development.md §11.")


# ============================================================================
# Profile discovery + listing
# ============================================================================

def list_profiles(profiles_dir: String) raises -> List[Profile]:
    """Read every pkg/bake/*.profile and parse it. Returns a list of
    profiles in sorted (filename) order. Profiles that fail to parse
    are skipped with a printed warning (never a hard error — --list
    is the inspection surface, not a validator; --show is stricter)."""
    var profiles: List[Profile] = List[Profile]()
    var os_py = Python.import_module("os")
    var builtins = Python.import_module("builtins")
    if not Bool(py=os_py.path.isdir(profiles_dir)):
        return profiles^
    var entries_obj = os_py.listdir(profiles_dir)
    var entries = builtins.sorted(entries_obj)
    for entry_py in entries:
        var entry = String(entry_py)
        if not entry.endswith(".profile"):
            continue
        var name = String(builtins.str(entry)[0:entry.byte_length() - 8])
        var path = profiles_dir + "/" + entry
        var prof = parse_profile(path, name)
        profiles.append(prof^)
    return profiles^


# ============================================================================
# Profile parsing (mirror run/bake.inc _parse_profile semantics)
# ============================================================================

def parse_profile(profile_path: String, profile_name: String) raises -> Profile:
    """Parse one INI profile. Semantics MUST match run/bake.inc's
    _parse_profile (bake.inc is the staging-time authority; this
    tool is the post-build mirror — any divergence is a bug to
    report, not a new interpretation).

    Supported subset:
      config_version = <int>     # required, must equal CONFIG_VERSION
      name           = <string>   # informational
      description    = <string>   # informational
      [packages]
        <pkg> = enabled           # only "enabled" is honored
        # comments OK (full-line or trailing after unquoted '#')
      [config]
        <key> = <value>           # plain key=value
      [theme]
        active = <theme-name>     # required
    Empty lines ignored. Whitespace stripped around keys + values."""
    var prof = Profile(profile_name)
    prof.source_path = profile_path

    var builtins = Python.import_module("builtins")
    var f = builtins.open(profile_path, "r")
    var content_obj = f.read()
    f.close()
    var content = String(builtins.str(content_obj))
    var lines = content.splitlines()

    var section = String("")  # "" | "packages" | "config" | "theme"

    for line_py in lines:
        var line = String(line_py)

        # Strip leading/trailing whitespace.
        var trimmed = String(builtins.str(line).strip())
        if trimmed.byte_length() == 0:
            continue
        # Full-line comment.
        if trimmed[byte=0] == '#':
            continue

        # Strip a trailing inline comment: walk char-by-char, track
        # quote state; if we hit an unquoted '#' with non-empty
        # prefix, cut there. Mirror bake.inc's algorithm.
        var in_quote = 0
        var cut_at = -1
        var i = 0
        while i < trimmed.byte_length():
            var c = trimmed[byte=i]
            if c == '"' or c == "'":
                if in_quote == 0:
                    in_quote = 1
                else:
                    in_quote = 0
            if in_quote == 0 and c == '#':
                cut_at = i
                break
            i += 1
        if cut_at >= 0:
            var cut_str = String(builtins.str(trimmed)[0:cut_at])
            trimmed = String(builtins.str(cut_str).strip())
            if trimmed.byte_length() == 0:
                continue

        # Section header: [name]
        if trimmed[byte=0] == '[' and trimmed[byte=trimmed.byte_length() - 1] == ']':
            var inner = String(builtins.str(trimmed)[1:trimmed.byte_length() - 1])
            section = String(builtins.str(inner).strip())
            if section != "packages" and section != "config" and section != "theme":
                print("[sponge-bake] warning: " + profile_path
                      + " has unknown section [" + section
                      + "]; valid: packages, config, theme")
                # Don't clear section — continue parsing but ignore it.
            continue

        # key = value
        var eq_idx = Int(py=builtins.str(trimmed).find("="))
        if eq_idx < 0:
            print("[sponge-bake] warning: " + profile_path
                  + " line ' " + trimmed + " ' has no '=' — skipping")
            continue

        var key_pys = builtins.str(trimmed)[0:eq_idx]
        var val_pys = builtins.str(trimmed)[eq_idx + 1:]
        var key = String(builtins.str(key_pys).strip())
        var val = String(builtins.str(val_pys).strip())

        if section == "":
            # Top-level keys.
            if key == "config_version":
                var re = Python.import_module("re")
                if not Bool(py=re.match(r"^-?\d+$", val)):
                    print("[sponge-bake] warning: " + profile_path
                          + " config_version is not an integer: '" + val + "'")
                    prof.config_version = -1
                else:
                    prof.config_version = Int(val)
            elif key == "name":
                # name mismatch is non-fatal — bake.inc logs but continues.
                # We mirror that: if mismatch, keep the requested name but
                # don't overwrite the field (bake.inc's profile_name wins).
                pass
            elif key == "description":
                prof.description = val
            continue

        if section == "packages":
            if val == "enabled":
                prof.packages.append(key)
            # Other values silently ignored — bake.inc contract.
            continue

        if section == "config":
            prof.config_keys.append(key)
            prof.config_values.append(val)
            continue

        if section == "theme":
            if key == "active":
                prof.theme_active = val
            continue

    return prof^


# ============================================================================
# --list / --show rendering
# ============================================================================

def render_list(profiles: List[Profile]) raises:
    """Print the --list output."""
    print("[sponge-bake] profiles in pkg/bake/  (" + String(len(profiles))
          + " total)")
    print()
    if len(profiles) == 0:
        print("  (no profiles found)")
        return
    for i in range(len(profiles)):
        # Field access — no Profile copy (Copyable but not
        # ImplicitlyCopyable; List[String] field is the blocker).
        var line = "  " + profiles[i].name
        if profiles[i].config_version != CONFIG_VERSION:
            line += "  [config_version=" + String(profiles[i].config_version)
            line += " != " + String(CONFIG_VERSION) + " — UNSUPPORTED]"
        line += "\n    " + profiles[i].description
        print(line)
    print()
    print("  Sentinel: 'none' is bake.inc's escape hatch (reproduces")
    print("  today's hardcoded hello-only behavior). It is not a profile")
    print("  on disk; set SPONGE_BAKE_PROFILE=none to opt out of baking.")


def render_show(mut prof: Profile, profiles_dir: String) raises:
    """Print the --show output: parsed profile + per-package payload sizes.
    Takes Profile by mut ref to avoid forcing an implicit copy at the
    call site (Profile holds List[String] which is not ImplicitlyCopyable
    in this Mojo version, even though the structs are Copyable+Movable)."""
    print("[sponge-bake] profile: " + prof.name)
    print("  source:        " + prof.source_path)
    print("  description:   " + prof.description)
    print("  config_version: " + String(prof.config_version)
          + (String("") if prof.config_version == CONFIG_VERSION
              else "  [REQUIRED: " + String(CONFIG_VERSION) + "]"))
    print()

    # Packages
    print("  packages (" + String(len(prof.packages)) + "):")
    if len(prof.packages) == 0:
        print("    (none)")
    else:
        # Compute per-package payload sizes with du.
        for i in range(len(prof.packages)):
            var pkg = prof.packages[i]
            var pkg_dir = "pkg/" + pkg
            var payload_dir = pkg_dir + "/payload"
            var os_path = Python.import_module("os.path")
            var line = "    " + pkg
            # Verify metadata exists; payload may or may not.
            if not Bool(py=os_path.isfile(pkg_dir + "/metadata.xml")):
                line += "  [MISSING: pkg/" + pkg + "/metadata.xml]"
            else:
                # T1 hint: peek at <binary> from metadata.
                var bin_name = read_binary_name(pkg_dir + "/metadata.xml")
                if bin_name.byte_length() > 0:
                    line += "  (binary=" + bin_name + ")"
            if Bool(py=os_path.isdir(payload_dir)):
                var size = payload_size_bytes(payload_dir)
                line += "  payload=" + fmt_human_size(size)
            else:
                line += "  payload=(none; source-built or depot-repackaged without payload)"
            print(line)
    print()

    # Config
    print("  config (" + String(len(prof.config_keys)) + " keys):")
    if len(prof.config_keys) == 0:
        print("    (none)")
    else:
        for i in range(len(prof.config_keys)):
            print("    " + prof.config_keys[i] + " = " + prof.config_values[i])
    print()

    # Theme
    print("  theme.active: " + (prof.theme_active if prof.theme_active.byte_length() > 0 else "(MISSING)"))
    if prof.theme_active.byte_length() > 0:
        var theme_path = THEMES_DIR + "/" + prof.theme_active + THEME_SUFFIX
        var os_path = Python.import_module("os.path")
        if Bool(py=os_path.isfile(theme_path)):
            print("    found at " + theme_path)
        else:
            print("    [MISSING: " + theme_path + "]")


def read_binary_name(metadata_path: String) raises -> String:
    """Peek <binary>NAME</binary> out of metadata.xml. Empty if absent.
    Single-value lookup, no XML lib required."""
    var builtins = Python.import_module("builtins")
    var os_path = Python.import_module("os.path")
    if not Bool(py=os_path.isfile(metadata_path)):
        return String("")
    var f = builtins.open(metadata_path, "r")
    var content_obj = f.read()
    f.close()
    var content = String(builtins.str(content_obj))
    var re = Python.import_module("re")
    var m = re.search(
        String(r"<binary[^>]*>\s*([^<\s]+)\s*</binary>"),
        content,
    )
    if not Bool(py=builtins.bool(m)):
        return String("")
    return String(m.group(1))


def payload_size_bytes(payload_dir: String) raises -> Int:
    """Total bytes under payload_dir (recursive). Uses `du -sb`-equivalent
    via `du -sb` (POSIX) for byte accuracy; falls back to `du -s` (KiB)
    if -sb is rejected."""
    var cap = run_cmd_capture(["du", "-sb", payload_dir])
    if cap.rc == 0:
        var builtins = Python.import_module("builtins")
        var parts = builtins.str(cap.output).split()
        if len(parts) >= 1:
            return Int(String(parts[0]))
    # Fallback: du -s (1-KiB blocks).
    cap = run_cmd_capture(["du", "-s", payload_dir])
    var builtins = Python.import_module("builtins")
    var parts = builtins.str(cap.output).split()
    if len(parts) >= 1:
        return Int(String(parts[0])) * 1024
    return 0


def fmt_human_size(num_bytes: Int) raises -> String:
    """Render a byte count as B / KiB / MiB / GiB with one decimal."""
    var units: List[String] = ["B", "KiB", "MiB", "GiB", "TiB"]
    var size = Float64(num_bytes)
    var i = 0
    while size >= 1024.0 and i < len(units) - 1:
        size = size / 1024.0
        i += 1
    var builtins = Python.import_module("builtins")
    if i == 0:
        return String(builtins.str("{} {}").format(Int(num_bytes), units[0]))
    return String(builtins.str("{:.1f} {}").format(size, units[i]))


# ============================================================================
# Manifest synthesis (mirror run/bake.inc _write_manifest + JSON escaping)
# ============================================================================

def json_escape(s: String) raises -> String:
    """RFC 8259 JSON string escape."""
    var builtins = Python.import_module("builtins")
    return String(builtins.str(s).encode("unicode_escape").decode("ascii"))


def json_string(s: String) raises -> String:
    return "\"" + json_escape(s) + "\""


def json_string_list(lst: List[String]) raises -> String:
    var parts: List[String] = List[String]()
    for x in lst:
        parts.append(json_string(x))
    var builtins = Python.import_module("builtins")
    return "[" + join_with_space(parts).replace(" ", ",") + "]"


def json_string_dict_pairs(keys: List[String], values: List[String]) raises -> String:
    """Render {"k1":"v1", "k2":"v2", ...} from parallel lists."""
    var parts: List[String] = List[String]()
    for i in range(len(keys)):
        parts.append(json_string(keys[i]) + ":" + json_string(values[i]))
    var builtins = Python.import_module("builtins")
    return "{" + join_with_space(parts).replace(" ", ",") + "}"


def synthesize_manifest(prof: Profile, staged_at_iso: String) raises -> String:
    """Return the bake_manifest.json content matching bake.inc's
    schema (D15.10). The signature stanza is what tool/bake --img
    uses for idempotency comparison."""
    var out = String("")
    out += "{\n"
    out += "  \"schema\": " + json_string(MANIFEST_SCHEMA) + ",\n"
    out += "  \"schema_version\": " + String(MANIFEST_VERSION) + ",\n"
    out += "  \"profile\": " + json_string(prof.name) + ",\n"
    out += "  \"profile_config_version\": " + String(CONFIG_VERSION) + ",\n"
    out += "  \"description\": " + json_string(prof.description) + ",\n"
    out += "  \"mode\": " + json_string("img") + ",\n"
    out += "  \"staged_packages\": " + json_string_list(prof.packages) + ",\n"
    out += "  \"config\": " + json_string_dict_pairs(
        prof.config_keys, prof.config_values) + ",\n"
    out += "  \"theme\": " + json_string(prof.theme_active) + ",\n"
    out += "  \"theme_source_path\": " + json_string(
        THEMES_DIR + "/" + prof.theme_active + THEME_SUFFIX) + ",\n"
    out += "  \"generator\": " + json_string(
        "tool/bake.mojo (Phase 15 W2b)") + ",\n"
    out += "  \"staged_at\": " + json_string(staged_at_iso) + "\n"
    out += "}\n"
    return out^


def synthesize_config_defaults(prof: Profile) raises -> String:
    """The W3-readable config.defaults (plain key=value, one per line)."""
    var out = String("# Generated by tool/bake.mojo (Phase 15 W2b).\n")
    out += "# Single-pass seed list for sponge_configd on first boot.\n"
    out += "# Plain key=value lines, one per [config] entry.\n"
    out += "# W3's first-boot sentinel gates re-seeding (D15.9).\n"
    for i in range(len(prof.config_keys)):
        out += prof.config_keys[i] + "=" + prof.config_values[i] + "\n"
    return out^


def synthesize_pkg_index(packages: List[String]) raises -> String:
    """Synthesize pkg_index.xml in the format run scripts already
    hand-write (sponge-falkon.run et al): <packages><pkg name="X"/>...</packages>."""
    var out = String("<packages>\n")
    for p in packages:
        out += "  <pkg name=\"" + p + "\"/>\n"
    out += "</packages>\n"
    return out^


def extract_manifest_field(manifest: String, field_name: String) raises -> String:
    """Minimal JSON string-field extractor for the specific
    bake_manifest.json schema we generate. Handles:
        "field": "value"
    Returns the empty string if the field is absent or malformed.
    Used by the idempotency check; not a general JSON parser."""
    var builtins = Python.import_module("builtins")
    var re = Python.import_module("re")
    var pattern = String("\"") + field_name + String(r"\"\s*:\s*\"([^\"]*)\"")
    var m = re.search(pattern, builtins.str(manifest))
    if not Bool(py=builtins.bool(m)):
        return String("")
    return String(m.group(1))


def extract_manifest_array_field(manifest: String, field_name: String) raises -> String:
    """Extract a JSON array-of-strings field. Returns the joined
    comma-separated string content (without the brackets). Used by
    idempotency: we compare the joined array against our intended
    joined list, so order matters (the parser is order-preserving)."""
    var builtins = Python.import_module("builtins")
    var re = Python.import_module("re")
    var pattern = String("\"") + field_name + String(r"\"\s*:\s*\[([^\]]*)\]")
    var m = re.search(pattern, builtins.str(manifest))
    if not Bool(py=builtins.bool(m)):
        return String("")
    return String(m.group(1))


def join_strings_with_comma(lst: List[String]) raises -> String:
    """Comma-joined list — used for idempotency comparison against
    the JSON array-of-strings the manifest carries."""
    var builtins = Python.import_module("builtins")
    var parts: List[String] = List[String]()
    for p in lst:
        parts.append("\"" + p + "\"")
    return String(builtins.str(",".join(parts)))


# ============================================================================
# --img injector
# ============================================================================

def repo_root_from_argv() raises -> String:
    """Locate repo root as parent of tool/ containing this script,
    mirroring tool/dist.mojo's repo_root()."""
    var os_py = Python.import_module("os.path")
    var arg_list = argv()
    var abspath = os_py.abspath(String(arg_list[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def extract_p3_to_tmp(img: String, p3: PartInfo, tmp_p3: String) raises -> Int:
    """Extract P3 sectors out of img into tmp_p3 via dd. Returns dd exit code."""
    var sectors = p3.last_sector - p3.first_sector + 1
    return run_cmd_stream([
        "dd", "if=" + img, "bs=" + String(SECTOR_SIZE),
        "skip=" + String(p3.first_sector),
        "count=" + String(sectors),
        "of=" + tmp_p3])


def write_p3_back(img: String, p3: PartInfo, tmp_p3: String) raises -> Int:
    """Write modified tmp_p3 back into img at P3's offset via dd (NOTRUC)
    so any P4 (mkdata's SPONGE-DATA) survives the write."""
    var sectors = p3.last_sector - p3.first_sector + 1
    return run_cmd_stream([
        "dd", "if=" + tmp_p3, "of=" + img,
        "bs=" + String(SECTOR_SIZE),
        "seek=" + String(p3.first_sector),
        "count=" + String(sectors),
        "conv=notrunc"])


def list_rooted_files(ext2_path: String, root_path: String) raises -> String:
    """Run e2ls listing of <root_path> on <ext2_path>. The /system/bin
    check uses this; e2ls with -l gives one line per entry."""
    var cap = run_cmd_capture(["e2ls", "-l", ext2_path + ":" + root_path])
    return cap.output


def e2_path_exists(ext2_path: String, path: String) raises -> Bool:
    """True iff `path` (a slash-rooted path on the ext2) exists. We
    probe via e2ls; rc 0 + non-empty output means success. The
    specific rc-1-with-output behavior varies across e2fsprogs
    versions, so we conservatively treat any non-zero rc as missing
    AND require non-empty stdout to declare presence."""
    var cap = run_cmd_capture(["e2ls", ext2_path + ":" + path])
    if cap.rc != 0:
        return False
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(builtins.str(cap.output).strip()))


def e2_mkdir_p(ext2_path: String, path: String) raises -> Int:
    """Create directory `path` on the ext2 (mkdir -p semantics:
    create parent dirs as needed). e2mkdir only creates ONE level;
    we walk the path and create each missing ancestor."""
    var builtins = Python.import_module("builtins")
    var parts = builtins.str(path).split("/")
    var accum = String("")
    for part_py in parts:
        var part = String(part_py)
        if part.byte_length() == 0:
            continue
        if accum.byte_length() > 0:
            accum += "/"
        accum += part
        var rc = run_cmd_stream(["e2mkdir", ext2_path + ":" + accum])
        if rc != 0:
            return rc
    return 0


def e2_cp_into(ext2_path: String, src: String, dst: String) raises -> Int:
    """Copy file from host into ext2 via e2cp <src> <ext2>:<dst>."""
    return run_cmd_stream(["e2cp", "-p", src, ext2_path + ":" + dst])


def read_manifest_from_img(ext2_path: String) raises -> String:
    """Read /system/bake/bake_manifest.json from the ext2 (current
    state). Returns empty string if the file does not exist."""
    var cap = run_cmd_capture([
        "e2cp", "-p", ext2_path + ":/system/bake/bake_manifest.json", "-"])
    # e2cp writes to stdout if dest is '-'. rc==0 means success.
    if cap.rc != 0:
        return String("")
    return cap.output


def budget_for(profile_name: String) raises -> Int:
    """D15.5: minimal <= 1 GiB, desktop <= 2 GiB (in bytes)."""
    if profile_name == "minimal":
        return BUDGET_MINIMAL_MIB * 1024 * 1024
    if profile_name == "desktop":
        return BUDGET_DESKTOP_MIB * 1024 * 1024
    # Other profiles default to desktop budget.
    return BUDGET_DESKTOP_MIB * 1024 * 1024


def dir_size_bytes(path: String) raises -> Int:
    """Total bytes under path (recursive). Same as payload_size_bytes
    but kept as a separate name for the staged-payload check."""
    return payload_size_bytes(path)


def current_iso_timestamp() raises -> String:
    """ISO-8601-ish local timestamp for bake_manifest.json's staged_at."""
    var datetime = Python.import_module("datetime")
    var builtins = Python.import_module("builtins")
    var now = datetime.datetime.now()
    return String(builtins.str(now.isoformat()))


def cmd_inject(img: String, profile_name: String, dry_run: Bool) raises -> Int:
    """Run the --img flow:
        1. Pre-flight: host tools + image file present + P3=GENODE.
        2. Parse the profile (config_version=1 validation).
        3. Compute staged delta size; D15.5 budget gate (exit 2 if
           image size + delta exceeds budget; 'before writing').
        4. Idempotency check: read current bake_manifest.json from
           the image, compare to what we would write.
        5. T1 defense: for each enabled package, verify the binary
           named in pkg/<name>/metadata.xml's <binary> element is
           either in pkg/<name>/payload (depot-repackaged) or in
           /system/bin on the image (source-built).
        6. Extract P3 to temp file, stage every artifact, dd back.
        7. sgdisk post-verify: partition table unchanged, P3 still
           named GENODE.

    Returns 0 on success or no-op, 1 on io/tool failure, 2 on
    validation/budget refusal.
    """
    if not check_host_tools():
        return 1

    var root = repo_root_from_argv()
    var profiles_dir = root + "/" + BAKE_PROFILES_DIR
    var profile_path = profiles_dir + "/" + profile_name + ".profile"

    # ---------- 1. profile present + parse ----------
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")
    if not Bool(py=os_path.isfile(profile_path)):
        print("[sponge-bake] ERROR: profile not found: " + profile_path)
        print("  Use --list to see available profiles.")
        return 2
    var prof = parse_profile(profile_path, profile_name)
    if prof.config_version != CONFIG_VERSION:
        print("[sponge-bake] ERROR: " + profile_path
              + " declares config_version=" + String(prof.config_version)
              + ", but this tool only understands config_version="
              + String(CONFIG_VERSION) + " (D15.10).")
        print("  Bumps are breaking-only — update run/bake.inc AND")
        print("  tool/bake.mojo together, then bump the manifest schema.")
        return 2
    if prof.theme_active.byte_length() == 0:
        print("[sponge-bake] ERROR: " + profile_path
              + " has no [theme] active = ... line; required (D15.9).")
        return 2

    # ---------- 2. image present ----------
    if not Bool(py=os_path.isfile(img)):
        print("[sponge-bake] ERROR: image not found: " + img)
        return 1

    # ---------- 3. P3 present + named GENODE (misleading-success defense) ----------
    var p3_before = query_partition(img, 3)
    if not p3_before.present:
        print("[sponge-bake] ERROR: P3 (GENODE) missing from " + img)
        print("  This tool expects an image/disk output (P1+P2+P3).")
        return 1
    if p3_before.name != P3_LABEL:
        print("[sponge-bake] ERROR: P3 name is '" + p3_before.name
              + "', expected '" + P3_LABEL + "'. Refusing.")
        return 2

    # ---------- 4. compute staged delta size + D15.5 budget ----------
    # The staged delta = sum of pkg/<name>/payload/* sizes for
    # packages that have a payload. Metadata XMLs, the manifest,
    # config.defaults, theme.defaults are ~kilobytes; the budget
    # computation focuses on the bulk payload contribution. We add
    # a 4 KiB safety margin per package for the metadata+index.
    var delta_bytes = 0
    var safety_overhead = 0
    for i in range(len(prof.packages)):
        var pkg = prof.packages[i]
        var payload_dir = "pkg/" + pkg + "/payload"
        if Bool(py=os_path.isdir(payload_dir)):
            delta_bytes += dir_size_bytes(payload_dir)
        safety_overhead += 4096  # 4 KiB/pkg for metadata XML + index slot
    delta_bytes += safety_overhead

    var img_size = file_size(img)
    var projected = img_size + delta_bytes
    var budget = budget_for(profile_name)

    print("[sponge-bake] D15.5 size budget")
    print("  profile:        " + profile_name)
    print("  current image:  " + fmt_human_size(img_size)
          + " (" + String(img_size) + " B)")
    print("  staged delta:   " + fmt_human_size(delta_bytes)
          + " (" + String(delta_bytes) + " B)")
    print("  projected:      " + fmt_human_size(projected)
          + " (" + String(projected) + " B)")
    print("  budget:         " + fmt_human_size(budget)
          + " (" + String(budget) + " B)")
    if projected > budget:
        print()
        print("[sponge-bake] ERROR: D15.5 budget exceeded (projected "
              + fmt_human_size(projected) + " > budget "
              + fmt_human_size(budget) + ")")
        print("  Refusing to inject; nothing written.")
        return 2
    print("  OK — within budget")

    # ---------- 5. T1 defense: every package's binary must already
    #           be in /system/bin OR in its own payload ----------
    # We need /system/bin to exist on the image; probe via e2ls. The
    # desktop-from-disk scenario (run/sponge-desktop-disk.run) stages
    # source-built binaries there. Extract P3 to a temp file for
    # read-only verification (no host-tool write yet).
    print()
    print("[sponge-bake] T1 defense: verifying every enabled package's")
    print("  binary is in /system/bin OR in pkg/<name>/payload")
    print()

    var tmpdir = "/tmp/sponge-bake-" + profile_name + "-" + String(os_py.getpid())
    os_py.makedirs(tmpdir)
    var tmp_p3 = tmpdir + "/p3.ext2"

    var rc = extract_p3_to_tmp(img, p3_before, tmp_p3)
    if rc != 0:
        print("[sponge-bake] ERROR: dd extract of P3 failed (rc=" + String(rc) + ")")
        return 1

    # Probe /system/bin (must exist on the image).
    if not e2_path_exists(tmp_p3, "/system/bin"):
        print("[sponge-bake] ERROR: /system/bin not found in P3 of " + img)
        print("  This image is not from a sponge-desktop-disk-like scenario.")
        print("  tool/bake --img only injects into images that already carry")
        print("  the source-built binaries in /system/bin (R15.3 / T1).")
        return 2

    # Per-package binary check.
    for i in range(len(prof.packages)):
        var pkg = prof.packages[i]
        var meta_path = "pkg/" + pkg + "/metadata.xml"
        if not Bool(py=os_path.isfile(meta_path)):
            print("[sponge-bake] ERROR: pkg/" + pkg + "/metadata.xml missing")
            print("  (R15.3: every enabled package must declare its metadata)")
            return 2
        var bin_name = read_binary_name(meta_path)
        if bin_name.byte_length() == 0:
            print("  " + pkg + ": no <binary> in metadata — skipping T1 check")
            continue
        var payload_dir = "pkg/" + pkg + "/payload"
        var from_payload = payload_dir + "/" + bin_name
        if Bool(py=os_path.isfile(from_payload)):
            print("  " + pkg + ": binary '" + bin_name
                  + "' supplied by payload — OK")
            continue
        var on_image = "/system/bin/" + bin_name
        if e2_path_exists(tmp_p3, on_image):
            print("  " + pkg + ": binary '" + bin_name
                  + "' found at " + on_image + " — OK")
            continue
        print("[sponge-bake] ERROR: T1 — pkg '" + pkg
              + "' requires binary '" + bin_name + "', but it is")
        print("  neither in pkg/" + pkg + "/payload/ nor in the image's")
        print("  /system/bin. Baked metadata without its binary is the")
        print("  plan's trap T1; refusing to inject.")
        print("  Fix: add the binary to pkg/" + pkg + "/payload/"
              + ", or include")
        print("  it in the scenario's build list (so /system/bin/"
              + bin_name + " ends up")
        print("  on the image), or remove '" + pkg + "' from the profile.")
        return 2

    # ---------- 6. idempotency check ----------
    # The manifest's staged_at differs every run, so byte-equal
    # comparison won't detect no-ops. We instead compare the
    # "structural" fields (profile + staged_packages + theme) that
    # actually define the staged state, AND verify the payload
    # files we ship are present. This is the R15.3-adjacent
    # idempotency contract: re-running with the same profile and
    # an already-correct image is a verified no-op.
    print()
    print("[sponge-bake] idempotency check")
    var staged_at = current_iso_timestamp()
    var new_manifest = synthesize_manifest(prof, staged_at)
    var old_manifest = read_manifest_from_img(tmp_p3)
    var already_done = False
    if old_manifest.byte_length() > 0:
        var same_profile = extract_manifest_field(old_manifest, "profile") == profile_name
        var same_packages = extract_manifest_array_field(old_manifest, "staged_packages") == join_strings_with_comma(prof.packages)
        var same_theme = extract_manifest_field(old_manifest, "theme") == prof.theme_active
        var all_payloads_present = True
        for i in range(len(prof.packages)):
            var pkg = prof.packages[i]
            var payload_dir = "pkg/" + pkg + "/payload"
            if Bool(py=os_path.isdir(payload_dir)):
                # Probe one entry — list /system/pkg/<pkg>/ — as a canary.
                var listed = list_rooted_files(tmp_p3, "/system/pkg/" + pkg)
                if listed.byte_length() == 0:
                    all_payloads_present = False
                    break
        if same_profile and same_packages and same_theme and all_payloads_present:
            already_done = True
    if already_done:
        print("  identical staged content detected — idempotent no-op")
        print()
        print("[sponge-bake] done: image already staged with profile '"
              + profile_name + "'; no changes made")
        return 0

    # ---------- 7. dry-run exit ----------
    if dry_run:
        print()
        print("[sponge-bake] --dry-run: staging plan (no writes)")
        print()
        print("  Image:    " + img)
        print("  Profile:  " + profile_name)
        print("  Packages: " + String(len(prof.packages)))
        for i in range(len(prof.packages)):
            var pkg = prof.packages[i]
            var line = "    - " + pkg
            var payload_dir = "pkg/" + pkg + "/payload"
            if Bool(py=os_path.isdir(payload_dir)):
                line += "  (+ payload, " + fmt_human_size(dir_size_bytes(payload_dir)) + ")"
            print(line)
        print("  /system/bake/{bake_manifest.json, config.defaults, theme.defaults}")
        print("  /system/pkg_index.xml")
        print("  /system/pkg_<name>.xml  for each enabled package")
        print("  Mode:  img (full bake: payload files included)")
        print()
        print("[sponge-bake] dry-run done")
        return 0

    # ---------- 8. write staging ----------
    print()
    print("[sponge-bake] staging into P3 (mode=img)")
    print()

    # 8a. /system/pkg_<name>.xml metadata copies
    for i in range(len(prof.packages)):
        var pkg = prof.packages[i]
        var src_meta = "pkg/" + pkg + "/metadata.xml"
        if not Bool(py=os_path.isfile(src_meta)):
            print("[sponge-bake] ERROR: missing " + src_meta)
            return 2
        var dst = "/system/pkg_" + pkg + ".xml"
        rc = e2_cp_into(tmp_p3, src_meta, dst)
        if rc != 0:
            print("[sponge-bake] ERROR: e2cp " + src_meta + " -> " + dst
                  + " failed (rc=" + String(rc) + ")")
            return 1
        print("  staged " + dst)

    # 8b. /system/pkg_index.xml
    var idx_text = synthesize_pkg_index(prof.packages)
    var idx_tmp = tmpdir + "/pkg_index.xml"
    var builtins = Python.import_module("builtins")
    var idx_f = builtins.open(idx_tmp, "w")
    idx_f.write(idx_text)
    idx_f.close()
    rc = e2_cp_into(tmp_p3, idx_tmp, "/system/pkg_index.xml")
    if rc != 0:
        print("[sponge-bake] ERROR: e2cp pkg_index.xml -> /system/pkg_index.xml failed")
        return 1
    print("  staged /system/pkg_index.xml")

    # 8c. /system/pkg/<name>/payload/* for every package that has one
    for i in range(len(prof.packages)):
        var pkg = prof.packages[i]
        var payload_dir = "pkg/" + pkg + "/payload"
        if not Bool(py=os_path.isdir(payload_dir)):
            continue
        var on_image = "/system/pkg/" + pkg + "/payload"
        rc = e2_mkdir_p(tmp_p3, on_image)
        if rc != 0:
            print("[sponge-bake] ERROR: e2mkdir " + on_image + " failed")
            return 1
        # Copy each payload file via e2cp.
        var files = find_files_recursive(payload_dir)
        for f_py in files:
            var f = String(f_py)
            var rel = String(builtins.str(f)[payload_dir.byte_length():])
            var dst = on_image + rel
            rc = e2_cp_into(tmp_p3, f, dst)
            if rc != 0:
                print("[sponge-bake] ERROR: e2cp " + f + " -> " + dst + " failed")
                return 1
        print("  staged " + on_image + "/* (" + String(len(files)) + " files)")

    # 8d. /system/bake/ defaults: manifest + config + theme
    rc = e2_mkdir_p(tmp_p3, "/system/bake")
    if rc != 0:
        print("[sponge-bake] ERROR: e2mkdir /system/bake failed")
        return 1
    # Write each file via e2cp from a temp source (e2cp takes
    # host->ext2 only; we need to drop them on disk first).
    var manifest_tmp = tmpdir + "/bake_manifest.json"
    var mf = builtins.open(manifest_tmp, "w")
    mf.write(new_manifest)
    mf.close()
    rc = e2_cp_into(tmp_p3, manifest_tmp, "/system/bake/bake_manifest.json")
    if rc != 0:
        print("[sponge-bake] ERROR: e2cp bake_manifest.json failed")
        return 1
    print("  staged /system/bake/bake_manifest.json")

    var cfg_tmp = tmpdir + "/config.defaults"
    var cf = builtins.open(cfg_tmp, "w")
    cf.write(synthesize_config_defaults(prof))
    cf.close()
    rc = e2_cp_into(tmp_p3, cfg_tmp, "/system/bake/config.defaults")
    if rc != 0:
        print("[sponge-bake] ERROR: e2cp config.defaults failed")
        return 1
    print("  staged /system/bake/config.defaults")

    # theme.defaults is a copy of repos/sponge/src/sponge-de/themes/<name>.theme
    var theme_src = root + "/" + THEMES_DIR + "/" + prof.theme_active + THEME_SUFFIX
    if not Bool(py=os_path.isfile(theme_src)):
        print("[sponge-bake] ERROR: theme file missing: " + theme_src)
        return 2
    rc = e2_cp_into(tmp_p3, theme_src, "/system/bake/theme.defaults")
    if rc != 0:
        print("[sponge-bake] ERROR: e2cp theme.defaults failed")
        return 1
    print("  staged /system/bake/theme.defaults")

    # ---------- 9. dd back into the image ----------
    print()
    print("[sponge-bake] writing P3 back into " + img + " (dd, NOTRUC)")
    rc = write_p3_back(img, p3_before, tmp_p3)
    if rc != 0:
        print("[sponge-bake] ERROR: dd write-back failed (rc=" + String(rc) + ")")
        return 1

    # ---------- 10. sgdisk post-verify ----------
    var p3_after = query_partition(img, 3)
    if not p3_after.present:
        print("[sponge-bake] ERROR: P3 missing after inject (sgdisk pre/post defense)")
        return 1
    if p3_after.name != P3_LABEL:
        print("[sponge-bake] ERROR: P3 name changed after inject ('"
              + p3_before.name + "' -> '" + p3_after.name + "')")
        return 1
    if p3_after.first_sector != p3_before.first_sector:
        print("[sponge-bake] ERROR: P3 first sector changed after inject ("
              + String(p3_before.first_sector) + " -> "
              + String(p3_after.first_sector) + ")")
        return 1
    if p3_after.last_sector != p3_before.last_sector:
        print("[sponge-bake] ERROR: P3 last sector changed after inject ("
              + String(p3_before.last_sector) + " -> "
              + String(p3_after.last_sector) + ")")
        return 1
    print("  sgdisk pre/post: P3 [" + String(p3_after.first_sector) + ".."
          + String(p3_after.last_sector) + "] name=" + p3_after.name
          + " (unchanged)")

    # Cleanup tmpdir.
    var shutil = Python.import_module("shutil")
    shutil.rmtree(tmpdir)

    print()
    print("[sponge-bake] done: profile '" + profile_name
          + "' staged into " + img)
    print("  staged packages: " + String(len(prof.packages)))
    print("  /system/bake/{manifest, config, theme} placed for first-boot seed")
    return 0


def find_files_recursive(root: String) raises -> List[String]:
    """List every file under `root` (recursive), as absolute paths.
    Uses Python's os.walk since Mojo has no path walker."""
    var os_py = Python.import_module("os")
    var out: List[String] = List[String]()
    var walker = os_py.walk(root)
    for entry in walker:
        var dirpath = String(entry[0])
        var filenames = entry[2]
        for filename_py in filenames:
            var fname = String(filename_py)
            out.append(dirpath + "/" + fname)
    return out^


# ============================================================================
# main
# ============================================================================

def main() raises:
    var args = argv()

    # Parse mode + flags. We dispatch by the first non-flag token.
    var mode = String("")  # "list" | "show" | "img" | "help"
    var img_path = String("")
    var profile_arg = String("")
    var dry_run = False
    var i = 1
    while i < len(args):
        var a = String(args[i])
        if a == "help" or a == "--help" or a == "-h":
            cmd_help()
            return
        if a == "--list":
            if mode.byte_length() > 0:
                print("error: --list is mutually exclusive with " + mode)
                exit(1)
            mode = "list"
            i += 1
            continue
        if a == "--show":
            if mode.byte_length() > 0:
                print("error: --show is mutually exclusive with " + mode)
                exit(1)
            if i + 1 >= len(args):
                print("error: --show requires <profile>")
                exit(1)
            mode = "show"
            profile_arg = String(args[i + 1])
            i += 2
            continue
        if a == "--img":
            if mode.byte_length() > 0:
                print("error: --img is mutually exclusive with " + mode)
                exit(1)
            if i + 1 >= len(args):
                print("error: --img requires <file>")
                exit(1)
            mode = "img"
            img_path = String(args[i + 1])
            i += 2
            continue
        if a == "--profile":
            if i + 1 >= len(args):
                print("error: --profile requires <name>")
                exit(1)
            profile_arg = String(args[i + 1])
            i += 2
            continue
        if a == "--dry-run":
            dry_run = True
            i += 1
            continue
        if startswith_str(a, "--profile="):
            var builtins = Python.import_module("builtins")
            comptime plen = 10  # len("--profile=")
            profile_arg = String(builtins.str(a)[plen:])
            i += 1
            continue
        if startswith_str(a, "-") and a != "-":
            print("error: unknown option '" + a + "'")
            print()
            cmd_help()
            exit(1)
        print("error: unexpected positional argument '" + a + "'")
        print()
        cmd_help()
        exit(1)

    if mode.byte_length() == 0:
        print("error: one of --list, --show, or --img is required")
        print()
        cmd_help()
        exit(1)

    var root = repo_root_from_argv()
    var profiles_dir = root + "/" + BAKE_PROFILES_DIR

    if mode == "list":
        var profiles = list_profiles(profiles_dir)
        render_list(profiles)
        return

    if mode == "show":
        if profile_arg.byte_length() == 0:
            print("error: --show requires <profile>")
            exit(1)
        var profile_path = profiles_dir + "/" + profile_arg + ".profile"
        var os_path = Python.import_module("os.path")
        if not Bool(py=os_path.isfile(profile_path)):
            print("[sponge-bake] ERROR: unknown profile '" + profile_arg + "'")
            print("  Use --list to see available profiles.")
            exit(2)
        var prof = parse_profile(profile_path, profile_arg)
        render_show(prof, profiles_dir)
        # Loud error on config_version mismatch (per spec).
        if prof.config_version != CONFIG_VERSION:
            print()
            print("[sponge-bake] ERROR: config_version="
                  + String(prof.config_version) + " (required: "
                  + String(CONFIG_VERSION) + ")")
            exit(2)
        return

    if mode == "img":
        if img_path.byte_length() == 0:
            print("error: --img requires <file>")
            exit(1)
        if profile_arg.byte_length() == 0:
            print("error: --img requires --profile <name>")
            exit(1)
        if profile_arg == "none":
            print("error: --profile=none is bake.inc's escape hatch;")
            print("  it produces nothing to inject. Use 'minimal' or")
            print("  'desktop' with --img instead.")
            exit(2)
        # Validate the profile name early (before any disk writes).
        # ALLOWED_PROFILES is a comptime List[StringSlice] — we
        # cannot iterate it at runtime without materialize. Instead
        # we compare directly against the two valid values.
        var ok = False
        if profile_arg == "minimal":
            ok = True
        elif profile_arg == "desktop":
            ok = True
        if not ok:
            print("error: --profile '" + profile_arg
                  + "' is not one of: minimal, desktop")
            print("  (Use --list to see what's available.)")
            exit(1)
        var rc = cmd_inject(img_path, profile_arg, dry_run)
        exit(rc)

    print("error: internal mode dispatch failure (mode='" + mode + "')")
    exit(1)