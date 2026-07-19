# 08 - Development Guide

> The environment, build steps, testing approach, and workflow needed to
> develop Sponge OS.

---

## 1. Prerequisites

### 1.1 Genode Source Tree

Sponge OS is designed to live under the `repos/` directory of a Genode
source tree. To build it:

1. Check out the Genode source tree. The canonical location is now
   [Codeberg](https://codeberg.org/genodelabs/genode); GitHub is archived
   as of May 2026.
   ```bash
   git clone https://codeberg.org/genodelabs/genode.git
   cd genode
   git checkout main   # Genode master branch is named 'main' on Codeberg
   ```
2. Install the Genode toolchain. The fastest path on Arch/CachyOS is
   the `genode-toolchain-bin` AUR package (maintained by a Genode Labs
   employee). On other distros, download the official prebuilt tarball
   from <https://genode.org/download/tool-chain> and extract with
   `sudo tar xPf` so it lands in `/usr/local/genode/tool/<VERSION>/`.
3. Symlink Sponge OS into Genode's `repos/`:
   ```bash
   ln -s /path/to/sponge-os/repos/sponge /path/to/genode/repos/sponge
   ```
   The path to `sponge-os` MUST NOT contain spaces. Genode's build
   system resolves symlinks via `realpath` and then passes the result
   through shell `find`, which breaks on paths containing spaces.

   The Genode source tree MUST live in a stable, non-temp directory
   (e.g. `$HOME/genode`). `/tmp` and similar are periodically swept by
   `systemd-tmpfiles` and will silently destroy the multi-GB tree, the
   `contrib/` ports cache, and the incremental build cache.

> **Note for the early stage**: Sponge OS runs on both `base-linux`
> (fast developer feedback, no QEMU) and `base-sel4` (production
> target, QEMU). The kernel switch is the `KERNEL=` variable in
> `build/etc/build.conf`. Both paths are verified by
> `make run/sponge-minimal`.

### 1.2 Build Tools

- The **Genode toolchain** (GCC 14.2.0, binutils 2.44). Genode's build
  system enforces a hard `$(error)` if the compiler does not report
  exactly the expected version; system gcc/clang will not work.
- GNU Make.
- The **Mojo SDK** (Modular's language). All host-side tooling under
  `tool/` is written in Mojo. Install as a Python package with
  `uv add mojo --prerelease allow`, or see
  [`tool/README.md`](../tool/README.md) for tool-by-tool usage and
  <https://mojolang.org/install/> for the official guide.
- For development on Linux with `base-linux`, the extra packages it
  requires.

---

## 2. Repository Structure Overview

```
sponge-os/
├── README.md              # User-facing introduction
├── AGENTS.md              # Contribution guidelines (top of this document hierarchy)
├── docs/                  # Design documents
├── repos/
│   └── sponge/            # Genode repo (symlinkable into genode/repos/sponge)
│       ├── src/           # Component sources
│       ├── lib/           # Shared libraries
│       └── include/sponge/ # Shared headers
├── tool/                  # Build and development helper scripts (Mojo)
└── run/                   # Genode run scripts (scenario definitions)
```

For the purpose of each directory, see `AGENTS.md` §2.

---

## 3. Build

### 3.1 Integrated Genode Build (Recommended)

Create a Genode build directory, point it at base-linux + Sponge OS,
and run the minimal scenario:

```bash
cd /path/to/genode
./tool/create_builddir x86_64
cd build/x86_64

# Switch from the default pc/nova target to base-linux for fast feedback.
sed -i 's/^#KERNEL ?= nova/KERNEL ?= linux/' etc/build.conf
sed -i 's/^BOARD ?= pc/BOARD ?= linux/'    etc/build.conf

# Register Sponge OS as a repository.
echo 'REPOSITORIES += $(GENODE_DIR)/repos/sponge' >> etc/build.conf

# Build + run the minimal scenario on the Linux host (no QEMU needed).
make run/sponge-minimal
```

Expected output ends with:

```
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] === Sponge OS status ===
Run script execution successful.
```

### 3.2 Per-Component Verification (Development)

To check whether a specific component's structure is complete
(presence of `target.mk`, `main.cc`, etc.), use `tool/check-compile`:

```bash
./tool/check-compile src/vct
```

This script only checks file presence; it does not compile anything.
For real compilation feedback, run the Genode build directly:

```bash
cd /path/to/genode/build/x86_64
make vct                  # build just the vct binary
```

### 3.3 Build Cache

The Genode build system supports incremental builds. Components that
have not changed are not recompiled.

### 3.4 Kernel Selection

`build/etc/build.conf` exposes `KERNEL` and `BOARD`. The default for
Sponge OS development is `linux`/`linux`, which boots Genode's `core`
as a Linux ELF process — fastest iteration. Other values:

| `KERNEL` | Use |
|---|---|
| `linux`  | Developer feedback (no QEMU, no kernel build). |
| `sel4`   | Production target. Requires the seL4 kernel build (cmake + ninja). |
| `hw`     | Genode's own microkernel. Requires QEMU for x86_64. |
| `nova`   | NOVA hypervisor. |

### 3.5 Building for `base-sel4` (Production Target)

Switching to seL4 is a one-line `KERNEL`/`BOARD` change in
`build/etc/build.conf`, but it pulls in three ports and a handful of
host-side Python modules that the seL4 kernel build needs.

**One-time setup:**

1. Install the seL4 kernel build's Python dependencies. Use `uv`
   consistently for all Python package work in Sponge OS:
   ```bash
   uv pip install --python <venv-python> future jinja2 ply six lxml pyfdt jsonschema
   ```
2. Fetch the seL4 kernel, seL4 tools, and grub2 bootloader ports. The
   bootloader is required because `base-sel4` boots from a GRUB ISO:
   ```bash
   cd $HOME/genode
   ./tool/ports/prepare_port sel4 sel4_tools grub2
   ```
3. Switch `build/x86_64/etc/build.conf`:
   ```makefile
   KERNEL ?= sel4
   BOARD  ?= pc
   ```
   (In a headless server, also comment out `QEMU_OPT += -display sdl`
   so QEMU does not require an X display. The run scenario's own
   `-nographic` arg handles the rest.)

**Run:**
```bash
cd $HOME/genode/build/x86_64
make run/sponge-minimal
```

Expected output (QEMU):
```
Genode 26.05
699 MiB RAM and 523288 caps assigned to init
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] === Sponge OS status ===
Run script execution successful.
```

**seL4-specific RAM requirement.** `base-sel4` on QEMU needs **at least
1 GiB** of guest RAM. seL4 reserves a SKIM window (Meltdown mitigation)
and its own kernel structures; with 256 MiB the kernel fails at boot
with `seL4 failed assertion 'load_paddr' at boot_sys.c:120` because it
cannot find a contiguous physical region large enough for the ~30 MiB
boot module. The scenario file sets `-m 1G` accordingly.

---

## 4. Run Scripts (Scenarios)

Each file in `run/` defines one boot scenario. A scenario defines:

- Which kernel base to use.
- Which components to include.
- The configuration of each component (resources, routing).
- (For automated tests) the verification script.

Scenarios:

| Scenario | Purpose | Status |
|---|---|---|
| `run/sponge-minimal.run` | Minimum Sponge OS boot (init + vct) | ✅ base-linux, ✅ base-sel4 |
| `run/sponge-de-minimal.run` | Sponge DE single-window demo | planned |
| `run/sponge-vct-commands.run` | Automated test of vct's basic commands | planned |

### 4.1 Anatomy of a working run script

The minimum reliable pattern, learned from `hello_tutorial` and
applied to `sponge-minimal.run`:

```tcl
# 1. Build list MUST include lib/ld, or the dynamic linker is missing
#    from the boot image and every component fails to load with
#    "environment ROM session denied (label=ld.lib.so)".
build { core lib/ld init <your-component> }

create_boot_directory

# 2. install_config accepts either raw XML or the '+' shorthand form.
install_config {
config
+ parent-provides
  + service LOG
  + service PD
  + service CPU
  + service ROM
+ default-route
  + any-service
    + parent
    + any-child
+ default | caps: 200 | ram: 8M
+ start <your-component>
  + config
    + args
      + arg status
-
}

# 3. [build_artifacts] is a run-tool helper that automatically stages
#    every binary the build step produced, including init.xsd (needed
#    for config validation) and ld.lib.so. Do NOT list them manually.
build_boot_image [build_artifacts]

append qemu_args " -nographic -m 1G "

# 4. run_genode_until matches a regex in the boot log within N seconds.
run_genode_until {.*=== Sponge OS status ===.*} 30
```

For how to write run scripts in general, see the
[Genode run documentation](https://genode.org/documentation/developer-resources/run).

### 4.2 Running a scenario

```bash
cd /path/to/genode/build/x86_64
make run/sponge-minimal
```

The run tool builds the requested binaries, stages the boot directory
under `var/run/<scenario>/`, runs the kernel (`./core` for base-linux),
and watches the log for the success regex. On success it prints
`Run script execution successful.` and exits 0.

---

## 5. Testing

### 5.1 Automated Tests

The verification logic embedded in Genode's run scripts is the default
testing tool. A `run/*.run` file uses `run_script` and pattern matching
(`expect`) to verify the expected output.

### 5.2 Component Unit Tests

Logic that is unit-testable (for example, vct's argument parser) is
extracted into separate headers so it can be tested outside Genode. A
unit-test framework has not been chosen yet (see `docs/09-roadmap.md`).

### 5.3 Manual Verification

- Boot success.
- `vct --help` output verification.
- (Sponge DE) check that the window appears.

---

## 6. Coding Workflow

Summary of `AGENTS.md` §4:

1. **Before changing anything**: read the relevant `docs/` and identify
   which philosophy or constraint is touched.
2. **Make the change**: update code and (if needed) documentation
   together.
3. **Verify**: build, run scenarios, and unit tests pass.
4. **Commit**: one logical change equals one commit. Conventional
   commits format.
5. **PR**: include in the PR body (a) which philosophy is satisfied,
   (b) the number of user steps, and (c) the test results.

---

## 7. Coding Rules Summary

(Excerpt from `AGENTS.md` §3)

- **C++ standard**: follow the version Genode supports (C++17 recommended).
- **Naming**: classes in `PascalCase`, functions in `snake_case`,
  members in `snake_case_`.
- **Header guards**: `#pragma once`.
- **Smart pointers**: prefer `Genode::Constructible<T>` and
  `Genode::Allocator_avl`.
- **Exceptions**: none (Genode builds with exceptions disabled).
  Constructor failures are propagated through explicit initialization
  patterns.
- **Prohibited**: type bypass, empty catch, monolithic components,
  capability bypass.

---

## 8. Contribution Workflow

### 8.1 Filing Issues

- (Use a separate channel until the repository becomes public.)
- An issue should include: symptoms, reproduction steps, expected
  behavior, actual behavior, environment information.

### 8.2 Submitting a PR

- Prefer small PRs.
- PR body template:
  ```
  ## Summary of changes
  ...
  ## Philosophies satisfied
  - [ ] Convenience: ...
  - [ ] Control: ...
  - [ ] Automation: ...
  ## Number of user steps
  This task asks the user for N interactions.
  ## Tests
  ...
  ```

### 8.3 Review Criteria

- Does the code follow the hard rules in `AGENTS.md`?
- Does the documentation match the code?
- Are tests included?
- Did the number of user steps grow (a convenience regression)?

---

## 9. Documentation Rules

- All user-facing documentation is in English (see `AGENTS.md` §1.3).
- Code comments are in English (consistent with identifiers).
- New components are added to the inventory in
  `docs/04-components.md`.
- Design changes update the relevant `docs/` first
  (see `AGENTS.md` §4.1).

---

## 10. Debugging

- Log output through Genode's `LOG` session (`Genode::log`,
  `Genode::warning`, `Genode::error`).
- When a run script executes, the LOG output is shown on the console.
- (Planned) `vct --verbose` will print backend communication in
  detail.
- (Planned) Leitzentrale will show component state in real time.

For difficult debugging issues, first re-read the principles in
`AGENTS.md` §5 "AI Agent-Specific Guidelines" and
`docs/02-philosophy.md`.