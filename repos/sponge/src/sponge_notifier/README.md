# sponge_notifier — notification backend daemon

The notification backend for Sponge OS. It is a long-lived,
signal-driven Genode component that owns the active notification list
and answers the notification bus per the D14.1 decision
(`docs/plans/phase14-daily-desktop.md`).

## Communication channel (settled design)

`sponge_notifier` does **not** expose an RPC interface. It communicates
through the canonical Report/ROM bus, bridged by `report_rom`:

```
client(s) --[Report "notif_request"]--> report_rom
            --[ROM "notif_request"]-->  sponge_notifier

sponge_notifier --[Report "notifications"]--> report_rom
                  --[ROM "notifications"]-->  client(s)
```

Clients (sponge-de, vct, notify_probe) write a `<notif_request>`; the
daemon validates and stores; every consumer (panel notifier_widget,
notify_probe) reads the `<notifications>` list.

The corresponding `report_rom` policies (full labels include the
child-name prefix):

```
policy | label: sponge_notifier -> notif_request | report: <client> -> notif_request
policy | label: <consumer>      -> notifications | report: sponge_notifier -> notifications
```

## XML contract

Inbound (`<notif_request>`):
```xml
<notif_request>
  <notification source="vct" kind="info" ttl_ms="5000">
    <title>install: hello completed</title>
    <body>package hello-1.0 installed</body>
  </notification>
</notif_request>
```

Outbound (`<notifications>`):
```xml
<notifications count="1" max_live="8">
  <notification id="1" ts="1718301234" source="vct" kind="info" ttl_ms="5000">
    <title>install: hello completed</title>
    <body>package hello-1.0 installed</body>
  </notification>
</notifications>
```

The `id` and `ts` attributes are assigned by the daemon (monotonic
id; `ts` is the millisecond-uptime captured at insertion). The
`notifications` root carries the active count and the configured
`max_live` so a watcher can render an "N more" affordance without
re-counting.

## Validation (fail-soft, never crash)

| Field     | Constraint                                          | Default             |
|-----------|-----------------------------------------------------|---------------------|
| `kind`    | `info` \| `warn` \| `error`                         | `info` if missing   |
| `ttl_ms`  | `1..30000` (cap 30000 per D14.1)                    | `default_ttl_ms`    |
| `source`  | non-empty printable string                          | `unknown` if empty  |
| `title`   | non-empty, ≤ 96 chars                               | rejected if empty   |
| `body`    | optional, ≤ 256 chars                               | omitted if absent   |

Unknown root elements, malformed XML, or empty `title` are silently
dropped with a single warning (the notifier is the dumb side; callers
are responsible for meaningful input).

## Behavior

- Watches the `notif_request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses each `<notification>` child and validates the
  fields above. Assigns a monotonic id and the current uptime as `ts`.
- Maintains the active list FIFO (capped at `max_live`, default 8).
  When the list is full, the oldest entry is dropped and a warning
  logged once per drop.
- Publishes the full active list as the `notifications` ROM on every
  state change (initial publish + insert + expiry).
- A periodic Timer (500 ms) sweeps expired entries — `now - ts >= ttl_ms`
  — and re-emits the broadcast. The Timer is the single authoritative
  "is anything expired" source.
- The generator is deterministic for a given active list, so a
  watcher's config-diff is byte-stable (no spurious re-renders).

## Optional config

```xml
<config>
  <max_live       value="8"/>
  <default_ttl_ms value="5000"/>
</config>
```

Empty config is acceptable (defaults applied). The absolute ceiling on
`max_live` is 32 (the daemon's `MAX_LIVE`); larger values are clamped
without warning (configd mirrors this pattern).

## What is deliberately not implemented

- Persistence (the active list is in-memory only; ephemeral by design).
- Per-client output routing (the notification feed is a single bus,
  not a per-client mirror — every consumer sees the same active list).
- Priority ordering (FIFO by id is the only ordering — v1 ships this
  and Phase 15+ can add priority if warranted).
- An "ack" or per-request result channel (clients are fire-and-forget;
  the daemon logs dropped entries but never replies to individual
  requests).

## Minimum privilege

The daemon requests only `Timer` + `ROM` (the `notif_request` input,
the optional `config` input, and any other ROM routed via `<parent/>`)
sessions. It exposes `Report` + `ROM` for the `notifications` channel.
No `PD`, no `RM`, no `GUI` — the notifier is a pure backend
(AGENTS.md §1.2). It is purely signal-driven (ROM sigh + periodic
Timer).
