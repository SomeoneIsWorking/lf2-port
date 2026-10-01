---
id: 43
title: Stage mode's walkable area must extend to the widescreen view, RE'd first
status: closed
symptom: in stage mode the area a fighter may walk to is the game's 4:3 bound, so a wider view shows stage the player cannot reach
tags: reported,widescreen,stage-mode,gameplay
created: 2026-08-11
updated: 2026-09-09
---

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
photograph the gap, not to add a constant. The recorded Stage Mode scenario drove the port
into stage mode and reports the lock binding the camera on 273 frames.
WHAT THIS DECOMPILE CANNOT TELL YOU, said plainly because the output looks more complete than

### Note (2026-08-11)
MY NOTE ABOVE WAS WRONG WHERE IT MATTERED, and the reporter's screenshot is what falsified it.
Correcting it here rather than leaving it, because it would have sent the next session away
from a real bug.
WHAT I GOT RIGHT: the derivation. The section's camera lock and walk lock ARE one stage-data
field B (camera = B - 794, walk = B), the walk bound carries no screen term, and widening the
walk bound would let a fighter past a boundary the stage data declares. Claim C024 stands.
WHAT I GOT WRONG: I concluded from that "the screen's right edge lands on B whatever the view
is, so the reported gap cannot survive issue #36". That is only true while B - view is still a
position the camera can take. It is not, and the arithmetic is in my own run:
    section lock reached 106   ->  B = 106 + 794 = 900
    view                978
    camera max = B - view = -78, and the camera clamps at 0
So the camera sits at 0, the screen shows world 0..978, and the fighter stops at 900. SEVENTY
-EIGHT world pixels of stage are visible that the player cannot walk to. That is exactly what
was reported -- "most right I can walk to" with stage still to the right of the fighter -- and
it is what the earlier note declared impossible.
WHY THE PORT'S OWN INSTRUMENT DID NOT SHOW IT, which is worth as much as the bug: the camera

(full record: git history)
