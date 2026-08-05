---
id: 6
title: Menu port: post-load screens ignore the mouse; slot state is not in .data
status: resolved
symptom: launcher menu takes mouse only, post-load menus take keyboard/pad only
tags: menu,port,input,mouse,character-select,method
created: 2026-08-04
updated: 2026-08-05
---

Screen map, measured with LF2_MENU_DEBUG:
  mode=0 screen=0  front-end launcher, fn_004246b0 -- mouse-native; the port already adds
                   keyboard/pad via item tables (screens 0, 6, 7) by moving an index and
                   WRITING the pointer onto the item, so the game's own highlight/dispatch runs
  mode=1           one-shot entry step
  mode=2           everything after loading, fn_0041bc90 (28 KB) -- mode menu, character
                   select, pre-fight overlay. NO mouse hit-testing exists in the original.

Adding mouse to mode 2 needs the mirror of the front-end trick: pointer -> selection index.
That needs each screen's selection variable and item rectangles.

DEAD END, do not repeat: tools/diff_data.py cannot find the character-select slot state.
Diffing .data across a right-arrow press showed 5 changed dwords -- and a NEGATIVE CONTROL
run with no key press between the same two frames changed the SAME 5 (00450b8c, 00450bd0,
00450bd4, 00451224, 00458580). They are free-running counters. Input is not the problem:
the same run reports 48 button presses merged, so the arrow does reach the game. The cause
is that dump_data covers only .data (DATA_BASE 0x0044d000, DATA_SIZE 0xc724); the per-slot
selection lives in a malloc'd structure on the guest heap (0x20000000+), outside the window.
Next attempt should widen the dump to the heap arena or watch writes, not re-run diff_data.

Also established: LF2_OVERLAY_FORCE=<n> pins OVERLAY_SEL (0x0044d06c) for mapping item
positions, but the pre-fight overlay does NOT open in the standard smoke route -- forcing
values 0..5 and dumping frame 2100 gave character select every time, and sel1..sel4 were
byte-identical to each other. Reaching the overlay needs a longer key script first.

### Resolution (2026-08-05)
Done. Every screen from the launcher to a running match takes mouse, keyboard and pad, and a
mouse-only route reaches a match with no key and no pad (tools/mouse_test.sh, ctest -R mouse).

Selection variables, all four:
  launcher (0/6/7)  the game's own index, from its hit-test constants
  mode menu         0x00451160, 0..7
  character select  +0x364 in the object at 0x00458c94[1+player], 0..7 (heap, not .data --
                    that was this issue's dead end)
  pre-fight overlay 0x0044d06c, 0..5

The overlay also needed the screen discriminator these notes said did not exist: 0x0044d070
is -100 while players join and pick, 0 while the overlay is up, 1 once the match runs. It is
what keeps character selection's hit test from fighting the overlay's, since the overlay is
drawn ON character selection and both were live at once. Found by diffing two overlay-closed
frames against two overlay-open ones, keeping only dwords stable within each pair (26
candidates), then checking each against a third state.

One thing this issue could not have caught, and which nearly shipped: LF2_CLICK_SCRIPT
pushed the WM_LBUTTONDOWN the GAME reads but never armed hostwin_mouse_clicked(), which is
what the PORTED menus read. Every scripted run therefore tested hover only -- the key script
that followed confirmed whatever the hover had selected, so a menu whose click did nothing
looked exactly like one that worked. Fixed in pump_autoclick, and the mouse test exists so
it cannot regress: with the click edge disabled the route stops at the mode menu (2 screen
transitions, 1 sound effect), with it live it reaches a match (4 and 10).
