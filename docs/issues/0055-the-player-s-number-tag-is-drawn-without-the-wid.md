---
id: 55
title: The player's number tag is drawn without the widescreen centring offset, so it lags the fighter it names
status: closed
symptom: reported with a screenshot. In a wide view the small player-number tag ('1') that should sit under a fighter is drawn to the LEFT of them, by what looks like the widescreen centring offset, and the gap appears once the fighter walks past where the 4:3 screen would have ended
tags: reported,widescreen,rendering,hud
created: 2026-08-11
updated: 2026-09-09
---

### Note (2026-08-11)
THE TAG IS NOT DRAWN BY AN UNWRAPPED CAMERA READER, so the hypothesis in this entry is not
confirmed and the obvious fix would have been wrong. Recording what was eliminated so the next
session does not repeat it.
EVERY INSTRUCTION IN THE BINARY THAT TOUCHES THE CAMERA WORD [0x00450bc4], from
the retail disassembly -- 26 sites, and this is the whole set rather than a sample:
    0x00416fb6, 0x00417096      the two audio pan functions, already overridden and scaled
    0x0041a059..0x0041a226      fn_0041a050  -- the game's BUILT-IN background (see below)
    0x0041a30e, 0x0041a46a      fn_0041a250  -- the layer parallax, background.c's override
    0x0041a760..0x0041adc2      fn_0041a5a0  -- the object pass, WRAPPED with the shifted
                                camera, so anything drawn there is shifted with the sprites
    0x0041bbbc..0x0041bd70      fn_0041b5d0  -- the camera itself, and the writes
So there is no draw that reads the camera and is missed by the shift, except inside
fn_0041a050 -- and that function draws a fixed backdrop, not a per-object tag. If the tag were
drawn from `world_x - camera` anywhere, it would be in fn_0041a5a0 and would move with the
sprite it names.
WHICH MEANS THE GAP IS SOMETHING ELSE, and the entry's own step 2 is now the whole of the work:
measure it to the pixel rather than off a screenshot. What is known is that the gap is NOT
constant -- in a VS match frame at 1920x1080 the tags sit correctly under their fighters, and
in the stage-mode frame the tag is a long way left of one that has walked to the right-hand
bound. A gap that grows with distance is a SCALE disagreement, not a missing offset, and the
92-pixel centring offset cannot explain it. Do not add screen_offset_x() to the tag.
WHERE TO LOOK NEXT, given that: something that positions the tag from a number that is not the
object's drawn screen x -- a cached position, a value computed before the wrapper shifts the
camera, or a draw that goes through a different scaling path than the sprite. fn_0041a5a0's
lines around its calls at guest 0x0041ad57/0x0041adc2 pass small negative offsets (-0x1e,
-0x33) alongside a camera subtraction, which is the shape of something drawn just below an
object and is the first place to read.

### Note (2026-08-11)
TWO MORE PATHS ELIMINATED, so the tag is drawn by neither of the game's text routes.
  NOT TextOutA. LF2_TEXT_DEBUG over a full stage-mode match lists 104 distinct (row, text)
  draws and there is no per-fighter tag among them -- the lowest in-match row is y 115, and
  the tag sits at the fighter's feet, far below.
  NOT fn_00423940, the game's own string helper. LF2_GAMETEXT_DEBUG over the same match prints
  exactly ONE distinct string: "Stage mode (Difficult)". Nothing else in a match goes through
  it.
So the tag is drawn as raw clips through fn_0043f010 -- the call every glyph and every sprite
ultimately goes out as -- by something that composes its own digits rather than calling the
string helper. That is where the next look goes, and it is a narrower target than "somewhere in
the draw".
TAKEN WITH THE CAMERA-READER ELIMINATION ABOVE, the shape of this is now quite constrained: the
tag's position is not computed from the camera word by any unwrapped reader, and it is not
handed to either text route. Whatever positions it is inside the object draw, which IS wrapped
-- so a constant offset really is ruled out, and the growing gap points at the position being
derived from something other than the drawn screen x.

### Note (2026-08-11)
MEASURED AT LAST, AND MY "IT IS A SCALE, NOT AN OFFSET" NOTE WAS WRONG. The entry's ORIGINAL
hypothesis was right. Correcting it here so nobody acts on the retraction.
The tag IS a sheet glyph -- my earlier "not fn_00423940, so it is composed as raw clips" was
right about the route and my filter for finding it was broken, which is why two passes reported
nothing. LF2_GLYPH_POS=1 (new) prints every sheet glyph with the position the GAME asked for,
and the tag is the run at y 398:
    794x550     y=398  x=102     (524 draws)
    1920x1080   y=398  x=102     (524 draws)
IDENTICAL. Stage mode is deterministic, so this is the same game state at both widths, and the
tag's x does not move when the view does.
WHICH MEANS the tag is drawn OUTSIDE the wrapper on fn_0041a5a0. Issue #39 applies the
widescreen re-centring by shifting the camera WORD around that call, so anything drawn inside

(full record: git history)
