---
id: 37
title: The key light's angle should be adjustable from a pause-menu Options screen
status: resolved
symptom: reported: the light direction is a compiled-in constant and should be something a player can set, from the pause menu under an Options item. RmlUi (as Dusklight uses for its game-facing UI) was raised as a possible way to build the screen
tags: reported,ux,pause,lighting
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
The pause menu has an OPTIONS page and the light's direction is the player's.

Two rows, LIGHT ANGLE and LIGHT HEIGHT, in degrees: which way the light comes from and how
high it is. Left/right adjust in 5-degree steps, confirm nudges (so a device with no left and
right still works rather than looking broken), BACK returns. It takes keyboard, pad and mouse
like every other menu here because it is built from the same rows, and it is over a FROZEN
frame -- so the shadows move while you watch, which is the whole reason this belongs in the
pause menu rather than a launcher.

The two angles are the single source: hd2d_light_set_angles recomputes the one direction
vector, so the shading, the shadow's direction and the shadow's LENGTH all follow together
(issue #38).

RMLUI WAS CONSIDERED AND DECLINED, and the reasoning is in runtime/app/pause.c beside the code.
Dusklight uses it for its game-facing UI and is right to -- it has documents, components and a
settings tree to build. This is two numbers on a menu that already exists, already takes all
three devices, and is already drawn with the game's own glyphs so it looks like the game.
RmlUi is C++ with its own build, font stack and render backend; it would become the largest
dependency in a port whose entire build is a C compiler and SDL. If a real settings screen
ever lands, that judgement should be revisited and Dusklight's src/dusk/ui is where to start.

NOT DONE: the setting does not persist across runs. There is no config file in this port yet,
and inventing one for two numbers is the wrong order to do things in.
