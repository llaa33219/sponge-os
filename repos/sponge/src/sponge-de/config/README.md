# sponge-de/config — ConfigController (Phase 11 W2)

The in-sponge-de bridge from sponge_configd's `config` broadcast Report
to Qt signals. It is the SECOND in-sponge-de configd-broadcast consumer
— the first is `ThemeController`, which watches the `theme` ROM (a
*different* report_rom slot, sourced from `sponge_themed`).
`sponge_de_main.cc`'s `Main` does not directly consume the `config`
broadcast today; the bridge exists so that a configd `set` for one of
the four panel / launcher / clock keys reaches the panel + launcher
without a restart.

## Channel ownership

```
sponge_configd --[Report "config"]--> report_rom --[ROM "configd"]--> ConfigController
```

The watched ROM session is labeled **`"configd"`** — not `"config"`.
The `"config"` label is reserved by Genode init for the child's inline
`<config>` block (delivered as an `Inline_config_rom_service` at
`genode/repos/os/src/lib/sandbox/child.cc:510-524`); routing the configd
broadcast to the `"config"` label would shadow the inline config that
carries the activation gate. Using a distinct `"configd"` label
sidesteps the collision while preserving the rest of the standard
report_rom relay plumbing.

The corresponding `report_rom` policies (full labels include the
child-name prefix):

```
policy | label: sponge-de -> configd          | report: sponge_configd -> config
```

`sponge_themed` is also a reader of `sponge_configd -> config` (the
`configd` writer); `report_rom` is a single-writer slot per label but
multi-reader is fine. The only constraint is that there must be
exactly ONE writer (which is `sponge_configd`'s
`Expanding_reporter("config")`).

The sponge_de_probe (`repos/sponge/src/test/sponge_de_probe/`) drives
sponge_configd's `config_request`/`config_result` channel from the
*other* side of the system (the same channel vct uses); see
`run/sponge-panel-config.run` for the wiring.

## Minimum privilege

The component requests one new ROM session for `"configd"`. No Timer
session (the controller's `QTimer` is a Qt timer, satisfied by Qt's
own event loop). No Report session (the controller does not write
back). No File_system, no NIC, no Platform — nothing else
(AGENTS.md §1.2). The session is opened conditionally inside the
controller's constructor; in fallback mode (no `<config
source="configd"/>` gate) NO session is opened at all.

## Activation gate

The component config mirrors the existing `<theme source="themed"/>`
gate:

```xml
<config>
  <config source="configd"/>
  <theme source="themed"/>
</config>
```

Without `<config source="configd"/>`, sponge-de boots exactly as
before — no `configd` ROM is opened, no signals are emitted, no errors
are raised. This preserves the W0 regression baseline (every scenario
that does not opt in keeps the Phase-10 behavior).

## Threading contract (failure-point 2 enforcement)

- The ROM signal handler runs on the Genode entrypoint dispatcher
  thread, NOT the Qt event-loop thread blocked in `QApplication::exec`.
- The handler reads the ROM, copies the four key/value pairs into
  QStrings, and marshals the work to the GUI thread with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- The actual signal emission happens in `applyConfig()` on the GUI
  thread. The widgets' `apply*` slots are bound via `QObject::connect`
  to those signals; they ALSO run on the GUI thread (Qt dispatches
  signal-connected slots on the emitting thread by default, which is
  the GUI thread here).
- The `QTimer` (250 ms pull) and the ROM signal (push) funnel into
  the same `applyConfig()`, de-duplicated per key — identical
  broadcasts are no-ops.