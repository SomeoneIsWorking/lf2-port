---
id: 113
title: Edge smoothing (aa) blurs the whole sprite, not just its edges
status: closed
symptom: With the sprite chain's terminal 'aa' step on, the entire fighter goes soft: every source texel becomes a full-width bilinear ramp, so interiors that should stay crisp pixel art are blurred.
tags: reported,renderer,shader
created: 2026-08-25
updated: 2026-09-09
---

## First rejected attempt
The first attempted change narrowed the ramp to ONE SCREEN PIXEL at each chain-pixel boundary:
remap each bilinear weight w by clamp((w - 0.5)/r + 0.5, 0, 1), where r = fwidth(q) is how many
chain pixels one screen pixel covers. r >= 1 (minification) collapses to plain bilinear, which
is what it should be there; under magnification the interior of a chain pixel returns weights of
exactly 0/1, i.e. bit-exact nearest, and only the boundary band blends.
WRONG FIX, do not ship it: gating the blend on an alpha (silhouette-only) test. Issue #112's
report is about interior colour edges staircasing too; the complaint here is the ramp's WIDTH,
not which edges get one.

### Reopened (2026-08-26)
USER 2026-08-26: "I still think sprites aren't rendered at full res (4K in this case) because edge smoothing does nothing here". Treat the no-visible-effect observation as a falsifier of the uncommitted Scale2x-style corner-filter resolution; establish the actual sprite render-target dimensions and per-draw pixel footprint at 4K before changing the filter.
USER 2026-08-26: "Edge smoothing should just convert pixelated edges into smooth edges and nothing else, kind of like xBRz but only for the edges".
This constrains the replacement to edge reconstruction: classify and reconstruct pixel-art
contours at output resolution while preserving flat interiors and intentional interior pixel
detail exactly. Plain bilinear filtering, whole-sprite resampling, and a four-neighbour rule
that only recognizes the simplest 45-degree corner are not sufficient.
