---
id: 25
title: Keyboard and mouse routes cannot be screen-keyed, so four tests still aim at moving targets
status: resolved
symptom: smoke, mouse, widescreen and pause_dropout schedule every press by bare frame number, including presses aimed at the mode menu, character select, the overlay and the match -- the exact drift issue #18 was about
tags: testing,virtual-pad,instrument,routes
created: 2026-08-06
updated: 2026-08-12
---

OBSERVED while using the route tests as evidence for issue #22.

docs/running.md says "Every route in tools/ is now screen-keyed from charselect onward; only
the front-end presses before any screen exists are still bare frame numbers." THAT IS FALSE
for four of the nine route tests, and for two of them it is not even possible:

  screen-keyed   controller, controller_2p, coop_dropin, coop_select, two_human_match
  frame-numbered smoke, mouse, widescreen, pause_dropout

THE CAUSE IS NOT LAZINESS IN THOSE FOUR. The `button@<screen>[+n]` form exists ONLY for
LF2_VIRTUAL_PAD. runtime/win32/win32.c parses both of the others with a bare strtol:

  LF2_KEY_SCRIPT     key_script_pressed()   -- `<vk>:<frame>`, strtol, no '@' case
  LF2_CLICK_SCRIPT   click_script_state()   -- `<x>,<y>:<frame>`, same

So smoke_test (keyboard) and mouse_test (mouse) STRUCTURALLY cannot be screen-keyed today,
and pause_dropout/widescreen are pad routes that were written before the form existed.

WHY IT MATTERS, and it is not theoretical: mouse_test clicks at bare frames 1350, 1450, 1600
and 1750, aimed respectively at the mode menu, a character portrait, the same portrait again
and the overlay's "Fight!". A frame number is exact and reproducible WITHIN a run, but the
frame a screen ARRIVES on is not -- it moves with the data load and with how busy the box is,
which is issue #18, which went red three times for that reason and never for a real one.
These four tests are the ones still exposed to it.

Both comments in win32.c already state the hazard ("a click aimed at one screen can land on
another") and then schedule by frame anyway, because when they were written the screen signal
did not exist yet. It does now: screen_first[]/screens_observe() in runtime/input/gamepad.c, off
panel_charselect_up() / panel_overlay_up() / panel_hud_up().

THE FIX, and note it is a MOVE rather than a copy: the resolver, the screen observation, the
per-press fired tracking and the exit report should not live in gamepad.c at all -- they are
about scripted input, not about controllers, and the keyboard and mouse need the same three.
Lift them into one place, have all three scripts use it, then convert the four routes.

DO NOT paper over it by giving those four bigger frame numbers. That is the same stopwatch
aimed at the same moving target, and it is what made issue #18 look like a regression.

### Note (2026-08-06)
FOUND BY THE NEW REPORT, on the first run after the shared module went in, which is the
argument for having built it: the recorded mouse runtime scenario's FIRST CLICK IS DEAD.

    scripted input: screen charselect first up at frame 1352
    LF2_CLICK_SCRIPT: 5 of 5 items fired

The route is "403,228:900" (commented "launcher: game start"), then 1350, 1450, 1600, 1750.
The post-load panel does not appear until frame 1352 -- one frame after the SECOND click. So
the click at 900 does nothing at all, the click at 1350 (commented "mode menu: Stage mode")
is what starts the game, and every later click is doing the job the comment gives the one
before it. A click-only run with just "403,228:900" and 500 further frames never leaves the
launcher: 27 vram allocations, 0 input gathers, no screen.

The test PASSES, and passed before this was noticed, because the remaining numbers happen to
land somewhere workable. That is the failure this issue is about, in its purest form: five
items fired, every assertion green, and the route is not doing what it says. Note also that
the pad route starts the game from a press at frame 900 and reaches the panel at 906 -- so
the difference is the mouse path, not the frame.

NOT YET ESTABLISHED, and it should be before the route is rewritten: WHY the click at
(403,228) hits nothing. The game's own front-end hit test brackets y into bands starting at
274 (menu.c records 274..300, 305..330, 336..361 for items 1..3), and 228 is above all of
them -- but so is the 241 of the click that DOES work, so the port's own item table is the
thing to read, not the game's. Do not "fix" the route by moving the coordinate until that is
understood: a click that works for a reason nobody has established is the same bug again.

### Note (2026-08-06)
CORRECTION and PROGRESS, 2026-08-06.

CORRECTION: it is THREE routes, not four. the recorded widescreen runtime scenario has no scripted input at
all -- it drives LF2_WINDOW_SIZE and LF2_WINDOW_RESIZE, whose frames land in the launcher
before any screen exists, where a frame number is exactly the right thing. My first survey
counted it by grepping for four-digit numbers, which is the sort of sizing-from-a-grep this
project already has a rule against. The affected set is smoke, mouse and pause_dropout.

DONE: the shared module (runtime/app/script.c, commit df3d148) and the recorded pause_dropout runtime scenario,
which is now keyed to charselect/overlay for pad one and entirely to @match for pad two --
every one of pad two's presses is about the match, and a join landing before the match starts
claims nothing, so the pause that follows would be a pause with no drop-out in it. Green in
102 s. Its LF2_QUIT_AFTER went 2900 -> 3200 for headroom, since the quit frame is still a
frame number and a route that now floats later must not be truncated by it.

LEFT: smoke and mouse, and both are blocked on the same unanswered question -- WHAT ACTUALLY
STARTS THE GAME on a mouse/keyboard route. Measured so far, and none of it fits the comments:

  - The click at (403,228) sets the game's own click flag (0x00457580 goes 0 -> 1, at guest
    ret 0x0043bc3a) and the game's own mouse X reaches 403 (0x004546f0 -> 0x193). So the
    click and the position BOTH arrive, and the game still does not start.
  - 0x004546f0 then oscillates 403 -> 0 every frame, zeroed at the same guest site that
    consumes the click.
  - (403,228) is not an invented coordinate: it is MAIN_MENU[0] in runtime/overrides/menu.c,
    the port's own "game start" item, and the PAD drives the front end by writing exactly
    that position plus the click flag -- and the pad route works.

So a click and a pad confirm put the same two values in the same two places and only one of
them starts the game. That difference is the thing to find, and it is worth finding: it is
also the reason a mouse route needs five clicks where a pad route needs one press.

DO NOT convert smoke or mouse to @charselect until this is understood. Their anchors would be
derived from a route whose first input does nothing, which is how the current numbers came to
be off by one screen in the first place.

### Note (2026-08-06)
CORRECTION, 2026-08-06: the note above claiming "the recorded mouse runtime scenario's FIRST CLICK IS DEAD" is
RETRACTED. It is not dead -- it starts the game. See issue #27, which carries the measurement
and the retraction: a lone click on the launcher takes the top-level mode word to 2 within
fifty frames and the game goes on to load and draw its mode menu.

What misled me was reading "screens reached -- NONE" as "the click did nothing", when it only
means the port's post-load panel signal never fired. mouse_test's own comments were closer to
right than my correction to them.

The part of this issue that stands, unaffected: LF2_KEY_SCRIPT and LF2_CLICK_SCRIPT could not
be screen-keyed at all until the shared module, three routes were frame-numbered, and two of
them (smoke, pause_dropout) are now converted. What is left for the mouse route is issue #26 --
it reaches only charselect and never a match -- and that was measured independently of any of
this.

### Note (2026-08-12)
THE STRUCTURAL HALF IS DONE AND HAS BEEN FOR A WHILE; the routes are what was left, and one more
is now converted.

The mechanism this entry says is missing exists: runtime/win32/win32.c parses BOTH scripts through
script_when (lines 282 and 835), the shared resolver in runtime/app/script.c, so LF2_KEY_SCRIPT and
LF2_CLICK_SCRIPT take '@screen+n' exactly as LF2_VIRTUAL_PAD does. The entry's 'STRUCTURALLY cannot
be screen-keyed today' is out of date.

MOUSE IS NOW FULLY ANCHORED, including the two clicks this entry and the route's own comment both
called impossible -- 'the two before any screen exists stay frame-numbered'. That was taken as a
fact about the game and is not one: the front end paints its own backdrop colour on FRAME 1 and
takes input there, which is what '@frontend' signals (issue #57). The launcher click at bare frame
900 was 840 frames of waiting for a screen that was already up.

    ok  reached charselect / overlay / match by mouse alone
    ok  every scripted click fired: LF2_CLICK_SCRIPT: 7 of 7 items fired
    LF2_QUIT_AFTER 3200 -> 2360

STILL FRAME-NUMBERED: smoke_test (its launcher click and its whole LF2_KEY_SCRIPT) and
widescreen_test's flow arm. Both are mechanical conversions of the same shape, and neither was done
here because both routes run arms on the GPU and this session had already seen a card reset -- see
issue #40. They are the last two, and converting them needs one GPU-capable session, not more RE.

### Resolution (2026-08-12)
ALL NINE ROUTES ARE NOW SCREEN-KEYED END TO END, the launcher clicks and the keyboard script
included. The last two -- smoke and widescreen's flow arm -- were converted once GPU runs were
authorised, since both drive arms on the native renderer.

    smoke        LF2_CLICK_SCRIPT 403,228:900 -> @frontend+0
                 LF2_KEY_SCRIPT   0x5A:960    -> 0x5A@frontend+60      QUIT_AFTER 3000 -> 2160
    widescreen   403,228:900;400,241:1350     -> @frontend+0;@frontend+450
    mouse        (converted earlier this session)

Both verified: smoke PASSED, widescreen ok.

WHAT THE ENTRY GOT WRONG, and it was true when written: 'the button@<screen> form exists ONLY for
LF2_VIRTUAL_PAD'. runtime/win32/win32.c now parses both other scripts through script_when, the
shared resolver in runtime/app/script.c, so all three devices take both forms. The structural half
was fixed before this session; what remained was the routes.

AND THE PREMISE UNDER THE WHOLE ENTRY WAS WRONG TOO: 'the two before any screen exists stay
frame-numbered'. There is no such window. The front end paints its own backdrop colour on FRAME 1
and takes input there, which is what @frontend signals (issue #57) -- so the 900 in every route was
840 frames of waiting for a screen that was already up, not a necessity.

ONE ARM KEPT ITS DURATION, and the reason is worth recording: widescreen's flow arm counts
BACKDROP DRAWS (900 of them) rather than reaching a screen, so shortening the run by 840 frames
removed the frames it counts and it failed with 'NO draw was ever kept at x 0'. Anchoring fixed
where its presses LAND; it does not make the run shorter when the assertion is about how long
something was on screen. Its LF2_QUIT_AFTER stays at 1500.
