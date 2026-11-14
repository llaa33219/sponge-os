# wm_settings — Window Management settings provider

Owns `wm.*` on the `wm_settings` configd label. The domain holds
exactly one registered key today — wm.layout.mode (floating|tiling,
D16W.11) — and the provider presents exactly that.

Quick vocabulary (`wm_settings_quick_request` /
`wm_settings_quick_result`):

| op | value | writes |
|---|---|---|
| `layout` | floating\|tiling | `wm.layout.mode` |

Future wm.* keys join through the same row/quick pattern when they
are registered in configd's closed typed registry; nothing is
invented in advance.
