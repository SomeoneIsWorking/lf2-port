---
id: 33
title: Sprites need scaling up individually now that the frame is native resolution
status: resolved
symptom: reported: rendering at the window's real pixels is right, but at 1:1 a fighter is ~40 px tall in a 1080-row window and reads as tiny. The sprites themselves want scaling up per-sprite -- the world stays at native resolution and full field of view, the actors get drawn bigger. Scaling the whole composition again would undo the native resolution, so this has to be per-quad
tags: renderer
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
Objects are drawn at a whole-number multiple of the size the game drew them, about their own
base. The world stays at native resolution and full field of view; only the actors are
magnified.

The factor is derived from real state, not chosen: how many times the game's own 550-row
screen fits in the window, rounded to a whole number. Whole because these are
nearest-neighbour pixel-art sprites and 1.96 would put some of their pixels down two screen
pixels wide and others three. The game's own window gets 1x and is exactly what it always was;
a 1080-row window gets 2x, so a fighter is the same apparent size as before the frame went
native while the background is twice as sharp and shows twice as much world.

ANCHORED AT THE SPRITE'S OWN BASE, and the first attempt got this wrong. Anchoring at the
ground marker so that a jump grew by the same factor sounds more correct and is not: LF2
launches objects a long way up, and doubling the height of one already 314 px above the floor
put it 628 px up and off the top of the screen. The world is drawn 1:1, so only an actor's
SIZE may change, never its position. A standing object's base IS its ground point, which is
why the two agree everywhere except in mid-air -- the one place the difference matters.
