---
id: 67
title: Keyed menu sprite leaks a bright green edge line
status: closed
symptom: The front-end menu renders a one-pixel bright green horizontal strip along the top edge of the selected game-start artwork, exposing the sprite sheet colour-key background.
tags: reported,renderer,sprite,colorkey,menu
created: 2026-08-13
updated: 2026-09-09
---

## Root cause
The first fix changed only the optional engine. `run.sh` uses the SDL display-list renderer,
whose `SDL_RenderTexture` source rectangle addressed the boundaries shared with texels outside
the selected menu cell. On the real Wayland display at its 2.5x pixel scale, raster rounding
selected the sheet's key-green border.
