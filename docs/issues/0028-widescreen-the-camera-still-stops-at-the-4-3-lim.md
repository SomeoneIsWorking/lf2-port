---
id: 28
title: Widescreen: the camera still stops at the 4:3 limit, so a wide view sees past the stage's walls
status: resolved
symptom: in a window wider than 794x550, running to a stage's right wall keeps scrolling until the world runs out -- the view shows past the edge a character can walk to, instead of stopping where 4:3 stops
tags: reported,widescreen,rendering,camera
created: 2026-08-06
updated: 2026-08-06
---

REPORTED 2026-08-06. CORROBORATED by measurement in the same session, so the cause is
already known rather than guessed.

WHAT WAS MEASURED. On Brokeback Clif (stage width 1500) at a 1600x550 window, the ground
layer's parallax offset reaches 706 at the right wall. 706 == 1500 - 794: the camera is still
clamped to (stage width - THE GAME'S 794), not to (stage width - the actual view width). With
a 1600-wide view that puts the visible world at 706..2306 over a stage that ends at 1500, so
the right end of the view is past the world entirely.

WHY THIS IS THE SAME ROOT AS #23 AND MUST BE FIXED WITH IT. fn_0041a250 (the background layer
draw, 828 bytes, read end to end this session) computes

    offset = -(camera * (layer_span - 794)) / (stage_width - 794)

with 794 as a LITERAL in the recompiled code -- not one of the three .data words menu.c
rewrites, so it cannot be widened the way the viewport words were. Every stage's layers are
authored so that span - 794 is exactly the scroll range: each layer covers the screen at
every camera position with no margin. Widening the view without widening the camera clamp is
what pushes the view past both the walls (this issue) and the layers (#23).

WHAT THE FIX MUST NOT BE: clamping the camera in the port while leaving the game's parallax
on 794. The two are one formula; correcting one and not the other moves the black band rather
than removing it.

### Resolution (2026-08-06)
The camera is clamped to (stage_width - view_width) instead of the game's (stage_width - 794), in camera_clamp_to_view() in runtime/overrides/background.c. The game's own clamp is at 0x0041bc47..0x0041bc60 inside fn_0041b5d0; the port re-applies it with the real view width at the top of the layer draw, which is the boundary between the camera update and anything that draws from it -- fn_0041b5d0 calls the layer draw as its last act, and the only other call site is immediately before the object draw fn_0041a5a0, so the clamp lands ahead of the sprites too. The mirror at 0x00450b7c is kept in step under the same condition the game reads it back, or the unclamped value would come round on the next frame. Where the view is wider than the whole stage no camera value can help, so the maximum floors at 0. Verified on Brokeback Clif (1500 wide) at 1600x550: the fighter now reaches the right edge of the view instead of the view scrolling 800 px past the wall.
