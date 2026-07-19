# Sponge DE Theme Loader

This directory contains the Phase 5 skeleton for the Sponge DE theme system.

## Contents

- `theme_loader.h` — Data model (`Color`, `Font`, `Theme`) and the
  `ThemeLoader` parser class.
- `theme_loader.cc` — Best-effort INI-style parser. It depends only on
  Genode base headers and avoids `std::string`, exceptions, and heap
  allocation.
- `../themes/default.theme` — The default system theme shipped with Sponge DE.

## Status

This is a parser + data model skeleton. It does **not** yet apply styles to
Qt widgets or connect to the `sponge_themed` backend. Those integration
points are marked with `// TODO(theme): ...` comments.

The loader is intentionally **not wired into `sponge-de`'s `target.mk`**.
Another task is integrating the Qt6 build of `sponge-de`; this directory
stays standalone until that work lands, at which point `theme_loader.cc` will
be added to `SRC_CC`.

## Format Specification

See [`docs/10-theme-format.md`](../../../docs/10-theme-format.md).
