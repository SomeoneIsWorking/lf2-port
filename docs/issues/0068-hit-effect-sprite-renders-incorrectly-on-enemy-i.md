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

The supplied enlargement shows that the white square with a dark centre is an eye from the
neighbouring animation cell, attached to the left edge of the struck fighter's hair. It is not
an impact effect. `run.sh` passed the cell's integer boundaries to `SDL_RenderTexture`; on the
scaled Wayland output, the left boundary could select the preceding sheet cell.

## Resolution

All display-list sprites, including combat characters and short-lived effects, now use the same
explicit texel-centre quad as the menu. This prevents all four forms of adjacent-cell bleed while
preserving the cell and full-surface paths. `LF2_TEXRECT_EDGE=1` restores the complete old
`SDL_RenderTexture` call, rather than the earlier ineffective approximation that changed only
manual-engine UVs. The native renderer still matches the software compositor at its differential
gate.

The two earlier resolutions were wrong: the first combat route captured Louis's `broken.bmp`
debris rather than this Bandit frame, and `cb4c5b2` changed only the optional engine even though
the user reported both defects in `run.sh`.
