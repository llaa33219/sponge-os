# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS top-level build wrapper.
#
# Automates the standard Genode build flow against the vendored Genode tree
# at <repo>/genode/ so contributors don't have to remember the
# create_builddir + prepare_port + make run/<scenario> dance.
#
# Every automated step only mutates the vendored genode/ tree or the
# git-ignored var/ scratch space, which AGENTS.md §3.5 explicitly permits.
# The manual equivalent of every step is documented in
# docs/08-development.md (control escape hatch).
#
# Usage:
#   mojo tool/build.mojo prepare
#   mojo tool/build.mojo ports
#   mojo tool/build.mojo list
#   mojo tool/build.mojo run <scenario>
#   mojo tool/build.mojo run --manual <scenario>
#   mojo tool/build.mojo help

from std.sys import argv, exit
from std.python import Python, PythonObject

# Marker pair delimiting the managed block in build.conf. Used for
# idempotency: the block is appended only when the marker is absent.
comptime BLOCK_BEGIN = "# >>> sponge-os managed block >>>"
comptime BLOCK_END = "# <<< sponge-os managed block <<<"

# Ports required for base-sel4 boot (sel4, sel4_tools, grub2) and the
# sponge-de Qt6 GUI (the rest). `linux` is pulled in by the
# drivers_interactive-pc USB stack (usb_hid/pc_usb_host via dde_linux),
# needed for run/sponge-de-sel4-interactive.run (absolute-pointer input).
# `dde_ipxe` and `lwip` are pulled in by the networking probe
# run/sponge-net-probe.run (todo 12).
# `bash`, `vim`, `ncurses` are pulled in by the terminal package
# (pkg/terminal, todo 13): bash-minimal + vim-minimal are noux packages
# built from source; vim-minimal links ncurses. ncurses' Caps generation
# requires `mawk` on the host (see docs/11-environment.md §7).
# Downloads are SHA-256-verified and land in genode/contrib/.
# prepare_port itself skips already-prepared ports.
def port_list() -> List[String]:
    return [
        "libc",
        "stdcxx",
        "mesa",
        "zlib",
        "libpng",
        "expat",
        "libdrm",
        "x86emu",
        "qoost",
        "qt6_api",
        "qt6_base",
        "sel4",
        "sel4_tools",
        "grub2",
        "linux",
        "jitterentropy",
        "dde_ipxe",
        "lwip",
        "dde_rump",
        "bash",
        "vim",
        "ncurses",
    ]


def repo_root() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def run_argv(cmd: List[String], cwd: String) raises -> Int:
    """Run a command, streaming stdout/stderr to this terminal, and return
    the child exit code. Uses Python subprocess because the Mojo stdlib has
    no process-spawning facility yet (see tool/README.md)."""
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


def print_help() raises:
    print("Sponge OS build wrapper")
    print()
    print("Usage:")
    print("  mojo tool/build.mojo <command> [args]")
    print()
    print("Commands:")
    print("  prepare                 Set up genode/build/x86_64 and build.conf")
    print("  ports                   Download the required Genode port sources")
    print("  list                    List available run scenarios in run/")
    print("  run <scenario>          Build and run a scenario")
    print("  run --manual <scenario> Print the manual commands instead of running")
    print("  help                    Show this help")
    print()
    print("Every automated step touches only the vendored genode/ tree or the")
    print("git-ignored var/ directory. The manual equivalent of every step is")
    print("documented in docs/08-development.md.")


def list_scenarios() raises:
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")
    var root = repo_root()
    var run_dir = root + "/run"

    if not os_path.isdir(run_dir):
        print("error: no run/ directory found at " + run_dir)
        exit(1)

    var entries = os_py.listdir(run_dir)

    var found = False
    for entry in entries:
        var name = String(entry)
        if name.endswith(".run"):
            found = True
            print("  " + name.removesuffix(".run"))

    if not found:
        print("No .run scenarios found in " + run_dir)


def ensure_build_conf(build_conf: String) raises -> Bool:
    """Make etc/build.conf Sponge-ready. Returns True when anything changed.

    Two kinds of edits, both idempotent:

    1. The generated template's KERNEL/BOARD defaults are switched in
       place, near the top of the file. Appending them at the bottom
       does NOT work: the template's own 'ifdef KERNEL' / 'ifdef BOARD'
       blocks read those variables earlier, and the generated
       'BOARD ?= pc' would silently win over a later 'BOARD ?= linux'.
    2. A marker-delimited block with the REPOSITORIES additions and the
       parallel-make setting is appended once.

    A user who switched KERNEL/BOARD to another kernel (e.g. sel4) is
    left alone: the template markers are only replaced when present
    verbatim.
    """
    var os_path = Python.import_module("os.path")
    var builtins = Python.import_module("builtins")
    var os_py = Python.import_module("os")

    var content = builtins.str()
    if os_path.isfile(build_conf):
        var f = builtins.open(build_conf, "r")
        content = f.read()
        f.close()

    var changed = False

    # 1. Top-section template defaults (see docstring). 'count' guards
    #    make this a no-op once the user has diverged from the template.
    if Int(py=content.count("#KERNEL ?= nova")) > 0:
        content = content.replace("#KERNEL ?= nova", "KERNEL ?= linux")
        changed = True
    if Int(py=content.count("BOARD ?= pc")) > 0:
        content = content.replace("BOARD ?= pc", "BOARD ?= linux")
        changed = True

    if changed:
        var out = builtins.open(build_conf, "w")
        out.write(content)
        out.close()

    # 2. Marker-delimited managed block (appended once).
    if Int(py=content.count(BLOCK_BEGIN)) > 0:
        return changed

    var cpu_count = Int(py=os_py.cpu_count())
    var block = (
        "\n"
        + BLOCK_BEGIN
        + "\n"
        + "# Appended by 'tool/build prepare'. Delete this whole block to undo.\n"
        + "# KERNEL/BOARD live near the top of this file because the generated\n"
        + "# ifdef blocks below them read those variables early.\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/sponge\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/libports\n"
        + "# ports: noux packages (bash-minimal, vim-minimal) for the terminal\n"
        + "# package (pkg/terminal). Depends on libports (ncurses). Source-built\n"
        + "# only — no depot import for these (Phase 7 todo 13).\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/ports\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/gems\n"
        + "# pc + dde_linux: PC hardware drivers (platform/pc, vesa via libports,\n"
        + "# ps2, acpi, pci_decode, event_filter via os) and the DDE-Linux USB\n"
        + "# stack (pc_usb_host, usb_hid) used by the base-sel4 interactive GUI\n"
        + "# scenario (run/sponge-de-sel4-interactive.run). Harmless on base-linux.\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/pc\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/dde_linux\n"
        + "# dde_ipxe: iPXE-based NIC driver (ipxe_nic) for the base-sel4\n"
        + "# networking scenarios (run/sponge-net-probe.run). Harmless on\n"
        + "# base-linux (REQUIRES=x86 target built only on demand).\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/dde_ipxe\n"
        + "# dde_rump: NetBSD rump kernel for ext2/ffs/msdos/cd9660/ntfs/udf\n"
        + "# filesystems via the vfs_rump plugin. Used by the base-sel4 storage\n"
        + "# chain (run/sponge-boot.run, docs/14 §5). Harmless on base-linux.\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/dde_rump\n"
        + "MAKE += -j"
        + String(cpu_count)
        + "\n"
        + BLOCK_END
        + "\n"
    )

    var out = builtins.open(build_conf, "a")
    out.write(block)
    out.close()
    return True


def cmd_prepare() raises:
    var os_path = Python.import_module("os.path")
    var root = repo_root()
    var genode_dir = root + "/genode"
    var build_dir = genode_dir + "/build/x86_64"

    print("[sponge-build] prepare: setting up the Genode build directory")
    print()

    # 1. The vendored Genode tree must exist.
    if not os_path.isdir(genode_dir):
        print("error: vendored Genode tree not found at " + genode_dir)
        print("The repository is expected to vendor Genode at genode/ (see")
        print("AGENTS.md §5.2). Restore the tree before running prepare.")
        exit(1)
    print("ok: vendored Genode tree found at " + genode_dir)

    # 2. Create the build directory if absent.
    if os_path.isdir(build_dir):
        print("ok: build directory already exists at " + build_dir)
    else:
        print("... creating build directory via tool/create_builddir x86_64")
        var rc = run_argv(
            ["./tool/create_builddir", "x86_64"], cwd=genode_dir
        )
        if rc != 0 or not os_path.isdir(build_dir):
            print("error: create_builddir failed (exit code " + String(rc) + ")")
            exit(1)
        print("ok: created " + build_dir)

    # 3. build.conf: template KERNEL/BOARD defaults + managed block.
    var build_conf = build_dir + "/etc/build.conf"
    if ensure_build_conf(build_conf):
        print("ok: configured " + build_conf + " (KERNEL/BOARD defaults + managed block)")
    else:
        print("ok: build.conf already configured (no changes needed)")

    # 4. Qt6 host tools (warning only, never a failure).
    var qt6_dir = root + "/var/qt6-host-tools"
    var qmake = qt6_dir + "/bin/qmake"
    var qt6_ok = os_path.isfile(qmake)
    if qt6_ok:
        print("ok: Qt6 host tools found at " + qt6_dir)
    else:
        print()
        print("WARNING: Qt6 host tools not found at " + qt6_dir)
        print("The sponge-de GUI scenarios need qmake/moc from a host Qt6")
        print("build. To provide them, either:")
        print("  - build them via genode/tool/tool_chain_qt6 and place the")
        print("    result at " + qt6_dir + ", or")
        print("  - symlink an existing install:")
        print("      ln -s <your-qt6-tools> " + qt6_dir)
        print("This is not fatal: non-GUI scenarios build and run without it.")

    # 5. Summary.
    print()
    print("[sponge-build] prepare summary")
    print("  genode tree:   " + genode_dir)
    print("  build dir:     " + build_dir)
    print("  build.conf:    managed block present")
    if qt6_ok:
        print("  qt6 host tools: present")
    else:
        print("  qt6 host tools: MISSING (see warning above)")
    print()
    print("Remaining manual steps (see docs/08-development.md):")
    print("  - run './tool/build ports' to download port sources")
    print("  - install Qt6 host tools if you plan to build sponge-de")


def port_prepared(genode_dir: String, port: String) raises -> Bool:
    """True when genode/contrib/<port>-<hash>/ already exists.

    prepare_port re-runs its install steps for already-prepared ports.
    Most ports no-op through that, but dde_rump's git `update` step resets
    only part of the tree, so its patches then fail to re-apply (upstream
    non-idempotency — verified 2026-08-07: `aarch64.patch` hunks ignored,
    "Reversed (or previously applied) patch detected"). Skipping already-
    prepared ports here keeps `./tool/build ports` safe to re-run."""
    var glob_py = Python.import_module("glob")
    var builtins = Python.import_module("builtins")
    var os_path = Python.import_module("os.path")
    var matches = glob_py.glob(
        genode_dir + "/repos/*/ports/" + port + ".hash")
    if Int(py=builtins.len(matches)) == 0:
        return False
    var hash_value = String(open(String(matches[0]), "r").read().strip())
    return Bool(py=os_path.isdir(
        genode_dir + "/contrib/" + port + "-" + hash_value))


def cmd_ports() raises:
    var os_path = Python.import_module("os.path")
    var root = repo_root()
    var genode_dir = root + "/genode"

    if not os_path.isdir(genode_dir):
        print("error: vendored Genode tree not found at " + genode_dir)
        print("Run './tool/build prepare' first.")
        exit(1)

    var ports = port_list()

    print("[sponge-build] ports: preparing " + String(len(ports)) + " ports")
    print("(ports whose contrib dir already exists are skipped)")
    print()

    var failed: List[String] = []
    var skipped: List[String] = []
    for port in ports:
        if port_prepared(genode_dir, port):
            skipped.append(port)
            continue
        print("[sponge-build] --- " + port + " ---")
        var rc = run_argv(
            ["./tool/ports/prepare_port", port], cwd=genode_dir
        )
        if rc != 0:
            failed.append(port)
            print("[sponge-build] --- " + port + ": FAILED (exit code "
                  + String(rc) + ") ---")
        print()

    print("[sponge-build] ports summary")
    for port in ports:
        var is_failed = False
        for bad in failed:
            if bad == port:
                is_failed = True
        var is_skipped = False
        for done in skipped:
            if done == port:
                is_skipped = True
        if is_failed:
            print("  FAIL: " + port)
        elif is_skipped:
            print("  ok:   " + port + " (already prepared)")
        else:
            print("  ok:   " + port)

    if len(failed) > 0:
        print()
        print("error: " + String(len(failed)) + " port(s) failed to prepare")
        exit(1)

    print()
    print("All ports prepared successfully.")


def cmd_run(scenario: String, manual: Bool) raises:
    var os_path = Python.import_module("os.path")
    var root = repo_root()
    var build_dir = root + "/genode/build/x86_64"

    if manual:
        print("To run scenario '" + scenario + "' manually:")
        print("  cd " + root + "/genode/build/x86_64")
        print("  make run/" + scenario)
        print()
        print("If the build directory does not exist yet, run first:")
        print("  ./tool/build prepare && ./tool/build ports")
        return

    if not os_path.isdir(build_dir):
        print("error: build directory not found at " + build_dir)
        print("Run './tool/build prepare' first.")
        exit(1)

    var rc = run_argv(
        ["make", "-C", build_dir, "run/" + scenario], cwd=""
    )
    if rc != 0:
        print("error: scenario '" + scenario + "' failed (exit code "
              + String(rc) + ")")
        exit(rc)


def main() raises:
    var args = argv()

    if len(args) < 2:
        print_help()
        return

    var subcommand = String(args[1])

    if subcommand == "help" or subcommand == "--help" or subcommand == "-h":
        print_help()
        return

    if subcommand == "list":
        list_scenarios()
        return

    if subcommand == "prepare":
        cmd_prepare()
        return

    if subcommand == "ports":
        cmd_ports()
        return

    if subcommand == "run":
        if len(args) < 3:
            print("error: 'run' requires a scenario name")
            print("Try: mojo tool/build.mojo list")
            exit(1)
        if String(args[2]) == "--manual":
            if len(args) < 4:
                print("error: 'run --manual' requires a scenario name")
                exit(1)
            cmd_run(String(args[3]), manual=True)
            return
        cmd_run(String(args[2]), manual=False)
        return

    print("error: unknown command '" + subcommand + "'")
    print()
    print_help()
    exit(1)
