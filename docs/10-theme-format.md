# 10 - Sponge DE Theme Format

> This document defines the canonical, INI-style text format for Sponge OS
> desktop environment themes. It is intentionally small and human-editable.

---

## 1. Format Overview

Sponge DE themes are plain text files that use an INI-style syntax:

```ini
[section]
key = value
```

INI was chosen because it is the smallest structured-text format that can
express the v1 theme surface (colors, fonts, spacing, and a small set of
enumerations). The format is parsed by `Sponge_DE::Theme::ThemeLoader`.

See the parser skeleton at
`repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}` and the default
theme at `repos/sponge/src/sponge-de/themes/default.theme`.

---

## 2. Grammar

A theme file is a sequence of UTF-8 lines. Each line is one of:

- A **blank line** (ignored).
- A **comment line** that starts with `#` (ignored).
- A **section header** in square brackets: `[section_name]`.
- A **key-value pair**: `key = value`.

Whitespace rules:

- Leading and trailing ASCII whitespace (` ` and `\t`) is trimmed from
  section names, keys, and values.
- Blank lines and lines containing only whitespace are ignored.
- Whitespace around `=` is optional but allowed.

Section header rules:

- A section header starts with `[` and ends with `]` on the same line.
- Section names are lower-case ASCII identifiers. They may contain
  underscores (`_`). They may not contain whitespace.
- A section header may contain surrounding whitespace, which is trimmed.
- Duplicate sections are allowed; they merge with the earlier section.
  The last occurrence of a key wins.

Key-value rules:

- A key is an ASCII identifier that may contain underscores. It must not
  contain whitespace, `=`, or `[`/`]`.
- A key and value are separated by the first `=` on the line. Values may
  contain `=` characters; only the first `=` is treated as the separator.
- Values are trimmed of leading and trailing whitespace before being
  interpreted according to their declared type.

Quoting rules for string values:

- A value may be wrapped in a single pair of double quotes (`"`).
- If the first non-whitespace character of a value is `"` and the last
  non-whitespace character is `"`, those quotes are stripped and the
  inner text is used as the value, including any embedded spaces.
- Quotes are not nestable and there is no escape syntax.

Example:

```ini
[fonts]
default_family = "Noto Sans"
```

---

## 3. Value Types

### 3.1 Color

A color is either a hexadecimal literal or one of the named colors.

Hexadecimal form:

- `#RRGGBB` — six hex digits, case-insensitive.
- Optional: `#RGB` shorthand is **not** supported in v1.

Internal representation: `0xRRGGBB` as a 32-bit unsigned integer.

Named colors (v1):

| Name    | Hex     |
|---------|---------|
| black   | #000000 |
| white   | #ffffff |
| red     | #ff0000 |
| green   | #00ff00 |
| blue    | #0000ff |
| yellow  | #ffff00 |
| cyan    | #00ffff |
| magenta | #ff00ff |
| gray    | #808080 |

Unknown named colors are a warning and leave the previous value unchanged.

### 3.2 Integer

An integer value is parsed as a base-10 non-negative ASCII number. It may
optionally be preceded by a single `+`. Negative values are **not**
supported in v1. The value is stored as an unsigned integer (`unsigned`).

### 3.3 String

A string value is any sequence of printable UTF-8 characters. It is
stored as a fixed-capacity `Genode::String<N>`. Quotes are stripped if
present, per §2.

### 3.4 Enum

An enum value is a lower-case ASCII identifier that matches one of the
allowed tokens for a given key. The only enum in v1 is `panel_position`.

| Key            | Allowed values          | Default |
|----------------|-------------------------|---------|
| `panel_position` | `top`, `bottom`, `left`, `right` | `top` |

Unknown enum values are a warning and leave the previous value unchanged.

---

## 4. Standard Sections and Keys

### 4.1 `[meta]` — Format Version

| Key       | Type | Default | Meaning |
|-----------|------|---------|---------|
| `version` | int  | `1`     | Theme format version. Must be `1`. |

The parser checks this value. If the major version is greater than the
parser understands, it logs a warning and falls back to the default theme.

### 4.2 `[colors]` — Color Palette

| Key         | Type  | Default (hex) | Meaning |
|-------------|-------|---------------|---------|
| `panel_bg`  | color | #1e1e2e       | Panel background |
| `panel_text`| color | #cdd6f4       | Text/icons on the panel |
| `accent`    | color | #89b4fa       | Active / highlighted elements |
| `separator` | color | #45475a       | Dividers and separators |
| `window_bg` | color | #313244       | Window background |
| `window_border` | color | #45475a   | Window border |
| `title_text`| color | #cdd6f4       | Window title text |
| `error`     | color | #f38ba8       | Error state / text |
| `success`   | color | #a6e3a1       | Success state / text |
| `warning`   | color | #f9e2af       | Warning state / text |

### 4.3 `[fonts]` — Typography

| Key            | Type   | Default | Meaning |
|----------------|--------|---------|---------|
| `default_family` | string | "DejaVu Sans" | UI font family |
| `default_size`   | int    | 11      | UI font size in points |
| `title_family`   | string | "DejaVu Sans" | Window title font family |
| `title_size`     | int    | 12      | Window title font size in points |

### 4.4 `[layout]` — Panel and Spacing

| Key              | Type  | Default | Meaning |
|------------------|-------|---------|---------|
| `panel_position` | enum  | top     | `top`, `bottom`, `left`, `right` |
| `panel_height`   | int   | 28      | Panel thickness in pixels |
| `padding`        | int   | 8       | Internal padding in pixels |
| `margin`         | int   | 4       | External spacing in pixels |
| `launcher_width` | int   | 48      | Launcher button width in pixels |
| `icon_size`      | int   | 24      | Panel icon size in pixels |

### 4.5 `[window]` — Window Decoration

| Key              | Type  | Default | Meaning |
|------------------|-------|---------|---------|
| `border_radius`  | int   | 6       | Corner radius in pixels |
| `border_width`   | int   | 1       | Border thickness in pixels |
| `shadow_radius`  | int   | 8       | Shadow blur radius in pixels |

---

## 5. Example Theme File

```ini
# Sponge OS default theme
# Mocha-inspired dark palette

[meta]
version = 1

[colors]
panel_bg       = #1e1e2e
panel_text     = #cdd6f4
accent         = #89b4fa
separator      = #45475a
window_bg      = #313244
window_border  = #45475a
title_text     = #cdd6f4
error          = #f38ba8
success        = #a6e3a1
warning        = #f9e2af

[fonts]
default_family = DejaVu Sans
default_size   = 11
title_family   = DejaVu Sans
title_size     = 12

[layout]
panel_position = top
panel_height   = 28
padding        = 8
margin         = 4
launcher_width = 48
icon_size      = 24

[window]
border_radius = 6
border_width  = 1
shadow_radius = 8
```

---

## 6. Versioning and Evolution

The format version is stored in the `[meta]` section. The rules are:

- Version `1` is the current version.
- A parser that understands version `1` accepts `version = 1`.
- If a theme file contains `version = 2` (or higher), the parser logs a
  warning and does not apply the file. Sponge DE falls back to the
  system default theme.
- New sections and keys may be added in future versions; unknown keys are
  warnings, not errors, so forward-compatibility is preserved for minor
  additions.
- Breaking changes (new section semantics, removed keys, changed value
  types) require a new major version.

---

## 7. Resolution Rules

Sponge DE resolves themes in two layers:

1. **System theme**: shipped with Sponge OS, typically
   `repos/sponge/src/sponge-de/themes/default.theme`.
2. **User theme**: stored in the user's configuration directory
   (`~/.config/sponge/theme/user.theme` or the Genode VFS equivalent).

Resolution order:

- The system theme is loaded first and provides default values for every
  key.
- If a user theme exists, it is parsed and applied **key-by-key**. Each
  key in the user theme overrides the system value; keys not present in
  the user theme keep their system values.
- If the user theme is malformed, the parser logs a warning, skips the
  malformed value, and continues. The remaining valid keys still apply.
- If the user theme version is unsupported, the entire user theme is
  ignored and the system theme is used.

This preserves user customization across system upgrades and avoids
silently overwriting user changes. See `docs/02-philosophy.md` §3.4.

---

## 8. Intentionally Out of Scope for v1

The following features are deferred to later phases. A parser that sees
keys related to these features should treat them as unknown keys and warn,
not fail:

- Animations and transitions.
- Gradients and opacity / alpha channels.
- SVG or bitmap icon themes (icon paths or icon sets).
- Per-monitor or per-component overrides.
- Conditional values based on contrast or high-DPI mode.
- Localization of theme text (themes are visual-only; translated UI
  strings come from the application layer).

---

## 9. Error Handling

The parser is **best-effort**:

- Unknown keys are logged with `Genode::warning("theme: unknown key ...")`
  and ignored.
- Malformed values are logged with `Genode::warning("theme: parse error at
  line N: ...")` and the corresponding key is skipped.
- The parser continues with the next line after a warning.
- The return value of `ThemeLoader::load()` reports whether the whole
  file was parsed without any warnings.

This matches the Sponge OS philosophy of automation-with-control: a theme
that is mostly correct still works, and the user is informed about the
parts that were not understood.

---

## 10. References

- `docs/05-sponge-de.md` §4 — Theme System design direction.
- `repos/sponge/src/sponge-de/theme/theme_loader.h` — C++ data model.
- `repos/sponge/src/sponge-de/theme/theme_loader.cc` — Parser implementation.
- `repos/sponge/src/sponge-de/themes/default.theme` — Default system theme.
