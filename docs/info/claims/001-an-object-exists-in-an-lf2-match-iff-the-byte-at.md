---
id: C001
kind: claim
status: holds
created: 2026-08-05
tags: coop,re,objects
depends: runtime/overrides.c
---

## Claim

An object exists in an LF2 match iff the byte at 0x00458b04 + index is 1; the 400 object pointers at 0x00458c94 (this+404) are all non-null at all times, so the pointer table is not the registration

## Evidence

fn_004064d0 walks this+404 and tests LD8(this+4+k)!=1 to skip an entry (lifted C). Measured: the gate byte reads 1 for exactly the live entries and 0 for every idle one. Setting it (plus a cloned record) on a running match makes the entry animate, walk, collide, take damage 500->432 and draw with a HUD bar; without it the identical clone is inert for 300 frames. scratch/screenshots/control.png vs spawned.png, from two runs with identical state (2 fighters) at the spawn frame.

## What would falsify it

an entry with the gate byte at 1 that the game does not update or draw, or a live fighter whose gate byte is not 1
