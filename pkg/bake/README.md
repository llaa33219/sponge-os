# pkg/bake/ — Sponge OS bake profiles

Bake profiles select which packages and default settings are staged
into product media at image-build time, and (optionally) how the
bootloader behaves. Introduced in Phase 15 (D15.3/D15.4;
`docs/plans/phase15-real-hardware-boot.md`).

## Profiles

| Profile | Packages | Boot config | Use |
|---|---|---|---|
| `minimal` | hello, terminal | production GRUB (hidden gfxterm menuentry) | smallest usable media |
| `desktop` | hello, terminal, textedit, files, calculator, pdf_view, falkon | production GRUB | everyday defaults (the default) |
| `test` | same as desktop | **diagnostic GRUB** (text-mode console, visible 10 s menu, `SPONGE-DIAG:` stage markers; template `run/fixtures/grub-diag.cfg`) | real-hardware boot bring-up — every boot stage is visible |

The `none` sentinel (not a file) opts out of baking entirely
(hello-only regression baseline).

## Selecting a profile

```bash
./tool/dist --bake-profile {minimal,desktop,test,none} --firmware {bios,uefi} --storage {ahci,nvme,usb}
SPONGE_BAKE_PROFILE=test make -C genode/build/x86_64 run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
./tool/bake --list                 # discover profiles
./tool/bake --show test            # inspect one
```

## Format (INI, config_version = 1)

```ini
config_version = 1
name = myprofile
description = What this profile is for.

[packages]
<pkg-name> = enabled        # one per line; pkg/<pkg-name>/metadata.xml must exist

[config]
<configd-key> = <value>     # seeded into configd on first boot (W3)

[theme]
active = <theme-name>       # required

[boot]                       # optional
grub_mode = production | diagnostic
```

- Unknown sections and unknown `[boot]` keys are parse errors (closed
  registry, same philosophy as `sponge_configd`).
- `grub_mode = diagnostic` makes the UEFI scenarios install
  `run/fixtures/grub-diag.cfg` as the ESP's grub.cfg instead of the
  production menuentry. BIOS scenarios ignore `[boot]` (El Torito
  GRUB is upstream-generated).
- Size budgets (D15.5): `minimal` ≤ 1 GiB, everything else ≤ 2 GiB
  (enforced by `run/bake.inc` at staging and `tool/bake` at
  injection).

## Adding a new profile

1. Copy the closest existing profile to `pkg/bake/<name>.profile`.
2. Edit packages/config/theme/boot. Keep `config_version = 1`.
3. Add `<name>` to `ALLOWED_BAKE_PROFILES` in `tool/dist.mojo`,
   `is_valid_bake_profile` in `tool/dist.mojo`, and
   `ALLOWED_PROFILES` in `tool/bake.mojo` (all three, fail-loud).
4. Boot-verify with the matching scenario before relying on it
   (the `test` profile is the reference for diagnostic boot configs).
