---
id: C056
kind: claim
status: holds
created: 2026-08-22
tags:
depends: runtime/video/texrect.h#texrect_for_output_blit, runtime/video/render.c#draw_texture_quad, runtime/video/render.c#engine_colour_pass, tests/test_texrect.c#magnified_stretch_matches_output
---

## Claim

Native source mapping classifies each axis from DirectDraw's logical source and destination: a logical 1:1 axis keeps texel-centre endpoints under magnification, while a true stretched axis maps once across the output fragments actually covered.

## Evidence

The shared shipping helper passes a 41->17 stretch over all 51 integer-position and 52 fractional-position output fragments; blanket logical sizing disagrees on 41 and 43 respectively. Both classic and engine call that helper. The pinned classic 3840x1975 route parsed zero engine frames, then measured 0 #00ff1e pixels in the accepted 1:1 arm versus 1,080 at x548..1549/y312..390 in the raster-as-stretch negative.

## What would falsify it

Classic and engine produce different UVs for the same logical/output rectangles, any 41->17 output fragment disagrees with the one-stage output map, or the accepted magnified overlay exposes an adjacent separator.
