---
id: C046
kind: claim
status: holds
created: 2026-08-21
tags: widescreen,background,renderer
depends: runtime/overrides/backdrop_layout.h#backdrop_plane_placement, runtime/overrides/background.c#fn_0041a250, runtime/video/backdrop.h#backdrop_mirror_segment, runtime/video/backdrop.h#backdrop_bottom_row, runtime/video/ddraw.c#surf_Blt, runtime/video/blt_trace.c#blt_trace_backdrop, runtime/video/render.c#render_blit_mirror_x, tools/routes/backdrop_pixels.py#measure_trace
reconfirmed: 2026-08-22
verified_at: 2026-08-22 16:26:34
---

## Claim

Declared non-looping backdrop placement preserves every source bitmap's native dimensions

## Evidence

2026-08-22: Supersedes the old interpretation of C046 as a `view/span` transform: that transform changed bitmap aspect ratio and was the reported mountain deformation. The replacement is declarative layout in the background owner. Every Lion Forest layer keeps the game's shared origin and exact authored X. Only three exact outer pieces continue RIGHT: opaque far art as 800x70 reflected segments and keyed `(1100,800)` / `(1400,1216)` art as 300x104 / 184x87 reflected segments. Key transparency is preserved; this is not generic keyed-layer coverage. `backdrop_bottom_row` and the same segment planner repeat 800x1 source rows into 800x1 destination rows. The 47 production checks and the positive/nine-negative pixel gate pass. Final 1302x550 captures prove actual scroll: guest camera 1385 -> 1898, draw camera 1131 -> 1644, and shared 1400-plane offset -58 -> -84. The static band changes by 0 bytes and the moving band by 12,493. Both frames have 0 key holes and 0/0/0 excess discontinuity; every one of 10 main and 1,324 continuation rectangles has equal source/destination width and height. Visual review found no black block, smear, hard new cutoff or changed silhouette.

## What would falsify it

A background draw changes a source bitmap's width or height, undeclared art or a non-outer keyed piece receives continuation, keyed continuation becomes opaque, an edge leaves a hole or excess discontinuity, scroll changes a mountain's shape rather than only its shared parallax offset/clipping, or native-width output changes.

## Re-confirmed 2026-08-22

Post-commit distinct-camera Lion Forest evidence remains guest cameras 1385/1898, draw cameras
1131/1644, and 1400-plane offsets -58/-84. I021 reports 0 holes, 0/0/0 seam excess,
0 static changed bytes, 12,493 moving-band changed bytes, and native-equal source/destination
extents for all 10 main plus 1,324 continuation blits. The final Clang build and all 30 offline
tests pass, including structure, clang-format, clang-tidy, 47 backdrop policy checks, and the
instrument's positive/nine-negative controls.
