---
id: 87
title: Stage background does not cover ultrawide composition
status: resolved
symptom: On an ultrawide Stage-mode results screen, background layers end early and expose black rectangular gaps inside the expanded view
tags: reported,widescreen,background,stage,rendering
created: 2026-08-21
updated: 2026-08-22
---

## Reported

USER 2026-08-21: "the background doesn’t adapt to ultrawide". The supplied screenshot shows black gaps where the mountain/forest background layers end inside the live composition.

## Constraint

Use each layer’s authored span/loop and the live composition width. Do not horizontally
stretch props, hide gaps with a flat fill, or add screenshot-specific offsets.

## Root cause

The semantic far backdrop was already widened across the live composition, but its bitmap
ends at y=198. At the original 794-pixel width, later colour-keyed forest layers cover that
lower edge. The ultrawide view exposes authored horizontal gaps between those later pieces
(one ends at x=1100 and the next starts at x=1216), leaving no surface behind them below
y=198. The black rectangles are therefore genuine uncovered backing, not failed colour-key
transparency; the traced surfaces had a valid key range of 0..0 and used `DDBLT_KEYSRC`.

## First attempt (rejected)

`runtime/video/backdrop.h` plans a clamp-to-edge continuation of the semantically marked far
backdrop's final source row behind the later keyed layers. It repeats existing backdrop pixels
only; it does not stretch or tile a prop, add new geometry, or guess from the screenshot. The
extension stops at the measured world/UI boundary y=528 so the game's 22-row black mode-caption
strip remains intact. The software and native-engine display lists use the same rectangle.

On the reproduced 1571x550 Lion Forest result frame, the formerly black x=1100..1216,
y=198..219 gap contains zero black pixels after the fix. `tests/test_backdrop.c` verifies the
semantic gate, source-row clamp, live width, world boundary, empty source, and no-op cases.

### Reopened (2026-08-21)
USER 2026-08-21: "not a good look still". The final-row backdrop clamp removes black but exposes the nearer non-looping mountain layers as abrupt vertical rectangles. The accepted outcome must remove these hard seams rather than merely replace black with sky colour.

### Dead end (2026-08-21)
Extending only the far backdrop final row removed black pixels but left the nearer 1100/1400-pixel non-looping mountain planes at native width, exposing their opaque bitmap edges as vertical cutoffs. Pixel coverage was not visual correctness.

## Second attempt (rejected)

The mountain rectangles are separate pieces of complete non-looping parallax planes. Lion
Forest's `forestm1`/`forestm2` share span 1100 and its `forestm3`/`forestm4` share span 1400.
Their opaque outer bitmap edges were authored to remain outside a 794-pixel camera; a
1571-pixel composition displayed those edges inside the frame.

The port then scaled a complete backdrop plane as one unit when the live view exceeded its
authored span. Every bitmap piece uses the same `view/span` mapping, so authored joins remain
joined and the plane's outside edges land at the viewport edges. Selection is data-driven:
non-looping pieces must share a span, start at that plane's left edge, occupy distinct X
positions, and sit above the walkable floor. The first painted backdrop also qualifies. This
deliberately excludes isolated CUHK lamps and grass, foreground decorations, ground strips,
and every declared looping layer.

On the deterministic 1571x550 Lion Forest route, the 1100-pixel band maps to x=0..1571 with
its piece boundary shared at x=1142, and the 1400-pixel band maps to the same viewport. The
reproduced frame has no interior vertical bitmap cutoffs. Native-width geometry remains
unchanged because no span scaling is active at 794.

The first widened GPU capture also exposed a separate sampling-contract error: the software
DirectDraw blitter selected `floor(pixel * source / destination)`, while the engine
interpolated from the first source texel centre to the last. That changed 19,226 pixels in the
scaled bands. `runtime/video/texrect.h` now owns both policies and makes stretched rectangles
follow the DirectDraw integer map. Its exhaustive ratio checks include both Lion piece widths;
the corrected 1571x550 engine frame differs from software by at most two channel levels, at
antialiased text edges only.

### Rejected resolution (2026-08-21)
The hard cutoffs were opaque outer edges of complete non-looping 1100/1400px backdrop planes exposed by a 1571px view. Pieces sharing an authored span now use one view/span mapping, preserving joins and moving outer edges to the viewport; looping layers, isolated props, and ground remain native. DirectDraw/GPU stretch sampling was unified in texrect.h. Verified by 27 backdrop checks, 2,799 texrect checks, native-width byte identity, a seam-free exact wide engine frame, and max engine/software channel difference 2.

### Reopened (2026-08-22)
USER 2026-08-22: Lion Forest's mountain sometimes stretches depending on scroll position. Two supplied captures show the same mountain plane with different apparent vertical/shape scaling as the camera moves. The accepted plane-widening rule must be scroll-invariant: camera motion may translate the plane according to authored parallax but must never change its scale or shape. Reproduce at multiple camera positions and fix the plane transform rather than adding a screenshot-specific crop or offset.

### Root cause of the recurrence

The mountain deformation was not an intermittent sampler fault. It was the previous coverage
policy itself: `backdrop_scale_span` changed only destination X coordinates by `view/span`.
Lion Forest's 1100-pixel mountain therefore stayed native in the supplied 1093-pixel view but
became 1.184 times wider in the supplied 1302-pixel view, while its height never changed. The
generic 794-to-viewport fallback and the far-backdrop bottom extension were two more bitmap
stretch paths. A camera position could reveal different overlaps between these inconsistently
sized parallax planes, making the deformation look scroll-dependent.

### No-scale replacement

All three stretch paths are removed. `runtime/overrides/backdrop_layout.h` is explicit port art
layout, keyed by the stage's own parsed name and exact authored layer X. Every Lion Forest piece
keeps the game's shared world origin: independently centring the 800/1100/1400 spans was rejected
because it moved their baked pixels by different +400/+250/+100 translations and exposed new
rectangular boundaries. The first repeat experiment was also invalid: `forestm1` and `forestm3`
are keyed partial mountain art, not tiling coverage; duplicating their valid dark pixels at
x=1100/1400 created block-like joins. They are now drawn once at their original relative X.
Only the opaque 800-pixel far picture may continue to the right, as an X-reflected native-size
800x70 segment. The joined texel is source x=799 on both sides, so the join is continuous without
duplicating the complete mountain identity. Two exact outer pieces are additionally authored to
continue RIGHT while preserving their colour key: `(span=1100,x=800)` in native 300x104 segments
and `(span=1400,x=1216)` in native 184x87 segments. This continues only the edges that would
otherwise become vertical cutoffs; it does not generically repeat keyed layers. Other stages and
undeclared layers inherit nothing.

The far backdrop's final row is repeated as native 800x1 source rows into native 800x1
destination rows rather than one tall destination blit; each right-side row uses the same
native-size reflected segment planner. The 47 production-policy checks prove shared-origin
placement, native-width no-op, exact per-piece continuation policy, native source/destination
dimensions and zero synthetic coverage holes. `backdrop_pixels.py` has a positive plus nine
negative controls: black/green key holes, hard x=800/x=1100/x=1400 joins, a changed supposedly
static band, two equal camera values, and stretched main or continuation rectangles all fail.

### Resolution (2026-08-22)

The final 1302x550 engine captures are genuinely separated by scroll: the guest camera moves
1385 -> 1898, the draw camera moves 1131 -> 1644, and both pieces of the 1400 plane share an
offset that moves -58 -> -84. The static far band changes by 0 bytes while the moving band
changes by 12,493, so this run exercises translation rather than comparing two copies of one
frame. Across both selected frames, all 10 main backdrop blits and all 1,324 authored
continuation/bottom-row blits have source width and height exactly equal to destination width
and height. The two frames have zero black/green key holes and 0/0/0 excess discontinuity at
the x=800/x=1100/x=1400 joins. Visual review of
`scratch/screenshots/lion-forest-scroll-camera-1385.png` and
`lion-forest-scroll-camera-1898.png` found the accepted shared-origin composition intact with
no black block, smear, hard new cutoff or changed mountain silhouette. No background bitmap is
stretched; camera motion only changes authored parallax translation and ordinary clipping.
