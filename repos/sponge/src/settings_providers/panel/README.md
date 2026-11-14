# panel_settings — Panel settings provider

Owns `panel.*` on the `panel_settings` configd label: panel.height
(16..128 px), panel.position (top/bottom — left/right stay deferred
with an explicit bounded message per D16.2), panel.visible_widgets
(clock/launcher/tasklist toggles), panel.ids, and the panel.<id>.*
pattern keys (per-instance rows rebuild whenever panel.ids changes).

Quick vocabulary (`panel_settings_quick_request` /
`panel_settings_quick_result`), one op per request:

| op | value | id | writes |
|---|---|---|---|
| `height` | 16..128 | optional | `panel.height` / `panel.<id>.height` |
| `position` | top\|bottom (left/right refused with the deferral message) | optional | `panel.position` / `panel.<id>.position` |
| `widgets` | clock,launcher,tasklist list | optional | `panel.visible_widgets` / `panel.<id>.visible_widgets` |

Legacy parity: the W2 panel_tab validators (range, enum, enum-list)
and the left/right honest-disclosure behavior are preserved; the
writes run on this provider's own ACL-limited label.
