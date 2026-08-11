---
id: C024
kind: claim
status: holds
created: 2026-08-11
tags: 
---

## Claim

A stage-mode section's camera lock and walk lock are ONE stage-data field: the camera gets B-794, the fighter gets B

## Evidence

fn_00437860 at 0x00437b25/0x00437b38 writes DAT_00450bb0 = *(section+0x7d8) - 0x31a and DAT_00450bb4 = *(section+0x7d8), from the same address, in adjacent instructions. fn_0041b5d0's per-object loop clamps obj->x (offset 0x58, double) to DAT_00450bb4 when non-zero, and to the stage's own width (+0x7d8+0) otherwise -- with NO 794 term in either; the camera code in the same function uses stage_width - 0x31a. Decompiled 2026-08-11 with tools/re/ghidra_scripts/DecompDump.py

## What would falsify it

a stage whose fighters can walk past the section boundary the camera stops at, or a walk clamp found elsewhere that does carry a screen width
