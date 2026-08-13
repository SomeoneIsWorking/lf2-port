---
id: 66
title: Stage preview composition exposes black gaps and unlit geometry
status: resolved
symptom: LF2_STAGE_PREVIEW captures show black wedges/voids and near-black hand-woven solids over the selected PvE background, making the gallery unsuitable to show the authored scenes.
tags: reported,stage,renderer,diagnostic,hd2d
created: 2026-08-13
updated: 2026-08-13
---

User screenshot on 2026-08-13 showed Lion Forest at a wide composition with uncovered background columns and dark added props. Scope was subsequently narrowed to graphical correctness of the original painted stages: no new art, props, or 3D reconstruction.

### Findings (2026-08-13)

The black columns came from non-looping backmost layers authored for the original 794-pixel
view.  Repeating or stretching every non-looping layer is wrong because narrow non-looping
layers also contain foreground details.  The background override now marks only layer zero,
and DirectDraw extends only that existing painted backdrop to the wide composition.  Looping
layers retain their authored repetition and all other layers retain their authored rectangles.

The deterministic selector now sets the game's background selection during pre-fight setup and
checks that match initialization preserves it; it no longer swaps rendering state during a draw.

### Resolution (2026-08-13)

The added `.stage` scenes and model pack were deleted, including stale copies beside the binary.
All nine real PvE backgrounds were selected independently and captured at 1920x1080 after match
initialization.  Each selector check survived initialization and the gallery has no uncovered
stage columns, added geometry, or composition wedges.
