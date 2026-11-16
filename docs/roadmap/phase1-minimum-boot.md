> Part of [09 - Roadmap](../09-roadmap.md) §3.

## 3. Phase 1: Minimum Boot

### Goal

Wire Sponge OS into a Genode source tree, produce a boot image that
contains Sponge OS components, and boot it on base-linux (the developer
feedback target). Production images target base-sel4.

### Completion Criteria

- [x] The symlink procedure with a Genode source tree is documented
  (`docs/08-development.md` §1.1).
- [x] A `run/sponge-minimal.run` scenario exists and runs successfully
  on base-linux.
- [x] A boot image is produced with vct as a child of `init`.
- [x] The boot completes and the LOG output is verified:
  ```
  Genode 26.05
  [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
  [init -> vct] vct 0.0.1-pre-alpha
  [init -> vct] Sponge OS codename: Archaeocyte
  Run script execution successful.
  ```
- [x] The `base-sel4` (seL4 microkernel) build is also green. Verified
  output on QEMU:
  ```
  Genode 26.05
  699 MiB RAM and 523288 caps assigned to init
  [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
  [init -> vct] vct — Very Convenient Tool
  [init -> vct] version: 0.0.1-pre-alpha (Archaeocyte)
  Run script execution successful.
  ```

### Verification Scenario

```
$ ./tool/build run sponge-minimal
# Manual equivalent:
$ make -C genode/build/x86_64 run/sponge-minimal
...
genode build completed
spawn ./core
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] vct 0.0.1-pre-alpha
[init -> vct] Sponge OS codename: Archaeocyte

Run script execution successful.
```

### Lessons captured during Phase 1

These were discovered the hard way; see `docs/08-development.md` and
[`docs/11-environment.md`](../11-environment.md) for full details.

- The Sponge OS source path MUST NOT contain spaces — Genode's
  `realpath` + `find` chain breaks otherwise.
- Always `build { core lib/ld init <component> }`. Omitting `lib/ld`
  produces a silent boot failure: every component is denied the
  dynamic-linker ROM session.
- Always pass `[build_artifacts]` to `build_boot_image` so that
  `init.xsd`, `ld.lib.so`, and the component binaries are all staged.
- Real Genode does not expose `size_t` / `addr_t` in the global
  namespace. Always qualify as `Genode::size_t`, `Genode::addr_t`.
- The Genode source tree is **vendored** at `genode/`. There is no
  `$HOME/genode` to keep alive anymore: a clean clone of `sponge-os`
  brings the whole tree, and `genode/build/`, `genode/contrib/`, and
  `genode/depot/` are all git-ignored. See
  [`docs/11-environment.md`](../11-environment.md) for the
  reproducibility contract and the patch ledger.
- `base-sel4` requires `prepare_port` for `sel4 sel4_tools grub2` plus
  seven Python modules (`future jinja2 ply six lxml pyfdt jsonschema`)
  on the host before the kernel build will succeed.
- `base-sel4` on QEMU needs **at least 1 GiB** of guest RAM. 256 MiB
  fails with `seL4 failed assertion 'load_paddr'` because the seL4
  kernel reserves a SKIM window (Meltdown mitigation) and its own
  structures, leaving too little contiguous physical memory for the
  ~30 MiB boot module.
