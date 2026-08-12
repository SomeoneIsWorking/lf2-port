---
id: 23
title: Widescreen: a stage's sky layer stops at 794 and leaves black beside it
status: resolved
symptom: in a window wider than 794x550 the upper part of the stage background ends partway across and the rest of that band is black, while the ground and the tiling layers do fill the width
tags: rendering
created: 2026-08-05
updated: 2026-08-12
---

OBSERVED, not reported, while verifying issue #20 -- and it is PRE-EXISTING, not caused by
that change: the same band appears on the commit before it (scratch/wide20old/frame_002250.png
at LF2_WIDESCREEN=1600, and scratch/wide20/frame_002250.png at a 1600x550 window). Both runs
drew different stages, which is itself worth knowing: the VS-mode background is picked at
random, so two runs of the same route are NOT pixel-comparable and a blit-count diff across
them will read as a huge change that is only a different stage.

WHAT IS ALREADY HANDLED, from issue #13, so nobody re-derives it: full-width colour fills are
carried across the viewport, and a tiling layer series the game stops at 794 is continued at
its own period. That is why the ground and the brick courses do reach the edge.

WHAT IS NOT: a stage's sky/backdrop is a single blit of one fixed-width picture. It cannot be
tiled -- it would visibly repeat -- and stretching it is a different picture rather than more
world, which is the whole distinction widescreen exists for.

NOT YET ESTABLISHED, and it decides the fix: whether LF2's own background data gives any
stage a backdrop wider than the viewport it is drawn into. A layer entry in the stage data
carries a width and a scrolling rate, so a backdrop that is wider than 794 and merely
CLIPPED to it would need only the clip widening -- which would be the game's own mechanism.
If it is exactly 794, there is no more picture to show and the honest options are to letterbox
that band deliberately or to leave it. Read the background data before choosing.

DO NOT stretch the backdrop to the viewport width as a fix. It would fill the black, and it
would make one stage's sky a different shape from every other element drawn beside it.

### Note (2026-08-06)
ESTABLISHED, 2026-08-06, by reading the game's own background data — and it CONTRADICTS the
assumption this entry was filed on. The data does give more picture, and the sky is not a
single fixed-width backdrop at all.

HOW IT WAS READ. bg.dat is encrypted like every other data file, so tools/re/decrypt_dat.py (new)
does the game's own cipher offline — the one from runtime/overrides/assets.c, proved
byte-identical to the game's on all 77 files. `tools/re/decrypt_dat.py --layers game/bg/*/*/bg.dat`
prints every stage.

THE KEY FACT: a layer's `width:` is a REPEAT PERIOD, not the width of its picture. The two are
different numbers and the difference is the whole answer:

  CUHK        stage width 1600
    sky1.bmp  period 967   x=0    bitmap 800x210
    sky2.bmp  period 967   x=800  bitmap 167x210      <- 800 + 167 = 967, exactly the period
  Template1/2/3               pic1 800 at x=0 + pic2 167 at x=800, period 967 — same shape
  HK Coliseum stage width 794
    back1.bmp period 794   x=0    bitmap 794x101      <- period == picture == stage: no scroll

So a stage's sky is a PAIR of bitmaps laid end to end to fill one period, and the period
repeats. Every stage but HK Coliseum is far wider than its sky's period — CUHK 1600 vs 967,
The Great Wall 2400 vs 800, Lion Forest 3200 vs 800, Tai Hom Village 1600 vs 800, Forbidden
Tower 2400 vs 797 — so THE GAME ITSELF REPEATS THE SKY as the camera scrolls. It has to.

That kills this entry's premise. It said "a stage's sky/backdrop is a single blit of one
fixed-width picture. It cannot be tiled -- it would visibly repeat". It is tiled, by the game,
by design, on every stage in the game. Repeating it across a widened viewport is the game's
own layout carried on, not something invented — the same justification the existing tiling
continuation already rests on.

WHY THE EXISTING CONTINUATION DOES NOT CATCH IT (runtime/video/ddraw.c, the `finish a tiling series`
block). It recognises a series by CONTIGUITY: this blit's left edge exactly where the previous
one ended, same rows. At camera 0 in a widened viewport the game draws sky1 clipped to its own
794 and never draws sky2 at all, because sky2 starts at x=800 — past the clip. One blit, no
predecessor, no continuation. And the period it would need is 967, the width of the PAIR,
which is not the width of any single blit it can see.

WHAT THE FIX NEEDS, and it is why this is not being landed in the same pass: the layer's period
at runtime. It is in the game's parsed background structures — bg.dat's `width:` per layer —
and locating that table is RE that has not been done. Guessing the period from the blit stream
is exactly the kind of inference that produced the "phase word" mistake in menu.c.

STILL CORRECT FROM THE ORIGINAL ENTRY: do not stretch the backdrop to the viewport width. That
remains the wrong fix, and now for a sharper reason — the picture already has a period the game
uses, so stretching would replace a real layout with an invented one.

### Note (2026-08-06)
THE LAYER TABLE IS LOCATED, 2026-08-06 — the fix now needs one more step, and it is named.

The stage was identified from a frame capture rather than assumed (Brokeback Clif, whose
bg.dat gives periods 1379/1379/1379/1500/1500 and stage width 1500). Those five dwords sit
contiguously in the guest heap, IN FILE ORDER, and from there the structure resolves as
PARALLEL ARRAYS OF 30 ENTRIES:

    +0     repeat period    1379, 1379, 1379, 1500, 1500
    +120   layer x          0, 460, 920, 0, 0
    +240   layer y          129, 129, 129, 261, 296

Every value matches the decrypted bg.dat exactly, which is what makes this an identification
rather than one number coinciding. 120 bytes = 30 dwords per field, so a background holds at
most 30 layers.

NOT located, and deliberately not guessed: the transparency array. Every stage's transparency
is 0, so a search for [0,0,0,0,0] matched several hundred places. A query that cannot
discriminate between candidates has not found anything, and picking a plausible hit from it is
how the "phase word" mistake in menu.c happened.

WHAT BLOCKS THE FIX, precisely. The port needs the period at runtime, and the table is
heap-resident:
  - It appeared at 0x24e72df4 in TWO separate runs, so allocation is deterministic. Hardcoding
    that address is not the fix -- it is a magic constant that breaks the moment anything
    upstream allocates differently, and it would look correct until it did.
  - There is NO pointer to it in .data. Not a near miss: .data holds 494 pointers into the
    guest heap (clustered at 0x200bxxxx, 0x206fxxxx, 0x25fxxxxx) and none of them lands within
    256K before the table.
  - There is no stored pointer to it anywhere in the HEAP either, scanned for targets within
    +/-512 bytes of the arrays.
So the game reaches these arrays as an offset inside a larger allocation whose base it holds
in a register or reaches through a chain -- which is exactly the shape that a pointer scan
cannot find.

THE NEXT STEP, and it uses an instrument this project already has rather than more scanning:
arm the READ WATCH (LF2_READ_WATCH, runtime/cpu/rwatch.c, instrument I001) on the period array and
let it name the guest instruction that reads it. The disassembly at that site then shows how
the address is computed, and that computation -- not the address -- is what the port copies.

Reproducing the measurement: tools/re/decrypt_dat.py --layers gives the expected periods, and
LF2_HEAP_DUMP=<frame> with LF2_MEM_DUMP=<frame> gives the pair of dumps this rests on. The
route used was tools/routes/controller_test.sh's, with the match reached at frame 1968.

### Note (2026-08-06)
UNBLOCKED, 2026-08-06 — the address computation is recovered, and it needs no magic constant.

HOW. The read watch (instrument I001) gained one line: on the FIRST read in the watched span
it prints the guest call ring. Armed on the period array during a match, the newest entry was
0x0041a250 — which runtime/video/ddraw.c already names as the background layer draw ("the count
comes from an immediate 794 inside FUN_0041a250"), so the two agree independently.

Reading that function's generated C gives the computation in full:

    registry = LD32(0x00458b00 + 2004)        // the world object's registry pointer
    bg       = LD32(0x0044d024)               // the current background index (99 = the
                                              //   built-in moon scene, cf. issue #3)
    field[i] = LD32(registry + (bg*612 + i)*4 + CONST)

Each background record is 612 dwords = 2448 bytes (the lifted code multiplies the index by
2448 at 0x0041a274 and by 612 for the dword form), and each field is a 30-entry array inside
it. Solved against the paired heap/.data dumps — registry 0x20129280, bg 6, period array
0x24e72df4 — the constants are:

    PERIOD   81027604      (0x4D46214)
    X        81027724      (= PERIOD + 120)
    Y        81027844      (= PERIOD + 240)

AND IT SELF-CHECKS: 81027844 was derived here as PERIOD+240 from the heap layout, and it
appears LITERALLY in the disassembly at 0x0041a2a4 as the base of an array indexed by bg*612.
Two independent derivations landing on the same number is what makes this an identification
rather than an arithmetic coincidence.

So the port can read any layer's repeat period at runtime with every term coming from the
game: a .data global for the background index, a .data global for the registry pointer, and a
stride the lifted code states. Nothing is hardcoded to an allocation, which is what the
previous note said the fix must avoid.

WHAT REMAINS is the drawing change itself in runtime/video/ddraw.c: the tiling continuation
currently infers a period from contiguity between blits, and for a sky it must instead take
the period from this table for the layer being drawn. Matching a blit to its layer index is
the one piece still to work out -- the y offsets (also in this table, at PERIOD+240) are the
obvious discriminator, since backgrounds do not scroll vertically.

### Note (2026-08-06)
THE RUNTIME READ IS PROVEN, 2026-08-06. The address computation from the previous note is
implemented (bg_layer_field in runtime/overrides/assets.c, addresses in world.h) and CHECKED
AGAINST THE FILE on a real stage -- the two agree entry for entry:

  from bg.dat (tools/re/decrypt_dat.py --layers)   from the running game (LF2_BG_TABLE=1)
    bc1  period 1379  x=0    y=129                layer 0  period=1379  x=0    y=129
    bc2  period 1379  x=460  y=129                layer 1  period=1379  x=460  y=129
    bc3  period 1379  x=920  y=129                layer 2  period=1379  x=920  y=129
    bc4  period 1500  x=0    y=261                layer 3  period=1500  x=0    y=261
    bc5  period 1500  x=0    y=296                layer 4  period=1500  x=0    y=296

Two independent sources -- a file decrypted offline and a heap-resident table addressed
through the game's own registry pointer -- agreeing on fifteen numbers. The port can now ask
the stage for a layer's repeat period at runtime.

The diagnostic earned its keep immediately by being wrong in a way that SAID so: sampled on
the first frame with a registry pointer, it caught the front end (background index 100, zero
layers) and reported "NO LAYERS -- either no stage is loaded or the address computation is
wrong; this says nothing either way". It now samples while panel_hud_up(), which is the moment
a stage is certainly loaded, so a zero there would be a real negative.

WHAT IS LEFT is the drawing change, and one question in it that is NOT yet answered: matching a
blit to its layer index. The obvious discriminator is the layer y, which this table also
carries, since backgrounds do not scroll vertically -- but y ALONE IS AMBIGUOUS. On CUHK the
sky (period 967) and hill.bmp (period 1140) both sit at y=128, so a match on y would sometimes
tile a hill at the sky's period or the reverse, and a wrong period tiles the wrong picture
across the whole widened band. The blit's HEIGHT separates those two (sky1 is 210 tall,
hill 264), so (y, height) is the candidate rule -- but it is a candidate, not a measurement,
and it must be checked on more than one stage before it is trusted. That is the next step.

### Note (2026-08-06)
MATCHING A BLIT TO ITS LAYER, analysed 2026-08-06 — and the obvious rule is NOT available.
This is the last unknown before the drawing change, so it is written down rather than tried.

Y ALONE IS AMBIGUOUS, measured over all twelve backgrounds (decrypt_dat.py over every
bg.dat). Three stages have layers at the same y with DIFFERENT periods:

  cuhk  y=128  sky1/sky2 period 967   vs  hill.bmp period 1140
  cuhk  y=283  statue    period 1175  vs  grass.bmp period 1210
  ft    y=129  w1.bmp    period 1500  vs  c1/f1..fd period 2300
  thv   y=128  5.bmp     period 800   vs  4.bmp     period 840

and cuhk's collision is on the SKY, which is exactly what this issue is about. So y-only
matching would either skip the case that matters or tile a hill at the sky's period.

(Y, HEIGHT) IS UNAMBIGUOUS — zero collision groups across all twelve — but the port cannot
use it, because the LAYER's height is not in the layer table. Searched the record for the
bitmap heights [150,150,150,35,231] and the widths [460,460,459,800,600] of Brokeback Clif:
neither appears as a contiguous in-order run. The dimensions live in the loaded surfaces, not
here. Nor is there a per-layer surface handle to match a blit's source against: every other
120-byte field array in the record is zero for this stage except one at +480, which holds
[0,0,0,800,600] — the last two layers' bitmap widths and nothing for the first three, so it is
not a dimensions array either.

RECOMMENDED NEXT STEP — match by DRAW ORDER, not by geometry. fn_0041a250 walks the layers of
one background in table order, so the Nth background blit of a frame is layer N and the period
is a direct index. That is exact rather than a heuristic, and it needs no geometry at all.
What it assumes, and what must be MEASURED before it is trusted: that the loop draws every
layer exactly once per frame and never skips one that is off-screen. Instrument it by counting
the world-band blits in a frame and comparing against bg_layer_count() — equal on every stage
means the index is safe; any stage where it differs kills the approach outright.

DO NOT implement geometric matching as a fallback for stages where the count disagrees. A rule
that is exact on some stages and approximate on others is one nobody can reason about, and a
wrong period tiles the wrong picture across the whole widened band -- which is worse than the
black strip this issue started from.

### Note (2026-08-06)
CORRECTION, 2026-08-06: "a layer's `width:` is the repeat period" IS WRONG, and claim C016 is
falsified. What replaces it makes the fix SIMPLER, not harder.

MEASURED, from the blits the game itself emits (LF2_BLT_FRAME), on Brokeback Clif at two
different camera positions. Layer 3 is bc4.bmp, bitmap 800x35, bg.dat width 1500:

  camera A   dst=(0,261)-(379,296)   srect=(421,0)-(800,35)
             dst=(379,261)-(794,296) srect=(0,0)-(415,35)
  camera B   dst=(0,261)-(201,296)   srect=(599,0)-(800,35)
             dst=(201,261)-(794,296) srect=(0,0)-(593,35)

Both wrap at 800 -- the BITMAP width -- with no gap anywhere. Were the repeat the 1500 of the
`width:` field, an 800-wide bitmap would leave a 700-pixel hole, and there is none at either
position. Layer 4 (bc5.bmp, 600 wide, also width 1500) repeats every 600 the same way.

So the game wraps a layer's SOURCE horizontally at its bitmap width. `width:` is something
else -- plausibly the scroll span that sets the parallax rate, since bc4's 1500 equals the
stage width (tracks the camera 1:1) while the cliffs' 1379 is less and they scroll slower.
That reading is a guess and is NOT recorded as established.

WHY THIS SIMPLIFIES THE FIX. The whole chain of work above -- locating the heap table,
recovering its address computation, the blit-to-layer matching problem and its ambiguities --
was in service of getting a period the port could not otherwise know. It turns out the port
already has it: the repeat distance is the SOURCE BITMAP WIDTH, which every blit carries.
runtime/video/ddraw.c's continuation currently repeats at `dr - dl`, the destination width of the
last blit in a run -- 593 for bc4 above, where the answer is 800. That is the bug, and it is
one expression.

WHAT IS STILL NOT SOLVED: a layer drawn as a CHAIN of different bitmaps side by side. The
cliffs are bc1|bc2|bc3 (460+460+459) and cuhk's sky is sky1|sky2 (800+167); continuing past
794 has to cycle through the chain rather than repeat the last bitmap, so the run's members
and their order have to be tracked, not just its period. The chain is recognisable from the
blits themselves -- contiguous, same rows, different source surfaces -- so this too needs no
table.

The layer-table work is not wasted (it is real, proved against the file, and is what
established that the sky is drawn as a pair) but it is NOT on the path to this fix.

### Note (2026-08-06)
THE WHOLE MECHANISM IS NOW READ OUT OF THE GAME, 2026-08-06, and it settles what this issue
can and cannot be fixed to. Claim C017 carries the evidence; the short version:

fn_0041a250 (the background layer draw, 828 bytes, read end to end) is:

    off = -(camera * (span - 794)) / (stage_width - 794)          // the parallax
    if (loop)  for (x = layer_x; x < span; x += loop)  draw(off + x)
    else       draw(off + layer_x)

  span = bg.dat's `width:`   loop = bg.dat's `loop:`   794 = the game's screen width

TWO THINGS IN THIS ISSUE'S EARLIER NOTES ARE NOW KNOWN TO BE WRONG.

  1. "the count comes from an immediate 794 inside FUN_0041a250" -- the comment in
     runtime/video/ddraw.c, and repeated here. It does not. The tiling loop terminates on the
     LAYER'S SPAN. The 794 appears only in the parallax, twice.
  2. "a stage's sky is a PAIR of bitmaps laid end to end to fill one period, and the period
     repeats ... THE GAME ITSELF REPEATS THE SKY. It has to." It does not, and it does not
     have to. CUHK's sky1|sky2 has no `loop:` and is drawn ONCE. Nor does any other stage's
     sky: hkc back1 (span 794), gw sky (800), lf forests (800), ft sky (797), cuhk sky (967)
     -- every one non-looping, every one a span barely over 794.

WHY: a layer's span is authored so that span - 794 is exactly its scroll range, i.e. the
layer covers the screen at EVERY camera position and by no more than a pixel. Measured on
Brokeback Clif at maximum camera, the cliff chain's right edge lands on screen x 794 exactly.
So a non-looping layer has 794 pixels of picture and never any more. THERE IS NO EXTRA
PICTURE TO UNCOVER, at any camera, on any stage. The original entry's instinct was right and
the middle notes were wrong.

WHAT THAT MAKES THE FIX. Two halves, and they are one formula so they land together:

  a. A LOOPING layer declares its own repeat, so carrying it past 794 is the game's layout
     continued. The port must take the step from the `loop:` field, which it can now read
     (BG_LAYER_LOOP, world.h) -- not from the blit stream.
  b. A NON-LOOPING layer cannot be carried. The only honest lever is the CAMERA: clamp it to
     (stage_width - view_width) instead of (stage_width - 794) and use the view width in the
     parallax denominators, so the wider view never scrolls past what the layers cover. That
     is issue #28, reported independently on the same day, and it is the same 794.

WHAT THE PORT IS DOING NOW IS WORSE THAN THE BLACK BAND, and this is the immediate defect.
runtime/video/ddraw.c's contiguity continuation cannot tell a looping layer from a non-looping one,
so on Brokeback Clif at 1600x550 it repeats bc2 -- the middle cliff -- from x=1027 to the
right edge, with a hard black seam at 1026 and another at 1487. Captured:
scratch/wide23/frame_2250.png. It invents cliff the game never draws. Whatever lands for (a)
and (b), that heuristic goes.

STILL CORRECT FROM THE ORIGINAL ENTRY, now for the strongest possible reason: do not stretch
and do not tile a backdrop. There is no more picture and the layer says so.

### Note (2026-08-06)
PARTIALLY FIXED, 2026-08-06 -- the invented content is gone and the looping layers now carry
correctly. What remains is the part that has no honest answer at this layer of the port.

WHAT LANDED. The layer pass is now runtime/overrides/background.c, and the widescreen change
in it is one substitution: the game's literal 794 becomes the live view width, in the parallax
and in the tiling bound. A LOOPING layer is carried past 794 at its own declared `loop:`
step, which is the stage's layout continued; a NON-LOOPING layer is drawn once and pinned
(the parallax inverts below span == view, so it would drift the wrong way).

runtime/video/ddraw.c's contiguity continuation is DELETED. It was the source of the worst of this:
on Brokeback Clif at 1600x550 it repeated the middle cliff across the widened band with hard
seams at 1026 and 1487. Before/after: scratch/wide23/frame_2250.png vs fixed_2250.png.

VERIFIED, three ways, by tools/routes/background_test.sh (tools/e2e.sh background):
  794x550   byte-identical to the recompiled body at two camera positions
  control   LF2_BG_SKEW=3 differs, so the identity above is not a blind pass
  1600x550  differs from the unwidened body, so the view width really does reach the pass

WHAT REMAINS OPEN, and it is the original entry's question: a non-looping layer has exactly
794 pixels of picture, so beside it there is black. On Brokeback Clif at 1600 that is the
221 px past the cliffs' 1379 span. Every stage's sky is in the same position.

THE DECISION TAKEN, so nobody re-opens it as a bug: leave it black for now. The three
alternatives were weighed and each is worse HERE --
  tile the sky            invents layout the stage does not have and visibly repeats
  pillarbox to the span   faithful but effectively disables widescreen (gw's sky is 800)
  edge-extend the columns invention too, and wrong on any sky with detail at its edge
The right answer is a lit backdrop from a real renderer, which is issue #30, and that is
where this should be finished rather than in the blit compositor.

### Note (2026-08-11)
THE EXACT LAYER IS IDENTIFIED, from the reporter's stage-mode screenshot and the game's own
data. Stage 1-1 is Lion Forest, and the band that stops is ONE layer:

    bg\\sys\\lf\\forests.bmp    width: 800   x: 0   y: 128   bitmap 800x70   NO loop:

Its span is 800 and the view is 978. 978 - 800 = 178, which is 18% of the picture -- the black
band in the screenshot, to the pixel.

AND THE PORT IS ALREADY DOING THE RIGHT THING WITH THE PARALLAX, which is worth stating so it
is not "fixed" again. geom_layer_offset (runtime/overrides/geom.h) already substitutes the view
for the game's 794:

    if (stage_width <= view || span <= view) return 0;
    return -(((span - view) * camera) / (stage_width - view));

For this layer span (800) <= view (978), so the offset is zero and the layer sits at x 0. That
is correct: the game's invariant is that a layer's span is chosen to cover the SCREEN at every
camera position exactly, with no margin, so a layer whose whole span is narrower than the view
has no position that covers it. Scrolling it would only move the black.

EVERY LAYER IN THE GAME THAT CAN DO THIS, listed rather than sampled -- spans under 978 across
all shipped stages:

    CUHK            sky1 + sky2   span 967   (a PAIR, 800 + 167, laid end to end)
    Forbidden Tower sky           span 797
    The Great Wall  sky           span 800
    HK Coliseum     back1/2/22    span 794   (stage width is also 794)
    Lion Forest     forests       span 800   <- the reported one
    Queen's Island  qi1 and 7 more            span 800
    Tai Hom Village 5             span 800

So it is the SKY of six stages, and always the sky. That is not a coincidence to note in
passing -- it is the layer that has no parallax left to give.

THE OPEN QUESTION IS NOW SHARP, and it is not about the parallax. runtime/video/ddraw.c already
has a rule for exactly this shape -- "a background layer drawn from x 0 across the whole native
width is a full-width backdrop ... those are the only pieces that cannot be made wider by
drawing more of them, so they are stretched across the viewport instead". forests.bmp is 800
wide at x 0, which is wider than the native 794, so it should qualify and it plainly is not
being stretched. Find out why that rule declines this blit before writing any new rule: the
answer is either a condition that is wrong or a blit that does not look the way the rule
expects, and both are cheap to see with LF2_DRAW_PATHS.

DO NOT add a second stretch path beside the existing one. Two rules for the same shape is how
they drift.

### Note (2026-08-11)
WHY THE STRETCH RULE DECLINES, measured with LF2_BAND_DEBUG (new, gated) rather than reasoned
about -- and the same measurement shows the rule cannot be repaired where it lives.

    band: dl 0 dr 978 dt   0 db 550   dest 978 wide   (NATIVE_W 794)
    band: dl 0 dr 800 dt 128 db 198   dest 978 wide       <- forests.bmp, the reported band
    band: dl 0 dr 800 dt 147 db 251   dest 978 wide       <- forestm1.bmp
    band: dl 0 dr 284 dt 170 db 254   dest 978 wide
    ...

The rule tests `dr == NATIVE_W`, i.e. 794. forests.bmp arrives as dr 800. It is not 794 because
the game never clipped it: the port patches the game's own width words to the view, so a layer
narrower than the view is delivered at its natural width instead of being cut to the screen.
The rule was written when 794 was that cut, and the widescreen work moved it out from under it.

BUT LOOSENING THE TEST TO `dr >= NATIVE_W` WOULD BE WRONG, and the second line above is why.
forestm1.bmp arrives at dr 800 too, and it is NOT a full-width backdrop -- its span is 1100,
it scrolls, and there IS more picture to draw. Two blits with identical rectangles, opposite
correct treatments. Nothing in a Blt's geometry can tell them apart, so no condition written at
that call site can be right.

WHAT DOES TELL THEM APART is the layer's own span against the view, and the port already has
it: runtime/overrides/background.c overrides fn_0041a250, walks the layer list, and reads
BG_LAYER_SPAN per layer (world.h). span <= view means the layer has no position that covers the
view and no more picture to give -- exactly the six skies listed above -- while span > view
means it scrolls and must not be touched.

SO THE FIX BELONGS IN background.c's LAYER DRAW, not in ddraw.c's blit rule. That is also the
tidier answer to the warning in the note above about a second stretch path: the ddraw rule
should probably LOSE this job rather than gain a companion, since it has been guessing at layer
identity from a rectangle and only worked while the game's clip happened to make that guess
correct.

WHAT IS STILL NOT DECIDED, and it is a look rather than a measurement: whether stretching a
70-pixel-tall treeline by 22% is better than the black. The original entry says not to stretch,
for the reason that it makes one stage's sky a different shape from everything drawn beside it
-- and that reason still stands even though the mechanism is now understood. The other honest
option is to letterbox the band deliberately. One frame of each, side by side, settles it.

### Note (2026-08-11)
CORRECTING THE NOTE ABOVE ON ONE POINT, before anyone acts on it: the fix cannot be done
*inside* background.c's layer draw either, and the reason is worth knowing.

A non-looping layer is drawn by one call:

    draw_layer(obj, off + lx, y, pic, arg0);

which hands the game's own draw a PICTURE and a POSITION. There is no width in it. The
destination rectangle is decided downstream, by the blit, from the picture's natural size --
so background.c can decide THAT a layer is a full-width backdrop but cannot itself stretch one.

WHICH SETTLES THE DESIGN, and the port already has the pattern for it. background.c knows the
layer's span and therefore knows the answer; ddraw.c owns the rectangle and therefore has the
only place the answer can be applied. So background.c must HAND THE FACT ACROSS, exactly as
world_band_hint_set already does for the world band -- a hint set immediately before the draw
and consumed by the next blit:

    span <= view  ->  "the next layer blit has no more picture; stretch it to the view"
    span >  view  ->  say nothing, and the blit is left alone

and ddraw.c's existing `dl == 0 && dr == NATIVE_W` test is then DELETED rather than loosened.
It was inferring layer identity from a rectangle, it only ever worked while the game's clip
made that inference true, and the measurement above shows two blits with identical rectangles
needing opposite treatment. A hint from the code that knows is not a workaround for that test;
it is the thing the test was approximating.

SO THE REMAINING WORK IS: one hint setter beside world_band_hint_set, one call in the
non-looping arm of background.c's layer loop, and the deletion of the rectangle test. The 794
arm stays byte-identical by construction, because at view 794 no layer has span <= view except
HK Coliseum's, whose stage is 794 wide and whose blit already fills the screen.

AND THE OPEN QUESTION IS UNCHANGED and still belongs to whoever looks at it: whether stretching
a 70-pixel treeline by 22% beats the black band. Everything above is how to do it, not whether
to. If the answer is "letterbox instead", the same hint is what a deliberate letterbox would be
driven from -- the port would still need to know which layers have no more picture.

### Note (2026-08-12)
THE THREE EDIT SITES, PINNED, so the next session executes rather than re-derives. Nothing below
is a new decision -- the entry already specifies the mechanism; this is where it lands.

  1. THE HINT SETTER -- runtime/video/hostwin.h, beside world_band_hint_set (line 80) and
     shadow_hint_set: a layer_exhausted_hint_set taking an int, defined in runtime/video/ddraw.c
     next to world_band_hint_set. Same shape as the two that already exist: set immediately
     before the draw, consumed by the next blit, cleared after.

  2. THE CALL -- runtime/overrides/background.c, the NON-LOOPING arm of the layer loop (the
     draw-once branch of the pass documented at lines 27-28). Set it when span <= view, and say
     nothing otherwise. The condition is the entry's, unchanged.

  3. THE TEST TO DELETE -- runtime/video/ddraw.c line 1484, the spans_screen predicate built
     from dl, dr and NATIVE_W, together with the comment at 1468-1473 that already admits it
     matches the front end's backdrop "to the pixel" and claims narrowing is enough. The
     entry's own measurement is why narrowing is not enough: forests.bmp and forestm1.bmp
     arrive with IDENTICAL rectangles (both dr 800) and need opposite treatment, so no
     predicate over that rectangle can be right.

THE GATE EXISTS, which is what makes this executable work rather than exploratory:
tools/e2e.sh background runs byte-identity at 794 -- which must stay identical, because at view
794 no layer has span <= view except HK Coliseum's, whose stage is 794 wide -- a 1600 arm that
must differ, and a skewed-parallax arm proving the identity check can fail.

EXPECT IT NOT TO WORK FIRST TRY. Two blits with identical rectangles needing opposite treatment
is exactly the shape that survives a careless change. Run the 794 arm before believing anything.

STILL NOT DECIDED, and still not mine: whether stretching a 70-pixel treeline by 22% beats the
black band. The hint above is what a deliberate letterbox would be driven from too -- the port
needs to know which layers have no more picture either way.

### Note (2026-08-12)
### Note (2026-08-12) -- THE PINNED PLAN IS WRONG, and the data says why

The 2026-08-12 note above pins three edits driven by `span <= view`. That predicate is wrong,
and reading every shipped stage's bg.dat shows it catches almost nothing it should.

MEASURED, all 12 stages, offline (tools/re/decrypt_dat.py plus each layer's BMP header, which
is where the PICTURE width lives -- bg.dat only gives the span). Non-looping layers whose
picture does not reach a 1600-wide view:

    Brokeback_Clif    3 such layers, worst bc1.bmp     460 px at x=0
    CUHK             20 such layers, worst grass.bmp   180 px at x=0
    The_Great_Wall    4 such layers, worst road1.bmp   235 px at x=0
    Queen's_Island   13 such layers, worst qia.bmp     127 px at x=27
    Lion_Forest       5 such layers, worst forestm3    284 px at x=0
    Tai_Hom_Village  17 such layers, worst 4.bmp       364 px at x=245

A 127-pixel lamp post and a 180-pixel patch of grass are not backdrops with "no more picture".
They are PROPS: they cover their own patch of the stage on purpose, and what is beside them is
not black, it is the layer behind them. `span <= view` does not distinguish a prop from a
backdrop, so a hint driven by it would have stretched the lamp posts.

WHAT ACTUALLY LEAVES BLACK is the BACKMOST layer, and only it -- nothing is drawn behind it, so
the columns it does not reach are the only genuinely empty ones. Layer 0 of every stage:

    Forbidden_Tower   sky.bmp     797 px at x=0, span  797     <- picture == span: static
    The_Great_Wall    sky.bmp     800 px at x=0, span  800     <-   "
    Lion_Forest       forests.bmp 800 px at x=0, span  800     <-   "
    Queen's_Island    qi1.bmp     800 px at x=0, span  800     <-   "
    Tai_Hom_Village   5.bmp       800 px at x=0, span  800     <-   "
    HK_Coliseum       back1.bmp   794 px at x=0, span  794     <-   "
    Template1/2/3     pic1.bmp    800 px at x=0, span  967     (pic2 167 at x=800 completes it)
    Brokeback_Clif    bc1/2/3     460+460+459 end to end       = 1379
    CUHK              floor1 x2   797+797 end to end           = 1594
    Stanley_Prison    wall.bmp    LOOPS at 277                 -- already handled, no black

So on 6 of 12 stages the backmost layer is a single picture 794-800 wide whose span EQUALS its
picture width -- meaning it does not scroll at all (the scroll range is `span - 794`, i.e. 0 to
6 pixels over the whole stage). It is a static full-screen backdrop and there is provably no
more of it. On a 1920x1080 window the view is 978, so 178-184 columns of every frame on those
six stages are black: about 18% of the picture. That is the reported symptom, exactly.

THE MECHANISM THIS NEEDS is therefore not a hint about spans. background.c already knows the
layer INDEX, and layer 0 is the backmost by construction -- no rectangle inference, no
predicate over a blit, nothing to get wrong. Whatever the answer below is, it is applied to the
backmost layer (or the backmost run of layers laid end to end, which is the Brokeback/CUHK/
Template shape) and to nothing else.

WHAT IS STILL A DECISION, now with numbers behind it rather than a guess about a treeline:

  A  STRETCH the backmost layer to the view. At a 1080p window that is 800 -> 978, a 22%
     horizontal stretch of one static gradient. At an ultrawide 2542 view it is 3.2x, which is
     visibly a different picture.
  B  TILE it at its own width. Free and exact for a sky that is a horizontal gradient
     (constant along x); a visible seam for any that is not.
  C  MIRROR-TILE. Seamless for every picture by construction, invents a reflection.
  D  EDGE-CLAMP -- continue the outermost column outward. Indistinguishable from correct for a
     horizontal gradient, smears for a picture with content at its edge. The least inventive of
     the four: it adds no shape that is not already there.
  E  LETTERBOX -- leave the black, deliberately, and say so.

The entry's standing principle is "do not invent layout", which argues for E and then D. But
18% of every frame on half the stages is what E costs, and that is the reporter's call rather
than this entry's.

### Note (2026-08-12)
### DECIDED (2026-08-12) -- none of A..E. The columns get HAND-WOVEN, so this waits on #62

Asked with the numbers above in front of it, the reporter's answer was "hand weave". So the
four algorithmic fills and the deliberate letterbox are all rejected: the columns beyond the
authored backdrop are to be filled with hand-authored per-stage content, which is issue #62.

WHAT THAT SETTLES, and it is worth stating because it closes several loops at once:

  - No hint, no predicate, no `span <= view` test. There is nothing for the port to INFER --
    if a stage has authored extension content the port draws it, and if it does not there is
    nothing to draw. The three edits pinned in the 2026-08-12 note are not to be made.
  - `ddraw.c`'s rectangle-based stretch tests keep their current scope. They were only ever
    going to be replaced by the hint this entry no longer needs.
  - The entry's original principle survives intact and is now the DESIGN rather than a
    constraint on one: "do not invent layout" -- so the layout is authored instead.

WHAT THIS ENTRY STILL OWNS, and it is the useful part: the measurement of what has to be
authored. The note above names, per stage, the backmost layer, its picture width and the
columns it leaves empty. That list IS the work order for the backdrop half of #62 -- six stages
whose backmost layer is a static 794-800 px picture, plus Brokeback_Clif (1379), CUHK (1594)
and the three Templates (967), against whatever view the window gives.

STATUS: not fixable on its own. Reopened work belongs to #62.

### Resolution (2026-08-12)
Folded into #62. Reading every shipped bg.dat killed the span<=view predicate three edits were pinned around -- it catches props (a 180px patch of grass, a 127px lamp post) that have the next layer behind them. Only the BACKMOST run leaves real black, and tools/re/stage_gaps.py now measures it: 9 of 12 stages short at a 978 view, 11 of 12 at 2542. Asked with those numbers the reporter chose to hand-weave the columns rather than stretch, tile, mirror, edge-clamp or letterbox them, so this entry has no fix of its own and hands its measurement to #62 as that entry's first work order.
