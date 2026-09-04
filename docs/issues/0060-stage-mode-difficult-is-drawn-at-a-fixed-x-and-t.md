---
id: 60
title: "Stage mode (Difficult)" is drawn at a fixed x and takes no widescreen offset
status: resolved
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
     an immediate; if it is, that word can be written the way the walk lock and the camera are
     (issues #43, #39). LF2_MEM_DUMP across two widths would show it. This is the only cheap
     outcome and it may not exist.
  2. LEAVE IT. The caption is right-anchored to the game's 794 and in a wide view sits 184 px
     left of the edge. That is wrong but it is not broken, and it is one static label.
  3. The one thing NOT to do is recognise the string inside fn_00423940 and shift it there --
     see the note above for why that is pattern-matching and would move anything sharing the row.

This entry is no longer 'find the constant'. It is 'decide whether a static label is worth a
data-word hunt inside a 20 KB function', and that is a judgement call, not a measurement.

### Note (2026-08-12)
### RESOLVED (2026-08-12) -- the monolith conclusion was wrong, and the fix is the usual one

The note above concluded "the caption's position comes from FUN_004246b0" -- a monolith, so
"the substitution that fixed issues #55 and #58 is NOT available here" -- and reduced the entry
to "decide whether a static label is worth a data-word hunt inside a 20 KB function". Both
halves were wrong, and both were wrong because the search was for a LITERAL rather than a read
of the code (issue #61).

WHAT THE CODE SAYS. fn_0041b130, 598 bytes, not a monolith:

    FUN_00423a70(&caption, strlen(caption) * -8 + 0x316, 0x213, 0x40, 4, 0, 0)

  - The constant is 0x316 = 790, not the 0x31a/0x319 the previous note searched for. 790 is
    794 less a four-pixel right margin.
  - The x is not a literal at all. It is the standard right-anchor -- the string's own length
    decides where it starts -- so scanning call sites for a pushed constant could never have
    found it, and the one call to the outline wrapper that IS inside fn_004246b0 pushes
    x = 0xd5 for a different string entirely.

The same function assembles the caption: five mode strings by game mode, a Survival special
case when the stage kind divides to 5, and one of four difficulty suffixes.

THE FIX is the same substitution as #55 and #58 after all -- own the function, and make its 794
the view. runtime/overrides/text.c now has fn_0041b130, which reads the game's strings FROM
THEIR GUEST ADDRESSES rather than transcribing them into the repo (they are the shipped
binary's text, and reading them is also what makes this exact rather than a re-typing).

THE COLLISION THE ENTRY FLAGGED IS NOT ONE. The port's controls hint is LEFT-anchored on the
same bottom edge (`8 + screen_offset_x()`). Right-anchoring the caption to the view moves it
FURTHER from the hint, not closer; they converge only as the view narrows, and the view floors
at 794 where this is byte-identical anyway.

VERIFIED by the recorded four-arm caption scenario:

    identity  794, port:  gametext x=613 y=532 cols=64 rows=4 font=0 "Stage mode (Difficult)"
              794, retail: identical, character for character, so this is a real A/B and not the port compared
                          with itself
    follows   1600, port: x 613 -> 1419, exactly the view's right edge
    control   1600, orig: x stays 613 -- which is the bug, and is what makes the line above
                          attributable to the port rather than to the wider run

### Resolution (2026-08-12)
fn_0041b130 (598 bytes, NOT a monolith) lays the caption out as strlen*-8 + 0x316, right-anchored to 794 less a 4px margin. The previous note searched the call sites for the literals 0x31a/0x319, found none, and wrongly concluded the constant lived in fn_004246b0 -- the constant is 0x316 and the x is computed from the string's length, so no literal search could have found it. The function is now a hand-port in runtime/overrides/text.c anchoring to bg_view_width() instead, with the game's own strings read from their guest addresses. The retained comparison establishes native-width identity and the move to the view's edge at 1600, with the retail routine as the control.
