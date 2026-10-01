---
id: 58
title: The game's built-in background (fn_0041a050) draws its fills 794 wide, so it stops short in a wide view
status: closed
symptom: found while tracing issue #55, not reported and not yet seen on screen. fn_0041a050 draws a fixed backdrop with five full-width fills whose width is the literal 0x31a (794), so on any view wider than the game's own screen those bands stop at 794 and leave the rest unpainted
tags: widescreen,rendering,background
created: 2026-08-11
updated: 2026-09-09
---

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
path nothing takes. That reasoning was wrong, and the retail disassembly settles it:
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
(the recorded mouse runtime scenario reads its row geometry from it), which suggests these are the
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

(full record: git history)
