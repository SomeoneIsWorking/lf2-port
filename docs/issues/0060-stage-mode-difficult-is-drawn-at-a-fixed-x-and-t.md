---
id: 60
title: "Stage mode (Difficult)" is drawn at a fixed x and takes no widescreen offset
status: open
symptom: measured. The stage-mode caption at the bottom of the screen is drawn at x 613 identically at 794 and at 1920x1080, so on a wider view it sits where 613 falls in a 978-wide composition instead of where it falls in the game's 794 -- the same left-anchoring issue #54 fixed for the status row, on a different draw path
tags: widescreen,stage-mode,hud
created: 2026-08-11
updated: 2026-08-12
---

FOUND 2026-08-11 while eliminating draw paths for issue #55, and visible in the reporter's
stage-mode screenshot at the bottom of the picture.

MEASURED with LF2_GAMETEXT_DEBUG, the same route at two widths:

    794x550     gametext x=613 y=531 ... "Stage mode (Difficult)"
    1920x1080   gametext x=613 y=531 ... "Stage mode (Difficult)"

Identical. The string is laid out against the game's own 794 -- 613 + 22 glyphs is 789, so it
is right-anchored to that screen -- and nothing moves it when the composition is wider.

WHY IT GETS NOTHING, and it is the same gap issue #54 turned up rather than a new one: this is
the game's own sheet text, so it reaches the screen through fn_0043f010 and the Blt path, and
during a match screen_offset_x() returns 0 by design (panel_hud_up) because the world is placed
by the camera shift instead. The caption is not world and not HUD band, so nothing claims it.

WHAT MAKES IT DIFFERENT FROM #54, and why it is filed separately rather than reopened there:
#54's row is GDI TextOutA and was fixed by giving that path its own band (HUD_TEXT_BAND_H).
This one is on the OTHER path -- the game's bitmap sheet through Blt -- so the same fix does
not reach it, and its y is 531, at the very bottom of the screen, nowhere near a HUD band.

WHAT IS NOT ESTABLISHED: where this caption belongs when the view is wider. It is
right-anchored to 794 in the game's own layout, so "add screen_offset_x()" would put it 92 px
right of where it is, which may or may not be what a 978-wide screen wants -- the game's intent
was "near the right of the screen", and the honest reading of that on a wider screen is
arguable. Look at it before choosing, and note that the port's own controls hint already sits
along that same bottom edge (runtime/video/ddraw.c, controls_hint_draw) and is placed
deliberately -- whatever is done here should not collide with it.

### Note (2026-08-12)
TRACED ONE LEVEL, and the x is COMPUTED rather than written down -- which rules out the cheapest
fix and names the next step.

There is no 0x265 (613) anywhere in .text, so the caption's x is not a literal at its draw. The
chain so far:

    FUN_00423a70   139 bytes, four calls to fn_00423940 at 00423a9d/ab8/ad3/aee -- a thin
                   wrapper that takes the position as an ARGUMENT and lays the string out,
                   ending in one fn_0043f010 at 00423a63 (this=[0x0044faf4], surface
                   =[0x00455608], clip 0x5f)

So the 794-anchoring is in FUN_00423a70's CALLER, not in the wrapper and not at the glyph draw.
That also means the fix cannot be an override of the small function -- it would be overriding the
thing that receives the wrong x rather than the thing that computes it.

WHAT TO DO NEXT, and it is a measurement rather than a search: LF2_GLYPH_POS already prints the
return address of every sheet glyph draw, which is how issue #55's tag was located in one run.
Run it on the stage-mode caption (the pair at y 531/532) and the ret names FUN_00423a70's caller
directly. Then read whether that caller right-anchors to 0x31a, the way fn_0041a5a0 clamped to it
-- and if it does, the fix is the same substitution made three times now, in whichever function
owns the constant.

STILL OPEN, unchanged: where the caption BELONGS in a wide view. It is right-anchored to the
game's 794, so the faithful port is to right-anchor it to the view -- but the port's own controls
hint already sits along that bottom edge, and the two must not collide. Decide that with a
screenshot, not from the code.

### Note (2026-08-12)
MEASURED, and it corrects the note above it. The caption's glyphs do NOT come from FUN_00423a70's
own draw at 00423a63; every one of them comes from ret=004239f2, which re/functions.tsv puts
inside FUN_00423940 itself (298 bytes).

    y=531/532   x=773, 774, 781, 782 ...   ret=004239f2   (1546 draws each)

fn_00423940 IS ALREADY AN OVERRIDE in runtime/overrides/text.c, so unlike issues #55 and #58 no
new port is needed to reach this draw -- the port already owns the function the glyphs come out
of. And there is no 0x31a or 0x319 anywhere in its 298 bytes: it draws at whatever x it is handed
and does no anchoring of its own.

SO THE CONSTANT IS STILL FURTHER UP, in whatever calls FUN_00423a70 (the 139-byte layout wrapper,
whose four calls into fn_00423940 are the only ones in the binary). The chain is now fully
mapped and only its top is unnamed:

    ??? computes x  ->  FUN_00423a70 (lays out)  ->  fn_00423940 (draws, OVERRIDDEN)  ->  glyphs

DO NOT 'fix' this inside fn_00423940 by recognising the caption and shifting it. That override
sees only an x and a string; recognising the caption there means matching on its text or its y,
which is the pattern-matching this port refuses -- and it would move any other string that
happened to share the row. The constant belongs to the function that computes it, and that is
the one to find and own.

The remaining question is still the design one and still wants a screenshot: right-anchored to the
view is the faithful reading, but the port's controls hint shares that bottom edge.
