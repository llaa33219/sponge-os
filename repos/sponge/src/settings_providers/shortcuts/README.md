# shortcuts_settings — Keyboard Shortcuts provider

Owns `shortcuts.*` on the `shortcuts_settings` configd label:
shortcuts.bindings, the structured multi-line
`action<TAB>key_sequence` table (closed actions:
launcher/focus_next/dismiss; key names/aliases per configd's
_shortcuts_valid).

Quick vocabulary (`shortcuts_settings_quick_request` /
`shortcuts_settings_quick_result`):

| op | value | writes |
|---|---|---|
| `bindings` | full table (the structured value) | `shortcuts.bindings` |

Legacy parity: the reject-the-whole-write rule is kept — every
change re-serializes the full table and one bad line refuses the
write with a bounded English error, leaving the store untouched.
Window rows: one action row + one key-sequence row per binding, plus
add/remove actions (remove needs the selection on a binding; the
last binding cannot be removed because configd rejects an empty
table).
