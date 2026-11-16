# Package lifecycle, admission, and launch

Breadcrumb: [`run/README.md`](../README.md) → Package lifecycle, admission, and launch

The `sponge_pkgd` install/explain/remove/list/persist flows, config
regeneration survival, resource admission, the shared launch backend,
and search/update verification. Full catalog entries follow verbatim
(cautions and limitations included); the parent index carries one-line
summaries.

## Scenarios
- `sponge-pkg-explain.run` — Phase 4a: end-to-end package-explain flow.
  `vct install nano --explain` writes a request report that `report_rom`
  relays to `sponge_pkgd`, which resolves the package + its `ncurses`
  dependency from staged `pkg_*.xml` metadata ROMs and returns the
  4-step install plan (docs/06-vct.md §5.2). Kernel-agnostic.

- `sponge-pkg-install.run` — Phase 4b: `vct install hello` actually
  starts the `hello` component under a nested `pkg_runtime` init.
  `sponge_pkgd` regenerates pkg_runtime's config (relayed as its `config`
  ROM by `report_rom`); pkg_runtime starts hello, verified by the
  payload's boot marker. Kernel-agnostic.

- `sponge-config-regen-survival.run` — does a `sponge_pkgd`
  config regeneration that crosses the `Expanding_reporter` buffer
  boundary kill already-running `pkg_runtime` children? Minimal
  headless topology (no Qt, no drivers, no disk): `regen_probe`
  installs the autostarted `witness` package (payload
  `config_witness`, a monotonically increasing beat counter relayed
  through `report_rom`), then drives four install/remove operations on
  a second package, each of which makes `pkgd` regenerate the whole
  config with `<repro_pad_grow: yes>` (write *N* padded with *N* ×
  32 KiB, so EVERY write provably overflows the 32 KiB reporter
  buffer) and requires the counter to stay monotonic. **Measured
  verdict: the children survive** — `report_rom` never notifies a
  reader that its writer closed, so the transient no-writer window is
  invisible to `pkg_runtime`. `SPONGE_REPRO_CONTROL=1` runs the
  POSITIVE CONTROL (`<repro_empty_from: 3>`, an empty-but-valid
  config), which MUST make the probe report FAIL — it proves the
  detector is not blind. Gates on `regen-probe: PASS`. Kernel-agnostic.
  Receipts: `docs/evidence/config-regen-survival.md`.

- `sponge-pkg-admission.run` — does `sponge_pkgd` **refuse** a launch
  whose declared quota does not fit `pkg_runtime`'s remaining budget,
  rather than answering `ok` and letting Genode's sandbox clamp the
  child (which kills it before its first instruction with nothing but a
  kernel warning)? Minimal headless topology (no Qt, no drivers, no
  disk), the resource twin of `sponge-pkg-lifecycle.run`'s
  `binary-missing` case. `pkg_runtime` is granted caps 3200 / ram 160M;
  an autostarted `witness` (4M/100) publishes a monotonic beat counter,
  and two probe-only fat packages `pkg/fat_a` + `pkg/fat_b` (96M/2000
  each, sharing the existing `config_witness` binary, enabled by no
  bake profile) drain the budget in turn. `admission_probe` reads
  `pkg_runtime`'s OWN sandbox state report — the same ROM `pkgd` reads,
  fanned out by `report_rom` — so its cross-checks are against the
  identical budget rather than a private estimate. It requires
  (1) the over-budget launch to answer `insufficient-resources` with
  coherent `need_ram`/`avail_ram`/`need_caps`/`avail_caps` that name no
  more budget than the probe itself observed, (2) the refused package
  to still be `running="no"` in the installed broadcast (it never
  entered the running set, so the config was never regenerated),
  (3) the already-running witness to keep counting (the refusal costs no
  collateral restart), and (4) removing the hog then re-launching the
  SAME package to SUCCEED — the positive control proving the gate is a
  live budget check, not a latch. Gates on `admission-probe: PASS`.
  Kernel-agnostic. Receipts:
  `docs/evidence/pkg-admission-control.md`.

- `sponge-resource-request.run` — can a component that hits its quota be
  given more **at runtime**, or is its declared quota a ceiling for the
  life of the process? The empirical counterpart to the decision
  `sponge_pkgd`'s admission margin rests on. Three nested sandboxes and
  one probe binary (`resq_probe`), each child issuing exactly one
  explicit `env().parent().resource_request()`: a slack-rich parent
  (512M) answers on the spot (the vendored immediate-grant path, patch
  ledger row 21); a tight parent (8M grant, ~75 KB slack) PARKS the
  request and it stays parked for the full 60 s window — while the
  top-level init still holds over 400 MiB, so there is measurably no
  upward forwarding; and a parked request in a config-owned sandbox
  resolves ONLY after `config_bump` rewrites that sandbox's own config
  (+42.6 MB observed). `config_bump` is the sandbox's SIBLING, not its
  parent. Gates on `resq-c: PASS` (the last experiment; the earlier
  verdicts are quoted verbatim in the evidence file — the two-argument
  `run_genode_until` power-cycles, so four stacked gates would not
  observe one experiment). Kernel-agnostic. Receipts:
  `docs/evidence/dynamic-resource-topup.md`.

- `sponge-pkg-remove.run` — Phase 4b: full install→remove lifecycle.
  The `pkg_seq_probe` component drives the channel (vct is short-lived),
  installs hello, removes it, and confirms both results; verified by a
  PASS marker. Kernel-agnostic.

- `sponge-pkg-list.run` — Phase 4c: the `pkg_seq_probe` installs hello,
  then issues a `list` request and asserts hello appears in the result,
  verifying the installed-set inspection path. Kernel-agnostic.

- `sponge-pkg-manual.run` — Phase 4c: `vct install hello --manual`
  renders the install step-by-step (explain→steps→execute) and still
  starts hello under pkg_runtime; verified by hello's marker. Kernel-
  agnostic.

- `sponge-pkg-persist.run` — Phase 4 follow-up #2: installed-set
  persistence across a reboot. Two boots over the same `lx_fs`-backed
  host directory (modelled on upstream `depot_remove.run`): boot 1
  installs hello and writes the store; boot 2 boots a fresh `core`/init
  against the same directory and issues a `list`-only request, asserting
  hello is present — which can only be the restored store, since no
  install was issued this boot. `base-linux` only (lx_fs is a Linux-host
  wrapper); the component code itself is portable.

- `sponge-pkg-gui.run` — Phase 7 todo 8: GUI-package runtime-config
  generator verification. Installs a minimal Qt6 colored-window package
  (`pkg_gui_demo`) through `sponge_pkgd` and pixel-verifies its
  distinctive green (#00ff00) window through a Capture session, proving
  the generator correctly emits `<binary>` (when it differs from
  `<name>`), the inline `<config>` verbatim (Qt6/libc wiring), the
  `<parent/>` route for `default-route="nitpicker"` Gui sessions, the
  extended `parent-provides` (Gui/Input/Report/File_system/NIC), and
  the GUI-safe `<default>` caps floor (1000, per the §11.1 lesson).
  Kernel-agnostic topology (base-linux native or base-sel4 QEMU); the
  acceptance run uses `KERNEL=sel4 BOARD=pc`. Gates on
  `pkg-gui-probe: PASS`. Updated in todo 9 to issue a `launch`
  after install (pkg_gui_demo has no `<autostart/>`, so install alone
  no longer starts the component).

- `sponge-pkg-lifecycle.run` — Phase 7 todo 9: installed-vs-running
  lifecycle verification (docs/12-package-format.md §9.2.1). The
  `pkg_lifecycle_probe` (modeled on `pkg_seq_probe`) drives a six-step
  sequence against `sponge_pkgd` AND reads the `installed` broadcast
  ROM to assert per-package `running="yes"|"no"` after every
  transition: (1) install `hello` → broadcast shows `hello/running=yes`
  (its metadata declares `<autostart/>`); (2) install `pkg_gui_demo`
  → `running=no` (no autostart — STOPPED); (3) `launch pkg_gui_demo`
  → `running=yes` AND pkg_gui_demo's `window shown` marker (the run
  gates on the marker to prove the launched `<start>` node actually
  booted the component); (4) launch again → `already-running`
  (idempotent); (5) `launch nosuchpkg` → `not-installed` (no implicit
  install); (6) `remove pkg_gui_demo` → broadcast drops it (`<start>`
  node dropped from `pkg_runtime`). Kernel-agnostic; the acceptance
  run uses `KERNEL=linux BOARD=pc` (fast iteration — Qt6 first paint
  under softpipe is the slow part on seL4). Gates on
  `lifecycle-probe: PASS`.

- `sponge-launch.run` — Phase 7 todo 10: click-to-launch + `vct launch`
  verification. Proves BOTH launch paths share the same `sponge_pkgd`
  backend (AGENTS.md §3.3 rule 5 — two interfaces, one backend). The
  `launch_probe` (1) sends `launch pkg_gui_demo` over the same
  `request` channel `vct launch` uses (VCT PATH — pkg_gui_demo boots
  and logs its `window shown` marker); (2) removes + reinstalls the
  package; (3) injects a synthetic click on sponge-de's launcher button
  to open the popup, then on the first menu entry — sponge-de's
  `LauncherController` writes `launch pkg_gui_demo` to the dedicated
  `launcher_request` channel (report_rom is single-writer per label,
  so the long-lived launcher cannot share vct's `request` label);
  pkgd processes it via the SAME `_do_launch` and answers on
  `launcher_result`; the probe pixel-verifies the green (#00ff00)
  window (CLICK PATH); (4) verifies `launch nosuchpkg` →
  `not-installed` and double-launch → `already-running`, emitting
  vct-equivalent JSON lines the run script asserts. Kernel-agnostic
  topology (base-linux native or base-sel4 QEMU); the acceptance run
  uses `KERNEL=sel4 BOARD=pc`. Gates on `launch-probe: PASS` (two Qt6
  first paints under softpipe Mesa on seL4; generous 600s timeout).

- `sponge-pkg-meta.run` — Phase 7 todo 18: search/update assertion
  matrix. vct is short-lived, so the `pkg_meta_probe` drives the SAME
  ROM reads + comparisons that vct's `SearchCommand`/`UpdateCommand`
  perform (pkg_index.xml + pkg_<name>.xml + the `installed` broadcast),
  covering six steps in one boot: (1) install hello via sponge_pkgd,
  (2) search "hello" → hit, (3) search "zzznomatch" → honest empty
  result, (4) update hello → "already current" (broadcast == repo),
  (5) update hello delta → "repo carries 1.0, installed 0.9 — effective
  after next image build" (synthetic `installed_delta` broadcast with a
  pinned old version, faithful to the cross-image-rebuild semantic of
  docs/12 §9.2.2), (6) update nosuchpkg → "not installed" error
  (non-zero path). Gates on `pkg-meta-probe: PASS`. Kernel-agnostic
  (no GUI/drivers); runs on base-linux for speed.

