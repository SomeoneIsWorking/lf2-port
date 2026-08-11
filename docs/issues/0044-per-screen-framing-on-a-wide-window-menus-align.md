---
id: 44
title: Per-screen framing on a wide window: menus align left, the loading screen fills
status: open
symptom: on a wide composition the two menu screens should sit at the LEFT edge where their character art is, not centred; the loading screen should stay centred but have the space beside it filled rather than left black. Character selection is correct as it is
tags: reported,rendering,widescreen,frontend,scaling
created: 2026-08-11
updated: 2026-08-11
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
    edge colour or a sampled flat colour, and neither is "the game's own" -- that has to be
    said out loud rather than shipped as if it were faithful.
  - WHY LEFT IS RIGHT FOR THE MENUS. If the art is composed against the left edge of the 794
    screen then left-aligning is restoring the author's framing, not a preference. Check the
    actual draw positions before asserting it.

The mechanism from issue #42 is the place this goes: the centring is applied while composing,
to draws that fit inside the game's own 794-wide screen. Making that offset per-screen is a
small change ONCE the screens are identified; identifying them is the work.

### Frames captured 2026-08-11, so the next session does not re-derive which screen is which

A 1710x370 run (composition 2542x550, software compositor, LF2_FRAME_DUMP=60,300,600,880,1000)
gives, measured as the non-black column range of each dump:

    frame   60, 300, 880   0..2541   the FRONT END. Its flat backdrop already spans the whole
                                     composition after issue #42; the character art, logo and
                                     menu list are centred on top of it. This is one of the two
                                     screens the reporter wants aligned LEFT.
    frame 1000              914..1618  CHARACTER SELECTION -- centred, 704 wide, black beside.
                                     Confirmed by eye against the dump. The reporter says this
                                     one is correct as it is, so it is the CONTROL: whatever
                                     lands here must leave it alone, and that is worth an
                                     assertion rather than an intention.

STILL TO CAPTURE: the mode menu and the loading screen. The route above pressed south at 900
and by 1000 was already at character selection, so neither was in the dump set. The load runs
to roughly frame 850 (docs/running.md), and the mode menu is the post-load panel that
modemenu_mouse identifies by MODEMENU_SEL holding a valid index.

WHICH TWO SCREENS "the two menu backgrounds" MEANS is not yet certain -- most likely the front
end and the mode menu, but ASK or confirm from a dump before building to it. Character
selection is explicitly excluded and the overlay was not mentioned.

A NOTE ON THE LOADING SCREEN AND "fill the rest": issue #42 established that a flat COLOUR fill
may be extended across the composition because it invents nothing, while a backdrop that is
ARTWORK may not. Which of the two the loading screen has decides whether "fill the rest" has a
faithful answer or is a choice that has to be declared as one.
