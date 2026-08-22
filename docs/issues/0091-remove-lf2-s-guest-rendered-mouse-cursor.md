---
id: 91
title: Remove LF2's guest-rendered mouse cursor
status: resolved
symptom: The game draws its own white arrow cursor in addition to the host cursor; remove the game-rendered cursor without breaking mouse input
tags: reported,ui,rendering,mouse,cursor
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The original game explicitly draws an 11x19 cursor sprite from the sheet stored at
`0x00451170`. The existing removal compared the draw coordinates with the current pointer
coordinates. That correlation was useful discovery evidence, but it was not the draw's identity;
layout transforms can change coordinates independently and let the guest cursor through.

Tracing the producer before the shared `fn_0043f010` draw helper identified the three exact
cursor call sites: `0x00424660`, `0x00428778`, and `0x004329ea`. The sheet itself cannot be
dropped because it also holds menu artwork.

## What was tried / dead ends

Declining every draw from the sheet removes 51,492 pixels of menu art. Comparing pointer and draw
coordinates is the old fragile path. `LF2_CURSOR_ON=1` was also the wrong product contract: the
game should never restore a second cursor through a diagnostic switch.

## Resolution

### Resolution (2026-08-22)
Removed the guest cursor by its three exact producer call sites plus shared cursor sheet; removed the coordinate coincidence and LF2_CURSOR_ON restoration path.
