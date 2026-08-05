---
id: C015
kind: claim
status: holds
created: 2026-08-06
tags: menu
depends: runtime/overrides/menu.c#fn_004246b0
---

## Claim

LF2 never returns its top-level mode word to the launcher: [0x00458b00] goes 0 -> 1 -> 2 once and is never written again, so there is no game-owned transition from a match back to the front end

## Evidence

Static: the only writes in the lifted binary are fn_00419e40 (the world object's constructor, [this]=0, reached only from the static initialiser fn_00446300 which has no caller) and ST32(R(ESI),0x2u) in fn_004246b0's mode==1 branch; the 0->1 write is at guest ret 0x00422ad2. Dynamic: LF2_WATCH=458b00 over a full route (launcher, load, charselect, overlay, running match, LEAVE MATCH via F4 + overlay Exit at frame 2389, then ~1000 further frames) printed exactly two transitions, both at boot, and nothing after. The two boot transitions are the positive control that the watch fires at all. Route report 21 of 21 presses fired, on the fixed report of issue #24.

## What would falsify it

a WATCH line on 00458b00 after the load in any run, or a store reaching that address through a register the static scan did not attribute (the scan covers constant loads of 0x458b00 and stores through this in the two functions that receive it, NOT an address computed as base+offset from elsewhere in the object)
