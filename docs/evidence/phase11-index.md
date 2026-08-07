# Phase 11 evidence index — DE customization and panel strengthening

> Phase 11 of `docs/09-roadmap.md` §10. Work plan:
> `docs/plans/phase11-de-customization.md` (Metis + Prometheus +
> Momus-reviewed, rev. 2). All four completion criteria GREEN.

| WS | Deliverable | Evidence | Result |
|----|-------------|----------|--------|
| W0 | Regression baseline (fresh env) | `task-0-phase11-baseline.md` + 6 per-scenario logs | 6/6 green |
| W1 | configd: 7-key registry + kind validators + 8 KiB theme transport | `task-5-phase11-linux-config-probe.log` (20-step matrix) | `config-seq-probe: PASS` |
| W2 | ConfigController + panel/launcher restyle migration | `task-5-phase11-linux-panel-config.log` (P1–P7) | `phase panel-config PASS` |
| W3 | theme surface + dark/compact + fallback liveness | `task-5-phase11-linux-theme.log` (5-step 3-theme probe) | `theme-probe: PASS` |
| W4 | themed_decorator drop-in + bridge + decor.tar | `task-4-phase11-themed-chrome.md` + `-final.log` + `-wm-qmp-regression.log` | `wm-probe: PASS` ×2 |
| W5 | panel-config on seL4 + payload-size host gate + sweep | `task-5-phase11-scenarios.md` + per-scenario logs | 8/8 green + 1 known flake |

## Criterion → scenario → PASS marker

1. **Panel customization through sponge_configd** —
   `run/sponge-config-probe.run` (20-step key matrix, base-linux),
   `run/sponge-panel-config.run` (base-linux),
   `run/sponge-panel-config-sel4.run` (seL4) →
   `config-seq-probe: PASS`, `sponge-de-probe: phase panel-config PASS`.
2. **Expanded theme surface** — `run/sponge-theme.run` (5-step probe:
   default→dark→compact→light 3-way coherence + pixel diffs,
   does-not-exist graceful fallback, liveness proof) →
   `theme-probe: PASS`; host gate `./tool/test_theme_payload_size`
   (4 themes ≤ 8192 B).
3. **Sponge-themed window chrome** — `run/sponge-de-themed-chrome.run`
   (seL4+QMP): themed_decorator drop-in, live bridge-delivered policy
   color, nonzero decorator_margins (20/8/1/1), title-bar tint
   RGB(91,91,100) vs untinted (180,180,191), tablet drag verified →
   `wm-probe: PASS`; Phase-10 drag regression `run/sponge-wm-qmp.run`
   stays green.
4. **Documented + scenario-verified** — this index,
   `docs/08-development.md` §"Phase-11 DE customization",
   `run/README.md` entries, `docs/10-theme-format.md` (W3 surface).

## Known issues carried forward (roadmap §11.3)

- `sponge-de-sel4-interactive.run` launch-click flake (PS/2 REL
  drift; tablet-absolute path is the Phase-12 fix) —
  `task-5-phase11-sel4-interactive-FLAKE.log`.
- themed_decorator live asset re-skin (tar cached in upstream
  statics; policy color is live, texture swap needs restart/patch).
- `panel.position` boot-time-only (nitpicker domain owns placement).
- Themed drag delta partial (+18/+10 of +100/+100; mechanics verified,
  timing-hardening candidate).

## Fresh-environment setup notes (W0 lessons, all committed)

- `ftpmirror.gnu.org` 502 → bash/ncurses ports re-pointed to
  ftp.gnu.org (patch ledger #8, `docs/11-environment.md` §4).
- `tool/build ports` skips already-prepared ports (dde_rump's
  re-prepare breaks on its own git update step).
- `tool/build prepare` no longer clobbers `BOARD ?= pc` on re-run,
  and moves `repos/base-$(KERNEL)` after `repos/base` (forwarding
  target.mk shadowing broke `hpet_timer` with
  `cannot find component.o`).
- Port set gained `stb` and `ttf-bitstream-vera` (alpha scenario).
