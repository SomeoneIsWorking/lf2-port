---
id: C052
kind: claim
status: holds
created: 2026-08-22
tags:
depends: runtime/video/render.c#engine_colour_pass, runtime/video/engine.c#engine_draw, runtime/video/engine.c#targets_make, runtime/overrides/geom.h#geom_compose_width, tools/routes/fullres_test.py#main
reconfirmed: 2026-08-26
verified_at: 2026-08-26 22:02:36
---

## Claim

The native engine treats composition dimensions as world coordinates and rasterises every draw into the full output target. When the aspect-derived view fits within `WIDE_MAX`, its integral composition width never undershoots the drawable; beyond that allocation limit, coverage is necessarily capped.

## Evidence

runtime/video/render.c passes output placement/scale and drawable dimensions into engine_colour_pass, which copies the resulting target 1:1. test_geom checks both output edges and an extreme-aspect cap before integer narrowing. A 3840x1975 offscreen capture reported 1070x550 world into a 3840x1975 target with 3842x1975 coverage at (-1,0), and its first/last columns were both nonblack (means 0.241/0.204). `tools/e2e.py fullres` is the persistent falsifier for the target report and captured edge coverage.

## What would falsify it

an engine path submits scale 1/composition dimensions and enlarges the finished target, an uncapped wide drawable has an uncovered edge, the engine allocates a target other than the reported drawable size, or host font/SVG coverage is quantised to the composition grid

## Re-confirmed 2026-08-26

Reverified 2026-08-26 after sprite-AA/source-rectangle changes: tools/e2e.py fullres reported composition 1070x550 at scale 3.591 drawn into 3842x1975, engine target 3840x1975, exact 3840x1975 capture and zero black pixels in either outer column; tools/e2e.py sprite_passes independently required five 3840x1975 match captures; tools/e2e.py render kept engine/software within max channel diff 2.
