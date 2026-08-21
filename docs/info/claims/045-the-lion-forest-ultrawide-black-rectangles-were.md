---
id: C045
kind: claim
status: holds
created: 2026-08-21
tags: widescreen,background
depends: runtime/video/ddraw.c#surf_Blt, runtime/video/backdrop.h#backdrop_bottom_extension
---

## Claim

The Lion Forest ultrawide black rectangles were uncovered backing below the far backdrop, not failed colour-key transparency.

## Evidence

2026-08-21: frame-1688 blit trace showed the widened far backdrop ending at y=198, keyed later layers with a real x=1100..1216 gap, DDBLT_KEYSRC and key range 0..0; the fixed 1571x550 engine frame has 0/2436 black pixels in x=1100..1216,y=198..219.

## What would falsify it

A reproduction shows black in the gap while the far-backdrop final-row extension was submitted, or shows the later layers were not keyed.
