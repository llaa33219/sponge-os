# sponge_console — the fault console / display client

A Qt-free, libc-free full-screen Gui client. It exists so that a
top-level fault (`docs/17-runtime-component-topology.md` §5) has a
user-visible surface that depends on **nothing** inside the subtree it
reports on: no Qt, no desktop environment, no configuration backend, no
session to any supervised component.

## Lifecycle

The component is started once and then follows the `console_state`
report relayed by the top-level `report_rom`:

- `armed="yes"` → open a Gui session, map a full-screen buffer, paint;
- `armed="no"` → close the Gui session again.

**Opening and closing the session**, rather than painting an idle
screen, is what makes "the console never coexists with the desktop"
structural: the supervisor arms it only *after* it has already
destroyed the desktop subtree, and disarms it only *before* it rebuilds
the desktop.

## Display ownership

This component does **not** open the framebuffer. The compositor keeps
the fb exactly as it is — one writer (`vesa_fb` / `boot_fb`) via its
Capture session — and this component is simply the only Gui client
while it runs. Seizing the device instead would mean two components
mapping the same physical memory, which is the permanent corruption
`docs/11-environment.md` row 12 measured on real hardware: `boot_fb`'s
Capture blit repaints only the compositor's dirty rectangles, so stale
console text is never erased. "Takeover" here means taking over the
**display**, which is the part the user actually sees.

## Second role: `role="desktop"`

The acceptance scenario needs a real Gui client *inside* the supervised
subtree, so that "the console took the display" is a pixel claim and not
a log line. `role="desktop"` is that stand-in. It paints once from its
static config and never subscribes to `console_state`. The two roles
are never instantiated at the same time.

## Sessions (AGENTS.md §1.2)

ROM `config`; ROM `console_state` (console role only); one Gui session,
only while armed. No Timer, no Report.

## Rendering

A 5x7 bitmap font in `font5x7.h` (`font5x7.h` is the only extra
dependency and 1 KB of table), a solid background, a brighter band
across the top, and three lines of text. `font5x7.h` is included from
`sponge_supervisor/text_scan.h`'s sibling so the two components share
the tolerant config scanner: a component whose job is to report faults
must never silently run on default configuration because the linker
delivered its config as HID instead of XML.
