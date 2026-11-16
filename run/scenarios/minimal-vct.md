# Minimal boot, vct commands, and power

Breadcrumb: [`run/README.md`](../README.md) → Minimal boot, vct commands, and power

The Phase 1 minimum boot, the vct command-verification scenarios, and
the shutdown/reboot power paths. Full catalog entries follow verbatim
(cautions and limitations included); the parent index carries one-line
summaries.

## Scenarios
- `sponge-minimal.run` — Phase 1 goal: minimum boot (skeleton).
  Kernel-agnostic; verified on `base-linux` (no QEMU) and `base-sel4`
  (QEMU, production target).

- `sponge-vct-help.run` — Phase 2: `vct help` prints the command summary.
  Kernel-agnostic.

- `sponge-vct-version.run` — Phase 2: `vct version` prints the version.
  Kernel-agnostic.

- `sponge-vct-status.run` — Phase 2: `vct status` reads live init state
  through a sub-init + `report_rom` relay. Kernel-agnostic.

- `sponge-vct-component-list.run` — Phase 2: `vct component list` lists
  the running components from the live init state report. Kernel-agnostic.

- `sponge-power.run` — Phase 7 todo 17 (SUCCESS path): verifies
  `vct shutdown` actually powers the guest off through the real ACPI
  stack. There is NO `System` RPC session in Genode 26.05; the verified
  power path is the `system` ROM report consumed by the `acpica`
  component (`genode/repos/libports/src/app/acpica/os.cc`), the exact
  route upstream Sculpt uses. vct publishes `<system state="poweroff"/>`
  via a Report session; `report_rom` relays it as the `system` ROM;
  acpica calls `AcpiEnterSleepState(5)` (S5 → QEMU exits). The flat
  driver set (acpi/pci_decode/platform as top-level children, the
  `libports/run/acpica.run` pattern) provides the Platform device acpica
  needs. Verification (fail-loud): match the audit line
  `vct: shutdown: requesting poweroff`, then a bounded `expect`
  disambiguates — QEMU exit (`eof` = acpica acted = PASS) vs the
  unavailable line / hard timeout (FAIL). base-sel4 only. Reboot
  (`vct reboot`, state="reset") shares the same code path; change the
  vct config arg to verify, optionally with `-no-reboot`.

- `sponge-power-fail.run` — Phase 7 todo 17 (FAILURE channel): the
  companion to `sponge-power.run`. Same vct code path WITHOUT acpica.
  vct publishes the `system` report, runs its bounded 4s wait, observes
  the guest is still alive, and logs `System service (acpica)
  unavailable - guest did not power off within 4000ms` (audit line +
  QEMU-monitor escape-hatch hint, `--json` emits structured status)
  exit 1. The guest (init + report_rom + timer + vct) keeps running —
  the control-philosophy door (AGENTS.md §1.1): never a silent hang or
  a misleading success when the automation's backend is absent. Gates
  on the unavailable line. base-sel4 only.

- `sponge-vct-search.run` — Phase 7 todo 18: proves the REAL vct
  binary's `SearchCommand` works end-to-end (the probe mirrors vct's
  logic; this scenario runs vct itself). Stages hello's metadata +
  pkg_index.xml, boots vct with `search hello`, gates on the human
  output line `hello  1.0  <description>`. Kernel-agnostic.

- `sponge-vct-search-json.run` — Phase 7 todo 18: the `--json`
  companion to `sponge-vct-search.run`. Same topology, vct config arg
  `search hello --json`; gates on the structured JSON match line
  (`"command":"search","status":"success","matches":[{"name":"hello",...}]`).
