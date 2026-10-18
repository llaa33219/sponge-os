# The framebuffer driver as a package — evidence

> SPDX-License-Identifier: Apache-2.0

What this file records: the framebuffer driver left the Tier-0 static
topology and became an ordinary `<kind>driver</kind>` package
(`docs/17` §1), the two product media boot their display through it,
and the switch between the two drivers is a package operation at
runtime — measured, not argued.

---

## 1. What moved

| before | after |
|---|---|
| `+ start fb \| caps: 1000 \| ram: 32M` + `+ binary vesa_fb` in the Tier-0 config (BIOS media) | `pkg/fb_vesa` → `driver_runtime` |
| `+ start fb \| caps: 24000 \| ram: 96M` + `+ binary boot_fb` in the Tier-0 config (UEFI media) | `pkg/fb_boot` → `driver_runtime` |
| `+ policy \| label_prefix: fb \| info: yes \| + pci \| class: VGA` | one grant per fb package's own label prefix (`run/driver_runtime.inc::platform_policies`) |
| `fb.config` as a Tier-0 boot module, relayed as `label: fb.config` | the `<config>` inline in the package's `metadata.xml` |
| `vesa_fb` / `boot_fb` in `[build_artifacts]` | out of Tier-0; staged to `/system/bin` by `driver_packages::stage` like every packaged component |

`nitpicker` stays in Tier 0: it is the compositor server every client
shares, not a driver, and no package install should be able to replace
it underneath all of them at once.

## 2. First boot: the display comes up through the package

Which driver is correct depends on the firmware, so the scenario says
so once (`driver_runtime::fb_package fb_vesa` /
`driver_runtime::fb_package fb_boot`) and the include forwards it to
`bake::add_package`. `driver_packages::stage` then **asserts** that the
choice reached the staged bake manifest — a media whose manifest does
not name an fb package would boot with no display at all, and that is
now a build-time error rather than a field surprise.

### BIOS media (`run/sponge-desktop-disk.run`)

```
bake: injected package 'fb_vesa' (scenario override)
bake:   pkgs (1):  hello, terminal, textedit, files, calculator, pdf_view, falkon, debug, fb_vesa
driver_packages: boot display driver = 'fb_vesa'  (seeded from the bake manifest)
...
[init -> system -> sponge_pkgd] sponge_pkgd: seeded 9 baked package root(s) from the bake manifest
[init -> system -> driver_runtime -> fb_vesa] fb mapped to 0x6000
[init -> system -> driver_runtime -> fb_vesa] Found: VESA BIOS version 3.0
[init -> system -> driver_runtime -> fb_vesa] OEM: SeaBIOS VBE(C) 2011
[init -> system -> driver_runtime -> fb_vesa] Found: physical frame buffer at 0xfd000000 size: 16777216
[init -> system -> driver_runtime -> fb_vesa] fb mapped to 0x17000000
[init -> system -> driver_runtime -> fb_vesa] using 1024x768 (1024x768)
[init -> system -> alpha_probe] alpha-probe: PASS
Run script execution successful.
```

`fb_vesa` is a child of `driver_runtime`, not of Tier-0, and the
desktop's pixel gate passes on top of it.

### UEFI media (`run/sponge-desktop-disk-uefi.run`)

```
bake: injected package 'fb_boot' (scenario override)
...
[init -> system -> sponge_pkgd] sponge_pkgd: seeded 9 baked package root(s) from the bake manifest
[init -> system -> driver_runtime -> fb_boot] using boot framebuffer: 2560x1600x32 @ 0x80000000 type=1 pitch=10240 -> 2560x1600
[init -> system -> alpha_probe] alpha-probe: PASS
[sponge-desktop-disk-uefi] PASS — UEFI product media boots on OVMF,
  the packaged fb_boot driver claimed the GOP framebuffer: 1
  (driver package + driver_runtime, not a static Tier-0 fb node)
  and the desktop reached its pixel-verified gate (alpha-probe: PASS).
```

The scenario's own gate changed with the work: it used to wait for a
`Genode v...` banner that this media never printed and to classify an
OVMF "core-init hang", which is why its `expect` block had
`-i $qemu_spawn_id` on the first arm only — the `eof` / `timeout` arms
watched the run tool's own silent spawn id and could never fire. The
gate now waits for the desktop marker itself (`alpha-probe: PASS`) with
every arm scoped, and reports whether the packaged driver claimed the
framebuffer.

## 3. Runtime handover: fb_vesa → fb_boot

`SPONGE_FB_SWITCH=on` adds `test/fb_switch_probe` to the product
topology. It asserts, in order, that the seeded driver is a
`driver_runtime` child, that the composited screen shows a rendered
desktop, that `remove` + `install` moves the driver, and that the
screen comes back and SETTLES on a rendered desktop.

```
[init -> system -> fb_switch_probe] fb-switch-probe: active=fb_vesa alternate=fb_boot screen 1024x768
[init -> system -> fb_switch_probe] fb-switch-probe: driver_runtime lists child 'fb_vesa'
[init -> system -> fb_switch_probe] fb-switch-probe: pre-switch frame rendered 33/640 sampled pixels
[init -> system -> driver_runtime -> fb_vesa] using 1024x768 (1024x768)
[init -> system -> driver_runtime -> fb_boot] using boot framebuffer: 1280x800x32 @ 0xfd000000 type=1 pitch=5120 -> 1280x800
[init -> system -> fb_switch_probe] fb-switch-probe: driver_runtime now lists child 'fb_boot'
[init -> system -> alpha_probe] alpha-probe: PASS
[init -> system -> fb_switch_probe] fb-switch-probe: post-switch frame rendered 247/640 sampled pixels (settled)
[init -> system -> fb_switch_probe] fb-switch-probe: switch fb_vesa -> fb_boot complete
[init -> system -> fb_switch_probe] fb-switch-probe: PASS
sponge-desktop-disk: FB SWITCH PASS - the display driver is a package: the running one was removed, the other installed, and the composited screen came back and settled on a rendered desktop
Run script execution successful.
```

Three things this settles that a log line alone would not:

* **The display changes driver** — 1024x768 VESA out, 1280x800 GOP in,
  from two different components.
* **The screen recovered**, and *settled*: the probe keeps sampling
  until two consecutive censuses agree AND the frame carries a rendered
  desktop. A driver that started and failed to map or blit settles to
  black; a client that never repainted settles to an empty or stale
  frame. Neither passes.
* **The takeover console is unaffected.** `sponge_console` is a Gui
  client of `nitpicker`; `nitpicker` outlives its Capture back end
  disappearing and reappearing at a different resolution. The
  supervisor-takeover regression (`run/sponge-supervisor-takeover.run`)
  stays green, so a supervisor-mediated `nitpicker` restart turned out
  NOT to be needed — the handover is the package stop/start itself.

The probe's pixel oracle compares RGB per channel against the
nitpicker background (`#1e1e2e`) rather than the packed 32-bit word.
That is deliberate: `alpha_probe`'s criterion (a) compares the packed
word, so an opaque background pixel (`0xff1e1e2e`) counts as
non-background there — a pre-existing weakness, recorded here rather
than silently inherited.

## 4. Exclusivity — a second fb driver is refused, by name

`run/sponge-pkg-driver.run` (lean topology, no desktop to race the
request channel) installs `fb_vesa` and then asserts that the next
install is refused:

```
[init -> system -> fb_lock_probe]     driver-probe: PASS (child assertion skipped)
[init -> system -> fb_conflict_probe] driver-probe: PASS (refusal as expected)
[init -> system -> sponge_pkgd] sponge_pkgd: install fb_boot -> exclusive-conflict (fb is held by the running package fb_vesa)
sponge-pkg-driver: PASS (pc_nic installed as a package into driver_runtime, bound the e1000, leased 10.0.2.15; a second fb driver was REFUSED as exclusive-conflict)
Run script execution successful.
```

The refusal is a status, not an error: `status="exclusive-conflict"`
with `group`, `peer` and `remedy`, and `vct` renders the remedy
inline. Both fb probes pass `expect_child: no` because this topology
has no compositor, so the vesa driver cannot really come up here —
what is under test is `sponge_pkgd`'s running set, not the hardware.

## 5. Three defects this work exposed and fixed

1. **`sponge_pkgd` dropped a package `<config>`'s own attributes.**
   The generator rebuilt `<config>` and appended only its CHILD nodes,
   so every attribute was silently dropped. Invisible for the shipped
   application packages (children-only configs) and fatal for an
   attribute-driven one: `vesa_fb` reads `width` / `height` straight
   off its config root, so a packaged framebuffer driver would have
   come up 0x0 and set no mode at all. Fixed by copying the element's
   attributes; documented in docs/12 §7.2 rule 4.

2. **`system`'s hardware services were spelled the way a human writes
   them.** `Irq` / `Io_mem` / `Io_port` are never requested by
   anything — Genode's session `service_name()` is `IRQ` / `IO_MEM` /
   `IO_PORT`, and the parent-provides match is case-sensitive. It was
   invisible while every hardware component was a Tier-0 child (the
   Tier-0 config routes by its own rules and never consulted
   `system`'s list); the first packaged driver to cross that boundary
   died with `parent denied IO_MEM-session`. Fixed in all five product
   scenarios, and in the four driver packages that declared the wrong
   spelling.

3. **A packaged driver cannot reach a CORE rom module through the
   stock route order — silently.** `platform_info` is exported by
   base-sel4 from the multiboot2 boot info, so only init's parent
   serves it. A packaged driver requests it as a grandchild of the
   Tier-0 `system` node, and that node's route list opens with
   `+ service ROM | + child rom_sys`, which denies the label. Two
   Genode facts turn that into a silent death: the query label is
   scoped to the child *at whichever node the request arrives at*, so
   a `label:` rule cannot match across a nesting level while
   `label_last:` can; and every `<service>` node sets
   `_specific_service`, so `Route_model::resolve` rethrows on a denial
   inside one instead of falling through to `any-service -> parent`.
   The observable symptom is a bare
   `parent denied ROM-session: label="platform_info"` with no
   `no route to service` line anywhere. Fixed with one explicit rule
   before the catch-all in the product scenarios:

   ```
   + service ROM | label_last: platform_info | + parent
   ```

   The reasoning is recorded in `run/driver_runtime.inc`'s header so
   the next driver that needs a core ROM does not have to rediscover
   it.

One more trap is recorded in the scenarios rather than fixed, because
it is a property of the placeholder-expansion idiom: a comment inside
a HID fragment must not name a `@DRIVER_...@` placeholder. The
expander substitutes every occurrence in the file, comments included,
and a substituted `<policy>` line inside a `<route>` block corrupts the
node — which surfaces as `Error: binary is not an ELF` three minutes
later.

## 6. Per-media results

| media | seeded driver | display evidence | scenario exit |
|---|---|---|---|
| `sponge-desktop-disk` (BIOS) | `fb_vesa` | `using 1024x768 (1024x768)` + `alpha-probe: PASS` | 0 |
| `sponge-desktop-disk` + `SPONGE_FB_SWITCH=on` | `fb_vesa` -> `fb_boot` | `using boot framebuffer: 1280x800x32`, screen settled | 0 |
| `sponge-desktop-disk-uefi` (OVMF) | `fb_boot` | `using boot framebuffer: 2560x1600x32 @ 0x80000000` + `alpha-probe: PASS` | 0 |
| `sponge-desktop-disk-uefi-nvme` | `fb_boot` | `using boot framebuffer: 2560x1600x32` + `sponge-de: panel shown` | 0 |
| `sponge-desktop-disk-uefi-usb` | `fb_boot` | DE started on OVMF from the USB stick | 0 |
| `sponge-desktop-disk-nvme` | `fb_vesa` (synthesized manifest) | `using 1024x768` + `sponge-de: panel shown` + `partition-check: PASS (Number: 3)` | 2 — see below |

Regressions: `sponge-pkg-driver` (0, incl. the new exclusivity gate),
`sponge-minimal` (0), `sponge-supervisor-takeover` (0, console takeover
and desktop restore by pixels).

**The NVMe variant's non-zero exit is not a display failure.** Its
display comes up through the packaged driver and the desktop renders
on it; the scenario then fails its `alpha-probe` gate because that
probe installs seven desktop packages this media never stages (it
stages the `hello` marker only) and `sponge-de` reports "Could not
find the Qt platform plugin". Both are recorded in docs/13 as the
pre-existing limitation they are, with the measured lines.

**`sponge-debug-firstpaint` and `sponge-debug-scrollstorm`: their probes
pass, their scenario pre-gates do not, and that is pre-existing.** Both
scenarios open with `run_genode_until {.*Genode v.*} 180`. Nothing in
these topologies prints that string: the vendored kernel announces
itself as `Genode 26.08-20-g87c8ebf33f`, while the evidence logs that
predate the Genode-as-submodule switch carry
`Genode v0.2.0-83-gdd452acf23 <local changes>`. The marker the gate
waits for stopped being printed when the version stamp moved to the
submodule, long before this work. The load-bearing markers are present
and good:

```
[init -> system -> debug] sponge_debug: FIRSTPAINT ms=884
[init -> system -> repostorm_probe] repostorm-probe: VERDICT PASS
```

Neither scenario, nor anything that supplies the banner, is touched by
this series. Left as-is deliberately: fixing a stale version-string
gate is unrelated churn, and changing it here would hide a real
repository-wide inconsistency behind this task's diff.

## 7. Receipts

| what | where |
|---|---|
| fb packages | `pkg/fb_vesa/metadata.xml`, `pkg/fb_boot/metadata.xml` |
| shared driver wiring | `run/driver_runtime.inc` |
| staging + the boot-display assertion | `run/driver_runtime.inc::stage` |
| first-boot seeding of the display driver | `run/bake.inc::add_package` |
| exclusivity enforcement | `repos/sponge/src/sponge_pkgd/main.cc` (`_exclusive_peer`, `_report_conflict`), docs/12 §4.6 |
| handover probe | `repos/sponge/src/test/fb_switch_probe/` |
| exclusivity probe | `run/sponge-pkg-driver.run` (`driver_probe` ×2) |
| BIOS product run | `run/sponge-desktop-disk.run` (`SPONGE_FB_SWITCH=on`) |
| UEFI product run | `run/sponge-desktop-disk-uefi.run` |
| logs | `var/run/` (working area, not tracked); the gate markers quoted above |
