---
id: 39
title: Widescreen adds all its extra world on the RIGHT instead of centring, and audio is still culled against 794
status: open
symptom: reported, two faults from one cause. The port widens the composition and patches the game's width words, so the game's original 794-wide view stays anchored at the LEFT and every extra pixel appears on the right -- it should be centred on what the 4:3 view showed. And the game still culls SOUND against the 794 screen, so effects on the right of a wide view are silent (or audible when they should not be). Both are the same shape of defect: the port widened the viewport without following what the game itself derives from that width. Asked for explicitly: port it from the game's own source rather than shimming the consequences
tags: reported,widescreen,audio,re
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-08-06)
SCOPED, NOT FIXED. The cause is one instruction, and it is exactly what was reported.

THE CAMERA TARGET, read off FUN_0041b5d0 at 0x0041ba98..0x0041bb87:

    for each player slot: if alive, EBX += player->x (obj+0x10); EDI++
    if (EDI == 0) { EBX = 0x320; EDI = 1 }      // nobody alive: focus on x=800
    ESI = EBX / EDI                             // the MEAN of the live players' x
    ESI -= 0x18d                                // ... minus 397, which is 794/2
    if (ESI < 0) ESI = 0
    // then: clamp to stage_width - 794, then to the section lock at [0x00450bb0]

So the game centres the players' centroid in a 794-WIDE WINDOW, and it says so with a
half-screen constant. The port widens the composition and patches the game's width words but
never touches this, so the centroid keeps sitting 397 px from the LEFT edge and every extra
pixel of a wider view appears on the RIGHT. That is precisely the report: 'you render the
original part on the left then expand to right'.

THE FIX IS THE SAME SUBSTITUTION THE PARALLAX AND THE CLAMP ALREADY GET -- 397 becomes
view/2. What is NOT settled is where to apply it, and this is the part worth writing down
because the obvious answer is wrong:

  DO NOT subtract the offset from BG_CAMERA_X after fn_0041b5d0 the way the clamp does. The
  clamp is idempotent; a shift is not. fn_0041b5d0 EASES the camera toward its target by a
  seventh (IDIV by 7 at 0x0041bbc6), and it reads back the value the port wrote. Writing
  c' = c - K each frame has fixed point c* = T - 7*K: the view ends up SEVEN TIMES further
  off than intended, and it gets there gradually so it looks like drift rather than like a
  wrong constant. Verified by algebra, not by running it -- c' = c + (T-c)/7 - K.

  THE SHAPE THAT WORKS is to leave the game's camera alone and shift only what the DRAW sees,
  which is the same trick hud.c already uses for the joiner's panel: save BG_CAMERA_X, write
  camera - (view - 794)/2, call the original body, restore. The object pass fn_0041a5a0 reads
  the camera itself, so it is the body to wrap; background.c's parallax reads it too and can
  simply subtract the same offset locally. No feedback, no reimplementation of the ease, and
  at view == 794 the offset is zero so tools/background_test.sh's byte-identity arm still
  holds.

  The existing clamps then handle the stage ends by themselves: at camera 0 there is no world
  to the left, so the view degrades to what it does today rather than opening a black band.

THE AUDIO HALF IS NOT SCOPED. Nothing here has looked at where the game gates a sound on
distance from the screen. It is very likely the same 794 written down somewhere else, and the
three places the binary subtracts 794 (0x0041bba2, 0x0041bc54, 0x004377d1) are all camera and
walk-boundary code, so it is NOT one of those -- it will be a different constant or a
comparison against the viewport words the port already patches. Start by finding the call
sites that gate a DirectSound play.
