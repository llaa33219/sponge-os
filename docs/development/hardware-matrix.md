> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §16. Hardware Matrix (headless pre-real-hw gate)

## 16. Hardware Matrix (headless pre-real-hw gate)

Before touching real hardware, the full boot + input-interaction
chain is verified across QEMU emulated-hardware variation:

```bash
./tool/hwtest                  # full default matrix (20 variants)
./tool/hwtest --list           # list variant names
./tool/hwtest --only cpu-max,input-kbd   # iterate on a subset
```

Every variant boots `run/sponge-hw-matrix.run` — the proven
interactive driver stack (vesa_fb / ps2 / pc_usb_host / usb_hid /
event_filter + the two usb_hid probes) — with one hardware axis
varied, then drives QMP input interaction (hotplug audit chain for
USB, PS/2 motion+click with a stability window for the i8042 path)
and gates on a fail-loud `sponge-hw-matrix: PASS (variant=...)`
marker. The matrix covers CPU models (SandyBridge..max, incl. the
5-level-paging `+la57` feature bit), SMP (1/2/4/8 plus a
2-socket×2-core×2-thread topology variant), memory (2G/4G),
machine (q35/pc), USB controller (xhci/ehci/uhci), input devices
(usb-tablet/usb-mouse/usb-kbd/ps2 plus a dual-HID residency
variant), VT-d interrupt remapping (`-device intel-iommu,
intremap=on` — which requires `kernel-irqchip=split` under KVM;
QEMU refuses intremap with the in-kernel irqchip, so `iommu=on`
selects it automatically), and a combined shakedown.

Manual equivalent (control escape hatch): every variant is a plain

```bash
SPONGE_HW_VARIANT=cpu-max SPONGE_HW_CPU=max ... \
    make -C genode/build/x86_64 run/sponge-hw-matrix KERNEL=sel4 BOARD=pc
```

invocation with the documented knobs (`SPONGE_HW_VARIANT/CPU/SMP/
MEM/MACHINE/VGA/USB/INPUT/IOMMU`), plus two escape knobs:
`SPONGE_HW_EXTRA` (raw extra QEMU args, appended verbatim — the
arbitrary-axis hatch) and `SPONGE_HW_TSCALE` (gate-timeout scale
factor, for slow accelerators). The driver persists every run's
full output to `var/hwtest/<variant>.log` and enforces a
`must_match` config-sanity guard on variants that need one (a
PASS under the wrong accelerator/config is not a PASS).

Manual TCG example (a documented boundary — see below):

```bash
SPONGE_HW_VARIANT=accel-tcg SPONGE_HW_TSCALE=3 \
    make -C genode/build/x86_64 run/sponge-hw-matrix \
    KERNEL=sel4 BOARD=pc QEMU_OPT="-accel tcg"
```

Known expected-unsupported results (documented, not regressions):

- **XSAVE-less CPU models** (qemu64, Nehalem): seL4 16.0.0 halts
  with `XSAVE not supported`. SandyBridge is the oldest viable
  axis point; older models are out of the supported envelope.
- **-vga cirrus**: vesa_fb fails to find a VBE mode on the
  emulated Cirrus card in this QEMU build. `-vga std` is the only
  supported display axis in this driver stack.
- **TCG (`QEMU_OPT="-accel tcg"`)**: deliberately NOT in the
  default matrix. ~50% of TCG boots die in a guest-side boot
  race — nitpicker's early Timer-session request is denied while
  the HPET timer component is still in bring-up ("stop because
  parent denied Timer-session"), and the boot then starves at
  the phase-0 gate. The PASS-vs-FAIL log divergence is exactly
  at `[init -> timer] Using timer 2 -> using GSI 2 edge
  triggered`. KVM: 20+/20 stable. Manually runnable via the
  invocation above; expect flakiness, not determinism.
- **`-no-hpet` (via `SPONGE_HW_EXTRA`)**: the timer binary is
  HPET-only (`timer/hpet/hpet_timer`) with no PIT fallback —
  without the HPET there is no Timer service and the boot gates
  starve. The HPET is a hard requirement of this stack.
