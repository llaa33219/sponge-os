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
#   mojo tool/build.mojo verify          # run ./tool/patches verify and ./tool/hw_compat assert sequentially
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
        # `stb` (stb_image header library) is pulled in by the alpha
        # desktop scenario's image-decode path (run/sponge-alpha.run).
        "stb",
        # `ttf-bitstream-vera` (Vera font set) is staged as a runtime ROM
        # by the alpha desktop scenario (run/sponge-alpha.run).
        "ttf-bitstream-vera",
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


def run_argv_env(
    cmd: List[String], cwd: String, env: PythonObject
) raises -> Int:
    """Phase 12 W1 patch-pre-flight helper: run a subprocess with an
    explicit environment dict.

    The default `run_argv` inherits the parent process env via
    `subprocess.call`. That works for non-mojo targets (create_builddir,
    prepare_port, make) but breaks the inner `./tool/patches verify` call
    from `verify_patches_or_exit`:

      * `/home/luke/sponge-os/.venv/bin/python` is a symlink to
        `/usr/bin/python3`. The kernel resolves the symlink before
        execve, so the spawned python sees sys.executable=/usr/bin/python3
        and sys.prefix=/usr.
      * Without VIRTUAL_ENV (which the agent harness does not set),
        python cannot activate the venv site-packages on its own, so
        `from mojo._entrypoints import exec_mojo` fails with
        `ModuleNotFoundError: No module named 'mojo'`.

    Passing VIRTUAL_ENV and PYTHONPATH explicitly is the documented
    Python way to activate a venv for a child process without relying on
    argv[0] resolution. This is read-only against the repository: it
    only sets env vars, never files. The existing non-mojo callers of
    `run_argv` are untouched.
    """
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var rc: PythonObject
    if cwd.byte_length() > 0:
        rc = subprocess.call(py_args, cwd=cwd, env=env)
    else:
        rc = subprocess.call(py_args, env=env)
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
    print("  verify                  Run ./tool/patches verify then")
    print("                          ./tool/hw_compat assert sequentially")
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

    # 1. Top-section template defaults (see docstring). The guards fire
    #    ONLY on a pristine create_builddir template: the '#KERNEL ?= nova'
    #    marker is present only then. Checking BOARD independently would
    #    clobber a user's 'BOARD ?= pc' seL4 setting on a prepare re-run
    #    (the template's own BOARD default is 'pc' too, so the marker
    #    cannot tell template from user config — only the KERNEL marker
    #    can).
    if Int(py=content.count("#KERNEL ?= nova")) > 0:
        content = content.replace("#KERNEL ?= nova", "KERNEL ?= linux")
        content = content.replace("BOARD ?= pc", "BOARD ?= linux")
        changed = True

    if changed:
        var out = builtins.open(build_conf, "w")
        out.write(content)
        out.close()

    # 1.5 REPOSITORIES order fix (in place, idempotent): the create_builddir
    #    template puts repos/base-$(KERNEL) BEFORE repos/base. With that
    #    order, forwarding-only target.mk dirs (base-sel4/src/timer/hpet)
    #    shadow repos/base's buildable variant of the same target, and the
    #    target's component.cc is not found (link fails with "cannot find
    #    component.o"). Move the kernel repo after the base/os/demo block.
    var kernel_block = (
        "##\n## Kernel-specific repository\n##\n\n"
        + "ifdef KERNEL\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/base-$(KERNEL)\n"
        + "endif\n"
    )
    var moved_marker = (
        "## Kernel-specific repository (moved after repos/base:"
    )
    var demo_block = (
        "REPOSITORIES += $(GENODE_DIR)/repos/base\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/os\n"
        + "REPOSITORIES += $(GENODE_DIR)/repos/demo\n"
    )
    if Int(py=content.count(moved_marker)) == 0 and Int(
        py=content.count(kernel_block)
    ) > 0 and Int(py=content.count(demo_block)) > 0:
        content = content.replace(kernel_block, "")
        var moved_block = (
            demo_block
            + "\n##\n"
            + moved_marker + "\n"
            + "## with base-$(KERNEL) first, forwarding-only target.mk dirs\n"
            + "## like base-sel4/src/timer/hpet shadow repos/base's buildable\n"
            + "## variant and the target's component.cc is not found)\n"
            + "##\n\n"
            + "ifdef KERNEL\n"
            + "REPOSITORIES += $(GENODE_DIR)/repos/base-$(KERNEL)\n"
            + "endif\n"
        )
        content = content.replace(demo_block, moved_block)
        var out2 = builtins.open(build_conf, "w")
        out2.write(content)
        out2.close()
        changed = True

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

    # Phase 12 W1 (docs/plans/phase12-hardware.md W1, risk 19): run the
    # read-only patch-ledger verify gate as the first action of every
    # build-related command, so the patch state is trusted before any
    # build directory, build.conf, or scenario is touched. The gate
    # itself never repairs, exports, drops, or modifies patches — see
    # docs/11-environment.md §4 / §4.1 for the read-only contract.
    verify_patches_or_exit(root)

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


def verify_patches_or_exit(root: String) raises:
    """Phase 12 W1 patch-ledger pre-flight (risk 19).

    Invokes the read-only `./tool/patches verify` contract before any
    build-related command proceeds. The gate is intentionally narrow:

      * it ONLY runs the existing read-only verify command;
      * it NEVER repairs, exports, drops, or modifies patches
        (docs/11-environment.md §4 / §4.1; tool/patches.mojo is
        read-only against the repository by design);
      * it propagates the verify exit code (non-zero = a ledger
        mismatch, build refuses to proceed);
      * it prints a direct message naming docs/11-environment.md §4 so
        the user knows where to read the contract and what to do.

    A non-zero exit here is the W1 loud failure mode: dropping a patch
    silently to make the build "go" is rejected. The manual equivalent
    of this gate is `./tool/patches verify`, also documented in §4.1.
    """
    var os_py = Python.import_module("os")
    var builtins = Python.import_module("builtins")

    # Build an env dict that mirrors the parent env but activates the
    # repo-local Mojo venv explicitly, so the child python can resolve
    # the `mojo` package (the venv python is a symlink to /usr/bin/python3
    # — see `run_argv_env` for the full rationale). This env tweak is the
    # minimum required to make the gate actually run; it does NOT touch
    # the repository or the patch ledger.
    var env = builtins.dict()
    var parent_env = os_py.environ
    for k in parent_env.keys():
        env[k] = String(parent_env[k])
    env["VIRTUAL_ENV"] = root + "/.venv"
    env["PYTHONPATH"] = root + "/.venv/lib/python3.14/site-packages"
    # Clear PYTHONHOME so python uses sys.prefix, not a stale override.
    if "PYTHONHOME" in env.keys():
        env.pop("PYTHONHOME")

    print("[sponge-build] pre-flight: verifying patch ledger (read-only)")
    var rc = run_argv_env(
        ["./tool/patches", "verify"], cwd=root, env=env
    )
    if rc != 0:
        print()
        print("error: patch-ledger verify failed (exit code "
              + String(rc) + ")")
        print("The Sponge patch ledger (docs/11-environment.md §4) is")
        print("out of sync with the vendored Genode subtree. The build")
        print("refuses to proceed rather than drop, export, or modify a")
        print("patch on its own. Read docs/11-environment.md §4.1 and run")
        print("`./tool/patches list` to diagnose; do NOT bypass this gate.")
        exit(rc)
    print()


def verify_hardware_compat_or_exit(root: String, env: PythonObject) raises:
    """Phase 12 W5 hardware-compat pre-flight (risk 13 + risk 22 + risk 23 +
    risk 24 mitigation).

    Invokes the read-only `./tool/hw_compat assert` contract against the
    committed docs/15-hardware-compatibility.md cross-product ledger.
    The gate is intentionally narrow:

      * it ONLY runs the existing read-only assert command;
      * it NEVER edits docs/15 or invents a cell; tool/hw_compat.mojo
        is read-only by design and has no `generate`/`update`/write
        path (plan step 6 / risk 23);
      * it propagates the assert exit code:
        - 0: all rules pass (4 verified, 1 smoke-only, 11 gap);
        - 1: one or more rule violations (missing scenario, missing
          marker, over-budget timing, etc.);
        - 2: a `target: real-hardware` cell was found — this is a
          loud refusal because real hardware is a Phase 15 deliverable.
      * it prints a direct message naming docs/15 §3 so the user knows
        where to read the cell-contract format and what to do.

    This helper is the read-only sibling of `verify_patches_or_exit`.
    It reuses the same `run_argv_env` helper (the W1 env dict already
    activates the repo-local Mojo venv — see `run_argv_env` docstring
    for the full rationale). It does NOT touch the repository.
    """
    print("[sponge-build] pre-flight: verifying hardware compat ledger (read-only)")
    var rc = run_argv_env(
        ["./tool/hw_compat", "assert"], cwd=root, env=env
    )
    if rc != 0:
        print()
        print("error: hardware-compat assert failed (exit code "
              + String(rc) + ")")
        if rc == 2:
            print("A `target: real-hardware` cell was found in")
            print("docs/15-hardware-compatibility.md. Real hardware is a")
            print("Phase 15 deliverable; not a Phase 12 cell. Read")
            print("docs/15 §3 and the plan W5 step 5 / risk 22.")
        else:
            print("The cross-product ledger in docs/15-hardware-")
            print("compatibility.md failed one or more rule checks. Read")
            print("docs/15 §3 (Cell contract format) and the plan W5")
            print("step 7 + risk 13 + risk 24 mitigations; do NOT bypass")
            print("this gate.")
        exit(rc)
    print()


def cmd_verify() raises:
    """Phase 12 W5 sequential verify path.

    Runs `./tool/patches verify` then `./tool/hw_compat assert`, in that
    order, propagating either failure with a direct message. Both
    commands are read-only against the repository; the gate never
    repairs, exports, drops, or auto-populates anything (plan step 6 +
    step 8 / risk 19 + risk 23).
    """
    var root = repo_root()
    # Build the same env dict that activates the repo-local Mojo venv
    # (see verify_patches_or_exit + run_argv_env for the full rationale).
    var os_py = Python.import_module("os")
    var builtins = Python.import_module("builtins")
    var env = builtins.dict()
    var parent_env = os_py.environ
    for k in parent_env.keys():
        env[k] = String(parent_env[k])
    env["VIRTUAL_ENV"] = root + "/.venv"
    env["PYTHONPATH"] = root + "/.venv/lib/python3.14/site-packages"
    if "PYTHONHOME" in env.keys():
        env.pop("PYTHONHOME")

    print("[sponge-build] verify: running patch + hardware-compat gates sequentially")
    print()
    verify_patches_or_exit(root)
    verify_hardware_compat_or_exit(root, env)
    print("[sponge-build] verify: OK (both gates passed)")


def cmd_run(scenario: String, manual: Bool) raises:
    var os_path = Python.import_module("os.path")
    var root = repo_root()
    var build_dir = root + "/genode/build/x86_64"

    # Phase 12 W1 (docs/plans/phase12-hardware.md W1, risk 19): the
    # patch-ledger pre-flight fires BEFORE any build step touches the
    # shared build directory. `manual=True` only prints the manual
    # commands and does not invoke make, so the gate is intentionally
    # scoped to the actual build path. The gate is read-only — see
    # docs/11-environment.md §4 / §4.1.
    if not manual:
        verify_patches_or_exit(root)

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

    if subcommand == "verify":
        cmd_verify()
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
