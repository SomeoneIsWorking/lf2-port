---
id: I013
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/e2e.sh hidpi -- a SIMULATED HiDPI display: kwin_wayland --virtual 3840x2160 with kscreen-doctor output.1.scale.2, which makes SDL report a real 2.00 pixel density (issue #56)

## Validated by

Run against BOTH classes. Scaled: SDL reports 794x550 points -> 1588x1100 pixels, density 2.00, and the port composes 794 columns at world scale 2.000 into the full drawable. Unscaled (the same nest without the kscreen-doctor call, and Xvfb at -dpi 192, and kwin's own --scale 2): density 1.00, and the route REFUSES -- it reads the port's own 'unscaled, so this run says nothing about HiDPI' line and fails on it rather than asserting anything.

## Known failure modes

(none recorded yet)
