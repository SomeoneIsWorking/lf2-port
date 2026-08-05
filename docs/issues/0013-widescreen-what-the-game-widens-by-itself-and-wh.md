---
id: 13
title: Widescreen: what the game widens by itself, and what has to be centred
status: resolved
symptom: a wider window only scales the 794-wide picture instead of showing more of the stage
tags: widescreen,rendering,viewport,camera,method
created: 2026-08-05
updated: 2026-08-05
---

`LF2_WIDESCREEN=<w>[x<h>]`. Measured, in the order it was found:

1. Enlarging only the primary surface STRETCHES. The game composes into an off-screen
   surface it asks for at exactly 794x550 and copies that to the primary in one blit, so a
   bigger primary just scales it.
2. The viewport size is not only immediates. Scanning a mid-match .data dump for the literal
   794 found three width/height pairs -- 0x0044d014/0x0044d018, 0x0044d78c/0x0044d790,
   0x00453cd4/0x00453cd8. Widening the compose surface AND writing those gives real
   widescreen: the camera clamp, the background layer loops and the sprite draw all follow.
   (Which of the three is load-bearing is not yet narrowed; all are written.)
3. What does NOT follow, and why each is handled the way it is:
   - the HUD strip is eight player slots as two rows of four 198x54 panels, 792 px, not a
     width-driven tiling. The panels are tiled out to the edge; an unused slot already looks
     like an empty panel, so the extra ones read as more empty slots.
   - full-width COLORFILL bands (sky, ground, road) use an immediate width. A fill spanning
     exactly 0..794 is extended to the viewport -- it is a full-width band by intent.
   - a backdrop drawn from x 0 across the whole native width in ONE blit (the sky panorama)
     cannot be made wider by drawing more of it, so it is stretched. Soft gradients over a
     third more width; the alternative is the black band that was there before.
   - the front end, mode menu, character selection and the overlay are fixed 794-wide
     compositions. They are CENTRED, by offsetting the single backbuffer->primary copy, and
     the pointer is shifted the other way so the game's hit tests and the ported menus still
     line up with what is on screen.

TRAP, measured: offsetting during composition as well as on the final copy moves everything
twice -- a 132 px margin came out at 264.

The centring is gated on whether the in-match HUD strip was drawn in the last couple of
frames, which is the same panel-observation mechanism as the overlay gate and has no mode
dependence.

Verified: all 10 ctest targets pass at the default width, and the mouse-only route reaches a
match with LF2_WIDESCREEN=1058, which is what proves the pointer offset.
