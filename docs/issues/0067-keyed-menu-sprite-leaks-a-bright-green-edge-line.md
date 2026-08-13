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

The first fix changed only the optional engine. `run.sh` uses the SDL display-list renderer,
whose `SDL_RenderTexture` source rectangle addressed the boundaries shared with texels outside
the selected menu cell. On the real Wayland display at its 2.5x pixel scale, raster rounding
selected the sheet's key-green border.

## What was tried / dead ends


## Resolution

The shared display-list draw now submits an explicit quad whose strict sprite subrect runs from
the centre of its first texel to the centre of its last. This is the path `run.sh` uses as well
as the input to the optional engine; whole-surface pictures retain 0..1 UVs.

`LF2_TEXRECT_EDGE=1` restores the complete old `SDL_RenderTexture` call as the negative arm. At
the reported 1177x550 Wayland window (2943x1375 pixels, display scale 2.5), that arm emits a
640-pixel run of the screenshot's exact `(0,255,12)` key colour; the corrected arm emits zero.
`tests/test_texrect.c` asserts all four bounds, and the renderer differential remains green.

The issue was reopened after `cb4c5b2`: that commit changed only `engine_colour_pass`, while
`run.sh` still passed integer-edge rectangles to `SDL_RenderTexture`. The resolution above is
the subsequent shared-path fix.

### Resolution (2026-08-13)
Default run.sh SDL display-list sprites now use explicit texel-centre quads. On the real 2.5x Wayland display the complete old SDL_RenderTexture negative reproduces a 640-pixel run of exact key colour (0,255,12); corrected path produces zero. ctest 13/13 and renderer differential pass.
