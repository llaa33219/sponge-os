# terminal_settings — Terminal settings provider (typed-key seam)

Ships in the terminal package payload (pkg/terminal/metadata.xml
declares the `<settings id="terminal">` entry). Owns the
`terminal.*` domain on the `terminal_settings` configd label, but
sponge_configd's closed typed registry has no terminal.* keys yet.
The provider ships its full lifecycle — window, bounded errors,
config channel (`terminal_settings_config_request`), quick pair —
with an honest empty write surface.

TODO(configd follow-up) — the exact typed-key additions:

1. `sponge_configd/main.cc` `_registry[]`: one Key_def entry per
   decided terminal.* key (name-sorted; bump _num_keys/MAX_KEYS),
   each with its typed validator in the wm.layout.mode style.
2. `terminal/main.cc`: fill the row table and quick-op table from
   the same key list and flip `SEAM_READY` to true.

No terminal settings are invented here (font/shell/colors are
product decisions for the follow-up). Quick requests answer the
bounded "not registered yet" error until the seam lands.
