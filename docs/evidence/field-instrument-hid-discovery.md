# Field instrument HID discovery (2026-10-03)

The panel-readout field instrument for the real-hardware launch-stall
investigation passed every QEMU serial check while being silently
wrong in two ways — both rooted in one fact discovered only through
pixel-level panel verification:

**Genode 26.08's sandbox delivers inline child configs AND sandbox
state reports as HID text (the new `Generator`), not XML.**

## Evidence chain

1. **The dead instrument** (`6673fb4c3c`): commit `a6f617d56d` (the
   U-readout) accidentally deleted the diag ROM construct block — every
   image built after 10/2 16:57 rendered C:/S:/M: never and U: as an
   unconditional 0. The user's "still U:0" reading carried no signal.

2. **The gate mystery**: after restoring the constructs, the new
   K/P/Q/D ROM subscriptions still never rendered. Static analysis
   said the quoted scan patterns (`"system_state"`) could not match
   the delivered config — yet the pre-existing `runtime_state` gate
   demonstrably fired (S/C/U rendered, pixel-verified). A boot-time
   debug line (config buffer head) revealed `head=config` — the buffer
   begins with the bare word `config`, not `<config>`: HID. The old
   colon-form pattern (`runtime_state: yes`) had matched HID's
   `diag | runtime_state: yes` attribute form **by accident**.

3. **The parse failures**: with gates fixed (bare attribute names,
   `41e30cc957`), a healthy QEMU boot (hello running, alpha-probe
   PASS) still displayed `C:0 · K:0 · P:0 · Q:0` — the sandbox state
   reports are also HID, and `Xml_node` construction throws
   `Invalid_syntax`, caught, silently degrading every count to zero.
   Rewriting the C/M and K/P/Q parses with `Genode::Hid_node`
   (`util/hid.h`, the `vct/args.cc` pattern) yields the correct
   healthy-boot panel, pixel-verified end-to-end:

       Sponge DE · R:debug,hello · N:8 · C:2 · S:2(4096b) · M:0k
                     · U:2 · W:1 · K:11 · P:1 · Q:0 · D:4096b

## Field-data consequences

Every historical `C:0` reading — including the real-hardware ones —
was this parse artifact, NOT evidence about pkg_runtime's children.
The historically valid channels are the parse-independent ones:
N/R/S/W (XML-report or bookkeeping sources) and U (byte-hash). The
one solid real-hardware fact, **U:0 = pkg_runtime never wrote any
state report**, still stands and remains the investigation's anchor.

## Rules for future components

- Parsing a sandbox-delivered config or a sandbox state report:
  use `Genode::Hid_node` over `Const_byte_range_ptr`, never
  `Xml_node` (which throws on HID and, inside a catch, reads as
  "zero").
- Emitting machine-parsed reports from a component: use
  `Expanding_reporter::generate_xml` (XML, `Xml_node`-parseable) —
  pkgd's runtime report and cfg_probe's report both parse fine
  cross-format because their emitters are explicit.
- Byte-scan gating over delivered configs: match bare attribute
  NAMES; quoted or colon-qualified forms silently depend on the
  serialization.

## Instrument readout contract (now trustworthy)

K = children in the SYSTEM init's own state · P = pkg_runtime
0 absent / 1 present / 2 present-but-incomplete · Q = children with
pending resource requests · D = bytes cfg_probe sees in the
`sponge_pkgd -> runtime` module (the exact config source feeding
pkg_runtime) · C = pkg_runtime's own children · U = state-ROM content
changes (parse-free) · M = cosmetic (top-level ram.used; renders but
semantics unverified).

## Commits

`6673fb4c3c` construct restore · `2dfbb431c1` K/P/Q/D instrument ·
`f6db24afeb` kernel CONFIG_DEBUG_BUILD restore (ledger row 21) ·
`52de10fabe` instrument propagation (5 product scenarios) ·
`41e30cc957` HID gates + Hid_node parses.

QEMU reproduction of the real-machine stall: not achieved (every
QEMU boot healthy). The decisive next datum is one real-hardware
panel reading with this instrument.
