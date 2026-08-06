---
id: C018
kind: claim
status: holds
created: 2026-08-06
tags: rendering,re,objects
depends: runtime/overrides/coop_debug.c
---

## Claim

An LF2 world object holds x at +0x10, y (jump height) at +0x14 and Z (the depth axis) at +0x18, and fn_0041a5a0 depth-sorts the stage's draw list on +0x18. The port's LF2_COOP_TRACK had been labelling +0x18 as 'y'.

## Evidence

Discriminated by driving ONE input at a time on the same route, both classes run rather than reasoned about:
  pressing RIGHT   x (+0x10) moves 815 -> 731 -> 804 -> 993;  +0x18 stays 334 throughout
  pressing UP      +0x18 moves 334 -> 300 and stays;          +0x14 never leaves 0
  +0x14 is seen at -6 for a single sample mid-jump in the RIGHT arm, which is the vertical axis behaving as a jump height and nothing else does.
INDEPENDENT CONFIRMATION from a different source entirely: pressing up walked +0x18 to exactly 300, and Brokeback Clif's bg.dat says 'zboundary: 300 510'. The stage data's own lower z bound and the field's floor are the same number, which no coincidence of labelling would produce.
WHY IT MATTERED: fn_0041a5a0 bubble-sorts the draw list on [[this+idx*4+0x194]+0x18] (0x0041a610). A renderer told that word was 'y' would have depth-sorted the world on jump height.

## What would falsify it

an object whose +0x18 moves under a purely horizontal input, or a stage whose zboundary does not bracket the values +0x18 takes
