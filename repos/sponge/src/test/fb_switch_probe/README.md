<!--
  fb_switch_probe — the runtime framebuffer-driver switch proof.

  What it proves, in the PRODUCT media topology (docs/17 §1): the display
  driver is an ordinary package, so switching it is a package operation
  and nothing else has to be re-wired.

    1. the media's seeded fb package really is a CHILD of
       driver_runtime — read from driver_runtime's own sandbox state
       report, which only that init generates from its own child table;
    2. the display is genuinely up BEFORE the switch — a Capture pixel
       oracle over the composited screen, requiring a rendered (non
       background) desktop, not merely a live session;
    3. `vct install <other-fb>` is REFUSED with `exclusive-conflict`
       naming the running driver (docs/12 §4.6) — two fb drivers must
       never race for the single PCI VGA resource;
    4. after `remove <active>` + `install <other>` the OTHER package
       appears as a driver_runtime child, and
    5. the screen comes BACK and SETTLES: the pixel readout is
       re-sampled until it stops changing, and the settled frame is
       required to contain a rendered desktop again.

  (5) is the assertion that makes the switch a display claim rather
  than a config claim. A fb driver that started and then failed to map
  or blit leaves a black screen; a client that did not repaint leaves
  a stale or empty one. Neither settles to a rendered frame.

  Every phase is bounded by a wall-clock budget and reports WHICH phase
  timed out — docs/09-roadmap.md §11.1 makes a silent hang the worst
  possible failure mode, so nothing here polls forever.

  Config:
    <config active: fb_vesa | alternate: fb_boot
      | timeout_ms: 60000 | settle_ms: 4000
      | screen_w: 1024 | screen_h: 768 />
-->