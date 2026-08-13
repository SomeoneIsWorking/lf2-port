---
id: 67
title: Keyed menu sprite leaks a bright green edge line
status: resolved
symptom: The front-end menu renders a one-pixel bright green horizontal strip along the top edge of the selected game-start artwork, exposing the sprite sheet colour-key background.
tags: reported,renderer,sprite,colorkey,menu
created: 2026-08-13
updated: 2026-08-13
---

## Root cause

Two renderer errors made a keyed sheet's border visible. Guest-surface writes did not invalidate
cached GPU uploads, and the engine's manual quads addressed source-rectangle EDGES. An edge is
shared with the adjacent texel; under magnification, nearest-sampler rounding can select the
key-green row outside the menu cell.

## What was tried / dead ends


## Resolution

Every guest-surface write invalidates both native texture caches. Strict sprite-sheet subrects
now address the centres of their first and last texels; whole-surface pictures retain 0..1 UVs.
Four consecutive selected-menu frames at 1177x550 contain zero runs of key-green pixels, and a
1920x1080 clean/injected pair proves the boundary-coordinate arm changes magnified subrects.

`tests/test_texrect.c` asserts all four texel-centre bounds and has the old shared-edge coordinate
as its negative. The renderer differential route remains green.
