---
id: C016
kind: claim
status: falsified
created: 2026-08-06
tags: rendering
depends: tools/re/decrypt_dat.py
falsified_on: 2026-08-06
---

## Claim

An LF2 background's layers live in the guest heap as parallel arrays of 30 entries -- repeat period at +0, layer x at +120, layer y at +240 -- and a layer's bg.dat 'width' is that repeat period, not the width of its bitmap

## Evidence

Decrypted bg.dat (tools/re/decrypt_dat.py) against a heap dump taken during a match on Brokeback Clif, the stage identified from a frame capture: periods 1379,1379,1379,1500,1500 appear contiguously at guest 0x24e72df4 in file order, x offsets 0,460,920,0,0 at +120, y offsets 129,129,129,261,296 at +240 -- every value matching the file. The period-vs-bitmap distinction is shown by CUHK, whose sky is sky1.bmp (800 wide) at x=0 plus sky2.bmp (167 wide) at x=800 with period 967 = 800+167, and by HK Coliseum, the one non-scrolling stage, where period 794 = bitmap 794 = stage width 794.

## What would falsify it

a stage whose decrypted layer periods do not appear as a contiguous in-order run in a heap dump taken during that stage, or a background with more than 30 layers (the 120-byte field stride would then be wrong). Note the base address 0x24e72df4 is NOT part of this claim -- it is allocation-dependent and must not be hardcoded.

## FALSIFIED 2026-08-06

The LAYOUT half stands (parallel arrays of 30, period-ish field at +0, x at +120, y at +240,
reached through registry/bg-index -- proved against the file entry for entry). What is FALSE
is the interpretation of bg.dat's `width:` as the on-screen REPEAT PERIOD.

Falsified by direct observation of the blits the game emits, at two different camera
positions, on Brokeback Clif. Layer 3 is bc4.bmp, bitmap 800x35, bg.dat width 1500:

  camera A   dst=(0,261)-(379,296) srect=(421,0)-(800,35)
             dst=(379,261)-(794,296) srect=(0,0)-(415,35)
  camera B   dst=(0,261)-(201,296) srect=(599,0)-(800,35)
             dst=(201,261)-(794,296) srect=(0,0)-(593,35)

Both are a wrap at 800 -- the BITMAP width -- with no gap. If the repeat were the 1500 of the
`width:` field, an 800-wide bitmap would leave a 700-pixel gap, and there is none at either
position. Layer 4 (bc5.bmp, 600 wide, also width 1500) likewise repeats every 600.

So the game wraps a layer's SOURCE horizontally at the bitmap width, and `width:` is something
else -- most likely the layer's scroll span, which sets its parallax rate (bc4's 1500 equals
the stage width, i.e. it tracks the camera 1:1, while the cliff layers' 1379 is less and they
scroll slower). That reading is NOT yet established and must not be recorded as if it were.

Consequence, and it is good news for issue #23: the repeat distance the widescreen
continuation needs is available WITHOUT the layer table at all -- it is the source bitmap
width, which the port already has for every blit it forwards.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
