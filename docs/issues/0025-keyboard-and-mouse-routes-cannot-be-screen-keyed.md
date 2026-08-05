---
id: 25
title: Keyboard and mouse routes cannot be screen-keyed, so four tests still aim at moving targets
status: open
symptom: smoke, mouse, widescreen and pause_dropout schedule every press by bare frame number, including presses aimed at the mode menu, character select, the overlay and the match -- the exact drift issue #18 was about
tags: testing,virtual-pad,instrument,routes
created: 2026-08-06
updated: 2026-08-06
---

OBSERVED while using the route tests as evidence for issue #22.

docs/running.md says "Every route in tools/ is now screen-keyed from charselect onward; only
the front-end presses before any screen exists are still bare frame numbers." THAT IS FALSE
for four of the nine route tests, and for two of them it is not even possible:

  screen-keyed   controller, controller_2p, coop_dropin, coop_select, two_human_match
  frame-numbered smoke, mouse, widescreen, pause_dropout

THE CAUSE IS NOT LAZINESS IN THOSE FOUR. The `button@<screen>[+n]` form exists ONLY for
LF2_VIRTUAL_PAD. runtime/win32.c parses both of the others with a bare strtol:

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
did not exist yet. It does now: screen_first[]/screens_observe() in runtime/gamepad.c, off
panel_charselect_up() / panel_overlay_up() / panel_hud_up().

THE FIX, and note it is a MOVE rather than a copy: the resolver, the screen observation, the
per-press fired tracking and the exit report should not live in gamepad.c at all -- they are
about scripted input, not about controllers, and the keyboard and mouse need the same three.
Lift them into one place, have all three scripts use it, then convert the four routes.

DO NOT paper over it by giving those four bigger frame numbers. That is the same stopwatch
aimed at the same moving target, and it is what made issue #18 look like a regression.
