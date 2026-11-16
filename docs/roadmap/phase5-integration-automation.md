> Part of [09 - Roadmap](../09-roadmap.md) §7.

## 7. Phase 5: Integration and Automation Layer

### Goal

Sponge DE grows into a full desktop environment, and the fact that vct
and Sponge DE share the same backends is verified.

### Completion Criteria

- [x] Sponge DE panel and launcher implemented (themed panel with
  clock + launcher menu populated from sponge_pkgd's `installed`
  broadcast; verified by `run/sponge-launcher.run`). Click-to-launch
  deferred — needs a start-on-demand lifecycle (documented).
- [x] Configuration management through the `sponge_configd` backend
  (flat key-value store, validation, broadcast config report;
  `vct config get/set/list`; `run/sponge-config*.run`).
- [x] Theme system through the `sponge_themed` backend (watches
  configd, resolves theme content, republishes; sponge-de live-reloads
  via the ThemeController; `run/sponge-theme.run`).
- [x] Sponge DE and vct read and write the same configuration
  (consistency verified: `vct config`/`theme apply` writes through
  configd → themed → sponge-de; the theme_probe asserts the 3-way
  value match plus the pixel repaint).
- [x] Theme file format defined and a default theme shipped.

### Progress Notes

- Theme format spec: `docs/10-theme-format.md` (completed in Phase 5
  skeleton work).
- Default theme: `repos/sponge/src/sponge-de/themes/default.theme`
  (completed).
- Parser skeleton: `repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}`
  (completed; not yet wired into the `sponge-de` build, integration
  pending with the Qt6 wiring task).
