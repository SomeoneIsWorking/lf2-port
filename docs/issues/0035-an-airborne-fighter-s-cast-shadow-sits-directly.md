---
id: 35
title: An airborne fighter's cast shadow sits directly under them instead of offset along the light
status: resolved
symptom: reported: with the key light from the left, a character in the air should throw its shadow to the side in proportion to how high it is. The shadow is drawn at the ground marker with only a SHEAR applied and no offset by height, so it stays under the fighter however high they jump -- which reads as the shadow being glued to them
tags: reported,rendering,shadows
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
Fixed as part of the projection rewrite -- see issue #38, which replaced the whole shape of
the shadow rather than just its offset.

TWO THINGS WERE WRONG, and the second only showed up because of the first. The shadow was
placed at the ground marker with no height term at all, so it stayed welded under a jumping
fighter. Adding the term then exposed a PAIRING bug that had been invisible: 'the sprite after
the marker' is the right rule until a sprite is dropped, and a sprite clipped entirely off the
composition never enters the display list, so the marker bound to the NEXT object's sprite
instead. Measured: a marker at x 988 paired with a sprite at x 2076, a thousand pixels away
and above the top of the screen, reporting an object 874 px in the air on a 550-row field. The
pairing is now checked against the game's own geometry -- the ellipse is drawn AT the object's
feet, so the sprite must overlap it horizontally -- and 102 orphaned markers per run are
discarded and counted rather than drawing a shadow under nothing.

VERIFIED with a route that jumps (east@match+270): the highest an object got off its ground
point was 332 px and its shadow was displaced along the light by that much. The report prints
the maximum, and says explicitly when it is zero that NOTHING jumped and the term was never
exercised -- a run where nobody leaves the floor cannot tell 'it works' from 'it never ran'.
