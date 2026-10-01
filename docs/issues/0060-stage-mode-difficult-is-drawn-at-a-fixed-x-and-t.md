---
id: 60
title: "Stage mode (Difficult)" is drawn at a fixed x and takes no widescreen offset
status: closed
symptom: measured. The stage-mode caption at the bottom of the screen is drawn at x 613 identically at 794 and at 1920x1080, so on a wider view it sits where 613 falls in a 978-wide composition instead of where it falls in the game's 794 -- the same left-anchoring issue #54 fixed for the status row, on a different draw path
tags: widescreen,stage-mode,hud
created: 2026-08-11
updated: 2026-09-09
---

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
own draw at 00423a63; every one of them comes from ret=004239f2, inside
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

### Note (2026-08-12)
THE CHAIN IS TRACED TO ITS TOP, and the answer changes the strategy: the constant is owned by one
of the game's MONOLITHS, which this project does not hand-port.
Callers of FUN_00423a70, the layout wrapper:
    FUN_0041b130    598 bytes    no 0x31a / 0x319 anywhere in it
    FUN_0041b390    575 bytes    no 0x31a / 0x319 anywhere in it
    FUN_004246b0  20570 bytes    one of the four monoliths
And the two small ones are not this string. Read at 0041b3c5..0041b3df, that call site pushes
x = 0xa -- a label at x 10, not the caption at 613. So the caption's position comes from
FUN_004246b0.
WHY THAT MATTERS. The game's four monolithic routines stay under ordinary guest execution;
hand-porting them is not on the table. So the substitution that fixed
issues #55 and #58 -- own the function, make its 794 the view -- is NOT available here, and any
plan for #60 that assumes it is has not read this far.
WHAT IS LEFT, honestly, and none of it is free:
  1. FIND A DATA WORD. The caption's x may be computed from something in .data rather than from

(full record: git history)
