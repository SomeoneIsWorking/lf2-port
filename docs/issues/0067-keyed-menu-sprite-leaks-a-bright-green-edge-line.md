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

The native renderer cached guest surfaces as GPU textures, but the DirectDraw `Blt`,
`COLORFILL`, `Unlock`, and GDI `StretchBlt` write paths never called either renderer's existing
dirty hook.  The sampled content hash was only a fallback and can miss a one-row change, so a
reused sheet could retain opaque colour-key pixels from an older upload.

## What was tried / dead ends


## Resolution

Every guest-surface write now invalidates both native texture caches.  At the reported
1177x550 window size, synchronized software and engine captures of `frontend+30` show the menu
edge clean after the change.  The full 12-test suite passes.
