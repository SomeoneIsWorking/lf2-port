---
id: 58
title: The game's built-in background (fn_0041a050) draws its fills 794 wide, so it stops short in a wide view
status: resolved
symptom: found while tracing issue #55, not reported and not yet seen on screen. fn_0041a050 draws a fixed backdrop with five full-width fills whose width is the literal 0x31a (794), so on any view wider than the game's own screen those bands stop at 794 and leave the rest unpainted
tags: widescreen,rendering,background
created: 2026-08-11
updated: 2026-08-12
---

FOUND 2026-08-11 while eliminating camera readers for issue #55. NOT a report, and NOT yet
observed in a frame -- filed because the constant is unambiguous and the next person to see a
half-painted backdrop should not have to find it again.

DECOMPILED, fn_0041a050 (502 bytes, tools/re/ghidra_scripts/DecompDump.py). It draws a fixed
backdrop rather than a stage's layers: a repeating element scrolled at camera/100, another at
camera*7/10, a fence pattern stepped every 0x46 from `(900 - camera) % 0x46`, and five solid
bands drawn through FUN_00415160(x, y, w, h, colour):

    FUN_00415160(0, 0x146, 0x31a, 0x14, 0x3f3f3f)
    FUN_00415160(0, 0x159, 0x31a, 0x9c, 0x575757)
    FUN_00415160(0, 0x1d7, 0x31a, 0x1e, 0x3f3f3f)
    FUN_00415160(0, 0x148, 0x31a, 2,   0x373737)
    ... and four more at 0x31a wide for the horizon lines

0x31a is 794. Every one of those bands is the game saying "the whole width of the screen", in
the only units it had. On a 978-wide composition they cover 81% of it.

WHAT IS NOT ESTABLISHED: which background this is and whether any route reaches it. Issue #3
records a built-in background 99 built from PE resources rather than from bg.dat, and this
function's hardcoded geometry fits that description, but the two have not been tied together.
Nothing in tools/routes/ selects it. So this could be a screen no player sees without asking
for it -- find that out before spending anything on the fix.

THE FIX, WHEN IT IS WANTED, is the substitution this port has made four times already: 794 is
the view, not a constant. The difference here is that these are the game's own literals inside
a recompiled function, so it means an override for fn_0041a050 rather than a value patched
somewhere -- 502 bytes, and the decompile above is the whole of it.

RELATED: issue #23 is the same SHAPE for a real stage's sky layer (a layer with less picture
than the view is wide), and #54 is the same shape again for the stage-mode UI row. Three
places where 794 was the width of the world.

### Note (2026-08-12)
MEASURED, and the answer is 'not by either mode a route can reach', which is NOT the same as
'unreachable' -- the counter now says so in its own output rather than leaving a zero to be
misread.

background.c counts every frame it dispatches to the alt pass, and bg_camera_report prints it
with the frame denominator. Two runs at 1920x1080, software renderer:

    VS mode      built-in background selected on 0 of 1464 frame(s)
    stage mode   built-in background selected on 0 of 2346 frame(s)

So no route in tools/routes/ reaches it, and neither of the two modes a scripted run can enter
draws it at all. That is what this entry asked for before spending anything on the fix, and it
argues for spending nothing yet.

WHAT THE ZERO DOES NOT SAY, and the report now prints this rather than leaving it to be
inferred: it does not show the backdrop is unreachable. Index 99 is selected by SOMETHING --
issue #3 records a built-in background built from PE resources rather than bg.dat -- and what
selects it has still not been identified. The remaining question is one grep of the background
table, not a port: find whether any bg.dat record or any mode maps to 99, and if none does,
this entry can be closed as dead code rather than fixed.

THE COUNTER IS PERMANENT, so this question is now answered by any run with LF2_CAMERA=1 rather
than by re-deriving it. If a player ever reports a half-painted backdrop, that line is the
first thing to read.

### Note (2026-08-12)
REACHABLE AFTER ALL, and I was one step from closing this as dead code. Recording the step that
stopped it.

The frame counts said 0 of 1464 (VS) and 0 of 2346 (stage), and the background table has THIRTEEN
records, indices 0..12 -- so 99 is nowhere in the shipped data and looked like a sentinel for a
path nothing takes. That reasoning was wrong, and one grep of re/instructions.tsv settles it:
FIVE sites write 99 straight into the background index word [0x0044d024]:

    0042d253 / 0042d7f6 / 0042df26   FUN_00429730   (18823 bytes -- one of the game's monoliths)
    004339af                          FUN_00432ab0   (8140 bytes)
    00435b48                          FUN_00434ab0   (9468 bytes)

against the ordinary selections, which are the literals 1..6 at 0042cfca..0042d01f. So the built-in
background is DELIBERATELY selected in three different places, and its 794-wide bands are a real
defect on any wide view that reaches one of them.

WHAT THIS SAYS ABOUT THE ZERO. The counter was right and its wording was right: 'this run says
nothing about issue #58 -- it does NOT show the backdrop is unreachable, only that this route did
not reach it.' Had it printed a bare 0, the table's 13 entries would have made 'dead code' look
proven from two facts that are both true and together mean nothing.

WHAT IS LEFT: name the three screens. FUN_00429730 is the pre-fight overlay's own function
(tools/routes/mouse_test.sh reads its row geometry from it), which suggests these are the
non-match screens rather than a stage -- but that is a guess and the entry has been burned once
already. Read the three call sites before assuming; the fix itself is unchanged and small, an
override for fn_0041a050 with its five band widths reading bg_view_width().

### Note (2026-08-12)
THE THREE SELECTION SITES READ, and background 99 is a NORMAL SELECTABLE BACKGROUND rather than
a special screen. That settles the entry's open question and justifies the fix.

    0042d7d6  CALL 0x00417170(0xe0, ...)          ; picks a background
    0042d7db  MOV [0x0044d024],EAX                ; ...and stores it
    0042d7e6  MOV ECX,[[ESI+0x7d4]+0x4d82384]     ; the background COUNT from the registry
    0042d7ec  SUB ECX,3
    0042d7f2  CMP EAX,ECX
    0042d7f4  JNZ +                               ; if the pick is (count - 3)...
    0042d7f6  MOV [0x0044d024],0x63               ; ...it becomes the built-in one

and the other two are a CYCLE, which is what a chooser looks like:

    004339a4  CMP EAX,0x64 / JNZ  ->  MOV [0x0044d024],0x63    ; 100 wraps back to 99
    004339be  CMP EAX,0x63 / JNZ  ->  MOV [0x0044d024],EBP     ; 99 steps on

So 99 sits INSIDE the range a player cycles through when choosing a stage, and one particular
pick out of the table maps onto it. It is not a debug screen and not dead code: an ordinary
player reaches it by choosing backgrounds, which is exactly why no scripted route ever has --
every route takes the default and never touches the chooser.

CONCLUSION: the fix is worth doing. fn_0041a050 (502 bytes) becomes an override with its five
band widths reading bg_view_width(), the same substitution made for the layer pass and now for
the object pass. It should be accepted the same way -- byte-identity against the recompiled body
at a 794 view, in pixels and state, which tools/routes/objects_test.sh is now the template for.
A route that reaches it needs to drive the background chooser, which nothing does yet.

### Note (2026-08-12)
THE OVERRIDE IS WRITTEN AND BUILDS, AND IT IS NOT VERIFIED IN PLAY. Both halves matter.

runtime/overrides/background.c now carries fn_0041a050 as a hand-port: __thiscall, one stack arg,
RET 4, its three clip receivers read from the background record ([[this+0x7d4]+0x4d81978 /
+0x4d8197c / +0x4d81974], all elided by Ghidra), its fills through the same cdecl helper the layer
pass uses and marked as world bands. Every 0x31a is bg_view_width() and the fence loop's 0x319 is
one less than it, exactly as the game wrote them. LF2_ALTBG_ORIG=1 runs the recompiled body.

WHAT IS VERIFIED: it compiles, ctest is 10/10, and background + objects are green -- which says
only that nothing ELSE regressed. That is a real result but a narrow one, and it is guaranteed by
construction rather than by testing: no route selects background 99, so fn_0041a050 is never
called in any of those runs and could not have broken them whatever it contained.

WHAT IS NOT VERIFIED: the port itself. Not one pixel of it has been drawn. There is no route that
reaches background 99, which is the same gap that made this entry's reachability an open question
in the first place.

WHAT IT NEEDS, and it is small: a way to select background 99 so the objects_test.sh treatment can
be applied -- byte-identity against LF2_ALTBG_ORIG=1 at a 794 view, in pixels and state, where
bg_view_width() is 794 and the two must agree exactly. The port already has the right precedent
for that in LF2_MODE (runtime/overrides/menu.c), which writes the mode menu's OWN selection word
and lets the route's confirm dispatch it rather than faking the screen. The background index at
0x0044d024 is the same kind of word, and 0042d7f6 shows the game writing 99 into it directly.

UNTIL THEN, treat this override as UNPROVEN. It cannot affect normal play -- nothing selects 99
without a player choosing it -- but that is an argument about blast radius, not about correctness.

### Resolution (2026-08-12)
FIXED AND VERIFIED. fn_0041a050 is an override in runtime/overrides/background.c with every
0x31a reading bg_view_width() (and the fence loop's 0x319 one less than it, as the game wrote
the pair).

THE A/B, port against the recompiled body, at two widths:

    794x550     composition 794:      0 differing px
    1920x1080   composition 978:  32857 differing px, x 794..977

Both halves are needed and each says something the other cannot. At 794 bg_view_width() IS 794,
so the port must be byte-identical to the body it replaces -- and is. At 978 it must differ, and
the differing pixels lie ENTIRELY in x 794..977: exactly the band the game left unpainted, and
nothing before it. That range is the result. A bare 'they differ' would not have distinguished
'the backdrop now fills the view' from 'the port draws something else'.

LF2_ALTBG_FORCE=1 is what made it reachable -- no route selects background 99, since every route
takes the default. It writes the game's OWN index word, the same one 0042d7f6 writes 99 into when
the chooser lands there, so it takes the game's branch rather than faking a screen.

AND THE FIRST VERSION OF THAT FORCE WAS VACUOUS, which is worth recording because it produced a
clean-looking pass. It set only the override's LOCAL copy of the index; the alt branch hands off
to fn_0041a250__orig, which reads the index out of 0x0044d024 itself. So both arms drew the
ordinary stage and came out identical at 794 AND at 1920 -- and the 794 identity was reported as
'the port matches the body it replaces' when neither arm had ever called the ported function. The
tell was the 1920 arm: it was supposed to DIFFER and did not. A one-sided check would have shipped
it.

ctest 10/10; background and objects green.
