---
id: 43
title: Stage mode's walkable area must extend to the widescreen view, RE'd first
status: open
symptom: in stage mode the area a fighter may walk to is the game's 4:3 bound, so a wider view shows stage the player cannot reach
tags: reported,widescreen,stage-mode,gameplay
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11, with the explicit instruction: REVERSE ENGINEER IT FIRST. No constant
that makes one screen look right -- find the game's own bound and what it is expressed
against, then decide.

WHAT IS ALREADY KNOWN AND MUST NOT BE RE-DERIVED:
  - The CAMERA half is done (issue #36, resolved). fn_0041b5d0 bounds the camera twice: by
    `stage_width - 794` and, when non-zero, by the stage-mode SECTION LOCK at 0x00450bb0 --
    what holds the camera partway along a stage until the section is cleared. Both are the
    game saying "the right edge of the screen goes HERE" against a 794-wide screen, so both
    take the same 794 -> view substitution. That lives in geom_camera_max
    (runtime/overrides/geom.h) and is checked by `ctest geometry`.
  - THIS ISSUE IS THE OTHER HALF: where a FIGHTER may walk, which is not the camera. A wider
    camera bound without a wider walk bound shows stage the player cannot reach -- and the
    reverse would let a fighter walk off the visible picture.

WHAT HAS TO BE FOUND, and none of it is established yet:
  - WHICH WORD BOUNDS A FIGHTER'S X. The object step is fn_004064d0. The bound may be the
    stage width, a per-section value, or the same 0x00450bb0 lock read by the object code as
    well as the camera. Until that is read out of the binary this issue has no design.
  - WHETHER IT IS EXPRESSED AGAINST 794 AT ALL. The camera's bounds are, and that is what made
    the substitution legitimate there. If the walk bound is in WORLD units with no screen term
    in it, then it must NOT be widened -- the world would be the same size and only the view
    changed, which is the correct answer and the opposite of the change requested.
  - WHAT STAGE MODE DOES DIFFERENTLY. Sections are a stage-mode concept; VS has the lock at
    zero. `LF2_MODE=stage` drives the port into stage mode (runtime/overrides/menu.c) and
    `tools/e2e.sh stage_mode` already reaches it, so there is a route to observe this on.

HOW TO READ IT: tools/re/ghidra_scripts/DecompDump.py, see docs/running.md. fn_0041b5d0 is the
function that already gave up the camera bounds; the object step fn_004064d0 is the other
candidate and has not been decompiled.

DO NOT start by clamping a fighter's x to the view width in an override. That is the same
class of mistake as a synthesised keypress: it would produce the right-looking result without
anyone having found the mechanism, and it would be wrong the moment a section boundary or a
stage-mode transition disagreed with it.

### Note (2026-08-11)
RE DONE 2026-08-11, and it says the requested change would be WRONG. The walk bound must not
be widened. Recording the mechanism in full, because the conclusion is the opposite of the
report and nobody should have to re-derive it to disagree with me.

WHERE THE WALK BOUND IS. Not fn_004064d0 -- that is the object list walk and contains no
screen constant at all (decompiled, checked: no 0x31a, no 0x18d). It is in fn_0041b5d0, the
SAME function that moves the camera, in the per-object loop that runs before the camera code
at 0x0041ba98. Every bound there is against the stage's own record, indexed by the stage id at
param_1+0x7d4:

    +0x7d8+0    the stage's WIDTH        stage_width
    +0x7d8+4    a second field           used against obj+0x68 (the z axis, not x)
    +0x7d8+8    a third field            added to the second

and the clamp on a fighter's x (obj+0x58, a double) is, for a normal object:

    if (obj->x < <low margin>)   obj->x = <low margin>;
    if (stage_width < obj->x)    obj->x = stage_width;

THERE IS NO 794 IN IT. The camera's bound in the same function is `stage_width - 0x31a`, and
0x31a is 794; the walk bound is stage_width with nothing subtracted. That is the distinction
this issue was filed to establish, and it is decided: the camera bound is expressed against
the SCREEN and the walk bound is expressed against the WORLD.

THE STAGE-MODE SECTION IS THE SAME STORY, and this is the part that settles it beyond doubt.
The two section words are written together in fn_00437860 at 0x00437b25/0x00437b38, from ONE
field of the stage's section record (+0x7d8):

    DAT_00450bb0 = B - 0x31a      the CAMERA lock
    DAT_00450bb4 = B              the WALK lock

So B is the section's right-hand edge as a world x. The walk lock IS B. The camera lock is
"put the screen's LEFT edge at B - 794", which is the game saying: the screen's RIGHT edge
lands exactly on B. One number, expressed once in world units for the fighter and once in
screen units for the camera.

THEREFORE THE FIX IS ALREADY IN, AND IT IS THE CAMERA'S. With issue #36's substitution the
camera lock becomes B - view, so the screen's right edge still lands on B whatever the view is
-- and B is exactly where a fighter may walk to. Widening the walk bound on top of that would
let a fighter walk PAST the section boundary the stage data declares, which is a different
game rather than a wider one.

WHAT IS STILL OWED, and it is an observation rather than a design: the report says a wide view
shows stage the player cannot reach. On this reading that cannot be true once #36's
substitution is in force, at either a section boundary or the end of a stage. So either the
symptom predates #36, or it is something else with the same look -- and the honest next step
is to put a fighter against the right-hand bound of a stage-mode section at 1920x1080 and
photograph the gap, not to add a constant. tools/e2e.sh stage_mode already drives the port
into stage mode and reports the lock binding the camera on 273 frames.

WHAT THIS DECOMPILE CANNOT TELL YOU, said plainly because the output looks more complete than
it is: Ghidra lost the x87 stack in this function (extraout_ST0/extraout_ST1 appear as
undefined reads), so the LOW margins and the projectile-despawn margin -- the terms added to
and subtracted from stage_width for the various object kinds -- are NOT readable from it. The
STRUCTURE above is solid because it rests on integer field reads and the two literal
constants; the exact margin values are not, and anyone who needs them must get them from the
instruction listing rather than from this note.
