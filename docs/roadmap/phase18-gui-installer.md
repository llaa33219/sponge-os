> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 18.

### Phase 18: GUI Installer for SSD/HDD Installation

> Completing this phase releases **0.3.0**.

#### Goal

Installing Sponge OS to internal storage is a guided graphical
experience: an everyday user can install the system without knowing
partition tables or Genode run scripts. **Completing this phase
releases 0.3.0-alpha.**

#### Completion Criteria

- [ ] GUI installer component shipped on the install media: disk
  selection, guided partitioning with safe defaults (automation),
  and an advanced/manual partitioning path (control).
- [ ] The installer writes the 4-partition layout from `docs/14` to
  the target disk and produces a bootable installation.
- [ ] End-to-end verification in QEMU: boot media → install to a
  second disk → reboot from the installed disk → desktop verified.
- [ ] Failure paths are loud and recoverable (no half-written disks
  presented as success); installation docs updated.
