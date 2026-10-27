================================================================================
Phase 16 runtime baseline — product application launch (PRE-CHANGE)
Measurement report  •  Date: 2026-10-28
Branch:  refactor/phase16-runtime-overhaul   HEAD: 3afb40c922
Scope:   run/sponge-desktop-disk-uefi-usb.run on QEMU/OVMF (seL4)
================================================================================

## 0. HEADLINE

This is the pre-change baseline for the product application-launch path,
captured at HEAD 3afb40c922 before the Phase 16 runtime-overhaul product
edits land. Two serial runs of the UEFI USB product topology were
executed with the opt-in launch arm (`SPONGE_LAUNCHTEST=on`); both runs
produced the same scenario verdict:

  run/sponge-desktop-disk-uefi-usb.run + SPONGE_LAUNCHTEST=on: FAIL (2/2)
    -> `launch-gate FAIL — terminal request
        (marker: 'prod-launch-probe: launch terminal -> ok')`

The FAIL is honest and the underlying per-app picture is worse than the
gate text suggests: of the eight built-in apps, exactly one (debug) has
first-paint evidence on this topology; one (hello) is a GUI-free
autostart whose "running" marker is a boot acknowledgement only; three
(terminal, files, falkon) were request-acked by the probe and then
failed to boot; three (textedit, calculator, pdf_view) were never
launched by anything and are NOT-MEASURED beyond install + launcher
feed. **No app has product input evidence on this topology (0/8).**
Boot acknowledgements are not paint and not input; this report keeps
those axes separate.

All evidence is QEMU/OVMF + seL4. No real-hardware claim is made or
implied by this document.

  Coverage axes used per app:
    install  — pkgd install ack + launcher feed pair (alpha_probe (b))
    boot     — the package runtime actually started
    paint    — first paint of the app's own window
    input    — product input path reaching the app

## 1. EXACT COMMANDS

Serial only (one `make` in `genode/build/x86_64` at a time):

```sh
# 1. Build the probe binary the launch arm stages and starts.
#    NOTE (gap, see §5 G7): the scenario does NOT build this target
#    itself; without this step the launch arm's <start prod_launch_probe>
#    node resolves no binary and the gate times out at its first marker.
make -j1 -C genode/build/x86_64 test/prod_launch_probe KERNEL=sel4 BOARD=pc

# 2. The baseline run (repeat once for the 2/2 confirmation):
SPONGE_LAUNCHTEST=on make -j1 -C genode/build/x86_64 \
    run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
```

Run conditions (defaults, nothing overridden):

- `KERNEL=sel4 BOARD=pc`, build dir `genode/build/x86_64`
  (`build.conf` `MAKE += -j80` is superseded by the documented
  `make -j1` convention for scenario work).
- Bake profile: default `desktop` (`pkg/bake/desktop.profile` —
  hello, terminal, textedit, files, calculator, pdf_view, falkon, debug).
- `SPONGE_SLOW_STORAGE_KBPS` unset (no storage throttle);
  `SPONGE_LOGDISK` unset (LOG stays on the serial line).
- QEMU (spawned by the run tool): `-accel kvm -machine q35
  -cpu Skylake-Client -m 8G -snapshot`, `nec-usb-xhci` + `usb-tablet`
  + `usb-storage` (the product image as the stick), OVMF pinned
  `var/ovmf/OVMF_CODE.fd`/`OVMF_VARS.fd` (2024.02), `e1000` + user
  netdev. Guest: seL4 Tier-0 chain (xhci -> usb_block -> part_block ->
  vfs -> cached_fs_rom) + nested `system` from P3.

Artifacts: the run tool's own serial capture lands at
`genode/build/x86_64/var/run/sponge-desktop-disk-uefi-usb/uefi-boot.log`
(build-tree scratch). Raw full-session make logs stay in session scratch
and are deliberately not promoted (AGENTS.md §5.6); every fact needed
for regression comparison is curated below.

## 2. SCENARIO GATE MODEL (what a green text can and cannot prove)

The scenario's gates are sequential `expect` matches on one serial
stream, in this order:

| # | Gate | Marker(s) matched | Proves | Does NOT prove |
|---|------|-------------------|--------|----------------|
| 1 | storage chain | `sponge-de diag gates:` from `[init -> system -> sponge-de]` | xhci -> usb_block -> part_block -> vfs -> cached_fs_rom served `/system`; the DE started | any app behavior |
| 2 | compositor health | `alpha-probe: PASS` **OR** `sponge_debug: FIRSTPAINT ms=` | first paint happened somewhere (see §4 E2 for which proof actually fired) | per-app window paint for the 7 non-debug apps |
| 3 | launch — ENFORCED | `prod-launch-probe: launch terminal -> ok`, then `pkg_runtime -> terminal -> vfs` | pkgd acked the terminal launch request; the terminal package's sub-init booted its `vfs` child | terminal window paint, terminal input, files/falkon, anything else |
| 3 | launch — WARNING-ONLY | `prod-launch-probe: launch files -> ok`, `prod-launch-probe: launch falkon -> ok` | pkgd acked the request | boot/paint/input of files and falkon; absence never fails the run |

Wedge/starvation negative checks (`treating the child as wedged`,
`attempt to request cap for unavailable session ... state=CREATING`)
gate the overall PASS block, not any app.

## 3. PER-APP OBSERVED RESULTS (the baseline)

Legend: RED = observed failure. NOT-MEASURED = the topology has no
probe/gate covering this axis (absence of evidence, explicitly not
PASS). N/A = axis does not apply by construction.

| app | install + launcher feed | boot / launch | window paint | input |
|-----|-------------------------|---------------|--------------|-------|
| hello | OK (`alpha-probe: [1] install hello ok`; feed `hello/Utilities`) | AUTOSTARTED — `hello from package: running` (boot ack only) | N/A — GUI-free by design (`pkg/hello/metadata.xml`: "GUI-free and session-free") | NOT-MEASURED |
| terminal | OK (`install terminal ok`; feed `terminal/System`) | **RED** — request acked (`prod-launch-probe: launch terminal -> ok`) but the package died at boot: `rom_sys: Error: /VeraMono.ttf not found` then `pkg_runtime -> terminal -> terminal: Error: stop because parent denied ROM-session: label="VeraMono.ttf"`. The enforced boot marker `pkg_runtime -> terminal -> vfs` was **not observed in either run** | NOT-MEASURED | NOT-MEASURED |
| textedit | OK (`install textedit ok`; feed `textedit/Editors`) | NOT-MEASURED — never launched by any probe; no boot marker | NOT-MEASURED | NOT-MEASURED |
| files | OK (`install files ok`; feed `files/Utilities`) | **RED** — request acked (warning-only stage) but the binary ROM never resolved: `files: create of ROM (session 4) failed - the provider did not answer within 60000 ms`, then `Error: destroying session ... label="pkg_runtime -> bin/sponge_files" ... state=DENIED` + `dangling session in parent-side ID space` | NOT-MEASURED | NOT-MEASURED |
| calculator | OK (`install calculator ok`; feed `calculator/Utilities`) | NOT-MEASURED — never launched | NOT-MEASURED | NOT-MEASURED |
| pdf_view | OK (`install pdf_view ok`; feed `pdf_view/Utilities`) | NOT-MEASURED — never launched | NOT-MEASURED | NOT-MEASURED |
| falkon | OK (`install falkon ok`; feed `falkon/Internet`) | **RED** — request acked (warning-only stage) but boot stalled at a ROM create: `falkon: create of ROM (session 4) failed - the provider did not answer within 60000 ms` (run 2 additionally shows `label="pkg_runtime -> bin/falkon" ... state=DENIED` + dangling-session errors) | NOT-MEASURED | NOT-MEASURED |
| debug | bake-seeded (package carries `<autostart/>`) | AUTOSTARTED — `sponge_debug: construct ms=354` (run 1) / `ms=...` (run 2) | **Observed (client-side self-report only)**: `sponge_debug: paint-begin ms=471`, `sponge_debug: FIRSTPAINT ms=560` (run 1) / `FIRSTPAINT ms=706` (run 2). This is the app's own "frame handed to the QPA plugin" line — NOT a Capture-side pixel verification of the debug window | NOT-MEASURED |

All eight apps appear in the launcher feed (`sponge-de: launcher list
updated (8 apps)`); alpha_probe asserts 7 of the 8 pairs (debug is the
launch-chain monitor and is not in `LAUNCHER_PAIRS`). Nothing in this
topology launches textedit, calculator, or pdf_view, and nothing feeds
input to any of the eight.

Global scenario-side observations from the same runs (NOT per-app
claims):

- `alpha-probe: PASS` — panel band pixel `0xff1e1e2e` at (512,4)
  (compositor/panel paint), `alpha-probe: (c) configd broadcast carries
  all 8 baked keys`, all 7 launcher pairs present
  (`alpha-probe: (b) launcher report contains all 7 desktop packages`).
- `sponge_pkgd: seeded 9 baked package root(s) from the bake manifest`,
  `sponge_configd: applied 5 baked default(s) from profile 'desktop' @ v1`.
- `sponge-de: panel shown`, `sponge-de: panel and window shown`,
  applying theme `default`; fb driver `fb_boot`: `using boot
  framebuffer: 2560x1600x32 @ 0x80000000`.
- wedge lines: 0, starvation lines: 0 (both runs).

## 4. EARLY-PASS CONDITIONS (known gaps in what "PASS" means)

E1. **The launch gate is opt-in.** Without `SPONGE_LAUNCHTEST=on` the
    scenario prints `launch-gate skipped (SPONGE_LAUNCHTEST=on to
    enable the launch-path test)` and exits 0 — a default-arm PASS says
    nothing about application launch. Prior default-arm receipts are
    not launch evidence.

E2. **`alpha-probe: PASS` OR `sponge_debug: FIRSTPAINT` satisfy the
    compositor gate.** In both baseline runs the debug app's
    CLIENT-SIDE paint self-report (`FIRSTPAINT ms=`) arrived before
    alpha_probe's pixel PASS and is what the gate consumed. A green
    compositor gate can therefore rest on one app's own paint
    announcement, which proves neither the panel pixels nor any other
    app's window.

E3. **files and falkon stages are WARNING-ONLY.** Their request
    markers can never fail the scenario. In this baseline both apps
    subsequently failed to boot and the run still would have printed
    its PASS block if the enforced stage had not failed first.

E4. **Request ack is not first paint.** `prod-launch-probe: launch
    <pkg> -> ok` means `sponge_pkgd: launch result <pkg> -> ok
    (channel=probe)` — pkgd answered the request channel. It says
    nothing about the package booting, painting, or taking input. In
    this baseline all three acked launches (terminal, files, falkon)
    failed at or before boot.

E5. **terminal is enforced only to `pkg_runtime -> terminal -> vfs`**
    (the package sub-init's vfs child), not to the terminal window
    painting and not to input.

E6. **Gate-ordering race (measured 2/2).** The probe's first ack lands
    BEFORE the launch gate starts expecting: the sequential `expect`
    chain consumes it (run 1: eaten while matching `sponge-de diag
    gates:`; run 2: eaten while matching `sponge_debug: FIRSTPAINT`).
    The launch gate's first marker is therefore unreachable on this
    timing — the FAIL text quotes a marker that IS present in the
    serial. This is structural, not the "heavy Qt6 launches" flake
    hypothesis recorded earlier in
    `docs/evidence/fb-driver-packages.md` (that hypothesis is refuted
    by these two runs; the ack is emitted promptly and never seen).

E7. **The scenario does not build `test/prod_launch_probe`.** Unlike
    the env-gated probes in `run/sponge-desktop-disk.run` (which get a
    conditional `build { test/... }` before staging), the uefi-usb
    launch arm only copies `bin/prod_launch_probe` if it already exists
    (silent skip otherwise) and then injects its `<start>` node. Run
    `make -j1 ... test/prod_launch_probe` first (§1) or the arm
    degrades to a guaranteed first-marker timeout.

## 5. EXACT FAILURES OBSERVED (verbatim, 2/2 runs unless noted)

F1. Scenario verdict (make error 1, run tool exit 1):
    `launch-gate FAIL — terminal request (marker: 'prod-launch-probe:
    launch terminal -> ok')`

F2. terminal boot (deterministic 2/2):
    `[init -> rom_sys] Error: /VeraMono.ttf not found`
    `[init -> system -> pkg_runtime -> terminal -> terminal] Error: stop
    because parent denied ROM-session: label="VeraMono.ttf",
    ram_quota=10K, cap_quota=3`
    Consequence: the enforced marker `pkg_runtime -> terminal -> vfs`
    never appears (0 occurrences in either serial).

F3. files boot (deterministic 2/2):
    `Warning: files: create of ROM (session 4) failed - the provider
    did not answer within 60000 ms`
    `Error: destroying session with a live parent-side id: service=ROM
    ... label="pkg_runtime -> bin/sponge_files" ... state=DENIED`
    `Error: dangling session in parent-side ID space: service=ROM ...
    label="pkg_runtime -> bin/sponge_files" ... state=CLOSED`

F4. falkon boot (deterministic 2/2; second form is run 2 only):
    `Warning: falkon: create of ROM (session 4) failed - the provider
    did not answer within 60000 ms`
    `Error: ... label="pkg_runtime -> bin/falkon" ... state=DENIED` +
    matching dangling-session error.

## 6. REPRODUCTION NOTES

- The two baseline runs differ only in which earlier gate consumes the
  probe's first ack (E6); every per-app outcome above reproduced
  identically. First-paint timings vary (`FIRSTPAINT ms=560` vs
  `ms=706`), which is expected.
- To extend the baseline honestly: a per-app row may only move off
  NOT-MEASURED when a probe on THIS topology observes that app's own
  window paint or input. Promoting files/falkon from warning-only to
  enforced requires the F3/F4 boot failures to be fixed first, and
  the terminal row additionally requires the VeraMono.ttf staging fix
  (F2) plus a paint/input probe beyond the vfs boot marker.

================================================================================
