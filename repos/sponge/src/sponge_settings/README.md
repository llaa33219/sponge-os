# sponge_settings — on-demand Settings control center (Criterion 18)

`sponge_settings` is a separate on-demand shell that **lists** the
provider-owned Settings entries of installed packages and **opens**
them through `sponge_pkgd`. It carries no settings UI of its own: the
window that changes a setting belongs to the provider that owns the
key or store (`docs/05-sponge-de.md` §5.4, `docs/12-package-format.md`
§14).

Plain Genode component (no Qt, no OpenGL, no libc): native
`Gui::Session` + framebuffer with `nitpicker_gfx` painters
(`Surface`/`Box_painter`/`Text_painter`) and `Tff_font`, the
`sponge_decorator` precedent.

## Capability surface (minimal by contract)

| Session | Label | Purpose |
|---|---|---|
| ROM | `installed` | installed metadata broadcast from `sponge_pkgd` (§14.4 discovery) |
| Report | `settings_request` | the shell's own single-writer settings launch request |
| ROM | `settings_result` | the matching result channel |
| Gui | (session label `sponge_settings`) | the listing window |
| Timer | — | the bounded one-shot result wait only |

There is **no** configd write channel, **no** package `File_system`
session, **no** provider code loading, and no GL requirement. The
report/result labels open only when the component config carries
`<settings_launch source="pkgd"/>`: without that opt-in (or when the
channels cannot be opened) the listing still renders and every
activation answers a bounded fail-fast
`package-manager-unavailable` (§14.7), so a topology that routes
nothing never attempts a session base would answer with a fatal
denial. Closing the window (the v1.1
app-visible close channel, `docs/05` §4.6) exits the component and
reclaims every session it held.

## Discovery

`sponge_pkgd` publishes each installed package's `<settings>` entries
in its `installed` broadcast (`docs/12` §14.2 fields per entry). The
shell re-parses that ROM on its update signal — no polling — and
groups entries by the closed category enum (`desktop`, `hardware`,
`system`, `applications`, `other`; absent/unknown falls back to
`other`), rendering title, icon-if-present, and keywords per entry.
Entries are addressed by the stable identity `(package, settings-id)`.

Adapter seam: the parser consumes

```
<installed ...><packages><package name="...">
  <settings id="..." title="..." category="..." icon="..." target="...">
    <keyword>...</keyword>
  </settings>
</package></packages></installed>
```

and only `Model::parse` (`model.cc`) changes if the published node
path differs.

**Icons.** `icon` names a staged payload image, but the §14 contract
deliberately gives callers no payload access ("callers hold nothing",
§14.5) and specifies no icon-pixel delivery channel. The shell
therefore renders a distinct icon-present marker versus the default
glyph and keeps the declared name on the model; the single pixel seam
is `draw_icon_marker` (`view.cc`).

## Activation (identity only)

Clicking an entry sends exactly:

```
<request op="launch" target="settings" pkg="..." id="..."/>
```

on `settings_request` (no command string, no `target` binary, no
`seq` — the sponge-de launcher precedent). The `id` attribute is the
settings-id — the primary identity vocabulary; the docs-prose
`settings-id` alias is equally accepted on results. The shell then
waits boundedly (6 s, one-shot Timer, no polling loop) for one
`<result>` on `settings_result`:

| Result | Surface (bounded English) |
|---|---|
| `ok` `action="started"` | Provider opened |
| `ok` `action="focused"` | Provider focused (already open) |
| `not-installed` | Not installed (package or entry was removed) |
| `binary-missing` | Provider binary missing from the image |
| `insufficient-resources` | Not enough resources to start the provider |
| channel unavailable / wait elapsed | Package manager unavailable |

Singleton and focus semantics belong to `sponge_pkgd` (§14.7: a
second open answers `ok action="focused"`, never a second window).
The shell is stateless with respect to providers — it holds no window
handles and accumulates no start state — so a second open of the shell
cannot duplicate provider windows and closing it reclaims everything.

The wire shapes live in exactly two adapter seams (`model.cc`):
`gen_settings_request` and `parse_settings_result`. If the pkgd
side of the settings lifecycle ever moves, any attribute-name
divergence is a one-file change there and nowhere else.

## Files

- `main.cc` — session wiring, event loop (ROM signal, input, Gui info
  close/resize, result, timeout), lifecycle.
- `model.h` / `model.cc` — pure discovery model, the two wire-shape
  seams, and the pure row layout/hit-testing. No sessions, no Env.
- `view.h` / `view.cc` — pure surface painting and the single
  compile-time palette block (see the theme note in `view.h`).
- `data/font.tff` — TFF font asset, byte-identical to upstream
  `genode/repos/gems/src/app/themed_decorator/theme/font.tff` (same
  asset and provenance as `sponge_decorator`'s `data/font.tff`,
  trimmed-TTF format read by the public `Tff_font`).
- `tests/` — host-side structural + model self-test (below).

## Host checks

`tests/check.sh` (from the repository root):

1. capability-surface structural check — asserts the sources open no
   forbidden session (File_system, configd request labels, ROM probes
   of payload binaries) and use exactly the contract labels;
2. builds and runs `tests/self_test.cc` on the host (g++ only, never
   the Genode build tree; same pattern as
   `sponge_decorator/tests/self_test.cc`) against the shipped
   `model.cc` — discovery parsing, category fallback, duplicate/
   overflow drops, both wire shapes byte- and parse-level, row
   grouping order, scroll clamp, and hit-testing.

## Integration dependencies (not owned here)

A run topology must route, per the table above: the pkgd `installed`
report through `report_rom` to this component's `installed` ROM; this
component's `settings_request` report and `settings_result` ROM
through `report_rom` to `sponge_pkgd`'s settings launch channel; and
Gui/Timer as usual. pkgd must publish the §14.2 `<settings>` entries
in the installed broadcast and answer the request shape of §14.5
(`ok` + `action="started"|"focused"`, `not-installed`,
`binary-missing`, `insufficient-resources`).
