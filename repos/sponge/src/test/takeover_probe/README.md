# takeover_probe — capture-side pixel oracle

Reads the composited display through a Capture session and reports
which of the configured "states" the screen is in. It exists so that
"the fault console took the display" is a **pixel claim with a log
marker attached**, not an inference from a log line: a supervisor that
claimed a takeover while the old desktop was still on screen would
still fail this probe.

## Sampling

The exact screen centre. `sponge_console` lays its banner out in the
upper third, so the centre row is flat background for **both** roles and
therefore means the same thing before and after a takeover. The two
corners are sampled for the log only — the top one lands in the
highlight band, and the bottom one is the first thing to go stale when
the compositor repaints only its dirty rectangles.

## Config

```
<config poll_ms="250">
  <state name="desktop" color="#1e3a6e"/>
  <state name="console" color="#601010"/>
</config>
```

The expected colours live with the scenario, not inside this component,
so a scenario can tighten or relax them without a rebuild.

State 0 is announced as `desktop-up`, state 1 as `console-up`; every
later transition is announced as `pixel state=<name>`.

## Sessions (AGENTS.md §1.2)

`Capture`, `Timer`, ROM `config`.
