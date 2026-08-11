---
id: 12
title: Overlay screen discriminator was the game mode; VS-mode overlay took no mouse input
status: resolved
symptom: the pre-fight overlay responds to the mouse in stage mode but not in VS mode
tags: menu,input,mouse,discriminator,method
created: 2026-08-05
updated: 2026-08-05
---

`overlay_open()` keyed off 0x0044d070, which stage-mode .data dumps showed as -100 while
players pick, 0 while the overlay is up and 1 in the match. It is the GAME MODE. In VS mode
it reads 1 with the overlay open, so `overlay_open()` was false there and the VS overlay took
no mouse input at all -- while the mouse test, which drives STAGE mode, passed.

The method failure is the point, and it is the one CLAUDE.md warns about: the discriminator
was derived from stage-mode dumps and validated against stage-mode dumps. It could not have
failed. Running it against the other class -- one VS-mode dump -- would have killed it in a
minute.

A second .data hunt found no word that means "overlay open" in both modes: the eight dwords
at 0x00451228 go 0 -> 1 when picking finishes, but stay 1 through the match, so they cannot
separate the overlay from a running match either.

Fixed by asking the game what it DRAWS instead of what it stores. Both panels have fixed
destinations, confirmed identical in both modes from LF2_BLT_FRAME:

  character-select panel   (40,33)-(745,520)
  pre-fight overlay panel  (3,3)-(307,159)

`panel_overlay_up()` / `panel_charselect_up()` in runtime/video/ddraw.c report whether each was
drawn in the last couple of frames. This has no mode dependence by construction -- it is the
screen being on screen -- and it was still checked in VS mode rather than argued: hover moves
the selection 2 -> 3 and a click on item 3 registers.
