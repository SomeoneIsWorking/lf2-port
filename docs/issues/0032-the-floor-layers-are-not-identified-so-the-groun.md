---
id: 32
title: The floor layers are not identified, so the ground cannot be lit as a ground plane
status: resolved
symptom: the lighting treats every surface as a camera-facing billboard. The floor a fighter stands on is a HORIZONTAL surface and would catch the key light differently -- that is real 3D shading from geometry that genuinely exists, and it is the one honest piece of 'make the stages 3D'. It needs the walkable z band (bg.dat zboundary) or the floor layers themselves located in the background record, and neither is
tags: background
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
The floor is located, and it is lit as a floor. bg.dat's `zboundary:` is at BG_LAYER_SPAN-1120
and -1116 in the background record (claim C021).

WHY THAT PAIR IS THE FLOOR AND NOT JUST A MOVEMENT CLAMP: LF2's depth axis projects straight
down the screen. That is why the game can depth-sort on z, and why it draws the shadow ellipse
at y = z. So the z boundary is literally the screen row the walkable floor begins at -- rows
below it are a horizontal surface, rows above it are the wall standing behind it.

HOW IT WAS FOUND, since the first attempt at this record went wrong twice before: dump the
whole record (LF2_BG_RECORD, widened to +/-2600) and read the per-background scalar block.
Brokeback Clif gives 1500, 300, 510, ..., 37, 9, 5 at -1124, -1120, -1116, -1104, -1100,
-1096 -- and the first, the fourth/fifth and the sixth were ALREADY mapped and verified as the
stage width, the shadow size and the layer count. The unknown pair is bracketed by known ones.

VALIDATED FOUR WAYS, because a value that looks right at one offset on one stage is how the
-1128 shadow-pointer mistake happened:
  1. position in the scalar block, between four verified fields
  2. 300 and 510 are exactly what claim C018 recorded for this stage from an unrelated
     direction -- walking a fighter to the wall and watching object+0x18 stop at 300
  3. the game's own drawing agrees: shadow ellipses in a match span screen y 302..441, inside
     the band and touching its far edge
  4. run against EVERY stage rather than one. LF2_BG_TABLE now prints the band for all of
     them: 12 of 12 give an ordered pair inside 550 rows, spanning 289..510. A wrong stride
     returns the neighbouring pointer and would put a floor at row 600000000, so this is the
     check that could have failed and did not. It counts and prints its refusals.

WHAT IT IS USED FOR, and how small that deliberately is. A floor faces straight up and takes
the whole sky; a backdrop faces the camera and takes half. That is the only difference
applied, and it is applied AS COLOUR ONLY -- both terms are normalised to the same luminance
first, so the ground picks up the sky's tint and the brightness of the art stays the artist's.
The backdrop's multiplier is EXACTLY 1, which is what keeps the pass's promise that a frame
with no stage and no fighters is byte-identical.

GATED ON THE IN-MATCH HUD, and that gate is load-bearing: the background record stays loaded
after a fight, so the front end, the mode menu and character selection would otherwise all be
handed a perfectly valid floor band and get their lower half tinted by a stage that is not on
screen.

NOT DONE, and named rather than left implied: this is the floor's ORIENTATION, not its shape.
The stage is still flat painted layers, and the parallax rate -- which is the only other depth
the data has -- is already exactly the projection the game draws with, so there is nothing
further to take from it without inventing geometry.

### Note (2026-08-06)
MEASURED, not just intended. Over a match frame's floor with the fighters excluded (14137
pixels sampled): mean luminance change -0.03 levels of 255, mean chroma change 3.0 levels.
So the ground does pick up the sky's colour and the picture does not get brighter or darker.
Over the backdrop above the floor line, 0 of 16880 sampled pixels changed at all.

The one thing not to over-claim: the tint is a PER-CHANNEL multiplier, so it is the tint that
carries unit luminance, not every pixel. A strongly saturated albedo can still move -- the
largest single |dluminance| seen on the floor was 56 levels. The mean is what the luminance
lock guarantees.
