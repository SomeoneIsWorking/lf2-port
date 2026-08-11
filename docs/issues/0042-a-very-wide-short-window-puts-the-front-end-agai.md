---
id: 42
title: A very wide, short window puts the front end against the right with black beside it
status: resolved
symptom: at a window like 1710x370 the launcher screen is not centred: black fills the left third and the game's picture runs from there to the right edge. The fixed-794 screens are supposed to be centred in a wider composition
tags: reported,rendering,widescreen,scaling,frontend
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11 with a screenshot, immediately after issue #41 landed. Filed on receipt.

SYMPTOM. A window far wider than it is tall -- roughly 1710x370 in the screenshot -- shows the
front-end launcher shoved to the RIGHT: solid black from the left edge to about a third of the
way across, then the game's blue backdrop, logo and menu running to the right edge. A centred
fixed-width screen would have black on BOTH sides, equally. It has it on one.

WHY THIS WINDOW SHAPE IS THE ONE THAT SHOWS IT, from issue #41's geometry: the scale is
min(win_h/550, win_w/794), so a 370-row window scales by 0.673 and the composition becomes
win_w/0.673 = about 2540 game pixels wide. The front end, the mode menu, character selection
and the pre-fight overlay are all FIXED 794-wide screens, so at that composition there are
~1750 pixels of composition with nothing authored to put in them. That is the widest this
mismatch has ever been -- previously the composition was the window's pixel width, so a short
window gave a small composition and the gap was never this large.

The reporter's suggestion is to put the background on the left. Whatever the answer is, it has
to come from what the GAME does with a screen wider than its own, not from the port choosing a
placement it likes the look of.

THE CONSTRAINT ON THE FIX, stated by the reporter and it applies to every issue from here:
solve it by REVERSE ENGINEERING, never by a bandaid. Named as the thing not to do again: the
exit-to-menu path synthesising button presses instead of driving the game's own mechanism.
A placement constant that makes this screenshot look right is the same class of mistake.

WHAT IS NOT YET MEASURED, and must be before anything is designed:
  - WHERE THE BLACK ACTUALLY COMES FROM. Two candidates and they need different fixes: the
    port's centring offset (screen_offset_x) being applied to some draws and not others, or
    the primary's left columns never being written at all -- which is issue #29's mechanism,
    where the offset copy to the primary does not touch the leftmost `offset` columns.
    Asymmetric black is much more like the second.
  - WHAT THE GAME'S FRONT END ACTUALLY DRAWS. Whether it blits one backdrop the width of the
    screen, or fills and then blits, decides whether there is anything to extend leftwards at
    all. fn_0043f010 draws every screen; the launcher's own path has not been read.
  - WHETHER A COMPOSITION THAT WIDE SHOULD EXIST. The alternative to filling the gap is not
    opening it: a fixed-width screen could be given its own 794 and the leftover window
    treated as border. That is a geometry question, not a drawing one, and it is cheap to
    check offline in runtime/overrides/geom.h once the above is known.

### Resolved 2026-08-11 -- the background is wide and starts at the left

MEASURED FIRST, from the game's own blit list (LF2_BLT_FRAME=60 at 1710x370, composition 2542):

    blt 1  dst=(0,0)-(2542,550)  COLORFILL 000000   from 0040127e   the frame clear
    blt 2  dst=(0,0)-(794,550)   COLORFILL 0010206c from 004151bf   the front end's backdrop
    blt 3-7                      the character, the logo, the menu strips
    blt 8  dst=(874,0)-(3416,550) src=[2542x550]    from 0043e975   compose -> primary

Non-black columns in the dump: 874..2541. 874 is exactly (2542-794)/2, the centring offset.

TWO CAUSES, one on top of the other.

1. THE WIDENING RULE FIRED ON THE FRONT END. runtime/video/ddraw.c widened any COLORFILL of
   `dl == 0 && dr == 794` to the whole composition, on the reasoning that a fill spanning the
   native width is a stage band -- the sky, the ground, the road. But that rectangle IS the
   front end's backdrop: the front end fills its whole 794-wide screen. Decompiled to be sure
   (tools/re/ghidra_scripts/DecompDump.py): FUN_00415160 is the game's ONE colour-fill helper --
   it takes (x, y, w, h, colour), builds a RECT and calls Blt with DDBLT_COLORFILL -- and
   runtime/overrides/background.c calls the SAME function for the stage's tinted layers,
   passing each layer's own authored span. So the call site cannot tell the two apart and the
   rectangle is not evidence of anything: a tinted layer is 794 wide only when the stage
   happens to be exactly one screen wide (of the twelve shipped stages, HK_Coliseum is).

   The discriminator now comes from the call structure instead. The background pass is already
   an override, so it says so directly -- world_band_hint_set() around its fill, the same shape
   as the existing shadow_hint_set(). LF2_BAND_DEBUG=1 reports how many stage fills were seen
   and how many were widened, and says which of the two reasons a zero means.

   The sibling rule for full-width backdrop BLITS had already been gated on the world view
   being up, with a comment saying exactly why -- "or it would also stretch the fixed 794-wide
   menu backdrops that are deliberately being CENTRED". The bug was that its twin had not.

2. THE CENTRING WAS APPLIED TO THE WRONG THING. It was added to the destination of the single
   compose -> primary copy, which centres the whole composition at a stroke -- and is why the
   backdrop could not be made to fill the window: widen it, and the copy then shifts it right
   and leaves the primary's first 874 columns written by nobody.

   So the centring is applied WHILE COMPOSING now, to the draws that belong to the game's own
   794-wide screen, and the copy is left 1:1. The rule is one line: a draw that fits inside
   the game's screen is centred, a draw that already spans the composition is background and
   stays where it is. It is applied AFTER panel_note, because the screen detector recognises
   screens by their rectangles in the game's own coordinates and is what decides the offset in
   the first place -- shifting before it would feed the offset back into its own input.

   GDI text needed the same offset added explicitly (runtime/win32/gdi.c), because it writes straight
   into the surface and never goes through Blt. It used to get the shift for free by riding on
   the whole-composition copy.

RESULT at 1710x370: non-black columns 0..2541, the backdrop edge to edge, the logo, character
and menu centred on top of it.

AND IT REMOVED ISSUE #29'S BUG CLASS BY CONSTRUCTION. That bug was the leftmost `offset`
columns of the primary never being written and holding a ghost of the previously-centred
screen; a clear covered it. With the copy 1:1 every column of the primary is written every
frame, so there is nothing to clear and primary_clear_on_move() is gone. tools/routes/resize_test.sh
went red the moment that landed -- its negative arm, LF2_PRIMARY_STALE, disabled a clear that
no longer existed, so the arm could not fail and the test said so rather than reporting a pass
it could not justify. LF2_PRIMARY_STALE is now a defect INJECTOR that reproduces the old bug
exactly: the copy skips (composition - 794) / 2 columns, which is the number the centred copy
used to miss. Skipping an arbitrary 64 first was not enough and is worth recording -- the
leftmost columns are black in every frame at any ONE size, so the injected and clean runs
agreed and the arm still could not fail. The ghost only exists where a DIFFERENTLY centred
screen had picture. With the right number the arm reports 65145 stray pixels.

VERIFIED: ctest 8/8; tools/e2e.sh smoke, mouse, widescreen, resize and background all PASSED,
background byte-identical to the recompiled body at 794x550 with both control arms differing.

NOT ADDRESSED, and it is a different question: character selection and the pre-fight overlay
have no full-screen colour fill -- their backdrop is artwork -- so they are still centred with
black beside them on a very wide window. Extending a BITMAP sideways would mean inventing
layout the game does not have, which is the same answer issue #23 gives for a stage's sky.
Flat colour could be extended honestly; a picture cannot.

### Resolution (2026-08-11)
The widening rule matched the front end's own backdrop by rectangle, and the centring was applied to the compose->primary copy so a widened backdrop got pushed right. The stage's fills are now marked by the background override (world_band_hint) instead of guessed from the rectangle, and the centring is applied while composing to draws that fit inside the game's 794-wide screen -- so the backdrop spans the composition from the left edge and the screen's art is centred on top. Removed issue #29's bug class by construction.
