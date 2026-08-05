---
id: 14
title: Pause menu: freezing the game also froze the picture
status: resolved
symptom: pausing by skipping the per-frame update stops the window updating entirely, and the dim compounds to black
tags: pause,menu,rendering,method
created: 2026-08-05
updated: 2026-08-05
---

The port has no way to ask the game to pause, so the pause menu is built on declining to
call `fn_004246b0__orig` -- the function the main loop calls to advance and draw everything.
Nothing to save, nothing to restore, and no half-advanced frame.

Two things had to be found by running it rather than by reasoning:

1. The PRESENT lives inside that body, not in the main loop. Skipping the body stopped the
   window updating at all, which looks like a hang rather than a pause -- and because the
   frame counter only advances on present, LF2_QUIT_AFTER stopped firing too, so the run
   just sat there. The pause path issues the present itself (`present_frozen_frame`).
2. The menu draws straight onto the primary, and the game is not redrawing it, so dimming
   the frame every frame compounds: the picture fades to black in about a second. The frozen
   frame is snapshotted on entry and restored before each draw.

Also: Escape is the GAME's quit key. Bound naively, the pause menu opened underneath an "Are
you sure to quit?" prompt. The port now withholds Escape from the game while a match is on
screen -- both from GetKeyState and from the message queue -- while its own key ledger, which
the pause menu reads, still sees it. Escape still quits from the menus, where the game's
prompt is the right behaviour.

Two entries, Resume and Quit game, both of which do exactly what they say. "Restart" and
"back to character select" are deliberately absent: driving the game back to those screens is
RE that has not been done, and a menu item that half-works is worse than one that is missing.

Verified end to end: pause at frame 2550, resume at 2680 with the match visibly running
again, pause at 2780, Quit game -- process exits 0 through the game's own shutdown.
