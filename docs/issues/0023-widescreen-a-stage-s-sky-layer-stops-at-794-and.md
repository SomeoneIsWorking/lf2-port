---
id: 23
title: Widescreen: a stage's sky layer stops at 794 and leaves black beside it
status: closed
symptom: in a window wider than 794x550 the upper part of the stage background ends partway across and the rest of that band is black, while the ground and the tiling layers do fill the width
tags: rendering
created: 2026-08-05
updated: 2026-09-09
---

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

(full record: git history)
