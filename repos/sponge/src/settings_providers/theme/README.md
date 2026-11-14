# theme_settings — Theme settings provider

Owns `theme.*` on the `theme_settings` configd label: theme.active
(open string in configd's registry; the four shipped themes are
default/light/dark/compact).

Quick vocabulary (`theme_settings_quick_request` /
`theme_settings_quick_result`):

| op | value | writes |
|---|---|---|
| `active` | theme name (non-empty, transport-bounded) | `theme.active` |

The window cycles the shipped four (the legacy Theme tab UX); a
theme set externally (vct) stays visible as-is and can be replaced
with Left/Right — the open-registry escape hatch stays intact.
