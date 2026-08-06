---
id: 23
title: Widescreen: a stage's sky layer stops at 794 and leaves black beside it
status: open
symptom: in a window wider than 794x550 the upper part of the stage background ends partway across and the rest of that band is black, while the ground and the tiling layers do fill the width
tags: rendering
created: 2026-08-05
updated: 2026-08-06
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

HOW IT WAS READ. bg.dat is encrypted like every other data file, so tools/decrypt_dat.py (new)
does the game's own cipher offline — the one from runtime/overrides/assets.c, proved
byte-identical to the game's on all 77 files. `tools/decrypt_dat.py --layers game/bg/*/*/bg.dat`
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

WHY THE EXISTING CONTINUATION DOES NOT CATCH IT (runtime/ddraw.c, the `finish a tiling series`
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
arm the READ WATCH (LF2_READ_WATCH, runtime/rwatch.c, instrument I001) on the period array and
let it name the guest instruction that reads it. The disassembly at that site then shows how
the address is computed, and that computation -- not the address -- is what the port copies.

Reproducing the measurement: tools/decrypt_dat.py --layers gives the expected periods, and
LF2_HEAP_DUMP=<frame> with LF2_MEM_DUMP=<frame> gives the pair of dumps this rests on. The
route used was tools/controller_test.sh's, with the match reached at frame 1968.

### Note (2026-08-06)
UNBLOCKED, 2026-08-06 — the address computation is recovered, and it needs no magic constant.

HOW. The read watch (instrument I001) gained one line: on the FIRST read in the watched span
it prints the guest call ring. Armed on the period array during a match, the newest entry was
0x0041a250 — which runtime/ddraw.c already names as the background layer draw ("the count
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

WHAT REMAINS is the drawing change itself in runtime/ddraw.c: the tiling continuation
currently infers a period from contiguity between blits, and for a sky it must instead take
the period from this table for the layer being drawn. Matching a blit to its layer index is
the one piece still to work out -- the y offsets (also in this table, at PERIOD+240) are the
obvious discriminator, since backgrounds do not scroll vertically.

### Note (2026-08-06)
THE RUNTIME READ IS PROVEN, 2026-08-06. The address computation from the previous note is
implemented (bg_layer_field in runtime/overrides/assets.c, addresses in world.h) and CHECKED
AGAINST THE FILE on a real stage -- the two agree entry for entry:

  from bg.dat (tools/decrypt_dat.py --layers)   from the running game (LF2_BG_TABLE=1)
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
