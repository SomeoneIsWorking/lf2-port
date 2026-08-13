---
id: 68
title: Native object pass draws hit effects from the glyph sheet
status: resolved
tags: reported,renderer,sprite,combat
created: 2026-08-13
updated: 2026-08-13
---

USER (2026-08-13): "hitting an enemy, the hit effect is wrong too"

The impact effect must be reproduced in both software and GPU render paths. Determine whether the defect is source-rectangle/color-key sampling, blend/compositing, animation selection, or placement. Do not repaint or replace game assets. Acceptance: the original hit-effect sprite renders without edge leakage, corruption, incorrect blending, clipping, or placement during a real enemy hit.

## Root cause

Commit `9c07a7a` replaced the game's object pass, `fn_0041a5a0`, with
`runtime/overrides/objects.c`. Its effects loop passed `LD32(0x0044faf4)`, the glyph sheet, as
the `this` receiver to `fn_0043f010`. The original instructions at `0041ad17`, `0041ad6b`, and
`0041adbc` load the receiver from `0x0044f8fc`, the blood/impact-effect sheet. Consequently the
effect state and clip number were correct, but the clip was read from unrelated artwork.

## Resolution

The effects loop now uses the binary's `0x0044f8fc` sheet pointer. The glyph pointer remains
confined to multiplier and name-tag drawing.

The two earlier resolutions were wrong. The first route captured Louis's `broken.bmp` debris;
the second interpreted the bad clip as neighbouring-cell bleed. Renderer comparisons could not
locate this defect because the wrong sheet was selected before both the software compositor and
GPU paths. Manual bisection established `9c54ac8` as good and its child `9c07a7a` as bad.

### Reopened (2026-08-13)
User confirms identical hit bug with LF2_ENGINE=1 and LF2_RENDERER=soft. This falsifies renderer-specific texel sampling as the cause; defect exists in or before the software composition. Must compare against vanilla and trace the composing blit.

### Resolution (2026-08-13)
Bisection found 9c07a7a as first bad commit. Its fn_0041a5a0 override used glyph-sheet pointer 0x0044faf4 for effect clips; original instructions load effect sheet 0x0044f8fc at 0041ad17/6b/bc. Corrected receiver restores blood burst. User visually confirmed; ctest 13/13 and objects pixel/state differential pass.
