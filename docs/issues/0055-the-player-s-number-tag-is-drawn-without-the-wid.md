---
id: 55
title: The player's number tag is drawn without the widescreen centring offset, so it lags the fighter it names
status: open
symptom: reported with a screenshot. In a wide view the small player-number tag ('1') that should sit under a fighter is drawn to the LEFT of them, by what looks like the widescreen centring offset, and the gap appears once the fighter walks past where the 4:3 screen would have ended
tags: reported,widescreen,rendering,hud
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11 with a screenshot, filed on receipt. Reporter's words: "the player tag gets
left behind after you walk to where the stage would go offscreen in 4:3".

THE MEASUREMENT OFF THE SCREENSHOT, and it names the suspect: the fighter is at roughly x 850
of the picture and their tag is at roughly 750 -- about 100 px apart. The run that produced
this geometry reports "view 978, centring offset 92". A hundred pixels and ninety-two are the
same number given the eye it was measured with, so the hypothesis is that the tag is drawn at
`world_x - camera` while the fighter is drawn at `world_x - bg_draw_camera()`, i.e. the tag
misses the widescreen centring shift that issue #39 gave the world.

WHY THAT IS PLAUSIBLE RATHER THAN JUST ARITHMETIC THAT FITS: issue #39's fix deliberately did
NOT write the shifted camera back into the game's camera word (fn_0041b5d0 eases toward its
target by a seventh and reads the word back, so a write feeds back and drifts to target-7K).
It is applied at DRAW time instead -- background.c's parallax and a wrapper on the object pass
fn_0041a5a0. Anything that draws from the camera word OUTSIDE those two places is therefore
unshifted by construction, and this tag is exactly the kind of small separate draw that would
have been missed.

WHAT TO ESTABLISH FIRST:
  1. WHICH DRAW IT IS. Find the call that draws the tag and whether it reads the camera word
     directly. If it is inside fn_0041a5a0 the hypothesis is wrong and the gap is something
     else, so this has to be checked and not assumed.
  2. WHETHER THE GAP IS EXACTLY THE OFFSET. It should be measurable to the pixel from a frame
     dump: run at a width whose offset is a known number, find the fighter's screen x and the
     tag's, and subtract. 'About a hundred' is where this starts, not where it ends.

DO NOT fix this by adding screen_offset_x() to the tag's x until step 1 says the tag is drawn
outside the shifted pass -- two different offsets are in play in this port (the composition
-space centring and the widescreen camera shift) and adding the wrong one lines the tag up at
one window size and not another.

RELATED: issues #23 and #54 are visible in the same screenshot and are different defects.

### Note (2026-08-11)
THE TAG IS NOT DRAWN BY AN UNWRAPPED CAMERA READER, so the hypothesis in this entry is not
confirmed and the obvious fix would have been wrong. Recording what was eliminated so the next
session does not repeat it.

EVERY INSTRUCTION IN THE BINARY THAT TOUCHES THE CAMERA WORD [0x00450bc4], from
re/instructions.tsv -- 26 sites, and this is the whole set rather than a sample:

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
