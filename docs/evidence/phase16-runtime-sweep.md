# Phase 16 Runtime Overhaul — P5.3 Full Regression Sweep

Date: 2026-11-04
Tree: HEAD `b19184958b` (clean), genode submodule pin `48a946ba15`
Runner: serial `make -j1 -C genode/build/x86_64`, one runner at a time
(D16R.13). QEMU receipts only — no real-hardware claim (D16R.12).
Host workaround: `LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so` on every
leg except `run/sponge-de-pkgd-failure`, which runs unclamped per
`docs/evidence/phase16-runtime-acceptance.md` §5.10 (the clamp correlates
with that leg's toggle-click failures on this host).

Scope: the full 36-scenario envelope of `docs/evidence/phase16-index.md`
§4 plus every scenario added by this runtime-overhaul phase (7 new run
files + the product arms), plus the adjacent product-family gates the
phase rewired (disk variants, UEFI variants, `run/sponge-pkg-driver`).
Raw per-run transcripts stay in session scratch
(`/tmp/opencode/sweep/`); curated per-scenario receipts are committed as
`docs/evidence/phase16-runtime-sweep-<id>-<scenario>.log` (AGENTS.md
§5.6; the misleading-success defense of `phase16-index.md` §3).

## 1. Verdict

**The phase's own gates are all green**: 7/7 phase-added scenarios and
4/4 product arms (`SPONGE_GROUPED_UI=on`, `SPONGE_APP_ISOLATION=on`,
`SPONGE_SUPERVISOR_TEST=contain`, default arm) pass on fresh runs.
The sweep's regression value landed where it was supposed to: the older
envelope exposes **6 red scenarios** (5 deterministic at HEAD + 1
deterministic run-script source error), **3 PARTIAL rows** (two carried
honest deviations + one axis split), and **26 fully green rows**.

| Section | Green | PARTIAL | Red |
|---|---|---|---|
| §3 envelope (36 rows / 35 scenarios) | 26 rows | 3 rows | 7 rows (6 scenarios + 1 duplicate row) |
| §4 phase-added scenarios (7) | 7 | 0 | 0 |
| §5 product arms (4) | 4 | 0 | 0 |
| §5 adjacent family (5) | 3 (+2 structure-only, not counted green) | 0 | 0 |
| §6 linux-axis extras | 4 green + 1 blocker receipt | 1 (launch axis) | — |

Post-fix-wave status (2026-11-05): the §3 envelope now stands at 32
green / 4 PARTIAL / 0 red — the two fix waves greened rows 4/31/30 and
11/22/32 on fresh receipts, and row 35 is PARTIAL with the F7 pointer.
The table above remains the sweep-at-`b19184958b` verdict (§3 totals,
§7, §11 carry the re-gate state).

No gate was deleted or weakened; every red row carries a follow-up
pointer (§7). No product or run-script code was modified during the
sweep (the §7 fix recipes are recorded, not applied).

## 2. Exact runner commands

All scenarios ran through the one runner; the per-row command is in each
receipt log line 2. Templates:

```sh
# sel4 / iso (default RUN_OPT) — rows e01..e36 except the disk legs
timeout <T>s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/<scenario> KERNEL=sel4 BOARD=pc

# sel4 / disk legs (D16R.1 media line) — a01..a04, x01..x04,
# sponge-desktop-defaults-firstboot, sponge-provider-stall
timeout <T>s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/<scenario> KERNEL=sel4 BOARD=pc \
  RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"

# product arms (in addition to the disk leg line)
env SPONGE_SUPERVISOR_TEST=contain ... run/sponge-desktop-disk ...
env SPONGE_GROUPED_UI=on          ... run/sponge-desktop-disk ...
env SPONGE_APP_ISOLATION=on       ... run/sponge-desktop-disk ...

# the documented unclamped exception (acceptance §5.10)
timeout 900s make -j1 -C genode/build/x86_64 run/sponge-de-pkgd-failure KERNEL=sel4 BOARD=pc

# linux axis (BOARD=linux is the correct base-linux board — §6)
timeout <T>s env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/<scenario> KERNEL=linux BOARD=linux
```

Per-scenario timeouts: 360 s (configd probes), 420–600 s (DE/QMP
scenarios), 900 s (per-package + phase scenarios + resize), 1200 s
(alpha / firstboot / UEFI), 1500 s (grouped-UI + app-isolation arms).
Flake policy: a failing gate is re-run at most once more with the
attempt count recorded (no third attempts were needed; the 2/2 reds
below are already deterministic). Nothing was retried until green and
reported as green without the attempt history.

## 3. Envelope matrix (phase16-index.md §4 rows 1–36)

KERNEL column = the axis run in this sweep. "Attempts" = green/total.

| # | Scenario | KERNEL | Attempts | Result | Marker (verbatim) | Receipt |
|---|---|---|---|---|---|---|
| 1 | sponge-configd-pattern-keys | sel4 | 1/1 | **PASS** | `pattern-keys-probe: PASS` | `phase16-runtime-sweep-e01-sponge-configd-pattern-keys.log` |
| 2 | sponge-configd-shortcuts | sel4 | 1/1 | **PASS** | `shortcuts-probe: PASS` | `...-e02-sponge-configd-shortcuts.log` |
| 3 | sponge-configd-badkey | sel4 | 1/1 | **PASS** | `badkey-probe: PASS` | `...-e03-sponge-configd-badkey.log` |
| 4 | sponge-desktop-defaults-firstboot | sel4 | 0/2 sweep, 1/1 fix re-gate | **GREEN** (fix wave 2026-11-04, §7 F1; was FAIL red F1: `alpha-probe: FAIL themed panel never composited on nitpicker`) | `alpha-probe: PASS` + `sponge-desktop-defaults-firstboot: PASS — first boot landed the 8 baked keys + 7 launcher pairs + themed panel` | `...-e04-sponge-desktop-defaults-firstboot.log` + `...-e30a2-...` sibling receipt + `phase16-runtime-f1-e04-sponge-desktop-defaults-firstboot.log` |
| 5 | sponge-de-settings | sel4 + linux | 1/1 + 1/1 | **PASS** (both axes) | `settings-probe: PASS` | `...-e05-sponge-de-settings.log`, `...-l03-sponge-de-settings.log` |
| 6 | sponge-de-settings-regression | sel4 | 1/1 | **PASS** | `settings-regression-probe: PASS` | `...-e06-...` |
| 7 | sponge-panel-menu | sel4 | 1/1 | **PASS** | `panel-menu-probe: PASS` | `...-e07-sponge-panel-menu.log` |
| 8 | sponge-de-bgmenu | sel4 | 1/1 (gate) | **PARTIAL (carried)** | `bgmenu-probe: PASS` (structural `open="ready"`) | `...-e08-sponge-de-bgmenu.log` |
| 9 | sponge-de-bgimage | sel4 | 1/1 | **PASS** | `bgimage-probe: PASS` | `...-e09-...` |
| 10 | sponge-de-bgimage-badpath | sel4 | 1/1 | **PASS** | `bgimage-badpath-probe: PASS` | `...-e10-...` |
| 11 | sponge-de-shortcuts | sel4 | 0/2 sweep, 1/1 fix re-gate | **GREEN** (fix wave 2026-11-05 after the outer report_rom `verbose: yes -> no`, §7 F2; was FAIL red F2: probe stuck pre-choreography + report_rom `illegal READ at address 0x1d6000 ... ip=0x100b7d1`) | `shortcuts-probe: PASS` | `...-e11-sponge-de-shortcuts.log`, `...-e11a2-...` + `phase16-runtime-f2-e11-sponge-de-shortcuts.log` |
| 12 | sponge-de-shortcuts-extend | sel4 | 1/1 | **PASS** | `shortcuts-extend-probe: PASS` | `...-e12-...` |
| 13 | sponge-de-multipanel | sel4 | 1/1 | **PASS** | `multipanel-probe: PASS` | `...-e13-...` |
| 14 | sponge-de-multipanel-idspace | sel4 | 1/1 | **PASS** | `pattern-keys-probe: PASS` (gate at HEAD; see §8 errata) | `...-e14-...` |
| 15 | sponge-de-themed-chrome-resize | sel4 | 1/1 fresh | **PARTIAL (carried)** | `release-resize-probe: PASS` (full sequence green this run; W9 sibling flakiness stays the status) | `...-e15-sponge-de-themed-chrome-resize.log` |
| 16 | sponge-de-release-resize | sel4 | 1/1 | **PASS** | `release-resize-probe: PASS` (criterion-9 headline) | `...-e16-sponge-de-release-resize.log` |
| 17 | sponge-minimal | sel4 + linux | 1/1 + 1/1 | **PASS** (both axes) | `Run script execution successful.` + vct banner | `...-e17-...`, `...-l01-...` |
| 18 | sponge-de-test | sel4 + linux | 1/1 + 1/1 | **PASS** (both axes) | `sponge-de-probe: PASS` | `...-e18-...`, `...-l02-...` |
| 19 | sponge-de-sel4-interactive | sel4 | 1/2 | **PASS (flake recorded)** | `sponge-de-probe: PASS` (3 phases) | `...-e19-...` (attempt 1 red), `...-e19a2-...` (attempt 2 green) |
| 20 | sponge-wm-qmp | sel4 | 1/1 | **PASS** | `wm-probe: PASS` | `...-e20-...` |
| 21 | sponge-launch | sel4 + linux | 1/1 + 0/2 | **PARTIAL (axis split, F3)** | sel4 `launch-probe: PASS`; linux `launch-probe: FAIL demo window green pixel never appeared after click launch` | `...-e21-...`, `...-l04-...`, `...-l04a2-...` |
| 22 | sponge-wm-tasks | sel4 | 0/2 sweep, 1/1 fix re-gate | **GREEN** (fix wave 2026-11-05 after drivers caps 1500 -> 1800 in `run/sponge-wm-tasks.run`, §7 F4; was FAIL red F4: `wm-tasks-probe: FAIL minimize transition: pkg_gui_demo did not reach off-screen position within 60s`) | `wm-tasks-probe: PASS`, `Run script execution successful.` | `...-e22-...`, `...-e22a2-...` + `phase16-runtime-f4-e22-sponge-wm-tasks.log` |
| 23 | sponge-clipboard-qtsettext | sel4 | 1/1 | **PASS** | `clipboard-probe: PASS` | `...-e23-...` |
| 24 | sponge-notify | sel4 | 1/1 | **PASS** | `notify-probe: PASS` | `...-e24-...` |
| 25 | sponge-configd-persist | sel4 | 1/1 | **PASS** | `configd-persist-probe: PASS` | `...-e25-...` |
| 26 | sponge-panel-config-sel4 | sel4 | 1/1 | **PASS** | `sponge-de-probe: phase panel-config PASS` | `...-e26-...` |
| 27 | sponge-de-themed-chrome | sel4 | 1/1 | **PASS** | `wm-probe: PASS` | `...-e27-...` |
| 28 | sponge-bake-firstboot | linux | 1/1 | **PASS** | `bake-firstboot-probe: PASS boot1/boot2` + `sponge-bake-firstboot: PASS` | `...-l05-sponge-bake-firstboot.log` |
| 29 | sponge-bake-reset | sel4 | 1/1 | **PASS** | `bake-reset-probe: PASS` (see §8 errata on the W12 kernel column) | `...-e29-sponge-bake-reset.log` |
| 30 | sponge-alpha | sel4 | 0/2 sweep, 1/1 fix re-gate | **GREEN** (fix wave 2026-11-04, §7 F1; was FAIL red F1: `alpha-probe: FAIL themed panel never composited on nitpicker`) | `alpha-probe: PASS` (all four criteria incl. the Leitzentrale window) | `...-e30-sponge-alpha.log`, `...-e30a2-...` + `phase16-runtime-f1-e30-sponge-alpha.log` |
| 31 | sponge-desktop-defaults-firstboot (duplicate of row 4) | sel4 | — | **GREEN** (same as row 4) | same as row 4 | same receipt |
| 32 | sponge-textedit | sel4 | 0/2 sweep, 1/1 fix re-gate | **GREEN** (fix wave 2026-11-05 after the guarded clipboard strip in `run/sponge-textedit.run`, §7 F5; was FAIL red F5: `textedit-probe: FAIL textedit window never rendered into nitpicker`) | `textedit-probe: PASS`, `textedit: clipboard disabled in app and package configs` | `...-e32-...`, `...-e32a2-...` + `phase16-runtime-f5-e32-sponge-textedit.log` |
| 33 | sponge-files | sel4 | 1/1 | **PASS** | `files-probe: PASS` | `...-e33-...` |
| 34 | sponge-calculator | sel4 | 1/1 | **PASS** | `calculator-probe: PASS` | `...-e34-...` |
| 35 | sponge-terminal | sel4 | n/a sweep (source error), 0/1 fix re-gate | **PARTIAL — not green** (the F6 braced fix landed 2026-11-04; the F7 first-bash-fork `ls -l /bin` wedge stands and the run still ends `Error: Test execution timed out`) | probe steps [1]-[5] green; step (6) `terminal-probe: FAIL ls -l /bin did not render a listing` | `...-e35-sponge-terminal.log` + `phase16-runtime-f6-e35-sponge-terminal.log`, `phase16-runtime-f7-e35-sponge-terminal.log` |
| 36 | sponge-hw-matrix | sel4 | 1/1 | **PASS** | `sponge-hw-matrix: PASS (variant=q35 ...)` | `...-e36-sponge-hw-matrix.log` |

Envelope totals (the sweep at `b19184958b`): **26 rows PASS, 3 rows
PARTIAL (8, 15, 21), 7 rows FAIL (4, 11, 22, 30, 31, 32, 35)** — 31 is
a duplicate citation of 4. 0 rows were greened by weakening; every red
keeps its gate intact.

Envelope totals (post-fix-wave re-gates, 2026-11-05; the row cells
above carry the re-gate outcomes): **32 rows GREEN, 4 rows PARTIAL
(8, 15, 21, 35), 0 rows red.** Rows 4/31/30 were greened by the
2026-11-04 fix wave and rows 11/22/32 by the 2026-11-05 fix wave, all
on fresh receipts with the gates unmodified; row 35 is **PARTIAL, not
green** (the F6 braced fix landed; the F7 first-bash-fork `ls -l /bin`
wedge stands, the run ends `Error: Test execution timed out`). Every
partial is documented with its follow-up pointer (§7 / §11).

## 4. Phase-added scenarios (runtime overhaul)

| id | Scenario | Result | Marker (verbatim) | Receipt |
|---|---|---|---|---|
| p01 | sponge-config-authority | **PASS** (1/1) | `config-authority-probe: PASS` | `phase16-runtime-sweep-p01-sponge-config-authority.log` |
| p02 | sponge-config-emission-invalid | **PASS** (1/1) | `config-emission-invalid-probe: PASS (structured rejection, nothing published)` | `...-p02-...` |
| p03 | sponge-pkg-grouped | **PASS** (1/1) | `grouped-runtime-probe: PASS (grouped emission + per-class admission)` | `...-p03-...` |
| p04 | sponge-pkg-exclusive | **PASS** (1/1) | `sponge-pkg-exclusive: PASS` (+ `exclusivity-replay: DONE byte-identical (225 bytes)`) | `...-p04-...` |
| p05 | sponge-pkg-dispatch-liveness | **PASS** (1/1) | `dispatch-liveness: PASS` | `...-p05-...` |
| p06 | sponge-provider-stall | **PASS** (1/1) | `provider-stall: PASS` (disk leg, clamped) | `...-p06-sponge-provider-stall.log` |
| p07 | sponge-de-pkgd-failure | **PASS** (1/1) | `de-survival-probe: scenario GREEN (P2.5 mechanisms present)` (unclamped leg) | `...-p07-sponge-de-pkgd-failure.log` |

These scenarios print their own verdict line and exit 0 (no
`Run script execution successful.` epilogue by design); the receipt
closes on the verdict line.

## 5. Product arms and adjacent family

| id | Scenario / arm | Result | Marker | Receipt |
|---|---|---|---|---|
| a01 | sponge-desktop-disk (default arm) | **PASS** (1/1) | `alpha-probe: PASS` | `...-a01-sponge-desktop-disk.log` |
| a02 | + `SPONGE_SUPERVISOR_TEST=contain` | **PASS** (1/1) | `SUPERVISOR CONTAIN PASS` | `...-a02-...` |
| a03 | + `SPONGE_GROUPED_UI=on` | **PASS** (1/1) | `grouped-ui-probe: PASS` | `...-a03-...` |
| a04 | + `SPONGE_APP_ISOLATION=on` | **PASS** (1/1) | `app-isolation-probe: PASS` | `...-a04-...` |
| x01 | sponge-desktop-disk-nvme | **PASS** (1/1) | `alpha-probe: PASS` | `...-x01-...` |
| x02 | sponge-desktop-disk-uefi | **structure-only gate** (exit 0) | `UEFI product media structure verified; QEMU boot blocked by the known UEFI/QEMU W^X + bender NOP gap` (verbatim run banner — stale text from a banner-classifier bug, B2; not a boot-block claim) | `...-x02-...` |
| x03 | sponge-desktop-disk-uefi-nvme | **structure-only gate** (exit 0) | same banner (same classifier bug, B2) | `...-x03-...` |
| x04 | sponge-desktop-disk-uefi-usb | **PASS** (1/1) | `alpha-probe: PASS` (the QEMU-verified UEFI product path) | `...-x04-...` |
| x05 | sponge-pkg-driver | **PASS on install/DHCP only** (see B3) | `driver-probe: install answered status=ok`, `driver_runtime lists child 'pc_nic'`, `driver-probe: PASS` | `...-x05-sponge-pkg-driver.log` |

x02/x03 are NOT green boot claims — they are the D15.16 structure-only
QEMU gates (B2): runs that do not exercise the desktop gate
(`alpha-probe`), not boot blockers. Their printed gap banner is stale
and is itself the banner-classifier bug recorded in B2 (both receipts
record `UNEXPECTED — QEMU reached the Genode v... banner` before the
fallback misfires). x05's final `sponge-pkg-driver: PASS (...)` string
claims fb exclusivity; that half is unproven at HEAD (B3) and no
exclusivity or real-hardware claim is taken from it.

## 6. Linux axis (base-linux) and the get_page_size blocker

| id | Scenario | Result | Notes |
|---|---|---|---|
| l01 | sponge-minimal (linux/linux) | **PASS** (1/1) | fresh build + boot: `vct (0.2.0 / Archaeocyte) starting`, `Run script execution successful.` |
| l02 | sponge-de-test (linux/linux) | **PASS** (1/1) | `sponge-de-probe: PASS` |
| l03 | sponge-de-settings (linux/linux) | **PASS** (1/1) | `settings-probe: PASS` (the W12 fast-path axis, refreshed) |
| l04/l04a2 | sponge-launch (linux/linux) | **FAIL 0/2** (F3) | `launch-probe: FAIL demo window green pixel never appeared after click launch` (the sel4 axis is green, e21) |
| l05 | sponge-bake-firstboot (linux/linux) | **PASS** (1/1) | the scenario's only axis (`have_spec linux`) — row 28 refreshed green |
| l06 | sponge-minimal (linux/**pc**) | blocker receipt (F/B1) | not a gate: the documented broken combo, characterized below |

**B1 — base-linux `get_page_size` failure, precisely recorded.** The
hazard is `genode/repos/base-linux/src/core/spec/pc/io_mem_session_
component.cc:31`, which calls the undefined `get_page_size()`:

```
error: 'get_page_size' was not declared in this scope; did you mean 'getpagesize'?
   31 |         base = base & ~(get_page_size() - 1);
```

(verbatim from the characterization build `make -j1 core KERNEL=linux
BOARD=pc` after touching that source; the spec/pc source set is pulled
in exactly when `BOARD=pc` is active for the base-linux core.) The
correct base-linux axis is `BOARD=linux`, where the spec/pc source is
not compiled and everything builds and runs (l01–l05 receipts above).
The `KERNEL=linux BOARD=pc` combo additionally dies at the run-tool
level even from a warm cache (`build_initrd: cp: 'genode/initramfs'`
missing — the class `docs/evidence/containment-churn.md` §9 already
named), receipt `...-l06-sponge-minimal.log`. This supersedes the
P3.3 phrasing "base-linux is unbuildable in this environment": at the
current pin, base-linux is buildable and green on `BOARD=linux`; the
blocker is the pc-board combo and the latent spec/pc compile hazard.

## 7. Red rows — diagnosis and follow-ups

None of the following was touched product-side **at sweep time**; each
fix recipe was recorded, not applied (the fix-wave notes under F1, F2,
F4, F5, and F6 record what has since landed and re-gated).

**F1 — `sponge-desktop-defaults-firstboot` + `sponge-alpha` (rows 4/31,
30; 0/2 each, deterministic).** Both scenarios still stage the
pre-overhaul topology template while the product code at HEAD expects
the P2.4 grouped-runtime + P3.4 per-child `state` model. Verbatim from
the fresh logs: `sponge_pkgd: <runtime_class>'terminal' of terminal
requires <grouped_runtimes/> — placing it in the legacy app runtime`,
`Error: pkg_runtime: ambiguous routes to service "Report"`,
`Warning: pkg_runtime: no route to service "Report" (label="pkg_runtime
-> state")`, `Error: stop because parent denied Report-session:
label="state"`, `Error: sponge-de: ambiguous routes to service
"Report"`, `no route ... (label="sponge-de -> de_op_error")`. The
staged configs still carry `pkg_runtime -> config | report:
sponge_pkgd -> runtime` and `layouter -> rules | report: layout_rules`
where `run/sponge-desktop-disk` (green, a01) carries the
`pkg_runtime_{simple,terminal,browser}` policies, per-child
`*_state` policies, and `layouter -> rules | report: sponge-de -> rules`.
With pkg_runtime killed at start, the panel never composites
(`alpha-probe: FAIL themed panel never composited on nitpicker`).
Follow-up: migrate the two run-script config templates to the
`run/sponge-desktop-disk.run` route model (grouped runtime classes +
heartbeat `state` policies + the `de_op_error` / `launcher_request` /
`window_list` / `window_layout` / `rules` / `focus_request` channel
set), then re-gate rows 4/31 and 30. Run-config change — needs
discussion before landing.

**F1 RESOLVED (2026-11-04 fix wave; re-gated green).** The recipe
above was applied verbatim via the `run/runtime-grouped.inc` port
checklist: the three `pkg_runtime_{simple,terminal,browser}` nodes
with the `run/runtime-quota.inc` class-row grant literals
(`runtime_quota::verify 6000 1488M 2000 247M 220000 512G`), the
`@GROUPED_RELAY_POLICIES@` / `@GROUPED_ADMISSION_NODES@` /
`@GROUPED_PKGD_STATE_ROUTES@` fragments, the six-channel set on
sponge-de's route and the report_rom relays, and `layouter -> rules |
report: sponge-de -> rules` + `layouter -> focus_request | report:
sponge-de -> focus_request` re-pointing. One derived change rode
along and is disclosed in the run headers: both QEMU envelopes grew
2G -> 4G in lockstep with the class grants (the same move the product
media made from 4G to 6G, `docs/evidence/remainder-clamp-ram.md`) —
at 2G the grants oversubscribed the pool, `pkg_runtime_terminal` /
`pkg_runtime_browser` died at "out of RAM during ELF loading", and on
alpha the top-level init (no clock on base-sel4) wedged its start
pass entirely (no child booted; row 30 could not be greened at 2G at
all). Re-gate receipts (this fix wave, clamped, serial): row 4/31
**PASS** — `alpha-probe: PASS` +
`sponge-desktop-defaults-firstboot: PASS — first boot landed the 8
baked keys + 7 launcher pairs + themed panel`,
`docs/evidence/phase16-runtime-f1-e04-sponge-desktop-defaults-
firstboot.log`; row 30 **PASS** (all four criteria incl. the
Leitzentrale window) — `alpha-probe: PASS`,
`docs/evidence/phase16-runtime-f1-e30-sponge-alpha.log`.

**F2 — `sponge-de-shortcuts` (row 11; 0/2, identical crashes).**
Attempt 1 and attempt 2 fail identically: the probe waits pre-
choreography (no `QMP-TARGET` marker within 120 s) while report_rom
logs a flood of empty (`\r\r`) report payloads right after the
`sponge-de -> event_filter_config` report, then dies with
`Error: illegal READ at address 0x1d6000 by pager_object:
pd='init -> report_rom' thread='ep' ip=0x100b7d1` (same address and
IP both times). `sponge-de-shortcuts-extend` (row 12) is green on the
same topology family, so the failure is specific to the default-
bindings phase's report traffic. The stuck-probe shape is a candidate
for W7 finding 2 (report_rom sessions opened before the first write),
and the empty-report flood is a candidate for W7 finding 1 (the
event_filter `<report>` source). Follow-up: reproduce with report_rom
logging widened, harden report_rom's empty-report path and/or
event_filter's empty-report emission, and pair it with the Phase 17+
input-frame work (`docs/evidence/phase16-w7-shortcuts.md`).

**F2 RESOLVED for row 11 (2026-11-05 fix wave; re-gated green).** The
trigger class was the outer report_rom's `verbose: yes` in
`run/sponge-de-shortcuts.run`: with verbose printing off (`verbose:
no`) the empty-report traffic no longer reaches the crashing print
path, and neither F2 marker (the empty-report flood nor the
`illegal READ at address 0x1d6000 ... ip=0x100b7d1` pager crash)
reproduced. The row re-gates green on the first attempt —
`shortcuts-probe: PASS` + `Run script execution successful.` — receipt
`docs/evidence/phase16-runtime-f2-e11-sponge-de-shortcuts.log`. The
deeper W7 findings 1/2 hardening (event_filter's empty-report emission
and the late-created report_rom sessions) stays registered for the
Phase 17+ input-frame work: the row is green, the hardening pairing is
not closed.

**F3 — `sponge-launch` linux axis (row 21; 0/2 on linux, sel4 green).**
`launch-probe: FAIL demo window green pixel never appeared after click
launch` (`center(512,320)=0xff313244 bg?1 demo?0` through the full
poll budget) on base-linux; the same scenario is green on sel4 (e21,
`window shown` + green pixel). Class: post-launch window paint on the
linux axis. Follow-up: linux-axis paint diagnosis (pairs with F5).

**F4 — `sponge-wm-tasks` (row 22; 0/2, deterministic).** Both attempts
complete steps 0–3 (`window_layout: pkg_gui_demo at (50,320)`, tasklist
entry live) and then time out at the minimize transition with **zero**
`layout_rules` / `window_layout` report activity after the click
window. Ruled out already: the rules wiring is correct at HEAD
(`policy label="layouter -> rules" report="sponge-de -> rules"` in the
staged config; the controller publishes a `rules` Report); the staged
event_filter.config has no `<accelerate>` (the 1:1 PS/2 walk
assumption in `qmp_click_tasklist` is valid); the layouter boundary
origin is (0,0), so the known parked-sentinel product bug
(acceptance §5.8) cannot shift the parked rect on this topology. The
`flush page table entries - mapping cache full - PD: init -> drivers ->
fb out of CAP` warning fires in the click window both attempts (the
same fb-PD capability-exhaustion class as F1's storm, §11.1 lineage).
Follow-up: witness whether the click lands on the tasklist entry (the
W2b pixel-witness method), add a controller-side rules-publish marker,
and revisit per-PD caps sizing for the vesa_fb Tier-0 topologies.
This is beyond the documented Phase 14 W12 one-retry cluster — 2/2 red.

**F4 RESOLVED (2026-11-05 fix wave; re-gated green).** The recipe's
per-PD caps sizing item landed: the `start drivers` node in
`run/sponge-wm-tasks.run` grew caps 1500 -> 1800. The fb-PD
`flush page table entries - mapping cache full ... out of CAP` warnings
no longer starve the click window, the minimize transition completes
(`window_layout: pkg_gui_demo parked at (-32000, -32000)`), and the
full W7 acceptance sequence re-gates green — `wm-tasks-probe: PASS` +
`Run script execution successful.` — receipt
`docs/evidence/phase16-runtime-f4-e22-sponge-wm-tasks.log`. The
sweep's failure marker (`wm-tasks-probe: FAIL minimize transition:
pkg_gui_demo did not reach off-screen position within 60s`) did not
reproduce; the W2b pixel-witness method stays available if the class
recurs.

**F5 — `sponge-textedit` (row 32; 0/2, deterministic).** The launch
chain is fully green in-run (`install ok`, `launch result textedit ->
ok`, the Qt child boots) but the window never renders into nitpicker
(`capture poll ... rendered_frac=0% color_buckets=1` to the probe's
budget, `textedit-probe: FAIL textedit window never rendered into
nitpicker`). Zero `out of CAP` / `null cap` lines (so NOT the F1/F4
capability class). The sibling per-package scenarios pass on the same
shape (e33 files, e34 calculator), and textedit paints on the grouped
arm (a03). Follow-up: compare the standalone topology's Gui-session /
window-lifecycle wiring against the grouped topology for textedit;
the QGenodeScreen 1×1 race (Phase 14 W11 row #46, re-scoped Phase 17+)
is the standing candidate.

**F5 RESOLVED (2026-11-05 fix wave; re-gated green).** The standalone
topology has no clipboard provider: the staged configs advertised
`clipboard="yes"` anyway, and the window never rendered. The fix is a
guarded clipboard strip in `run/sponge-textedit.run` (mirroring
`run/runtime-simple.inc`'s transform: require exactly one anchor per
file, then strip — loud error on zero or duplicate anchors), applied
to both the application config and the package metadata `sponge_pkgd`
consumes, printing `textedit: clipboard disabled in app and package
configs`. Re-gate green: `textedit-probe: PASS` (window detected at
100% non-bg / 44 color buckets; the not-installed and already-running
legs included) + `Run script execution successful.` — receipt
`docs/evidence/phase16-runtime-f5-e32-sponge-textedit.log`. The
standing candidates (the QGenodeScreen 1×1 race, the Gui-session
wiring comparison) were not the cause on this topology.

**F6 — `sponge-terminal` (row 35; deterministic source-time error).**
`run/sponge-terminal.run:55-58` passes `{*}$terminal_runtime::
extra_packages` **inside a braced** `build { ... }` argument list;
Tcl never expands `{*}` inside braces, so `build`'s `llength` parse
fails with `list element in braces followed by "$terminal_runtime::e"
instead of space` before QEMU is even spawned (exit 2, 0 s). Verified
locally on Tcl 8.6 (tclsh and expect). The line dates from
`9cd888fd9e` (2026-08-14); the W12 row-35 PASS lineage is therefore
inconsistent with the current file content (see §8). Fix recipe (NOT
applied — run-script change, needs discussion): `build [concat {core
lib/ld init timer server/report_rom server/nitpicker sponge_pkgd}
$terminal_runtime::extra_packages $terminal_runtime::extra_libs]`.

**F6 FIX APPLIED (2026-11-04 fix wave; row 35 re-gated red one gate
deeper).** The concat recipe landed together with the vct staging the
scenario also needed to run at all: the emitted vfs config mounts
`<tar name="vct-bin.tar"/>` (pkg/terminal/metadata.xml), so the run
now calls the shared `runtime_terminal::produce_vct_bin` producer
(`run/runtime-terminal.inc`, extracted from `stage_payload` so the
product media and the terminal scenarios keep ONE producer), stages
the tar as a boot module, and calls `runtime_terminal::
strip_vct_bridge` (no vct_tty provider in this topology — same as the
product media). Re-gate receipt
(`docs/evidence/phase16-runtime-f6-e35-sponge-terminal.log`): the
source error is gone (the guest boots; the pre-fix state was exit 2
in 0 s), probe steps [1]-[5] are green (install / installed
broadcast / launch / window detect 302 glyphs / keystroke echo
302 -> 584), and the row stays **RED** one gate deeper: step (6) —
the Phase-13 CLI-toolset assertion that types `ls -l /bin` — wedges
at the FIRST bash fork (the terminal stops rendering mid-step and
`terminal-probe: FAIL ls -l /bin did not render a listing` fires
deterministically). Class: the libc fork child's startup on the
sandboxed topology (product-side; recorded as follow-up **F7**). The
braced-expansion defect this row named is resolved; the row is NOT
greened (gate intact).

**F6/F7 RE-GATE (2026-11-05 fix wave; row 35 PARTIAL — not green).**
A fresh run on the same fixed tree reproduces the 2026-11-04 re-gate
exactly: probe steps [1]-[5] green, step (6) (`ls -l /bin`, the first
bash fork) wedges the terminal mid-step, `terminal-probe: FAIL ls -l
/bin did not render a listing` fires deterministically, and the run
ends `Error: Test execution timed out` (make exit 254). The F6
braced-expansion fix stands; the F7 fork-wedge stands with its
follow-up pointer to the D16R.17 session-machinery/fork family. The
row is recorded **PARTIAL** and must not be reported as green (the
`terminal-probe: PASS` gate is unweakened and unreached). Receipt
`docs/evidence/phase16-runtime-f7-e35-sponge-terminal.log`.

## 8. Known pre-existing blockers and honest disclosures (recorded, not greened)

1. **B1 — base-linux `get_page_size`**: §6 above (precise record +
   verbatim error + the BOARD=linux green axis). No base-linux row was
   silently skipped or converted; the broken combo is characterized,
   the working axis is refreshed green.
2. **B2 — UEFI structure-only variants (D15.16)**: `sponge-desktop-
   disk-uefi` and `-uefi-nvme` exit 0 on the structural assertions and
   print their gap banner; they are structure gates that do not
   exercise the desktop gate (`alpha-probe`), so no desktop PASS is
   claimed from them — that gate-scope limit is the D15.16 limitation,
   not a boot block. The banner's stated cause ("QEMU boot blocked by
   the known UEFI/QEMU W^X + bender NOP gap") is retired: the W^X/
   bender-NOP cause is superseded by `docs/11-environment.md` §4
   ledger row 10 (principled bender `check_mem` rebuild, replacing the
   W1 NOP) + row 11 (seL4 kernel reserved-extent export + core-side
   artifact detection), and QEMU/OVMF boots reach Genode at the
   current pin. The banner text is itself a **banner-classifier bug**,
   not proof of a boot blocker: the run's `Genode v` regexp cannot
   match the current `Genode 26.08-38-…` banner, so the fallback
   prints the stale gap line even though both receipts record
   `UNEXPECTED — QEMU reached the Genode v... banner` from the expect
   arm. The QEMU UEFI desktop claim is `sponge-desktop-disk-uefi-usb`
   (x04, fresh green, `alpha-probe: PASS`). Real-hardware UEFI is
   Phase 15 15-3 territory and is never claimed here (D16R.12).
3. **B3 — pkg-driver fb-probe policy gap**: x05's fresh log shows both
   fb probes dying at start on a report_rom policy denial — verbatim
   `Error: stop because parent denied ROM-session: label="result"` from
   `[init -> system -> fb_conflict_probe]` and
   `[init -> system -> fb_lock_probe]` (no report_rom policy serves a
   `result` ROM to either probe) — while the install/DHCP gate is
   green. The scenario's final PASS string still
   claims "a second fb driver was REFUSED as exclusive-conflict"; that
   half is unconditional and unproven. **No fb-exclusivity and no
   real-hardware claim is taken from `run/sponge-pkg-driver`.**
   Exclusivity evidence is `run/sponge-pkg-exclusive` (p04 green,
   including the byte-identical duplicate-replay receipt).
4. **B4 — documented timing flakes**: W6 deviation 1 (row 8: the
   structural `open="ready"` gate is green; the per-event right-click
   acceptance stays open, `docs/evidence/phase16-w6-bgmenu-followup.md`);
   W9 sibling flakiness (row 15: the full 8-zone sequence passed fresh
   1/1 this sweep, but the recorded intermittent stray-maximize class
   stands as the row status); the Phase 14 W12 wm-tasks one-retry
   cluster (now exceeded — F4); the provider-stall host-I/O flake
   (p06 passed 1/1 this sweep; the class stays documented); the
   timer-quota deferral class (not triggered this sweep); the P5.1
   tablet click classes (row 19 attempt 1 hit the popup entry-click
   delivery class, attempt 2 green).

## 9. Errata against the W12 table (phase16-index.md §4)

Recorded honestly rather than copied forward:

1. Row 14's W12 marker `pattern-keys-probe: idspace = PASS` is stale;
   the gate at HEAD is `pattern-keys-probe: PASS`
   (`run/sponge-de-multipanel-idspace.run` gates on the W2 probe's
   pattern-keys phase PASS).
2. Row 29's W12 KERNEL column says linux; `run/sponge-bake-reset.run`
   has asserted `have_spec sel4` since it landed (`4226e05345`). This
   sweep ran it sel4 (green).
3. Row 35's W12 PASS is inconsistent with the current
   `run/sponge-terminal.run` content under Tcl 8.6 (F6): the braced
   `{*}` build call has been unloadable since `9cd888fd9e`
   (2026-08-14). The receipt lineage of that row is an open question;
   this sweep's receipt is the first verifiable one for the current
   file.

## 10. Environment and patch ledger

- `./tool/build verify` (run once at HEAD): **OK** — `tool/patches
  verify` (all 33 ledger rows at pin `48a946ba15`) then `tool/hw_compat
  assert` (`4 verified, 1 smoke-only, 12 gap — all rules pass`).
- Genode submodule: clean at `48a946ba15` (26.08-38), no dirty
  vendored state at sweep end (the l06 characterization only touched a
  source mtime; git status stays clean).
- All results are QEMU-labeled (D16R.12). No real-hardware, fb-
  exclusivity, or UEFI-boot claim is made beyond the receipts cited.

## 11. Follow-up register (Phase 17+ / pre-close-out)

| # | Item | Target | Pointer |
|---|---|---|---|
| F1 | Migrate `sponge-desktop-defaults-firstboot.run` + `sponge-alpha.run` config templates to the grouped-runtime + heartbeat-route model (rows 4/31, 30 red) | **RESOLVED 2026-11-04** (re-gated green, both rows PASS; envelopes grew 2G->4G in lockstep) | §7 F1 |
| F2 | report_rom empty-report flood + `illegal READ 0x1d6000` on `sponge-de-shortcuts` (row 11 red) | **RESOLVED for row 11 (2026-11-05)** (re-gated green after the outer report_rom `verbose: yes -> no`; the W7 findings 1/2 hardening pairing stays for Phase 17+ input-frame) | §7 F2 |
| F3 | `sponge-launch` linux-axis post-click paint (row 21 axis split) | next sweep / Phase 17+ | §7 F3 |
| F4 | `sponge-wm-tasks` minimize-click path + fb-PD caps sizing (row 22 red) | **RESOLVED 2026-11-05** (drivers caps 1500 -> 1800 in `run/sponge-wm-tasks.run`; re-gated green) | §7 F4 |
| F5 | `sponge-textedit` standalone first-paint (row 32 red) | **RESOLVED 2026-11-05** (guarded clipboard strip in `run/sponge-textedit.run`; re-gated green) | §7 F5 |
| F6 | `run/sponge-terminal.run` braced `{*}` build call (row 35 red) | **FIX APPLIED 2026-11-04** (row 35 PARTIAL on the 2026-11-05 re-gate, not green; see F7) | §7 F6 |
| F7 | libc fork child startup wedge on the sandboxed topology: `sponge-terminal` probe step (6) (`ls -l /bin` via the first bash fork) wedges the terminal mid-step — terminal i/o freezes (no echo, frozen glyph bbox), deterministic. Data on file: the fork's child construction completes through its File_system session then stalls before the clone destruct (`fork.cc`/`kernel.cc` path); bash blocks in `fork()` and stops reading the vfs terminal ring. NOT a quota cliff at the bash node (caps+ram bump 450/64M -> 1200/192M reproduced identically; the terminal sub-init clamps bash to 459 caps / 98M — `configured RAM/caps exceed available, proceed with ...`); not the upgrade-wedge (the `sponge-upgrade-wedge` receipt is ALIVE at this pin). Product-side (base/libc fork + session machinery); run-side unblock not found in this wave; reproduced identically on the 2026-11-05 re-gate. | Phase 17+ (pairs the D16R.17 session-machinery/fork family) | §7 F6 re-gate + §7 F6/F7 2026-11-05 re-gate; receipts `phase16-runtime-f6-e35-sponge-terminal.log`, `phase16-runtime-f7-e35-sponge-terminal.log` + session scratch `f6-diag*.log` |
| B1 | base-linux spec/pc `get_page_size` hazard (latent) | vendored-tree follow-up (ledger process) | §6/§8 |
| B3 | `sponge-pkg-driver` fb-probe report_rom policy gap | policy fix + re-gate exclusivity probes | §8 B3 |

## 12. Receipt index

Every matrix row cites `docs/evidence/phase16-runtime-sweep-<id>-
<scenario>.log` (curated: exact command, exit status, elapsed, marker
lines and verdict lines verbatim, tail 20). 62 receipts = 62 runner
rows (36-envelope rows incl. the duplicate citation + 7 phase
scenarios + 4 arms + 5 adjacent + 6 linux-axis/characterization rows +
re-run attempts e11a2/e22a2/e19a2/e32a2/e30a2/l04a2). Raw transcripts
(`/tmp/opencode/sweep/*.log`) are session scratch and are not promoted
(AGENTS.md §5.6); every fact in this document is reproducible from the
committed receipts' verbatim excerpts.

Fix-wave receipts (2026-11-04, the F1/F6 re-gate) follow the same
curation rules under a `phase16-runtime-f<id>-<scenario>.log` infix:
`phase16-runtime-f1-e04-sponge-desktop-defaults-firstboot.log`,
`phase16-runtime-f1-e30-sponge-alpha.log`, and
`phase16-runtime-f6-e35-sponge-terminal.log`.

Fix-wave receipts (2026-11-05, the rows 22/11/32 fixes + the row 35
re-gate) add four more of the same shape:
`phase16-runtime-f4-e22-sponge-wm-tasks.log`,
`phase16-runtime-f2-e11-sponge-de-shortcuts.log`,
`phase16-runtime-f5-e32-sponge-textedit.log`, and
`phase16-runtime-f7-e35-sponge-terminal.log` (the F7 wedge
reproduction; row 35 PARTIAL, not green).
