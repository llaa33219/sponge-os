# sponge_decorator_bridge — themed_decorator live-policy color bridge

Phase 11 W4. A small, signal-driven Genode component that bridges the
`sponge_themed` theme ROM to `themed_decorator`'s `<config>` ROM. The
**boot-time tar identity** is built by `tool/decor_assets` and stays
staged as a ROM module via the decorator's VFS; the bridge's only job
is to **live-update the policy color** as the active theme changes.

## Pipeline

```
vct --[config_request]--> sponge_configd --[config]--> sponge_themed
                                                          |
                                                          v
                                                [theme] report_rom
                                                          |
                                                          v
                                          sponge_decorator_bridge (this)
                                                          |
                                                          v
                                          [decorator_config] report_rom
                                                          |
                                                          v
                                                  themed_decorator
```

The bridge reads the same `theme` ROM sponge-de's `ThemeController`
reads, parses the `[colors] panel_bg` key, and emits a
`<decorator_config>` report carrying a `<policy ... color=".."/>`
block. The cosmetic texture (the png 9-slice frame, the closer/maximizer
glyphs, the font) is unchanged at the decorator level — the color is a
**tint** over the cached texture (see
`genode/repos/gems/src/app/themed_decorator/window.h:285-288`).

## Channel ownership

| Channel | Direction | Writer | Reader |
|---|---|---|---|
| `sponge_themed -> theme` | Report | `sponge_themed` (single writer) | `sponge-de`, `sponge_decorator_bridge` (read-only) |
| `sponge_decorator_bridge -> decorator_config` | Report | `sponge_decorator_bridge` (single writer) | `themed_decorator` (read-only) |

Both `sponge-de` and `sponge_decorator_bridge` consume the `theme`
report; multi-reader is fine because `report_rom` is single-writer per
label. The `decorator_config` channel has exactly one writer
(`sponge_decorator_bridge`'s `Expanding_reporter("decorator_config")`).

## Minimum privilege

The bridge requests **exactly two** sessions:

1. **ROM session `"theme"`** — reads the active theme's raw INI.
2. **Report session `"decorator_config"`** — emits the generated
   decorator config.

No Timer, no libc, no File_system, no NIC, no Platform. Construction
opens both sessions unconditionally; the component is purely
signal-driven (AGENTS.md §1.2, §5.5 risk-register row 11).

## The `config` ROM label reservation (sandbox/child.cc:510-524)

`themed_decorator` reads its live config via the `"config"` ROM label,
which Genode init reserves for the child's inline `<config>` block
(`genode/repos/os/src/lib/sandbox/child.cc:510-524`). To give the
decorator a LIVE config, the run script:

1. Declares **no inline `<config>`** for the decorator child.
2. Routes the decorator's `config` ROM request to `report_rom`.
3. Adds a `report_rom` policy mapping the decoration's config label
   to the bridge's report:

   ```xml
   <policy label="themed_decorator -> config"
           report="sponge_decorator_bridge -> decorator_config"/>
   ```

This is the exact label syntax used in `run/sponge-wm-qmp.run`'s
existing policies (e.g. `label: decorator -> pointer`).

## The bridge is the SOLE source of the decorator's config

Because of the inline-config reservation above, the run script
**cannot** fall back to an inline `<vfs>` or `<libc>` on the
decorator child — any inline `<config>` block shadows the bridge's
report. The bridge therefore emits the **complete** decorator config
in its `decorator_config` report:

```xml
<config>
  <libc stdout="/dev/log" stderr="/dev/log"/>
  <vfs>
    <dir name="dev"><log/></dir>
    <tar name="decor.tar"/>
  </vfs>
  <policy label_prefix="" decoration="yes" motion="1" color="…"/>
  <default-policy decoration="yes" motion="1" color="…"/>
</config>
```

The `<vfs>` mounts the boot-time `decor.tar` (built by `tool/decor_assets`)
and the `<libc>` pipes stdout/stderr to `/dev/log`. The `<policy>` and
`<default-policy>` carry the theme-derived color. `motion="1"` is the
unsigned-attribute literal required by `Config::motion()` (see below).

## `motion="1"` (unsigned attribute — risk-register row 18)

The bridge emits `motion="1"` (NOT `motion="yes"`). The decorator's
`Config::motion()` parses `motion` as `unsigned`
(`genode/repos/gems/src/app/themed_decorator/config.h:63-66`):

```cpp
unsigned motion(Window_title const &title) const {
    return _policy_attribute(title, "motion", 0U); }
```

The `unsigned` parse rejects `"yes"` as a malformed integer and falls
back to the default `0`, which disables window-move animation. The
layouter's deferred-DRAG protocol relies on the window animating toward
the target for `Input::Seq_number` to settle, so `motion=1` is
required for the drag gate.

## De-duplication

The bridge tracks the last color it published and skips re-emits when
the parsed color is unchanged:

```cpp
if (color == _last_color) return;
_last_color = color;
_publish_config(color.string());
```

The XML reporter would dedup byte-identical payloads anyway (report_rom
only updates the module if the bytes change), but the explicit check
keeps the log noise low and the bookkeeping trivial.

## Fault tolerance

The bridge publishes the COMPLETE decorator config (libc + vfs +
policy) on every theme change AND on initial construction. The
decorator can therefore boot even when:

- `theme` ROM unreadable — the bridge falls back to the default
  `#1e1e2e` and emits the full config; the decorator boots with a
  sane untinted title bar.
- Theme ROM has no `[colors] panel_bg` entry — the bridge keeps the
  last published color; if no color was ever published, emits the
  `#1e1e2e` fallback with the full config.
- `sponge_themed` is removed from the topology — the bridge's `theme`
  ROM is unresolved and the bridge falls back to the fallback config.

The decorator's texture frame (which is the dominant visual) is staged
by the boot-time `decor.tar` mounted in the bridge's `<vfs>` block. The
bridge only injects the live tint color via `<policy>`.

## Activation / deactivation

The bridge has no activation gate — it is always-on when the
component is started. Scenarios that don't wire the
`sponge_themed -> theme` policy simply don't start the bridge.

## Files

| File | Purpose |
|---|---|
| `main.cc` | The component (~250 lines, plain Genode, no libc). |
| `target.mk` | `TARGET := sponge_decorator_bridge`, `LIBS := base`. |
| `README.md` | This file. |

## Verification

The acceptance scenario is `run/sponge-de-themed-chrome.run` (base-sel4,
QMP). The bridge's role in the gate sequence:

1. `fb using 1024x768` — vesa_fb bound the framebuffer.
2. `Connected device ... POINTER` — usb_hid bound the absolute pointer.
3. `pkg_gui_demo window shown` — `wm_probe` installs + launches.
4. `decorator_margins: top=20 bottom=8 left=1 right=1` — the
   themed_decorator publishes its margins; the script asserts the
   stock-decorator defaults.
5. `QMP-TARGET drag` — `wm_probe` emits the marker; the run script
   dispatches the PS/2 Mouse drag.
6. `wm-probe: PASS` — the window moved.
7. **Title-bar pixel assertion** — the run script's Capture check
   samples the title-bar region and asserts it matches the bridge's
   `panel_bg` color, NOT the stock-decorator look. (The default
   `panel_bg` is `#1e1e2e`; the stock motif decorator's untinted
   title bar is a different shade, so the pixel delta is measurable.)

## Why `panel_bg` and not `accent` or `window_bg`

`themed_decorator` tints the entire texture frame; the
`_config.base_color()` is the **base color** of the window. The
bridge picks `panel_bg` because:

- `panel_bg` matches the desktop's panel surface — the title bar
  visually flows from the panel.
- `accent` is the second-highlight color used by the Sponge DE's
  toggle hover ring and the launcher separator. Tinting the title
  bar with `accent` would re-color the chrome on every theme switch
  with an unintended hue.
- `window_bg` is the inner content background (the canvas between
  the title bar and the bottom edge). Picking `window_bg` over
  `panel_bg` would make the title bar a different shade from the
  panel, which looks visually inconsistent.

The hand-edit escape hatch lives in `tool/decor_assets_data/metadata.txt`
(geometry) and the four shipped theme files in
`repos/sponge/src/sponge-de/themes/` (palette). The bridge picks
`panel_bg` because that is the documented intentional target.
