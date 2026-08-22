---
id: C055
kind: claim
status: falsified
created: 2026-08-22
tags:
depends: runtime/video/render.c#draw_texture_quad, tests/test_texrect.c#overlay_output_sample_uses_logical_destination
falsified_on: 2026-08-22
---

## Claim

A legacy DirectDraw blit's source mapping is derived from its logical destination before composition-to-output scaling; the output transform changes geometry only.

## Evidence

Issue #96's shipping helper test maps the reported 3840x1975 overlay edge to source (330,254), while the old raster-feedback mutation maps it to adjacent (329,253). The real overlay_sampling route then measured 0 exact #00ff1e pixels with logical mapping and 1,080 with the mutation (1,002px row, 79px column).

## What would falsify it

A real magnified overlay rendered through draw_texture_quad exposes an adjacent source separator with logical destination mapping, or the raster-feedback negative no longer exposes one.

## FALSIFIED 2026-08-22

The statement was too broad: deriving one continuous UV interval from the logical destination fixes magnified 1:1 blits but does not preserve DirectDraw's discrete logical StretchBlt result when that result is magnified. A 41->17 stretch at 3x disagrees on 27/51 fragments.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
