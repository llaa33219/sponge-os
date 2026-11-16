> Part of [09 - Roadmap](../09-roadmap.md) §8.

## 8. Phase 6: Leitzentrale Integration

### Goal

`vct leitzentrale` opens Sculpt's Leitzentrale as a window. The
synchronization strategy between Leitzentrale changes and Sponge OS
configuration is implemented.

### Completion Criteria

- [x] Sculpt's Leitzentrale component is included in the build
  (`run/sponge-leitzentrale.run`; the subsystem boots headlessly).
- [x] `vct leitzentrale` starts Leitzentrale (enable/off/status via the
  configd `leitzentrale.enabled` key + lz_bridge; audit log on start).
- [x] Sponge DE (or a dedicated viewer) shows Leitzentrale as a window
  (the `lz_viewer` component captures the subsystem's composited UI and
  presents it on the outer nitpicker; pixel-verified by lz_viz_probe).
- [x] Detection of Leitzentrale changes and synchronization with
  `sponge_configd` (lz_watch checksums the model fs; configd mirrors a
  read-only `leitzentrale.diverged` key in its broadcast).
- [x] Conflict-resolution UI (keep / revert / merge): `vct leitzentrale
  diff/keep/revert` implemented and verified by the edit→detect→revert
  probe cycle. Merge is documented as deferred — no Sponge automation
  writes the lz model in Phase 6, so a merge operation would be
  vacuous; the trigger for implementing it is recorded in
  docs/07-leitzentrale.md's open questions.
