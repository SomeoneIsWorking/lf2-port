---
id: 68
title: Neighboring Bandit animation cell bleeds into struck sprite
status: resolved
tags: reported,renderer,sprite,combat
created: 2026-08-13
updated: 2026-08-13
---

USER (2026-08-13): "hitting an enemy, the hit effect is wrong too"

The impact effect must be reproduced in both software and GPU render paths. Determine whether the defect is source-rectangle/color-key sampling, blend/compositing, animation selection, or placement. Do not repaint or replace game assets. Acceptance: the original hit-effect sprite renders without edge leakage, corruption, incorrect blending, clipping, or placement during a real enemy hit.

## Root cause

The supplied enlargement shows that the white square with a dark centre is an EYE attached to
the left edge of the struck fighter's hair. It is not an impact effect. The engine normalized
the animation cell's integer rectangle edges directly; the left coordinate therefore lay on
the boundary shared with the preceding sprite-sheet cell, and magnified nearest sampling could
select that neighbour.

## Resolution

Manual-engine subrects now run from the centre of the first texel to the centre of the last.
This prevents all four forms of adjacent-cell bleed while preserving the cell's dimensions and
the full-surface path. `LF2_TEXRECT_EDGE=1` restores the faulty coordinates as a test-only
discriminator; at 1920x1080 it changes the magnified animation cells while the clean arm stays
within them. The native renderer still matches the software compositor at its differential gate.

The earlier resolution was wrong: renderer agreement only showed both paths consumed the same
composition, and the first combat route captured Louis's `broken.bmp` debris rather than this
Bandit frame. The user's screenshot supplied the missing discriminator.
