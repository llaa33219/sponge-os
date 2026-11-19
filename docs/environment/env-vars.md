# 11 - Development Environment: Environment Variables & Build Knobs (§8)

> **Breadcrumb:** parent [11 - Development Environment & Reproducibility](../11-environment.md); this file holds its §8 (Environment Variables & Build Knobs), moved verbatim.
> Siblings in `docs/environment/`: [third-party-ports.md](third-party-ports.md) (§5-§6), [bootstrap.md](bootstrap.md) (§7), [base-sel4-gui.md](base-sel4-gui.md) (§10), [disk-image-p4.md](disk-image-p4.md) (§12-§13).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 8. Environment Variables & Build Knobs

The knobs that affect a Sponge OS build, grouped by who sets them.

| Knob | Set By | Default | Purpose |
|------|--------|---------|---------|
| `GENODE_DIR` | `create_builddir`-generated `etc/build.conf` | absolute path of the `genode/` vendored tree | Every Genode build rule eventually resolves to `$(GENODE_DIR)/...`. The tool wrappers under `repos/sponge/tool/` and the `import-qt6.inc` makefile both rely on it. |
| `QT_TOOLS_DIR` | `import-qt6.inc` (patch #3) | `$(abspath $(GENODE_DIR)/../var/qt6-host-tools)` | Tells the Qt6 cross-build where to find the host `qmake`, `moc`, `rcc`, etc. Override on the make command line or in `etc/build.conf` if you keep the host tools elsewhere. |
| `KERNEL` | `etc/build.conf` | `linux` (set by `./tool/build prepare`) | One of `linux`, `sel4`, `hw`, `nova`. `linux` skips the microkernel build entirely (no QEMU needed for the run). `sel4` triggers the cmake kernel build and QEMU. |
| `BOARD` | `etc/build.conf` | `linux` (set by `./tool/build prepare`) | Board flavor for the chosen kernel. For seL4 the meaningful value is `pc`; for the Linux dev target it is `linux`. |
| `REPOSITORIES` | `etc/build.conf` | `["base", "os", "demo", "ports", "libports", ...]` | Genode repo list, **in shadowing order**. The front-most repo wins when two repos declare the same path. `./tool/build prepare` appends `sponge/libports/gems` so Sponge OS components, the Qt6 libs, and the Leitzentrale source are all visible. |
| `MAKE += -j<N>` | `etc/build.conf` (added by `prepare`) | `-j$(nproc)` | Parallel build. |
| `QEMU_OPT` | `etc/build.conf` | platform-dependent | QEMU flag pass-through. Headless servers want `# QEMU_OPT += -display sdl`. |
| `QEMU_ARGS` | run scripts | per-scenario | Per-scenario QEMU args. The run scripts in `run/` append `-nographic -m 1G` for seL4 scenarios; `sponge-de.run` appends only `-nographic` because it needs the SDL framebuffer. |
| `CONTRIB_DIR` | Genode build system | `genode/contrib/` | Where `prepare_port` drops fetched sources. |
| `BUILD_DIR` | Genode build system | `genode/build/x86_64` | Build artifact output root. |
| `SPONGE_COMPOSITOR` | run scripts / caller | unset (native) | Selects the Tier-0 compositor. Unset or `native` selects `sponge_compositor` (the default); `upstream` selects the explicit nitpicker fallback; any other value fails loudly. The G1 differential scenario requires the explicit `native` override. |

### 8.1 Order of repo shadowing

`REPOSITORIES` is a **list, and order matters**. The Genode build
system picks the first match it finds for any `<lib>` or `<src>`
reference, so the Sponge OS repo must be listed after the upstream
repos whose files it overrides (currently nothing) and before any
repos whose files Sponge OS wants to override (currently also nothing).
The `./tool/build prepare` step appends `sponge/libports/gems` plus
`pc`/`dde_linux` to the list in the right order; do not reorder it
manually without a reason. `pc` and `dde_linux` are needed by the
base-sel4 interactive GUI scenario (`run/sponge-de-sel4-interactive.run`):
they contribute `pc_platform`, `pc_usb_host`, and `usb_hid`. They are
harmless on base-linux (their targets are only built on demand).
`dde_ipxe` (the iPXE-based NIC driver, `ipxe_nic`) is appended to
`REPOSITORIES` for the networking scenarios (`run/sponge-net-probe.run`,
todo 12). `ports` is appended for the `terminal` package (todo 13): it
contributes the `noux-pkg/bash-minimal` and `noux-pkg/vim-minimal`
build targets (which depend on the prepared `bash`/`vim`/`ncurses`
ports — `ncurses` itself lives in `libports`, already in the list).
The full managed block in `genode/build/x86_64/etc/build.conf`
is therefore: `sponge`, `libports`, `ports`, `gems`, `pc`, `dde_linux`,
`dde_ipxe`.
Like `pc`/`dde_linux`, `dde_ipxe` is harmless on base-linux (its
`REQUIRES = x86` target only builds on demand). `ports` is likewise
harmless when no noux target is built.

### 8.1.1 `base-$(KERNEL)` goes AFTER `repos/base` (Sponge prepare fix)

Upstream's `create_builddir` template prepends
`REPOSITORIES += $(GENODE_DIR)/repos/base-$(KERNEL)` before
`repos/base`. With that order, forwarding-only target directories —
`base-sel4/src/timer/hpet` contains only a `target.mk` that includes
`repos/base`'s `target.inc` — shadow `repos/base`'s buildable variant
of the same target, and the build fails at link time with
`cannot find component.o` (the target's own `component.cc` lives next
to the `.inc` in `repos/base`, not in the forwarding dir; `vpath`
covers only the forwarding dir). `./tool/build prepare` therefore
moves the kernel repository after the `base`/`os`/`demo` block, in
place and idempotently (marked with a `# Kernel-specific repository
(moved after repos/base:` comment). The kernel library imports are
unaffected: `lib/import/import-*.mk` exists only in the kernel repo,
so it wins regardless of order.

### 8.2 Why `make -j$(nproc)` is set by `prepare`

The Genode build system does not default to parallel builds; it
expects each developer to set their own `MAKE` flag. A cold rebuild of
the base tree is the difference between a coffee break and lunch, so
`prepare` sets `-j$(nproc)` (the host CPU count). Override per-run by
passing `MAKE=` on the command line.

---
