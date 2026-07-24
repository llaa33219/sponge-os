# sponge_themed — theme resolver daemon

The middle hop of the Sponge OS theme pipeline. A plain Genode component
(no Qt, no libc) that resolves the active theme *name* (owned by
`sponge_configd`) to theme *content* and republishes it for Sponge DE.

## Data flow (one-way)

```
vct --[config_request]--> sponge_configd --[config broadcast ROM]-->
sponge_themed --[theme report ROM]--> sponge-de
```

`sponge_themed` never interprets theme content — it is Qt-free. It only
resolves **which** theme file is active (from the `theme.active` config
key) and ships that file's bytes as a `theme` report, so all theme
interpretation stays concentrated in the renderer (sponge-de), which
already owns the Qt-free `ThemeLoader`.

## report_rom wiring

```
policy | label: sponge_themed -> config | report: sponge_configd -> config
policy | label: sponge-de      -> theme  | report: sponge_themed  -> theme
```

The `theme` report carries the theme name as a `name` attribute and the
raw theme-file bytes as the XML node's decoded text content:

```xml
<theme name="light">[meta] version = 1 ...</theme>
```

## Behavior

- Watches the broadcast `config` ROM via `Attached_rom_dataspace` + `sigh`.
- Reads `theme.active` (default `default`).
- Opens the `<name>.theme` ROM module (e.g. `default.theme`,
  `light.theme`) staged in the boot image.
- Republishes the resolved content via `Expanding_reporter` (`theme`).
- Regenerates **only** when the resolved theme name changes (configd
  already de-duplicates its broadcast; matching that keeps an unrelated
  `panel.position` set from re-triggering sponge-de's re-style).
- Unknown theme name (no staged ROM, or ROM open failure) is **never
  fatal**: the previously published theme is kept and a warning is
  logged, so a typo in `vct theme apply neon` cannot blank the desktop.

## Minimum privilege

The component requests only `ROM` and `Report` sessions — everything it
needs to read configd's broadcast + theme files and write the resolved
theme report, and nothing more (AGENTS.md §1.2). It is purely
signal-driven and needs no Timer session.
