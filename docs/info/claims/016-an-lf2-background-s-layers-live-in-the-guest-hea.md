---
id: C016
kind: claim
status: holds
created: 2026-08-06
tags: rendering
depends: tools/decrypt_dat.py
---

## Claim

An LF2 background's layers live in the guest heap as parallel arrays of 30 entries -- repeat period at +0, layer x at +120, layer y at +240 -- and a layer's bg.dat 'width' is that repeat period, not the width of its bitmap

## Evidence

Decrypted bg.dat (tools/decrypt_dat.py) against a heap dump taken during a match on Brokeback Clif, the stage identified from a frame capture: periods 1379,1379,1379,1500,1500 appear contiguously at guest 0x24e72df4 in file order, x offsets 0,460,920,0,0 at +120, y offsets 129,129,129,261,296 at +240 -- every value matching the file. The period-vs-bitmap distinction is shown by CUHK, whose sky is sky1.bmp (800 wide) at x=0 plus sky2.bmp (167 wide) at x=800 with period 967 = 800+167, and by HK Coliseum, the one non-scrolling stage, where period 794 = bitmap 794 = stage width 794.

## What would falsify it

a stage whose decrypted layer periods do not appear as a contiguous in-order run in a heap dump taken during that stage, or a background with more than 30 layers (the 120-byte field stride would then be wrong). Note the base address 0x24e72df4 is NOT part of this claim -- it is allocation-dependent and must not be hardcoded.
