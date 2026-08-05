---
id: C006
kind: claim
status: holds
created: 2026-08-05
tags: coop,input,re
depends: runtime/overrides.c
---

## Claim

Player slot i is object index i: the game's gather walks the device-selector table and the object pointer table in lockstep over exactly eight entries

## Evidence

fn_00419a60__orig sets EBP=0x450b4c and EAX=this+404, reads the object as LD32(EAX) when (int32_t)LD32(EBP) > 0, and at the loop tail does EBP+=4, EAX+=4, ECX+=1, looping while EBP < 0x450b6c. So a human player's fighter must be at its own index and eight is the game's own bound. A computer's fighter is not bound by this -- its AI writes buttons directly -- which is why one is observed at index 11 while the joined mask reads two players and index 1 is empty.

## What would falsify it

a human-controlled fighter observed at an object index other than its player slot, or a gather loop bound that is not DEVSEL_END
