---
id: I013
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

`tools/e2e.py hidpi` — a simulated HiDPI display: `kwin_wayland --virtual 3840x2160`
with `kscreen-doctor output.1.scale.2`, which makes SDL report a real 2.00 pixel density
(issues #56 and #82).

## Validated by

Run against BOTH classes. Scaled: SDL reports 794x550 points -> 1588x1100 pixels, density
2.00, the port composes 794 columns at world scale 2.000, and an open RmlUi document reports
content scale 2.00 with its 16dp body font computed as 32px. Unscaled (the same nest without
the kscreen-doctor call, Xvfb at -dpi 192, and kwin's own --scale 2): density 1.00, and the
route REFUSES by reading the port's own "unscaled, so this run says nothing about HiDPI" line.
The route now also runs the shipping GPU engine rather than forcing the software compositor:
at 2x it reports 23 game glyphs rasterised at 2.00x with zero cache drops and requires an
`engine: DRAWING (ready)` report. This extends the instrument to the game-font path; it does
not cover labels baked into screen bitmaps (#84).

## Known failure modes

KWin drops a trailing helper-mode argument. The Python route uses the inherited
`LF2_HIDPI_INSIDE` diagnostic flag; replacing it with a command-line mode recursively launches
nested compositors instead of running the game.
