---
id: 23
title: Widescreen: a stage's sky layer stops at 794 and leaves black beside it
status: open
symptom: in a window wider than 794x550 the upper part of the stage background ends partway across and the rest of that band is black, while the ground and the tiling layers do fill the width
tags: rendering
created: 2026-08-05
updated: 2026-08-05
---

OBSERVED, not reported, while verifying issue #20 -- and it is PRE-EXISTING, not caused by
that change: the same band appears on the commit before it (scratch/wide20old/frame_002250.png
at LF2_WIDESCREEN=1600, and scratch/wide20/frame_002250.png at a 1600x550 window). Both runs
drew different stages, which is itself worth knowing: the VS-mode background is picked at
random, so two runs of the same route are NOT pixel-comparable and a blit-count diff across
them will read as a huge change that is only a different stage.

WHAT IS ALREADY HANDLED, from issue #13, so nobody re-derives it: full-width colour fills are
carried across the viewport, and a tiling layer series the game stops at 794 is continued at
its own period. That is why the ground and the brick courses do reach the edge.

WHAT IS NOT: a stage's sky/backdrop is a single blit of one fixed-width picture. It cannot be
tiled -- it would visibly repeat -- and stretching it is a different picture rather than more
world, which is the whole distinction widescreen exists for.

NOT YET ESTABLISHED, and it decides the fix: whether LF2's own background data gives any
stage a backdrop wider than the viewport it is drawn into. A layer entry in the stage data
carries a width and a scrolling rate, so a backdrop that is wider than 794 and merely
CLIPPED to it would need only the clip widening -- which would be the game's own mechanism.
If it is exactly 794, there is no more picture to show and the honest options are to letterbox
that band deliberately or to leave it. Read the background data before choosing.

DO NOT stretch the backdrop to the viewport width as a fix. It would fill the black, and it
would make one stage's sky a different shape from every other element drawn beside it.
