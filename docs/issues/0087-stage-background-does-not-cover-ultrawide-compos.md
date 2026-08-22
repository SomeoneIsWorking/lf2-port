---
id: 87
title: Stage background does not cover ultrawide composition
status: open
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

## Correct resolution

The mountain rectangles are separate pieces of complete non-looping parallax planes. Lion
Forest's `forestm1`/`forestm2` share span 1100 and its `forestm3`/`forestm4` share span 1400.
Their opaque outer bitmap edges were authored to remain outside a 794-pixel camera; a
1571-pixel composition displayed those edges inside the frame.

The port now scales a complete backdrop plane as one unit when the live view exceeds its
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

### Resolution (2026-08-21)
The hard cutoffs were opaque outer edges of complete non-looping 1100/1400px backdrop planes exposed by a 1571px view. Pieces sharing an authored span now use one view/span mapping, preserving joins and moving outer edges to the viewport; looping layers, isolated props, and ground remain native. DirectDraw/GPU stretch sampling was unified in texrect.h. Verified by 27 backdrop checks, 2,799 texrect checks, native-width byte identity, a seam-free exact wide engine frame, and max engine/software channel difference 2.

### Reopened (2026-08-22)
USER 2026-08-22: Lion Forest's mountain sometimes stretches depending on scroll position. Two supplied captures show the same mountain plane with different apparent vertical/shape scaling as the camera moves. The accepted plane-widening rule must be scroll-invariant: camera motion may translate the plane according to authored parallax but must never change its scale or shape. Reproduce at multiple camera positions and fix the plane transform rather than adding a screenshot-specific crop or offset.
