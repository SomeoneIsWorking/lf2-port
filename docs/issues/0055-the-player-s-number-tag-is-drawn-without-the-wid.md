---
id: 55
title: The player's number tag is drawn without the widescreen centring offset, so it lags the fighter it names
status: open
symptom: reported with a screenshot. In a wide view the small player-number tag ('1') that should sit under a fighter is drawn to the LEFT of them, by what looks like the widescreen centring offset, and the gap appears once the fighter walks past where the 4:3 screen would have ended
tags: reported,widescreen,rendering,hud
created: 2026-08-11
updated: 2026-08-12
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
it comes out shifted; the tag comes out at the same x whatever the view, so it is not inside.
The sprite it names is. That is exactly the mechanism this entry was filed on.

WHY THE SCREENSHOTS LOOKED LIKE A GROWING GAP, which is what misled me. The shift is
k = (view - 794) / 2 = 92 at 1920x1080, but bg_draw_camera CLAMPS AT ZERO -- near a stage's
start the camera has not travelled past k, the shift is 0, and there is no divergence at all.
In the VS frame I compared, the camera was still at 0 and the tags sat correctly under their
fighters. In the stage frame the fighter had walked right, the camera was past the clamp, and
the full 92 was in force. Zero-then-92 read as "grows with distance" and I inferred a scale
from two samples that were really two sides of a clamp.

SO THE FIX IS THE ONE THE ENTRY ORIGINALLY NAMED, and the caution I added against it should be
ignored: the tag needs the same draw-time camera shift the world gets. What still has to be
found is WHERE it is drawn, since it is outside fn_0041a5a0 -- and the glyph route is now known,
so a call-ring print on the y-398 draw (the same trick that located the layer table for issue
#23) names it in one run.

DO NOT add screen_offset_x(). That is the composition-space centring and is a different number
from the camera shift; during a match it is zero by design. The one wanted here is
bg_draw_camera's k.

### Note (2026-08-11)
FOUND, IN THE GAME'S OWN CODE -- and the note above it is ALSO wrong, for the third time on
this entry, in the same way both earlier notes were: a comparison run against two sides of a
clamp. Read this note and not the two before it.

WHY "IDENTICAL AT BOTH WIDTHS" PROVED NOTHING. That reading concluded the tag is drawn outside
fn_0041a5a0 because x was 102 at 794 and at 1920. The glyph probe now prints the camera on the
SAME line, and over the whole run:

    cam=0

The camera never left the stage's left edge in either run -- the pad script did not walk. Below
the clamp bg_draw_camera returns the camera unchanged, so the shifted and unshifted hypotheses
predict the SAME x and the measurement cannot separate them. Repeating it with a walking script
(right held for 680 frames) gave 785 at both widths -- also cam=0, also vacuous.

WHERE IT IS DRAWN, from the return address the probe now prints: all 524 tag glyphs come from
guest 0x0041ab26, and re/functions.tsv puts that inside FUN_0041a5a0 (0x0041a5a0 + 2173). So
the tag IS inside the wrapped object pass, it DOES get the shifted camera, and the entry's
original hypothesis -- a missing centring offset -- is dead. So is the "scale" note. The fix
this entry twice pointed at (give the tag bg_draw_camera's k) would have double-shifted it.

THE ACTUAL CAUSE, from the walking run: the tag's x tracks the fighter (609, 619, ... 782) and
then STOPS at 785 and stays there for 869 frames while the fighter keeps walking into the width
the widened bound of issue #43 opened. That is the reporter's "the player tag gets left behind
after you walk to where the stage would go offscreen in 4:3", exactly.

785 is not an accident. scratch/decomp/0041a5a0.c, the tag draw:

    x = (obj[0x1c] - (len*9)/2 + obj[0x10]) - DAT_00450bc4;   /* world x, centred, minus camera */
    if (x < 0) x = 0;                                          /* left clamp  */
    if (0x31a - len*9 < x) x = 0x31a - len*9;                  /* RIGHT clamp: 0x31a == 794 */

A one-character tag gives 794 - 9 = 785. The game clamps the tag into its own 794-wide screen
so a name never runs off the edge; in a 978-wide view that clamp bites 184 px early and pins
the tag while the fighter walks on. Both clamps are wrong in a wide view -- the right one wants
the view width, and the left one is only correct because the shifted camera keeps screen x >= 0.

WHY THIS IS NOT A ONE-LINE FIX, and what NOT to do. 0x31a is an IMMEDIATE in the recompiled
code, not a data word, so no ST32 can reach it -- unlike the walk lock (issue #43) and the
camera word (issue #39), which are memory and are why those fixes were small. The wrapper in
background.c is a camera substitution around fn_0041a5a0__orig, not a hand-port, so there is
nothing there to edit either.

  DO NOT special-case the clamped value at the glyph call (x == 794 - 9*len => add view-794).
  It is unrecoverable-by-construction -- once clamped, a tag genuinely at 785 and one clamped
  from 900 are the same number -- and len is only knowable by counting consecutive glyph calls,
  i.e. pattern-matching the output. That is the bandaid this port exists to not ship.

THE PROPER FIX is to hand-port fn_0041a5a0 into runtime/overrides/ and make its two clamps read
bg_view_width() the way bg_view_width() already replaced the game's 794 elsewhere. It is 2173
bytes / 368 decompiled lines -- an object-collection, depth-sort and draw pass -- so it is a
real piece of work rather than an edit, and it is not started. It would also subsume the
wrapper: with the function ported, the camera substitution in background.c becomes a plain
`- bg_draw_camera()` at each of the nine subtraction sites.

INSTRUMENT NOTE: LF2_GLYPH_POS now prints `cam=` and `draw=` next to every glyph precisely so
this class of vacuous comparison cannot be read as an answer again -- a tag x that does not move
with the view means nothing while cam=0.

### Note (2026-08-12)
SCOPING THE PROPER FIX -- it is bigger than '368 lines', and here is the reason, so the next
session does not find it out halfway through.

fn_0041a5a0's draws are __thiscall. scratch/decomp/0043f010.c's signature is

    void __thiscall FUN_0043f010(undefined4 *this, int x, int y, int ch, int, int, int *surface)

but every call inside FUN_0041a5a0 decompiles as SIX arguments with the receiver elided --
Ghidra types the call and hides the ECX load. The receiver is not incidental: text.c's override
identifies a glyph BY it (font_sheet_index(R(ECX))), and the shadow hint keys off it too. So a
port cannot be written from the decompilation alone; it needs the raw listing from
re/instructions.tsv for the whole 2173 bytes to recover which object is in ECX at each of the
draw sites. Same question for FUN_0040de30 (819 bytes), the sprite draw the pass delegates to.

WHAT THE PORT WOULD CONTAIN, from the decompilation, so the size is not guessed: collect the
live indices out of param_1+4 over 0..399; bubble-sort them by obj[0x18] (depth); then per
object -- the stage shadow, the sprite via FUN_0040de30, the 'x N' multiplier label built by
hand from obj[0x30c], the name tag in TWO variants (the ordinary one and a MENU_CLIP7 one, and
BOTH carry the 0x31a clamp), and a trailing effects/icon loop over obj+0x3c0 with its own four
category branches that also WRITE back through the object. That last part is the risk: the pass
is not purely a draw, it advances per-effect counters, so a port that is subtly wrong corrupts
state rather than just misplacing a pixel.

The guest-ABI mechanics themselves are known and are not the problem -- runtime/overrides/coop.c
calls fn_004061d0 in the game's own ABI (PUSH32 a return address, set R(ECX), call), so the
idiom exists and is proven.

NOT STARTED, and deliberately not started halfway: a partially-correct object pass would break
every object in the game to fix one label's clamp, and the defect it fixes is one tag pinned
184 px early in a wide view. It wants a session with the instruction listing open and a way to
verify the pass draws identically at 794 before the clamp is touched -- that byte-identity arm
is the acceptance gate and it does not exist yet either.

### Note (2026-08-12)
THE CAUSE IS NOW MEASURED RATHER THAN READ OFF THE DECOMPILATION, and the blocker this entry
recorded against the port is gone -- the receivers are recoverable after all. Claim C025.

THE DISCRIMINATING RUN, which is what the earlier notes on this entry never had. A 794-based
clamp and a view-based bound predict DIFFERENT freeze positions, so two window widths separate
them: view-based would freeze at 1091 and at 969, 794-based freezes at 785 in both.

    window 1100x550    tag x=785 on 1268 frames
    window 1920x1080   tag x=785 on 1268 frames

Identical to the frame count. It is the game's 794, not the view.

THE CLAMP IN MACHINE CODE (re/instructions.tsv, 0041a9c9..0041aa33):

    0041a9e2  SUB ESI,dword ptr [0x00450bc4]   ; world - camera
    0041a9f3  XOR ESI,ESI                      ; low clamp at 0
    0041aa0e  MOV ECX,0x31a / SUB ECX,EAX      ; 0x31a == 794
    0041aa15  CMP ESI,ECX / JLE
    0041aa2e  MOV ESI,0x31a / SUB ESI,EDX      ; high clamp at 794 - 9*len

FOUR sites load 0x31a in this function -- 0041aa0e, 0041aa2e, 0041abf0, 0041ac10 -- i.e. TWO
clamp pairs, the ordinary name tag and the MENU_CLIP7 variant the decompilation shows as the
second branch. A port must fix all four, and a port that fixes one will look correct until
whatever draws the second variant appears.

THE RECEIVER PROBLEM IS SOLVED, so the note above it that called the port "bigger than 368
lines" overstated the obstacle. Ghidra elides the __thiscall receiver, but the listing does not:

    0041ab15  MOV ECX,dword ptr [0x0044faf4]   <- the receiver
    0041ab21  CALL 0x0043f010                  <- the tag glyph draw (ret 0041ab26, measured)

Every other call site resolves the same way by reading back from the CALL to the nearest ECX
load. The function is 636 instructions.

WHAT IS STILL TRUE: 0x31a is an immediate in code the recompiler bakes into C, so no ST32
reaches it and the fix is still a hand-port of fn_0041a5a0 with its four sites reading
bg_view_width(). NOT STARTED, deliberately and not for lack of information: the pass WRITES
BACK through the objects it draws (the effects loop at obj+0x3c0 advances per-effect counters),
so a half-correct port corrupts state rather than misplacing a pixel, and the acceptance gate
it needs -- byte-identity against fn_0041a5a0__orig at a 794 view, the shape background.c
already has with LF2_BG_ORIG -- does not exist yet. Those two together are the session's worth
of work, and starting them half-way is worse than not starting.

DO NOT, when that port is written, "fix" this by widening only the high clamp. The low clamp at
0 is correct only because the shifted camera keeps screen x >= 0; check it against the shift
rather than assuming it.

### Note (2026-08-12)
THE ACCEPTANCE GATE THE PORT NEEDS NOW EXISTS, built before the port rather than after it.

tools/routes/objects_test.sh (tools/e2e.sh objects), at a 794 view, software renderer:

    ok  frame_001351  two default runs are byte-identical, so the pass is deterministic
    ok  frame_001351  moving the pass's camera by 3 changes 5598 pixel(s)
    ok  frame_001801  two default runs are byte-identical
    ok  frame_001801  moving the pass's camera by 3 changes 5702 pixel(s)

WHY IT IS BUILT THIS WAY. A gate written after the change it is meant to catch is a gate nobody
has ever seen fail, and this project has shipped two of those tonight already (render_test
classified its frames by a hardcoded filename; both dump routes passed on half their coverage).
So the negative came first: LF2_OBJ_SKEW=<n> moves the camera this pass draws from, which moves
every object it draws and nothing else in the frame, and the 5598/5702 numbers are the proof
that the identity comparison can report a difference.

The determinism arm is also not decoration. The identity gate the port will use rests entirely
on this pass drawing the same frame twice, and NOTHING asserted that anywhere -- background_
test's byte-identity compares two different code paths and would not notice a pass that simply
varied run to run.

WHEN THE PORT LANDS: change the  arm from a second default run to LF2_OBJ_ORIG=1 and this
becomes the real gate. Until then the first arm is a determinism check and the file says so
rather than claiming to have compared a port with anything.

THE ABI IS ALSO RESOLVED NOW, so nothing about the port is research any more:

    fn_0043f010(this=ECX, x, y, ch, 1, 0, surface)   -- args pushed right to left
    receiver for every glyph draw:  ECX = [0x0044faf4]   (0041a896, 0041ab15, ...)
    receiver for the shadow draw:   ECX = [ECX + 0x4d4673c] at 0041a748
    fn_0040de30(this=object, camera, EDI, EAX)       -- the sprite draw, 0041a79f

Eight calls in the whole function: 0041a767, 0041a79f, 0041a89d, 0041ab21, 0041ac5c, 0041ad1d,
0041ad79, 0041add0 -- seven glyph draws and one sprite draw. 636 instructions.

WHAT IS LEFT is the port itself: 636 instructions of C in the guest ABI, with the four 0x31a
sites reading bg_view_width(), gated behind LF2_OBJ_ORIG so it can be A/B'd, and accepted only
when the identity arm above stays green at 794.

### Note (2026-08-12)
THE PORT'S REMAINING UNKNOWNS ARE NOW ZERO. Everything below was read out of re/instructions.tsv
rather than the decompilation, because the decompilation is wrong or silent on each of them.

THE SIGNATURE. Ghidra gives __thiscall FUN_0041a5a0(this, param_2, param_3) -- TWO stack args.
The function ends 'RET 0xc', which pops THREE. The prologue reads arg1 at [ESP+0x688] and the
sprite call reads arg2 at [ESP+0x68c]; the third is popped and never read. So the override's
return is R(ESP) += 4 + 12, and a port written from the decompilation alone would have
unbalanced the guest stack on every frame.

THE SHADOW DRAW (0041a716..0041a767), exact:

    rec   = LD32(this + 0x7d4) + bg * 0x990          ; bg = LD32(0x0044d024)
    w     = LD32(rec + 0x4d45dc4)                    ; shadow width
    h     = LD32(rec + 0x4d45dc8)                    ; shadow height
    sheet = LD32(rec + 0x4d4673c)                    ; the bitmap -- the RECEIVER
    x     = obj[0x1c] - w/2 + obj[0x10] - camera     ; signed halves (CDQ;SUB;SAR 1)
    y     = obj[0x18] - h/2
    fn_0043f010(this=sheet, x, y, -1, 1, 0, arg1)

THE GLYPH DRAWS all take this=LD32(0x0044faf4) and the surface LD32(0x00455608), pushed right
to left as (x, y, ch, 1, 0, surface).

THE SPRITE DRAW (0041a782..0041a79f): this=OBJ(idx), args (camera, arg1, arg2).

THE STRING BUILDING, which the decompilation renders as CONCAT12/CONCAT11 soup and is simply:
  - the multiplier label is "x" followed by obj[0x30c] in decimal (1 or 2 digits), drawn only
    when obj[0x30c] > 1;
  - the name is DAT_0044fcc0 + idx*0xb for idx < 10 and the literal "Com" otherwise, and when
    idx < 10 and (&DAT_00450b4c)[idx] == -1 it is wrapped as "[" + name + <DAT_00449060>.

WHAT REMAINS is transcription and verification, not investigation: 636 instructions, eight
calls, the four 0x31a sites reading bg_view_width(), the whole thing behind LF2_OBJ_ORIG, and
accepted only when tools/e2e.sh objects stays green at 794. The risk that keeps it from being
started at the end of a session is unchanged and is not about information: the effects loop at
obj+0x3c0 WRITES BACK (it advances per-effect counters and decrements obj[0x36c]), so a port
that is subtly wrong corrupts game state rather than misplacing a pixel, and the gate compares
PIXELS -- it would not necessarily catch a counter drifting.

### Note (2026-08-12)
THE GATE NOW COVERS STATE, WHICH REMOVES THE LAST STATED REASON NOT TO START THE PORT.

The objection recorded above was that the gate compared PIXELS, and fn_0041a5a0 does not only
draw -- its effects loop advances per-effect counters and decrements obj[0x36c] -- so a subtly
wrong port could corrupt the game while drawing a frame that still compared equal. That is now
covered: tools/e2e.sh objects dumps .data and the guest heap at the same anchored frames and
compares them too.

    ok  frame_001351 / frame_001801   two default runs byte-identical (pixels)
    ok  frame_001351 / frame_001801   camera skewed by 3 changes 5598 / 5702 pixels
    ok  state vs orig data_001351/001801, heap_001351   same, 0 dwords outside the mask
    ok  state vs alt  data_001351/001801                differ, 73 / 78 dwords
    ok  state vs alt  heap                              differ (106818032 bytes against 106858112)

WHAT HAD TO BE ESTABLISHED FIRST, and it is claim C026: the guest's state is deterministic
across identical runs to ONE BYTE IN 106 MB. Everything that varies is the __security_cookie
(0044eea4/eea8) or the wall-clock date string (0044fda4, 00451d58, 00458360 in .data;
20000040 in the heap). Those are masked -- the cookie as two dwords, the date as its +/-16 byte
buffer, because which dwords of a string differ depends on how far the clock moved between runs
-- and everything else must match EXACTLY. One differing dword fails.

FOUR THINGS WENT WRONG BUILDING IT, all of them controls that could not fire, and each was
caught by running the arm rather than by reasoning about it:
  1. LF2_OBJ_SKEW was going to be the negative for the state arms too. It moves where the pass
     DRAWS and nothing else, so it can never make state differ -- a control incapable of
     failing.
  2. The first state negative was an extra in-match press. That changes the fighter's RECORD
     (heap) but not the object table or EXISTS bytes (.data), so the .data negative came out
     IDENTICAL.
  3. Steering the character cursor instead reached .data and DERAILED the route: the alt arm
     never got to a match and dumped nothing.
  4. A different game MODE reaches both -- but its match starts earlier, so its anchored dumps
     are named for different frames and comparing by NAME reported 'the alt arm produced no
     such dump'. The comparison now pairs by position, and a size difference counts as a
     difference rather than an error.

SO THE PORT'S PREREQUISITES ARE DONE: the RE is transcribed, the ABI is resolved including the
RET 0xc the decompilation gets wrong, and the gate covers pixels and state with negatives that
have been seen to fire. What is left is writing 636 instructions of guest-ABI C with the four
0x31a sites reading bg_view_width(), behind LF2_OBJ_ORIG, and running this file.
