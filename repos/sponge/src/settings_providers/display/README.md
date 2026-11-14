# display_settings — Display settings provider (typed-key seam)

Owns the `display.*` domain on the `display_settings` configd label
(key_domain_acl.cc), but sponge_configd's closed typed registry has
no display.* keys yet. The provider ships its full lifecycle —
window, bounded errors, config channel
(`display_settings_config_request`), quick pair — with an honest
empty write surface.

TODO(configd follow-up) — the exact typed-key additions:

1. `sponge_configd/main.cc` `_registry[]`: one Key_def entry per
   decided display.* key (name-sorted; bump _num_keys/MAX_KEYS),
   each with its typed validator in the wm.layout.mode style.
2. `display/main.cc`: fill the row table and quick-op table from the
   same key list and flip `SEAM_READY` to true.

No display settings are invented here (no guessed brightness/
resolution policy). Quick requests answer the bounded "not
registered yet" error until the seam lands.
