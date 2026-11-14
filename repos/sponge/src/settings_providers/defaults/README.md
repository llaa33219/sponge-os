# defaults_settings — Reset-to-Defaults provider

Owns exactly `bake.applied` on the `defaults_settings` configd
label. The reset is one write of `bake.applied=no`, which triggers
configd's baked-defaults path (the same `_apply_bake_defaults` chain
vct's `bake reset` uses): configd reapplies only the baked keys and
theme.active, preserves every other user-set key, then records
bake.applied=yes.

Quick vocabulary (`defaults_settings_quick_request` /
`defaults_settings_quick_result`):

| op | value | writes |
|---|---|---|
| `reset` | (none) | `bake.applied=no` |

The destructive action requires a two-step Enter confirmation
(AGENTS.md §1.1 explicit affordance). If baked defaults are not
routed to configd, the bounded error "baked defaults are
unavailable" surfaces; nothing is written.
