---
id: 44
title: Per-screen framing on a wide window: menus align left, the loading screen fills
status: closed
symptom: on a wide composition the two menu screens should sit at the LEFT edge where their character art is, not centred; the loading screen should stay centred but have the space beside it filled rather than left black. Character selection is correct as it is
tags: reported,rendering,widescreen,frontend,scaling
created: 2026-08-11
updated: 2026-09-09
---

REPORTED 2026-08-11, following issue #42. That one made a screen's flat backdrop span the
composition; this is about WHERE each screen's content sits and what fills what is left.
WHAT WAS ASKED FOR, screen by screen:
  - THE TWO MENU SCREENS -> aligned LEFT, "where the character is". Their art is anchored on a
    character portrait at the left of the 794 screen, so centring them puts that portrait in
    the middle of a wide window and reads as a picture floating in a field.
  - THE LOADING SCREEN -> centred is fine, but the space beside it must be FILLED rather than
    black.
  - CHARACTER SELECTION -> correct as it is. Do not touch it.
SO THE PORT NEEDS PER-SCREEN FRAMING, which it does not have: today there is one rule
(screen_offset_x) applied to every fixed-794 screen alike, plus the flat-backdrop widening from
issue #42. Three different answers are now wanted from three screens.
WHAT MUST BE ESTABLISHED FIRST -- and the standing instruction applies, RE it, no constants
that make one screenshot look right:
  - WHICH SCREENS THESE ARE, in the game's own terms. runtime/overrides/screens.c already
    identifies several by what the game DRAWS rather than by a mode word (panel_charselect_up,
    panel_overlay_up, panel_hud_up) because a .data flag turned out to be the game mode
    wearing a disguise. The launcher's own screens are 0/6/7 (runtime/overrides/menu.c). The
    loading screen and the mode menu need the same treatment: find what identifies each.
  - WHETHER "FILL THE REST" HAS AN HONEST ANSWER FOR THE LOADING SCREEN. Issue #42 established
    the rule: a flat COLOUR fill can be extended across the composition because it invents
    nothing, but a backdrop that is ARTWORK cannot -- stretching a picture invents layout the
    game does not have, the same answer issue #23 gives for a stage's sky. So this depends on
    what the loading screen's background actually is. If it is artwork, the options are its
