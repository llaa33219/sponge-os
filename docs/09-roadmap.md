# 09 - Roadmap

> This document defines the development phases and milestones of
> Sponge OS. Dates are not firm; they express relative order. The
> completion criterion of each milestone is a clearly verifiable
> condition.

---

## 1. Phase Overview

```
Phase 0: Design and scaffold      ✅ done
   |
   v
Phase 1: Minimum boot             ✅ done (base-linux + base-sel4)
   |
   v
Phase 2: vct minimum working      ✅ done
   |
   v
Phase 3: Sponge DE single window  ✅ done (headless verification on nitpicker)
   |
   v
Phase 4: Package management (sponge_pkgd)  ✅ done
   |
   v
Phase 5: Integration and automation layer  ✅ done
   |
   v
Phase 6: Leitzentrale integration  ✅ done
   |
   v
Phase 7: Alpha — first usable version  ✅ done with caveats
   |
   v
Phase 8: Boot & storage architecture (docs/14)  ✅ done
   |
   v
Phase 9: seL4 capability-space scaling  ✅ done (Falkon first paint)
   |
   v
Phase 10: Sponge DE — fully interactive desktop  ✅ done
   |
   v
Phase 11: DE customization and panel strengthening  ✅ done
   |
   v
Phase 12: Hardware support expansion  ✅ done (QEMU-verified matrix)
   |
   v
Phase 13: Package ecosystem growth  ✅ done (criterion 3 partial: importer breadth deferred, D13.5)
   |
   v
Phase 14: Sponge DE as a daily-usable desktop  ✅ done
   |
   v
Phase 15: Real-hardware boot  ✅ done (0.2.0 released; full interaction on 17ZD90N)
   |
   v
Phase 16: Practical daily-usable desktop defaults & configuration
   |
   v
Phase 17: Sponge IME (multi-language / CJK input)
   |
   v
Phase 18: GUI installer for SSD/HDD installation  -> releases 0.3.0
```

---

## Phase index

Per-phase detail lives in child documents under `roadmap/`:

| Original section | Child document |
|---|---|
| §2 Phase 0 | [roadmap/phase0-design-scaffold.md](roadmap/phase0-design-scaffold.md) |
| §3 Phase 1 | [roadmap/phase1-minimum-boot.md](roadmap/phase1-minimum-boot.md) |
| §4 Phase 2 | [roadmap/phase2-vct-minimum.md](roadmap/phase2-vct-minimum.md) |
| §5 Phase 3 | [roadmap/phase3-sponge-de-single-window.md](roadmap/phase3-sponge-de-single-window.md) |
| §6 Phase 4 | [roadmap/phase4-package-management.md](roadmap/phase4-package-management.md) |
| §7 Phase 5 | [roadmap/phase5-integration-automation.md](roadmap/phase5-integration-automation.md) |
| §8 Phase 6 | [roadmap/phase6-leitzentrale.md](roadmap/phase6-leitzentrale.md) |
| §9 Phase 7 | [roadmap/phase7-alpha.md](roadmap/phase7-alpha.md) |
| §10 Phase 10 | [roadmap/phase10-interactive-desktop.md](roadmap/phase10-interactive-desktop.md) |
| §10 Phase 11 | [roadmap/phase11-de-customization.md](roadmap/phase11-de-customization.md) |
| §10 Phase 12 | [roadmap/phase12-hardware-support.md](roadmap/phase12-hardware-support.md) |
| §10 Phase 13 | [roadmap/phase13-package-ecosystem.md](roadmap/phase13-package-ecosystem.md) |
| §10 Phase 14 | [roadmap/phase14-daily-usable-desktop.md](roadmap/phase14-daily-usable-desktop.md) |
| §10 Phase 15 | [roadmap/phase15-real-hardware-boot.md](roadmap/phase15-real-hardware-boot.md) |
| §10 Phase 16 base | [roadmap/phase16-daily-desktop-defaults.md](roadmap/phase16-daily-desktop-defaults.md) |
| §10 Phase 16 Extension | [roadmap/phase16-extension.md](roadmap/phase16-extension.md) |
| §10 Phase 16 scope expansion | [roadmap/phase16-scope-expansion.md](roadmap/phase16-scope-expansion.md) |
| §10 Phase 17 | [roadmap/phase17-sponge-ime.md](roadmap/phase17-sponge-ime.md) |
| §10 Phase 18 | [roadmap/phase18-gui-installer.md](roadmap/phase18-gui-installer.md) |

---

## 2. Phase 0: Design and Scaffold (Current)

Detail: [roadmap/phase0-design-scaffold.md](roadmap/phase0-design-scaffold.md)

---

## 3. Phase 1: Minimum Boot

Detail: [roadmap/phase1-minimum-boot.md](roadmap/phase1-minimum-boot.md)

---

## 4. Phase 2: vct Minimum Working

Detail: [roadmap/phase2-vct-minimum.md](roadmap/phase2-vct-minimum.md)

---

## 5. Phase 3: Sponge DE Single Window

Detail: [roadmap/phase3-sponge-de-single-window.md](roadmap/phase3-sponge-de-single-window.md)

---

## 6. Phase 4: Package Management (`sponge_pkgd`)

Detail: [roadmap/phase4-package-management.md](roadmap/phase4-package-management.md)

---

## 7. Phase 5: Integration and Automation Layer

Detail: [roadmap/phase5-integration-automation.md](roadmap/phase5-integration-automation.md)

---

## 8. Phase 6: Leitzentrale Integration

Detail: [roadmap/phase6-leitzentrale.md](roadmap/phase6-leitzentrale.md)

---

## 9. Phase 7: Alpha

Detail: [roadmap/phase7-alpha.md](roadmap/phase7-alpha.md)

---

## 10. Post-Alpha Phases (10–18)

Phases 8 and 9 (boot/storage architecture per `docs/14`, seL4
capability-space scaling) are delivered and closed the two largest
Alpha caveats — the boot-module ceiling and the missing persistence.
The phases below define the road from the current QEMU-verified Alpha
to a daily-usable system on real hardware. As with earlier phases,
dates are not firm; they express relative order, and every phase is
scenario-gated.

**Version milestones**: completing Phase 15 (real-hardware boot)
released **0.2.0** (2026-09-16); completing Phase 18 (GUI
installer) releases **0.3.0**. Phases 10–14, 16 and 17 accumulate
on top of 0.2.0 without their own version bumps.

Detail: [roadmap/phase10-interactive-desktop.md](roadmap/phase10-interactive-desktop.md) · [roadmap/phase11-de-customization.md](roadmap/phase11-de-customization.md) · [roadmap/phase12-hardware-support.md](roadmap/phase12-hardware-support.md) · [roadmap/phase13-package-ecosystem.md](roadmap/phase13-package-ecosystem.md) · [roadmap/phase14-daily-usable-desktop.md](roadmap/phase14-daily-usable-desktop.md) · [roadmap/phase15-real-hardware-boot.md](roadmap/phase15-real-hardware-boot.md) · [roadmap/phase16-daily-desktop-defaults.md](roadmap/phase16-daily-desktop-defaults.md) (base) · [roadmap/phase16-extension.md](roadmap/phase16-extension.md) · [roadmap/phase16-scope-expansion.md](roadmap/phase16-scope-expansion.md) · [roadmap/phase17-sponge-ime.md](roadmap/phase17-sponge-ime.md) · [roadmap/phase18-gui-installer.md](roadmap/phase18-gui-installer.md)

---

## 11. Current Focus

Phases 0–15 are complete (0.2.0 released). Phase 7 Alpha caveats are recorded in
[`docs/13-installation.md`](13-installation.md); Phases 8 (boot
and storage architecture, `docs/14`) and 9 (seL4 capability-space
scaling) resolved the two largest Alpha caveats — the boot-module
ceiling and missing persistence — by delivering disk-based payload
staging + persistence on the 4-partition product media and a lazy
`vm_space` growth patch that lets Falkon reach first paint on seL4.
Phase 10 closed the fully-interactive-desktop track: every desktop
panel/window/launcher action is driven by real host input, and
window dragging + click-to-launch + keyboard input to focused apps
are all proven by QMP-driven run scenarios. Phase 11 delivered DE
customization: four new configd keys applied live by the new
in-sponge-de `ConfigController` (panel height/visibility, clock
format, launcher sort), an expanded theme surface with four shipped
themes and a hardened unknown-theme fallback, and Sponge-themed
window chrome via the upstream `themed_decorator` drop-in fed by the
new `sponge_decorator_bridge` (live palette-tinted title bars,
drag-verified on seL4). **Phase 12** delivered the hardware-support
expansion: the explicit q35+Skylake-Client pin on every disk-touching
script, six new focused scenarios (i440fx/PIIX4 IDE smoke,
q35/AHCI multi-disk order check, q35/NVMe desktop-from-disk,
`pc_nic`/e1000 DHCP, BIOS-side USB storage, and USB HID keyboard via
QMP hotplug), the `tool/dist --storage {ahci,nvme}` product-media
selector, the hand-curated 5×5 surface matrix + 16-cell ledger in
`docs/15-hardware-compatibility.md` (4 verified, 1 smoke-only, 11
gap), and the read-only `tool/hw_compat.mojo assert` validator
reachable from `./tool/build verify`. Phase 12 is **QEMU-verified
only**; physical-hardware boot remains a Phase-15 deliverable.

**Phase 13** grew the package ecosystem: the `terminal` package gained
a full UNIX CLI toolset (coreutils/grep/sed/tar/less/findutils/
diffutils/which as noux tar mounts, D13.1), two source-built GUI
packages joined `pkg/` (`calculator` from the qt6_tools example,
`pdf_view` from mupdf), each with a boot-verified probe scenario on
base-sel4; the end-to-end authoring guide landed as
`docs/16-package-authoring.md`; `tool/pkg_import`'s session-name
casing defects were fixed; and the toolset work exposed and repaired
a latent Phase-7 bug (the `<env name=... value=...>` form yields
empty variables — no external command could ever have run from the
Alpha terminal) plus the mid-settle prompt-detection race behind a
terminal-qmp host-protocol timeout. The third criterion's importer
breadth remains partially deferred (D13.5).

**Phase 14** closed the daily-usable-desktop track and **Phase 15
closed real-hardware boot, releasing 0.2.0** (2026-09-16): the
desktop is fully interactive on the physical LG gram 17ZD90N-VX7BK
(panel, pointer/click, in-window interaction, mouse movement, native
2560x1600). The pre-real-hw QEMU harness shipped alongside: the
20-variant `run/sponge-hw-matrix.run` + `tool/hwtest` matrix and the
`tool/hwrepro` reproduction battery (boundary / in-window /
acpi-dmar), which root-caused and fixed the acpi DMAR misparse
(ledger row 16) that had stalled the usb chain on real Intel
firmware. See docs/08 §16, docs/11 §10.6, docs/15 §7.

Next up: **Phase 16 — practical daily-usable desktop defaults &
configuration** (inserted 2026-09-16 ahead of the IME); then
**Phase 17 — Sponge IME (multi-language/CJK input)** and
**Phase 18 — GUI installer** (releases 0.3.0), the remaining
post-Alpha sequence defined in §10.

**Phase 16 closed-out 2026-09-20** (10 of 12 criteria delivered; 2
delivered with honest disclosure — criterion 6+7 carry the D16.7
IMG-only scope; criterion 10 carries the W6 deviation 1 right-click
delivery finding). Full per-criterion evidence index in
[`docs/evidence/phase16-index.md`](evidence/phase16-index.md).

**Phase 16 compositor G1+G2 completed 2026-11-18**: the public
`sponge-compositor` repository now implements the Mojo-owned scene,
view stack, reducer, clipping/damage, opaque painter, and input
hit-testing behind thin Genode adapters. Two opaque views and real Capture
are byte-exact against upstream nitpicker on seL4 (`0x670d6b5d9fc0ed7b`),
and the G2 input battery (QMP clicks, alpha/mask fall-through, focus
transitions, Event injection, pointer report) passes on both Tier-0s with
an exact Event-injection match and a documented full-trace divergence.
Tier-0 selection is default-native since 2026-11-19 (user approval after
the green G2 receipts): unset or `SPONGE_COMPOSITOR=native` selects
`sponge_compositor`, and `SPONGE_COMPOSITOR=upstream` is the explicit
nitpicker fallback. G3+ is not claimed: no WM/DE integration, translucent
blending, physical-hardware, or product-integration result comes from this
([`docs/evidence/phase16-compositor-index.md`](evidence/phase16-compositor-index.md)).

Deferred follow-ups (not blockers):

1. **base-sel4 interactive GUI** ✅ driver stack delivered
   (`run/sponge-de-sel4-interactive.run`): the full
   `drivers_interactive-pc` driver set — vesa_fb, ps2, usb_hid,
   pc_usb_host, event_filter, platform, acpi, pci_decode — is wired into
   a base-sel4 QEMU run script with `-device nec-usb-xhci,id=xhci
   -device usb-tablet` for absolute-pointer input, exactly as this item
   asked. The scenario boots seL4 on QEMU (1 GiB RAM) and is verified
   headlessly by three boot-log assertions: vesa_fb sets the 1024x768
   mode and maps the physical VESA framebuffer, usb_hid binds the QEMU
   usb-tablet as a `POINTER` device, and `sponge_de_probe` reports
   `PASS` — the themed window is pixel-verified through a Capture
   session and a synthetic click round-trips through sponge-de's
   `input` report. The Qt6/Mesa (EGL) rendering path on base-sel4 was
   fixed as part of this work; see §11.1 below for the root cause
   (capability exhaustion, not a Mesa bug). Reference:
   `docs/11-environment.md` §7.2/§10 and the vendored
   `genode/repos/os/recipes/raw/drivers_interactive-pc/` config set.

   11.1. **Resolved — Qt6/Mesa (EGL) on base-sel4 was capability
   exhaustion, not a Mesa bug.** The original symptom: sponge-de's
   Qt6/EGL initialization hung on base-sel4 after logging the benign
   libEGL `MESA-LOADER: failed to retrieve device information` and
   `failed to get driver name for fd 0` warnings (both also appear on
   base-linux, where softpipe fallback follows and the window paints),
   plus one `upgrading quota donation for PD session (0 bytes, 4 caps)`
   line — then silence forever. Root cause, confirmed by experiment:
   base-sel4's capability accounting costs roughly 3x base-linux's per
   operation (each Genode capability maps to several raw seL4 caps), so
   the former `caps: 300` assignment ran out **in the middle of Mesa's
   screen creation**. The child requested a 4-cap upgrade, and the
   subsequent out-of-caps allocation blocked *silently* — no error, no
   log, an infinite hang. Raising sponge-de to `caps: 1000` (RAM was
   never the bottleneck — the upgrade request was 0 bytes, so
   `ram: 128M` matches the base-linux scenarios) makes the full desktop
   render on seL4: `sponge-de: panel and window shown`, the probe's
   Capture pixel check detects the themed window, and an injected click
   round-trips (`sponge-de-probe: PASS`). The scenario now gates on that
   PASS marker, so a regression fails loudly instead of hanging.
   **Open question (upstream)**: a Genode component that exhausts its
   capability quota mid-initialization hangs with zero diagnostics — the
   last visible line is the quota-upgrade request. A loud failure (or a
   retry-then-abort) in the out-of-caps path would have turned this
   multi-day mystery into a one-line log. Candidate for an upstream
   Genode issue; not worked around in Sponge OS code because the fix
   belongs in base/capability accounting, not in individual components.
     Remaining follow-up on this scenario — drive the click from the host
     through the real usb-tablet (QMP `input-send-event` over a
     `-qmp tcp:...` socket) instead of the probe's synthetic Event
     injection, exercising usb-tablet → pc_usb_host → usb_hid →
     event_filter → nitpicker → sponge-de with real hardware input —
     **delivered in Phase 10** (W1): see `run/sponge-de-sel4-interactive.run`
     + `docs/evidence/task-1-phase10-interactive.md`.
2. **Package install persistence** ✅ delivered: the explicitly-installed
   root set is mirrored to a versioned XML store on a `File_system`
   session (format in `docs/12-package-format.md` §13) and reloaded on
   construct, so installs survive a reboot. Proven by
   `run/sponge-pkg-persist.run` (two boots over the same lx_fs-backed
   host directory; boot 2 issues a `list`-only and finds the previously
   installed package, which can only have come from the restored store).

### 11.2 Phase-10-known-issue follow-ups

These are not blockers for the Phase 10 checkboxes (all five criteria
are GREEN with the per-criterion scenarios), but they will read better
once resolved in a later phase. They are tracked here so they don't
silently rot.

1. **Genode QPA misroutes tablet absolute-motion under multi-domain Qt
   setups.** Observed in W2 (criterion 3 click-to-launch): the
   usb-tablet's `Absolute_motion` events reach `QCursor::pos()` only via
   `qgenodeplatformwindow::handle_absolute_motion`; PS/2's
   `Relative_motion` events do not update `_mouse_position`. The tablet
   click landed correctly in earlier passes (W1 criterion-1) because
   the demo-domain target was large enough that the screen-center clamp
   still hit it; the launcher's `Main` widget receives presses even when
   the launcher popup is hidden. A future QPA patch that processes
   REL → cumulative ABS internally would let the cursor reach the
   exact target and drop the QPA-level click-to-launch escape hatch.
   Recorded in `docs/evidence/task-2-phase10-interactive.md` §"Open
   issues for W6". Out of scope per Phase 10's "no `genode/` changes"
   rule; candidate for Phase 12 if/when the QPA is on the patch list.

2. **Nitpicker pointer ROM only updates on `absolute_motion`.** The
   `nitpicker::Session::pointer` report ROM
   (`genode/repos/os/src/server/nitpicker/user_state.cc:117-124`)
   receives `report_pointer_position` only when the pointer is
   **explicitly** positioned via `absolute_motion`. PS/2
   `relative_motion` updates the cursor internally but does NOT set the
   `_pointer` value the report reads from. Therefore a "closed-loop"
   pointer-position protocol that drives the cursor via PS/2 REL and
   reads its position from the pointer report is unusable on this host
   (the report is empty). The W5 scenario compensated by using the
   usb-tablet's `mouse_set` + a single `absolute_motion` to materialize
   a known cursor position, then a closed-loop REL walk on top of that.
    This works but is one boot-specific. Future work: extend nitpicker's
    `user_state` to update `_pointer` from REL too. **Partially delivered**
    (Phase-11 media follow-up): the relative-motion `Nowhere` branch now
    initializes the pointer instead of dropping the event (patch ledger
    #9), which is what makes a visible cursor track under PS/2-only
    input. The report-ROM side of the gap (REL not updating the pointer
    report) remains open.

3. **Pre-existing base-sel4 Qt6 staging issue blocks
   `sponge-de-test.run` + `sponge-launcher.run` on sel4.** Reported in
   `docs/evidence/task-1-phase10-interactive.md` §"Step 5 Regression":
   both scenarios copy Qt6 `.lib.so` files into
   `[run_dir]/genode/` AFTER `build_boot_image`, which is invalidated
   by `genode/tool/run/boot_dir/sel4:59 remove_genode_dir`. The
   fixes are scenario-side (move the `cp` calls BEFORE
   `build_boot_image`); out of scope for Phase 10. The scenarios still
   run on `KERNEL=linux BOARD=pc`. Carried over as "BLOCKED-pre-
   existing" entries in the Phase-10 regression table
   (`docs/evidence/task-6-phase10-interactive.md`).

4. **`QTimer + QCursor::pos()` popup auto-close was invalid on the
   Genode QPA — replaced by event-driven click-outside.** The first
   attempt at a "click-outside closes the popup" handler in
   `repos/sponge/src/sponge-de/launcher/launcher_menu_view.{h,cc}`
   used a 50ms `QTimer` that read `QCursor::pos()` and hid the popup
   when the cursor left the launcher domain rect. On the Genode QPA
   (`genode/depot/cproc/src/qt6_base/.../qgenodeplatformwindow.cpp:387-400`)
   `QCursor::pos()` always reports `(0,0)` because `_mouse_position` is
   only updated from `handle_absolute_motion`. The timer fired the
   moment it elapsed and hid the popup between S-click and entry-click
   in the launch chain. Replaced with `qApp->installEventFilter` and an
   `eventFilter` override (commit `3727eaf2d2`); the launcher
   event-driven close now works deterministically. This is a real UX
   improvement (genuine click-outside close), not a workaround.

### 11.3 Phase-11-known-issue follow-ups

Recorded during Phase 11 (W4/W5); none block the Phase 11 checkboxes.

1. **`sponge-de-sel4-interactive.run` launch-phase click flake.** ✅
   **delivered in Phase 12 W3b** (resolution of the Phase-11 §11.3
   item-1 follow-up). On the W5 Phase-11 sweep this host failed 3/3:
   the PS/2 REL click lands ~10 px below the launcher entry button
   (the W2-calibrated recipe's geometry predates the current popup
   layout). The deterministic fix is to switch the launch-click
   choreography to the usb-tablet absolute path that W4 proved for
   drags (`qmp_tablet_index` + `mouse_set` + abs events). W3b added
   the smallest launch-only selector in `run/qmp.inc` and changed
   `run/sponge-de-sel4-interactive.run`'s launch-entry call site to
   select that tablet-absolute recipe. The launch phase passed 3/3
   back-to-back on the W3b host (`docs/evidence/task-3b-phase12-launch-click.md`)
   and the W6 envelope re-ran the launch phase three consecutive
   times. **Residual nondeterminism recorded as a Phase-12 gap:**
   the launch phase retains a small empirical probability of a
   spurious failure (the W6 envelope documented this — the
   `sponge-de-sel4-interactive.run` ×3 gate required 3 consecutive
   passes, and the Phase-11 launch flake signature may still strike
   on a single attempt; the regression evidence is honest about every
   attempt). PS/2-relative dispatch is preserved as the default for
   the input and panel phases — the original Phase-10 recipes still
   exercise their original path. Merely adding `-device usb-tablet`
   was not the fix; W3b changes the choreography (workspace press →
   workspace move → workspace release with bounded pacing) on the
   existing device. Evidence:
   `docs/evidence/task-5-phase11-sel4-interactive-FLAKE.log` (before
   trace) vs. `docs/evidence/task-3b-phase12-launch-click.md` (after
   trace).
2. **themed_decorator live asset re-skin.** The bridge updates the
   policy `color=` live, but the frame texture/button glyphs/font in
   `decor.tar` are cached in upstream statics — a full chrome re-skin
   on `theme.active` change needs either a decorator child restart or
   an upstream asset-invalidation patch (patch-ledger candidate, see
   `docs/11-environment.md` §4 note).
3. **`panel.position` is boot-time-only.** configd persists the value
   but panel placement is owned by the run script's nitpicker domain;
   live repositioning needs either dual pre-defined domains with a
   visibility toggle or WM-managed panel placement — Phase 12+ open
   question (documented in `sponge_configd/README.md`).
4. **Partial drag delta on themed chrome.** The W4 themed drag moved
    pkg_gui_demo (50,320)→(68,330) instead of the dispatched
    +100/+100 — the mechanics (press→DRAG→move) are verified; the
    partial delta matches the Phase-10 W6 host-timing variance class.
    Hardening candidate: paced tablet steps with per-step hover
    confirmation.

### 11.4 Phase-14-known-issue follow-ups (W3)

Recorded during Phase 14 W3 (vendored-patch investigation, D14.8);
none of the items below are blockers for the Phase 14 checkboxes
(the W3 workstream is investigation only — `AGENTS.md` §5.2 — no
vendored-tree patches land here). Each item carries the same
disposition rule as the Phase-10 / Phase-11 follow-ups above
(recorded but not silently absorbed).

 1. **QGenodeScreen 1x1-stale race — D14.8(b) outcome:** **Re-scoped
    to Phase 15+.** The Phase 11 alpha-flake review
    (`docs/evidence/task-6-phase11-alpha-flake.md`) suspected a race
    where `QGenodeScreen` publishes a 1x1 screen geometry until
    nitpicker's panorama info arrives — late on heavy seL4
    topologies — and sponge-de's `PanelWidget::_apply_geometry`
    trusted that width. The Phase 11 fix added a width-floor guard
    (`screen_w > 64 ? screen_w : 1024`) and the alpha sweep went 6/6
    green. The Phase 14 W3 workstream re-tested that fix with a
    focused cold-boot reproducer
    (`run/sponge-de-qscreen-race-probe.run`,
    `repos/sponge/src/test/qscreen_race_probe/`): 10 cold-boot trials
    on base-sel4, every trial logged
    `qscreen-race-probe: trial verdict clean race_panel_black_max=0`
    (panel band was never black, demo-domain window_bg painted
    within 1–3 polls in every trial). Per D14.8(b)'s "≥ 3 of 10"
    threshold, candidate (b) is **Re-scoped to Phase 15+** with the
    width-floor guard kept as the resolution path. No vendored-tree
    patch lands in Phase 14 — the investigation confirms the Phase
    11 fix is sufficient on the proven-reference topology. If Phase 15
    discovers a new topology where the width-floor guard does NOT
    hold (e.g., real-hardware boot, multi-monitor, or a
    faster-boot configuration that exposes the race), the focused
    reproducer can be re-run unchanged. Evidence:
    `docs/evidence/task-3-phase14-qscreen-race.log`.

 2. **Nitpicker pointer ROM from REL — D14.8(c) deferral
    confirmed.** The Phase 11 §11.2 item-2 carryover
    (`nitpicker::Session::pointer` report ROM receives
    `report_pointer_position` only on explicit `absolute_motion` —
    PS/2 REL updates the cursor internally but does NOT set the
    `_pointer` value the report reads from) was **partially** patched
    in Phase 11 (patch-ledger row #9: REL `Nowhere` branch now
    initializes the pointer instead of dropping the event), but the
    report-ROM side remains open. Per D14.8(c) this is **Re-scoped**
    to upstream Genode (not absorbed as a Phase-14 Sponge patch)
    because the proper fix belongs in nitpicker's `user_state.cc`
    REL→`_pointer` propagation path — a vendored-tree patch would
    violate `AGENTS.md` §5.2's "minimal local patches" rule. No W3
    code changes; the deferral pointer is recorded here for the
    Phase-15+ planner. Trigger for re-opening: any concrete Phase-15
    run scenario (real-hardware pointer, multi-monitor) exposes a
    visible discrepancy between cursor-on-screen and the
    `nitpicker::Session::pointer` report.
