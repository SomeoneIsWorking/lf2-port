---
id: 36
title: Stage mode: on a wide view the camera does not stop the same distance from the walk boundary as it does at 4:3
status: resolved
symptom: reported. In stage mode the game locks the player to a section and the camera stops when that section's right edge reaches the screen's right edge -- at 794 wide. On a wider view the camera stops at the same WORLD position, so the boundary ends up mid-screen and the player can see well past where they can walk. Wanted: the camera stops at the same distance from the walk point that it does at 4:3, i.e. the same 794->view substitution the parallax and the camera clamp already got (issue #28). Needs the stage-mode lock located first
tags: reported,camera,widescreen,re
created: 2026-08-06
updated: 2026-08-07
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-08-06)
IMPLEMENTED, NOT YET WATCHED IN STAGE MODE.

The section lock is a second upper bound fn_0041b5d0 puts on the camera right after the
stage-width one, applied only when non-zero (0x0041bbad..0x0041bbba):

    EAX = stage_width; EAX += -794; if (target > EAX) target = EAX;
    EAX = [0x00450bb0]; if (EAX && target > EAX) target = EAX;

It is BG_CAMERA_LOCK in world.h now, and background.c's camera_clamp_to_view bounds the
camera by `lock + 794 - view` alongside the stage-width bound it already re-applied. Both
bounds are the game saying 'the right edge of the screen goes HERE' in terms of a 794-wide
screen, so both need the same substitution; the result is that the camera stops the same
distance from the walk boundary whatever the window is, which is what was asked for.

WHY IT IS FOUND HERE AND NOT WITH A GREP FOR 794: the binary writes it as -794, i.e.
0xfffffce6, in an ADD or a LEA. A search for 0x31a finds 34 sites and NONE of them are the
camera. There are exactly three places that subtract it -- 0x0041bba2 and 0x0041bc54 (both in
the camera) and 0x004377d1 (a walk-boundary test against a player's x at obj+0x10) -- and
that third one is worth a look for whoever picks this up.

STILL OPEN because nothing has watched it hold a camera in an actual stage. Every scripted
route this port has reaches VS mode, where the lock reads 0 and the branch never runs, so the
change is a proven no-op there and unproven where it matters. At view == 794 the term is
identically `lock`, which is why tools/routes/background_test.sh's byte-identity arm still passes --
that shows it cannot have broken the 4:3 game, and nothing more.

### Resolution (2026-08-07)
Verified. The blocker was never the code -- it was that no test could reach stage mode.

THE PATH IN IS THE PORT'S NOW. Every scripted route reached the game by pressing buttons at
counted frames and taking whatever the mode menu was already sitting on, which is VS. One of
the game's eight modes was tested and seven were not. LF2_MODE=<name> (menu.c) puts the GAME'S
OWN mode-menu selection where the run asks -- the same word screens.c already writes for the
mouse -- and lets the route's existing confirm dispatch it, so the mode change, its sound and
its screen transition are all the game's. It presses nothing and it is released once the
pre-fight overlay is up, so it cannot fight a later screen. An unknown name is refused loudly:
a run that quietly entered VS while asking for stage would be a green test for a mode it never
visited.

THE FIX ITSELF, from before: fn_0041b5d0 bounds the camera a second time by [0x00450bb0] when
that is non-zero (0x0041bbad..0x0041bbba), which is what holds the camera partway along a
stage. BG_CAMERA_LOCK in world.h; background.c's camera_clamp_to_view now bounds the camera by
 alongside the stage-width bound it already re-applied. Both bounds are the
game saying 'the right edge of the screen goes HERE' in terms of a 794-wide screen.

Worth keeping: a grep for 794 (0x31a) finds 34 sites and NONE of them are the camera. The
binary writes it as -794 = 0xfffffce6 in an ADD or a LEA, and there are exactly three such
sites -- 0x0041bba2, 0x0041bc54 (both camera) and 0x004377d1 (a walk-boundary test against a
player's x at obj+0x10).

VERIFIED by tools/routes/stage_mode_test.sh, three arms:
  stage@794   the section lock is SET (1173 frames, reaching 106) -- the run really is in
              stage mode, since nothing else sets that word
  vs@794      the lock is NEVER set -- so the arm above can tell the two modes apart, which
              is what stops it passing on a build that set the lock everywhere
  stage@1100  the lock BOUND the camera. Setting it is not enough: the substitution only does
              work when the lock is the tighter of the two bounds, and a run where the stage's
              own bound always won would exercise none of it.

What the clamp does at these numbers is worth stating, because it is not what it looks like:
with a lock of 106 and a view of 1100,  is negative, so the camera is
pinned at 0 rather than being allowed to range 0..106. That is correct and it is the same
graceful degradation the stage edges get -- the view is wider than the section can honour, so
the camera stops as close to the walk boundary as there is world to allow.
