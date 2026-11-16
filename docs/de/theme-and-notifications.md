# Sponge DE Design: Theme System and Notification System

> **Breadcrumb:** [05 - Sponge DE Design](../05-sponge-de.md) / Theme System and Notification System
>
> Full text of `docs/05-sponge-de.md` §4 and §4.5. Section numbers
> are kept unchanged; the parent keeps numbered stubs so existing
> `docs/05-sponge-de.md §4` style references still resolve.

---

## 4. Theme System

Visual elements are not hardcoded in the source. They live in theme
files (see `AGENTS.md` §3.4). Initial design direction:

- **Theme format**: simple INI-style key-value text (see
  `docs/10-theme-format.md` for the concrete specification). JSON or YAML
  may be reconsidered as complexity grows.
- **Storage location**: two layers — the user settings directory
  (`~/.config/sponge/theme` or its Genode VFS counterpart) and the
  system default (`repos/sponge/src/sponge-de/themes/default.theme`).
- **Application**: loaded at Sponge DE start. `vct theme apply` can
  reapply the theme at runtime (through the `sponge_themed` backend).
- **Scope**: colors, fonts, spacing, icon set, panel position and size.
- **Parser skeleton**: `repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}`
  implements the INI parser and data model for Phase 5. It is not yet
  wired into the build or applied to Qt widgets; integration is pending.

User-customized themes take priority over the system default, but
upgrades do not silently overwrite user changes
(see `docs/02-philosophy.md` §3.4).

---

## 4.5 Notification System (Phase 14 — Implemented)

Notifications are delivered via a Sponge-native daemon
`sponge_notifier` (decision D14.1 in
`docs/plans/phase14-daily-desktop.md`). The bus uses the same
Report/ROM pattern as the other Phase-4/5 daemons:

```
client(s) --[Report "notif_request"]--> report_rom
            --[ROM "notif_request"]-->  sponge_notifier

sponge_notifier --[Report "notifications"]--> report_rom
                  --[ROM "notifications"]-->  client(s)
```

The panel renders the active list as a themed popover. The popover is
drawn into the panel domain (under the panel bar) so it docks
automatically; the geometry is fixed at `(700, 36, 300, 60)` and is
the contract with the W4 acceptance probe's Capture-pixel check.

Each entry has a `kind` (`info` / `warn` / `error`), a `title`, an
optional `body`, a monotonic `id`, and a `ttl_ms` that the daemon
enforces via a 500 ms periodic Timer sweep. The active list is FIFO
(capped at `max_live`, default 8); the oldest entry is dropped silently
when the list is full.

In Phase 14 only Sponge DE and `vct` post notifications (system
events and audit events). The opt-in is per-component:

- `vct` advertises `<config enable_notifications="yes">` (default ON).
  Posts on `install` / `remove` / `shutdown` / `reboot` completions.
- `sponge-de` advertises `<notifier source="daemon"/>`. Posts on
  theme apply, config change, and package install completion.

The absent-daemon warning (D14.1): when the daemon is absent from
the topology, the poster's `post()` emits a single
`Genode::warning("notifier unavailable, dropping: <title>")` per
unique title. The acceptance run
`run/sponge-notify-without-notifier.run` proves vct boots cleanly
without the daemon in the topology; the warning path is exercised by
the main `run/sponge-notify.run` scenario on the same build.

> **Capability surface (the D14.1 boundary, AGENTS.md §1.2)** — the
> daemon PROVIDES `Report` + `ROM` for the `notifications` channel and
> REQUESTS `Timer` + `ROM` (notif_request input) + `ROM` (optional
> config). No `PD`, no `RM`, no `GUI`. The `notifier_source` config
> gate keeps scenarios without the daemon from being killed by the
> parent when the Report session is denied.

