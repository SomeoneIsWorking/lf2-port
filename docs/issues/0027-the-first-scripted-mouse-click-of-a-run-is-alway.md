---
id: 27
title: The first scripted mouse click of a run is always swallowed
status: open
symptom: a run whose only click is on the launcher's game-start item reaches no screen at all; the same click with a second one after it starts the game, so every mouse route has silently been one click behind
tags: reported,mouse,testing,input
created: 2026-08-06
updated: 2026-08-06
---

OBSERVED while measuring route anchors (#25) and then chased with a discriminator, because
"the click at 900 is dead" has two very different explanations and they are told apart by one
run each.

MEASURED, all with LF2_CLICK_SCRIPT alone and nothing else driving the game:

  403,228:900                              -> screens reached: NONE
  403,228:1200                             -> screens reached: NONE
  403,228:900;403,228:1000;...             -> charselect@1002   (the SECOND click acted)
  403,228:1200;403,228:1215                -> charselect@1217   (the SECOND click acted)

So it is NOT "too early": a lone click at 1200 does nothing, and a click at 1000 works when
one preceded it. THE FIRST CLICK OF A RUN IS SWALLOWED, whatever frame it lands on, and
fifteen frames is enough of a gap for the second to work.

This is the root cause behind the dead first click in both mouse routes -- tools/smoke_test.sh
(where the KEY at 960 turned out to be what starts the game) and tools/mouse_test.sh (where
the click at 1350 does, one frame before charselect@1352, so every later click is doing the
job its comment gives the one before it, and the run never reaches a match at all -- #26).

WHAT IS ALREADY RULED OUT:
  - Not the coordinate. (403,228) is MAIN_MENU[0] in runtime/overrides/menu.c, the port's own
    "game start" item, and the PAD starts the game by writing exactly that position plus the
    click flag.
  - Not the click failing to arrive. With LF2_WATCH on the game's own click flag the first
    click sets it (0x00457580: 0 -> 1, at guest ret 0x0043bc3a), and the game's own mouse X
    reaches 403 (0x004546f0 -> 0x193). Both values are in both places and nothing happens.
  - REFUTED BY EXPERIMENT, so nobody repeats it: the theory that the game never sees a
    WM_MOUSEMOVE *before* the button, because pump_autoclick's four-frames-early placement
    sets host_ptr_x and then returns without pushing a message (the move the game's handler
    reads goes out on a 500 ms timer). Making a MOVE go out whenever the position changes was
    implemented and measured: the lone click at 1200 still reached no screen. Reverted, since
    it fixed nothing and pushed the pointer's snap-back to (0,0) more aggressively.

WORTH KNOWING ANYWAY, found while ruling that out: between click windows autoclick_state
returns without setting *x/*y, so pump_autoclick sets host_ptr_x/y to 0,0 -- the scripted
pointer snaps to the origin between clicks rather than staying where it was put. That is not
this bug, but it is not right either.

NEXT, and it is an ordering question rather than a value one: instrument what the front-end
menu SEES at the top of fn_004246b0 on the click frame -- the game's mouse pair and its click
flag, for a few frames either side -- and compare the first click against the second. Both
watches above report per host call, which is too coarse to say what the menu read.

DO NOT fix this by making routes click twice. That hides it in nine tools files instead of
one runtime file, and it is exactly the workaround that let it live this long.
