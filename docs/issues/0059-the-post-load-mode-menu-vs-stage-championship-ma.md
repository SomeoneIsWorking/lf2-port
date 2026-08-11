---
id: 59
title: The post-load mode menu (VS / Stage / Championship) may never be drawn at all -- every run goes launcher to character selection
status: open
symptom: found while trying to produce a positive control for issue #22. No run this port makes has ever been shown to draw the mode menu. With no input the game sits on the launcher indefinitely; with one attack press it is on the CHARACTER SELECTION panel twenty frames later and stays there, with the game-mode word still reading -100 (no mode chosen)
tags: widescreen,menu,screens,re
created: 2026-08-11
updated: 2026-08-11
---

FOUND 2026-08-11 while trying to get a frame that is demonstrably the mode menu, which issue
#22 is blocked on. NOT established as a defect -- three readings fit the evidence and they are
told apart below.

WHAT WAS MEASURED, and both arms have a control:

  NO INPUT AT ALL. Frames 200, 600, 900, 1000, 1100, 1300, 1600 dumped: every consecutive pair
  differs by 0.0% of pixels. The game sits on the launcher indefinitely and the load never
  starts, so the press is what starts it.

  ONE ATTACK PRESS AT FRAME 900. Frames 920, 960, 1000, 1050, 1100, 1200, 1400 dumped: every
  consecutive pair differs by 0.2%, which is a blinking element and not a screen change. Frame
  920 -- twenty frames after the press -- is already the CHARACTER SELECTION panel, all eight
  slots on "Press Attack to join!", and it is unchanged 500 frames later.

  And from issue #22's diff, over that same period the game-mode word at 0x0044d070 reads -100:
  NO MODE HAS BEEN CHOSEN. So the game is drawing character selection while its own state says
  no game mode is picked.

THREE READINGS, and nothing here distinguishes them:
  1. The mode menu is never drawn -- the port skips it and lands on character selection. If so
     a player cannot choose VS vs Stage vs Championship at all, and the only reason this port
     reaches stage mode is that LF2_MODE WRITES the selection (menu.c) rather than driving a
     visible menu. That would make LF2_MODE a feature behind an env var, which this project's
     own working agreement forbids.
  2. The mode menu is drawn and looks like this. docs/running.md already records that the
     post-load panel and the mode menu SHARE A BLIT DESTINATION; if they also share the
     picture, then the list is somewhere in it and this screen IS the mode menu with its own
     selection at MODEMENU_SEL (0x00451160).
  3. The mode menu needs input this run never gave it.

WHY IT MATTERS BEYOND #22: screens.c carries modemenu_was_open and a MODEMENU_SEL, so the port
already believes this screen exists and acts on that belief. If reading 1 is right then that
code is operating on a screen no player ever sees, and issue #22's whole "exit to the mode
menu" goal is asking for a screen the port does not present in the first place.

HOW TO SETTLE IT, cheaply and first: run the game on a real display and look at what appears
after the load. That is one manual run and it decides between the three without any more
scripted archaeology. Do not build another .data diff before it -- issue #22 has now produced
two confident results from controls that were never run, and both had to be retracted.
