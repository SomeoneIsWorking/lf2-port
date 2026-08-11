---
id: 27
title: RETRACTED (was: the first scripted mouse click of a run is always swallowed) -- the click always worked; 'screens reached -- NONE' was misread
status: dead-end
symptom: a run whose only click is on the launcher's game-start item reports 'screens reached -- NONE' -- which was misread as the click doing nothing; the game in fact starts, loads and rests on the mode menu, and the open question is why the post-load panel signal fires for a pad press but not for a click
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

This is the root cause behind the dead first click in both mouse routes -- tools/routes/smoke_test.sh
(where the KEY at 960 turned out to be what starts the game) and tools/routes/mouse_test.sh (where
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

### Note (2026-08-06)
NARROWED, 2026-08-06, with a new instrument and TWO more refuted theories. The cause is in the
GAME's handling of the mouse messages, not in the click flag and not in the port's delivery.

NEW INSTRUMENT (kept, in runtime/overrides/menu.c under LF2_MENU_DEBUG): what the front-end
menu is entered with -- the game's click flag, its mouse pair, the port's index and pointer
ownership -- plus a count at exit that prints even when it is zero. The watch on 0x00457580
could not answer this: it reports per host call, so it says the flag changed between two
calls, never whether the MENU saw it.

WHAT IT SHOWED, and this is the heart of it. The swallowed click and the one that works are
INDISTINGUISHABLE at the menu:

  lone click at 1200   menu entered with click=1 at mouse=(403,228), index=0 screen=0 -> NOTHING
  second of two        menu entered with click=1 at mouse=(403,228), index=0 screen=0 -> STARTS

Identical state, opposite outcome. So nothing the port hands over distinguishes them, and no
amount of work on the delivery path can: the difference is state inside the game.

THE CONTROL THAT REFRAMES IT: a lone PAD confirm at the same frame 1200 starts the game
(charselect@1206) -- and the run reports "0 frame(s) reached the front-end menu with the
game's click flag set". The pad path does not go through that flag at the point the menu is
entered at all; it writes the flag late in the override, immediately before the original body
runs. So the pad succeeds WITHOUT the flag being set on entry and the mouse fails WITH it.

REFUTED, implemented and measured, so nobody retries them:
  1. "The game wants two sightings of the click flag" -- fitted everything (the pad holds it
     for two frames via menu_confirm(); two clicks fifteen frames apart also work; one click
     never does). Implemented as: a front-end click calls menu_confirm() like the pad does.
     Measured -- on_item=1, port_edge=1, game_flag=1, menu_confirm() demonstrably fired, the
     flag was then held for two further frames -- and the game STILL did not start. Reverted.
  2. "The game never sees a WM_MOUSEMOVE before the button" (from the earlier pass) -- also
     implemented, measured, reverted.

WHAT IS LEFT, and it is now a narrow question: the mouse path additionally makes the game
process WM_MOUSEMOVE and WM_LBUTTONDOWN in that frame, and the pad path does not. Something
the game's own message handling sets is what suppresses the first activation. The candidate
worth testing next is that the game acts on the button being RELEASED, or refuses to act
while it believes the button is down -- which would explain why a second, later click works
(the first release has happened by then) and why the pad, which never touches button state,
is unaffected.

A cheap discriminator for that: a scripted click whose HOLD is one frame rather than eight,
and one whose hold spans the whole run. If activation follows the release, the first will act
on its release frame and the second will never act at all.

### Note (2026-08-06)
THIRD THEORY REFUTED, 2026-08-06, and one of them without needing a run at all.

REFUTED FROM EVIDENCE ALREADY IN HAND -- "the game acts on the button being RELEASED, and
refuses while it believes the button is down". This was the candidate the previous note named
as most promising. It does not survive the lone-click run: the scripted click is held eight
frames, so the release happened at frame 1208, and that run reached NO screen through frame
1500. If activation followed the release it would have started the game at ~1210. No new
measurement was needed; the old log already contained the answer, which is worth noting as a
habit -- the run that refutes a theory is often one already on disk.

REFUTED BY MEASUREMENT -- "the periodic WM_MOUSEMOVE tells the game the pointer is at (0,0)".
This had a real defect behind it: autoclick_state only writes a position while a click window
is open, so pump_autoclick seeded x,y from 0 and the scripted pointer teleported to the origin
between clicks, with the 500 ms resend then reporting (0,0) to the game -- outside every menu
band. Making the pointer persist where it was put changed nothing: the lone click at 1200
still reached no screen.

That fix was kept anyway, unlike the previous two, because it is right on its own terms rather
than as a theory about this bug -- a real mouse does not go home between clicks -- and because
it measured neutral on the full mouse route (charselect@1352, plays=4, 5 of 5 items fired,
identical before and after). It is NOT a fix for this issue and must not be read as one.

WHERE THAT LEAVES IT. Three mechanisms are now excluded: the click flag's timing (two
sightings), the missing prior move, and the pointer position. The menu is entered with
identical state in the swallowed and the working case, so the remaining difference is
something the game's own WM_MOUSEMOVE/WM_LBUTTONDOWN handling sets that is not any of the
three words the port writes. The next step is to find what the game's window proc touches
besides 0x00457580 and 0x004546f0/0x00453cdc -- a .data diff across the first click, against a
control frame with no click, would name it, and that is the same method that located the
overlay selection index and the mode menu's selection.

DO NOT spend another pass on theories about the port's delivery path. Three have died there.
The measurement above says the port hands over identical state; the next pass belongs in the
game's own .data.

### Note (2026-08-06)
RETRACTED, 2026-08-06 — THE CENTRAL CLAIM OF THIS ENTRY IS WRONG. The first scripted mouse
click is NOT swallowed. It starts the game, every time, and always did. I put a false finding
in this registry and the correction matters more than any of the theorising above it.

WHAT IS ACTUALLY TRUE, measured:

  - A lone click on the launcher's game-start at frame 900 takes the game's top-level mode
    word (0x00458b00) to 2 by frame 950 — mode 2 is the game proper. Dumped at 950, 1100,
    1400, 1800 and 2200: all read 2.
  - It does this BOTH with and without the scripted-pointer change of commit 1860861. That
    change is therefore not implicated in either direction, and the A/B is the only reason
    anyone can say so.
  - The run then loads (vram 27 -> 393 allocations, bgm/main.wma) and comes to rest on the
    MODE MENU. A frame dump at 2500 shows it plainly: the LF2 title screen with VS mode
    highlighted.

WHERE THE FALSE CLAIM CAME FROM, because the mechanism is the lesson. Every "the click did
nothing" reading in this entry rests on one line of output:

    scripted input: screens reached -- NONE

That line means the port's `charselect` panel signal never fired. It does NOT mean the click
did nothing, and I read it as though it did — for four theories and three code changes. The
game had started, loaded and drawn its mode menu the whole time. Two of my runs even carried
the contradiction in them: "vram: 27 allocations" at a 1400-frame cutoff and "vram: 393" at
2600 is a load in progress, not a game that never started, and I read the first as proof of
nothing happening.

The instrument was not lying. It answered the question it was built for — which screens did a
route reach — and I asked it a different one.

WHAT SURVIVES, and it is a real question, just not the one this entry was titled for: in a
run driven by a single PAD press the signal fires (charselect@906) while the game sits on the
mode menu, and in a run driven by a single mouse CLICK that also ends on the mode menu it
never fires at all. Both end in the same place; only one draws the panel this port keys on.
That is worth understanding, and it is what the `charselect` signal's name already overstates
(see docs/running.md — the signal is the post-load panel, not character selection).

ALSO RETRACTED, downstream of this: "tools/routes/mouse_test.sh's FIRST CLICK IS DEAD" (issue #25's
note, and a comment I wrote into tools/routes/smoke_test.sh). The first click starts the game; the
second lands on the mode menu and picks VS mode. Its original comments were close to right
and mine were wrong. Issue #26 is NOT affected — mouse_test still reaches only charselect@1352
with no overlay and no match, and its assertions still pass regardless, which stands on its
own measurements.
