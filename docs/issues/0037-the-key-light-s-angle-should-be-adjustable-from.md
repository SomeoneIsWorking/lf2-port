---
id: 37
title: The key light's angle should be adjustable from a pause-menu Options screen
status: resolved
symptom: reported: the light direction is a compiled-in constant and should be something a player can set from the pause-menu Options screen
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

The original two-value menu used the existing LF2 UI path. The later full
settings tree is now owned by the project's dedicated RmlUi modules.

NOT DONE: the setting does not persist across runs. There is no config file in this port yet,
and inventing one for two numbers is the wrong order to do things in.
