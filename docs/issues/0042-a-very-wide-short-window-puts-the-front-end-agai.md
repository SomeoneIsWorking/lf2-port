---
id: 42
title: A very wide, short window puts the front end against the right with black beside it
status: closed
symptom: at a window like 1710x370 the launcher screen is not centred: black fills the left third and the game's picture runs from there to the right edge. The fixed-794 screens are supposed to be centred in a wider composition
tags: reported,rendering,widescreen,scaling,frontend
created: 2026-08-11
updated: 2026-09-09
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
