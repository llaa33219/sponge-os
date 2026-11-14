# background_settings — Background settings provider

Owns `background.*` on the `background_settings` configd label:
background.color (#RRGGBB hex) and background.image (closed
allowlist; the shipped image is /system/background/default.png).

Quick vocabulary (`background_settings_quick_request` /
`background_settings_quick_result`):

| op | value | writes |
|---|---|---|
| `color` | `#RRGGBB` | `background.color` |
| `image` | allowlisted path | `background.image` |

Legacy parity: the W2 background_tab hex check and the F10
path-traversal allowlist are preserved as provider-side validation;
the configd validator remains authoritative.
