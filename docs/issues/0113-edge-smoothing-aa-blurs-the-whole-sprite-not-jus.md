---
id: 113
title: Edge smoothing (aa) blurs the whole sprite, not just its edges
status: resolved
symptom: With the sprite chain's terminal 'aa' step on, the entire fighter goes soft: every source texel becomes a full-width bilinear ramp, so interiors that should stay crisp pixel art are blurred.
tags: reported,renderer,shader
created: 2026-08-25
updated: 2026-08-26
---

USER 2026-08-25: "edge smoothing should only smooth the edges not the whole sprite".

## What is actually happening

runtime/shaders/quad.frag's sample_chain() takes a plain bilinear of the chain image when
f_data.z (aa) is set. At magnification the ramp between two chain pixels spans the WHOLE cell
-- centre to centre -- so at ~2x every screen pixel of the sprite sits inside some gradient and
the whole picture reads as blurred, interiors included.

This contradicts the constraint issue #112 recorded for this step in the first place:
'premultiplied 4-tap over the source texel cell, exact where texels agree (pixel-art interiors
stay crisp), a ramp only across edges'. The implementation shipped as unconditional bilinear
and lost that.

## First rejected attempt

The first attempted change narrowed the ramp to ONE SCREEN PIXEL at each chain-pixel boundary:
remap each bilinear weight w by clamp((w - 0.5)/r + 0.5, 0, 1), where r = fwidth(q) is how many
chain pixels one screen pixel covers. r >= 1 (minification) collapses to plain bilinear, which
is what it should be there; under magnification the interior of a chain pixel returns weights of
exactly 0/1, i.e. bit-exact nearest, and only the boundary band blends.

WRONG FIX, do not ship it: gating the blend on an alpha (silhouette-only) test. Issue #112's
report is about interior colour edges staircasing too; the complaint here is the ramp's WIDTH,
not which edges get one.

### Superseded resolution (2026-08-25)
quad.frag's aa step now remaps each bilinear weight through spritechain_edge_weight(): the blend band is one screen pixel at the chain-pixel boundary (ramp = fwidth(q)) instead of the whole cell, so interiors return weights of exactly 0/1 and stay bit-exact nearest. tests/test_spritefilter.c walks the ramp.

### Discarded follow-up (2026-08-25)
The narrowed box filter was still wrong: at integer magnification texel boundaries land on pixel boundaries, so every weight was 0 or 1 and aa degenerated to nearest. A Scale2x 45-degree corner rule and a private sprite-page prepass were then attempted. The 2026-08-26 4K falsifier showed that the exact-colour rule remained visually ineffective on shaded LF2 contours, while code inspection showed the prepass merely ran the same shader and copied its result; both were removed by the final resolution below.

### Reopened (2026-08-26)
USER 2026-08-26: "I still think sprites aren't rendered at full res (4K in this case) because edge smoothing does nothing here". Treat the no-visible-effect observation as a falsifier of the uncommitted Scale2x-style corner-filter resolution; establish the actual sprite render-target dimensions and per-draw pixel footprint at 4K before changing the filter.

USER 2026-08-26: "Edge smoothing should just convert pixelated edges into smooth edges and nothing else, kind of like xBRz but only for the edges".

This constrains the replacement to edge reconstruction: classify and reconstruct pixel-art
contours at output resolution while preserving flat interiors and intentional interior pixel
detail exactly. Plain bilinear filtering, whole-sprite resampling, and a four-neighbour rule
that only recognizes the simplest 45-degree corner are not sufficient.

### Resolution (2026-08-26)
Root cause: the prior aa path was an exact-colour five-tap Scale2x corner rule, so it recognized only simple 45-degree elbows, visibly left most shaded LF2 contours staircased, and changed scattered interior corners; its private sprite page was a redundant same-shader draw plus copy, not extra resolution or filtering. The renderer was already proven at 3840x1975. Fix: remove the unlanded prepass, pass source origin/extent explicitly instead of inverting extent-1 texel-centre UV endpoints, and reconstruct edges in the full-resolution scene shader with an original alpha-aware perceptual 3x3 classifier, inside/outside continuation and shallow/steep wedge coverage. The persistent 4K route now isolates aa: 3,920 pixels changed on real fighters and 0 changed in flat 5x5 interiors; nearest:2 remains byte-exact, fullres passes, renderer A/B passes, all 41 CTests pass under Clang, and SPIR-V/MSL are current.
