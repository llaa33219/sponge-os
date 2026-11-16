> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 16 Extension.

### Phase 16 Extension (2026-10-04, user-directed): desktop + application completeness

User directive after the first real-hardware session where launched
windows appear (the launch-stall root cause fixed,
`docs/evidence/launch-stall-root-cause.md`): the desktop is now
usable enough that the APPLICATION layer's gaps are the blocker.
The initial Extension entries reopen the Phase 16 completion
criteria:

- [ ] **Criterion 13 — the browser opens.** Falkon launches from
  the release media and renders a window on real hardware and in
  product QEMU scenario. Blocker status (assessed 2026-10-05,
  **updated 2026-11-04**): the
  per-PD CSpace size is RESOLVED in the vendored tree (ledger row 6
  + the Phase-15 addendum raising `CSPACE_SIZE_LOG2_1ST` to 7 =
  65536 slots/PD) and already covers falkon's capability appetite.
  The remaining, terminal-class integration blockers:
  (a) falkon + the WebEngine `.lib.so` set are neither built in the
  product build dir nor staged to `/system` (the scenario's
  `falkon_libs` list EXCLUDES them from `/system/lib`);
  (b) caps plumbing — falkon's quota is caps=200000/ram=1G but
  pkg_runtime holds caps=8000 inside system's 39600: both grants
  need orders-of-magnitude bumps (init has 523288 caps — feasible);
  (c) P3 has ~150 MiB free vs the WebEngine lib footprint — the
  image/partition may need growing; (d) criterion 15 (page load)
  additionally needs the nic wiring (driver + slirp + pkg_runtime
  Nic route) carried into the product scenario.
  **Status update (2026-11-04):** blockers (a)–(c) are delivered on
  the product QEMU media — P2.6 stages the measured WebEngine closure
  with new-media P3 content-sizing (docs/13; receipt
  `docs/evidence/phase16-p26-browser-staging.md` §5) and the browser
  runtime carries its own grant (docs/17 §6); the P2.6 gate reached
  real launcher open + first paint + basic input, and P5.1's
  acceptance re-ran the falkon leg (first paint + URL-bar typed
  delta) in three consecutive all-green suite passes
  (`docs/evidence/phase16-runtime-acceptance.md`). **The criterion
  itself stays open:** it also names real hardware (QEMU evidence is
  never real-hardware PASS), and blocker (d) is criterion 15's
  registered follow-up.

- [ ] **Criterion 14 — terminal + vct install flow.** The terminal
  package opens a window on the release media, and inside that
  terminal `vct install <pkg>` resolves, downloads/installs from
  the staged repository, and the installed program runs. Blocker
  (root-caused 2026-10-03, `docs/evidence/launch-stall-root-cause.md`
  §Follow-up item 2): the terminal's sub-init children
  (`terminal`, `vfs`, `vfs_rom`→`cached_fs_rom`) resolve their
  binaries without the `bin/` prefix — pkgd's `binary_prefix`
  rewriting covers only the top-level start — and the payload
  (noux tars, `VeraMono.ttf`) is not staged on the product image.
  Fix: stage the sub-init children binaries + payload, and teach
  pkgd's runtime generator to prefix nested starts' binaries when
  `binary_prefix` is set.
  **Status update (2026-11-04):** the P2.3 work verified the nested
  `binary_prefix` rewriting is in place (the stated defect was stale;
  receipt `docs/evidence/phase16-runtime-rom-failures.md` §9) and the
  terminal launches with first paint + typed input in P5.1's
  acceptance, but **the criterion stays open**: its own
  resolve/download/install/run acceptance is a registered follow-up
  (`vct-install-in-terminal`, see the register below).

- [ ] **Criterion 15 — the browser loads a web page.** With
  criterion 13 delivered, navigating to a page over the
  `pc_nic`/slirp stack renders content (the falkon-rescue scenario
  proved first paint + fixture GET on a dedicated topology; the
  release media needs the nic wiring carried over).
  **Status update (2026-11-04): stays open** — P2.6 delivered
  criterion 13's launcher open + first paint + basic input only;
  NIC-dependent page acceptance is the registered `Falkon page load`
  follow-up (see the register below).

- [ ] **Feature — additional shells (zsh, fish).** No upstream
  Genode/noux port exists for either shell (the noux ecosystem ships
  bash + the coreutils-style toolset only), so per the user's
  directive both are direct ports: zsh first (C, autotools, depends
  on ncurses — the ncurses port already exists in-tree), built as a
  noux tar alongside bash-minimal.tar; fish second (C++17, bundled
  libs + pcre2 — a materially heavier port), same tar pattern once
  its dependencies stage. A default-shell selection mechanism in the
  terminal package follows both.

- [ ] **Feature — same-program multi-instance.** Launching two
  instances of one program creates two independent windows (today
  pkgd answers `already-running` by design, Phase 7's verified
  idempotency). Requires a pkgd launch-model extension: per-instance
  unique child names (e.g. `terminal@2`), per-instance running-set
  tracking, and launcher feed entries per instance.

**Runtime-overhaul track status (2026-11-04) — follow-ups register
pointer.** The runtime-overhaul track of this Extension
(`docs/plans/phase16-runtime-overhaul.md`) has landed through P5.3:
P3.3 removed storage I/O from session-create paths (dispatch parses
from bootstrap-cached metadata, store flushes on the worker thread),
P3.4 scopes the supervisor gates per runtime subtree with **real
revival** of the contained child (the base-layer
`create_thread: invalid PD argument` blocker is fixed in place by
ledger rows #32/#33 at fork pin `48a946ba15`), P4.1 classified all 33
patch-ledger rows (**keep 32 · remove 0 · replace 1**),
P4.2 dispositioned the pkg-driver uplink regression **fixed in place**
(six current-pin runs reach the DHCP gate), and P5.1 passed the
six-leg acceptance suite **three consecutive all-green passes**
(`docs/evidence/phase16-runtime-acceptance.md`). **What stays open is
registered, never silently closed** (plan D16R.18): the
**follow-ups register** is §11 of
[`docs/plans/phase16-runtime-overhaul.md`](../plans/phase16-runtime-overhaul.md)
— `vct-install-in-terminal` (criterion 14), `Falkon page load`
(criterion 15), additional shells, same-program multi-instance, the
ISO ceiling, and the Phase-17 UI rows — and the criteria checkboxes
above stay **open**: fixing a root cause never closes an item, and
criterion 13 additionally requires real hardware, which no QEMU
receipt can claim. P5.2 (this docs sync) is landed, and P5.3 is complete (2026-11-06):
the full regression sweep landed
(`docs/evidence/phase16-runtime-sweep.md`: 32 green / 4 documented
partials / 0 red after the two fix waves) and the `review-work`
post-implementation review passed all five lanes (goal/constraint
14/14, security with the app hardware-capability boundary confirmed,
context-mining with its date/pending reservations resolved by
`9552f9318b`, code quality after the M1 create-expiry fix at fork
`9088c7227c` / parent `fcae2cbf4b`, QA 7/7 at `36f3e1e9d6` plus
focused 4/4 at `9552f9318b`), with that matrix unchanged. The
review's nonblocking follow-ups (security MEDIUM metadata attribute
injection into non-gated report/broadcast documents, security LOW
fail-closed publication wedge recoverable by remove, code-quality
minor F2 `_close` SERVICE_DENIED teardown hardening) are registered
in `docs/evidence/phase16-runtime-sweep.md` §13; they are not
blockers and none is claimed fixed.

**Real-hardware runtime field report (2026-11-07): registered,
closing nothing.** A USER-ATTESTED real-hardware field report, **not
an independently reproduced agent test**, records that on a physical
machine the system ran, memory-pressure freezing was prevented, an
error was actually displayed on the panel, and normal operation
continued afterward (provenance as attested: parent `79d70902f5`,
Genode pin `9088c7227c`, the canonical UEFI/USB desktop image
`var/dist/sponge-os-0.2.0-x86_64-sel4.img`, SHA-256
`a907d733f726a7789f6f54944aba3b552579e6ee3c798878e1b4e3636b17fcfa`;
the image independently passed the OVMF/QEMU alpha-probe before
staging; no serial log, machine model, repeat count, timing, or
memory measurements came with it, so it supports the observed
degradation/recovery behavior but is not a blanket hardware
certification). Full record:
`docs/evidence/phase16-runtime-acceptance.md` §7 +
`docs/evidence/phase16-runtime-sweep.md` §14. It closes no criterion
(13/14/15 and every other checkbox above stay open) and changes no
QEMU matrix row or documented partial.
