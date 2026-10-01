---
id: 112
title: Magnified character sprites have hard staircased edges; no anti-aliasing exists or is configurable
status: closed
symptom: Sprites are magnified about 2x at the window's resolution with NEAREST sampling, so every silhouette and interior colour edge is a staircase; there is no smoothing option anywhere.
tags: reported,feature,renderer,options,rmlui,ssaa,supersampling
created: 2026-08-25
updated: 2026-09-09
---

## The constraint
- The AA belongs in the engine quad shader as an edge-aware sample: premultiplied 4-tap over
  the source texel cell, exact where texels agree (pixel-art interiors stay crisp), a ramp
  only across edges. Guest alpha is binary and fill_rgba zeroes RGB under the key, so
  premultiplied mixing cannot fringe.
- SHEET BLEED is the trap: frames butt against each other on one BMP, so taps must clamp to
  the quad own UV rect, which means the uniform becomes per-draw and object batches split
  when AA is on. A handful of draws; negligible.
- Objects only (is_object): backgrounds, HUD and text tiles keep NEAREST.
- Default OFF: off IS the original picture, and the recorded renderer comparison diffs GPU against the
  software compositor. The option lives in config as sprite_aa with an LF2_SPRITE_AA initial
  pin for route arms (the issue #69 pattern).

### Note (2026-08-25, same session)
USER 2026-08-25: "add options for sprite rendering, like super sampling but with integer
scale, scale up using integer scale then scale down using bilinear, integer scale should be
either auto or chosen number".
This reshapes the plan into a SPRITE SCALING selector rather than one toggle:
- NEAREST   off, the original picture.
- SMOOTH    the edge-aware premultiplied blend described above.
- SSAA      the requested filter: magnify by an integer N (auto = round(magnification), or
            the chosen number), then sample that intermediate with bilinear on the way down.
The engine computes SSAA ANALYTICALLY in quad.frag -- taps land on floor(c/N) of the
integer-upscaled grid with fract(c) weights, which is exactly what nearest-up-then-
bilinear-down produces -- because per-sprite intermediate render targets would mean a pass
and a texture per painter-ordered sprite. Same output, one draw, no extra targets; the
equivalence is stated in the shader and pinned by the offline gate.
