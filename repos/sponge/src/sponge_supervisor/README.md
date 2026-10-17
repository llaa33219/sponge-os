# sponge_supervisor — the top-level supervisor

Closes the containment gap recorded in
`docs/17-runtime-component-topology.md` §5 ("Broker fault containment -
top level").

## Why it exists

The top-level `init` cannot run a `<heartbeat>`: Genode's heartbeat
takes its Timer session from `env.parent()`
(`genode/repos/base/src/lib/timeout/timer_connection.cc:43`), and for
the top-level init the parent is **core**, which provides no Timer. The
session is denied and init stops. So the `system` sandbox — which holds
`sponge-de` and everything else the user sees — had no supervisor above
it. A wedged `system` child was unkillable and unobservable from
outside its own subtree.

## What it is

The pkgd pattern one level up. It:

- **owns the config of the `system` sandbox**, delivered through
  `report_rom`, and of the fault console's arming report;
- **reads the sandbox's own `state` report**, relayed by the
  *top-level* `report_rom` — never by a relay inside the supervised
  subtree, which is part of the failure domain;
- **has its own Timer**, so its idea of time does not depend on the
  subtree it supervises.

## The restart lever

`Sandbox::Start_model::matches()`
(`genode/repos/os/src/lib/sandbox/config_model.h:171`) compares only
the `name` and `version` attributes of a `<start>` node. A config
rewrite that leaves both untouched leaves every running child
untouched — the finding in `docs/evidence/config-regen-survival.md`. To
kill a wedged child the supervisor must therefore **change its
version**, which makes the node fail to match, which abandons the old
`Child` and creates a new one.

Every `<start>` node in the supervisor's templates carries
`version="@KILL@"`, substituted with a monotonically rising counter on
every rewrite. It is the only restart lever a config owner has, and it
is deliberately total.

## Phases

| Phase | What it does |
|---|---|
| `BOOT` | publish the nominal config and `armed="no"` once |
| `NOMINAL` | healthy; poll, write nothing |
| `CONTAIN` | fault seen → rewrite with a fresh `@KILL@` (full teardown + respawn), start a grace timer |
| `CONSOLE` | fault survived the grace window → publish the desktop-less variant, then arm the console |
| `RELEASE` | console told to disarm; wait for its Gui session to actually close before rebuilding the desktop |
| `NOMINAL` | after a bounded hold, republish the nominal config |

The `RELEASE` phase exists so that "the console never coexists with the
desktop" is a timing property, not a hope: the rebuild is published a
settle window *after* the disarming report.

## Escalation — when it acts, and when it does not

The supervised sandbox may have its own containment. The product media
runs `<heartbeat rate_ms="2000"/>` with `restart_after_skipped="3"` on
the critical children, so the sandbox itself abandons and respawns a
wedged child about eight seconds after it stopped answering — and a slow
first paint or a heavy repaint is indistinguishable from a wedge at that
scale.

So a fault does **not** act on the first tick. It starts an escalation
window of `grace_ms`, and the fault flag is re-read on every tick of
that window. The rule:

> act only when the supervised sandbox's own containment has **failed
> or been exhausted** — the fault is still there after `grace_ms`,
> which is longer than the sandbox's own restart threshold — or when
> the sandbox has published nothing at all within `boot_grace_ms`,
> which covers the sandbox init itself not coming up.

A fault the sandbox's own restart genuinely fixed therefore costs one
log line and nothing else. This is a *timing* rule: no detection
threshold is changed by it, and the acceptance scenario
(`run/sponge-supervisor-takeover.run`) passes unchanged with it.

`settle_ms` is a **topology** parameter, not a constant: it must exceed
the time a respawned child needs to climb back over `fault_skipped`, or
the supervisor declares the fault "contained" while the respawned child
has not reached the threshold yet — and the console phase becomes
structurally unreachable. Measured: 1.75 s at the acceptance
scenario's 500 ms heartbeat, 10–12 s on the product media's 2000 ms one.

## Clean exits

A boot probe is expected to exit: `alpha_probe` prints its PASS marker
and exits, `cfg_probe` likewise. Genode keeps an exited child in the
state report until it is abandoned, so an unconditional "any exit is a
fault" rule tears down a healthy desktop minutes after a successful
boot. The `ignore_exited` policy carries the names that are allowed to
exit; the same pair is excluded by sponge-de's health watcher
(`repos/sponge/src/sponge-de/main.cc`), so the panel and the supervisor
cannot disagree about what a clean exit is.

## Detection

Primary: `skipped_heartbeats` on a monitored child. The supervised
sandbox carries a bare `<heartbeat/>` on its critical children, which
turns monitoring **on** with `restart_after_skipped` defaulting to `0` —
local auto-restart deliberately **off**, so this supervisor is the
single authority that restarts them.

Secondary: a **rise** in the number of exited children above the
baseline recorded while healthy. Genode keeps an exited child in the
report with its `exited` attribute until it is abandoned, so a plain
"any exit" test would latch the supervisor permanently.

Rejected: state-report **staleness**. A Genode state report is
event-driven — a healthy sandbox publishes nothing — so report age
cannot distinguish "quiet" from "dead". This was measured, not assumed:
the control run of `run/sponge-supervisor-takeover.run`
(`SPONGE_TAKE_NO_FAULT=1`) produced exactly that false positive at
`state_age_ms=10250` on a healthy system, and the detector was
rewritten in response. The only staleness statement that survives is
the boot-time one.

## Sessions (AGENTS.md §1.2)

`Timer`, `Report` (via `report_rom`), ROM `config`, ROM `templates`,
ROM `supervisor -> system_state`. Nothing else, and nothing inside the
subtree it supervises.

## Configuration

Own policy (`config` ROM) — all optional:

| Attribute | Default | Meaning |
|---|---|---|
| `poll_ms` | 250 | Timer period; also the time base for every deadline |
| `fault_skipped` | 3 | `skipped_heartbeats` at which a child counts as wedged |
| `grace_ms` | 4000 | how long a killed subtree may take to come back before the console is armed |
| `settle_ms` | 3000 | how long it must stay healthy to count as recovered, and how long the console's release is awaited. **Must exceed the respawn-to-refault time** of the supervised sandbox, which is 1.75 s at a 500 ms heartbeat and 10-12 s at the product media's 2000 ms — hence `settle_ms: 20000` in `run/supervisor.inc` |
| `console_hold_ms` | 6000 | how long the console stays on the display |
| `boot_grace_ms` | 30000 | how long the sandbox may publish nothing at all before that counts as a fault |
| `ignore_exited` | *(empty)* | comma-separated child names whose `exited` is not a fault (product media: `alpha_probe,cfg_probe`) |

`templates` ROM — one document containing a `<system>` and a
`<system_console>` element, each wrapping the `<config>` element that is
forwarded verbatim.

The document is not well-formed XML (two roots) on purpose: the scanner
is a text scanner, not a parser. That scanner handles attributes and
self-closing tags, because a config produced by `tool/hid format
--output-xml` is full of `<config verbose="no">` nodes and a scanner
that recognises only the bare `<config>` returns the first nested
`</config>` — a 13662-byte template forwarded as 3325 bytes, which
parses, applies, and starts three children out of eleven. As a second
line of defence `_publish()` **refuses** any config that does not end
in `</config>`.
