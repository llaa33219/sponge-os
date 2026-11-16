# Alpha product, media selection, and compatibility contract

Breadcrumb: [`run/README.md`](../README.md) → Alpha product, media selection, and compatibility contract

The unified Alpha desktop scenario, the minimal seL4 media smoke, the
`tool/dist` storage selector, and the hardware compatibility contract.
Full catalog entries follow verbatim (cautions and limitations
included); the parent index carries one-line summaries.

## Scenarios
- `sponge-media-smoke.run` — Phase 7 todo 3: minimal seL4 media smoke
  test. Same payload as `sponge-minimal.run` (core, lib/ld, init, vct)
  but the boot image is materialised as a real El Torito ISO or GPT disk
  image and QEMU boots from that media file (not a direct kernel boot).
  Run with `RUN_OPT="--include image/iso"` or `RUN_OPT="--include
  image/disk"`; gates on vct's banner. Proves the grub2→bender→seL4
  boot chain from removable media before the full desktop exists.
  base-sel4 only.

- `sponge-alpha.run` — Phase 7 todo 4: the unified Alpha desktop
  scenario on base-sel4. Merges five proven component sets into ONE
  boot: the interactive-PC driver stack (vesa_fb/ps2/usb_hid/xHCI), the
  upstream `wm`+`window_layouter`+`decorator` window-management stack,
  the live `sponge_configd`→`sponge_themed`→sponge-de theme pipeline,
  the `sponge_pkgd`+`pkg_runtime` launcher feed (with pre-staged
  `hello`), and the Leitzentrale subsystem (`lz_runtime`/`lz_bridge`/
  `lz_watch`/`lz_viewer`). QEMU RAM bumped to 2G. The composite
  `alpha_probe` asserts all four Alpha criteria in bounded iterations
  (themed panel pixel, launcher report carries `hello`, configd
  broadcast ROM live, lz_viewer Leitzentrale window pixel) and logs
  `alpha-probe: PASS`; the run gates on that marker (fail-loud on
  timeout). base-sel4 only.

- `tool/dist --storage {ahci,nvme}` — **Phase 12 — product-media
  selector**. `tool/dist` accepts a `--storage {ahci,nvme}` option
  (added in W2). The default is `ahci` (preserves current behavior
  and artifact naming); `nvme` selects a one-namespace NVMe product
  path (the `run/sponge-desktop-disk-nvme.run` topology). Invalid
  values are rejected before a build starts with a concise English
  error and usage line. The ISO / live media path is unchanged. See
  `docs/evidence/task-2-phase12-storage.md` for the receipts.

- `docs/15-hardware-compatibility.md` — **Phase 12 — compatibility
  contract**. The hand-curated 5×5 surface matrix + 16-cell tuple
  ledger (4 verified, 1 smoke-only, 11 gap) plus the six Phase-12
  known gaps. Validated by the read-only `tool/hw_compat.mojo
  assert` (no `generate` / `update` / repo write); reachable from
  `./tool/build verify`.

