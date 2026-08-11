---
id: C022
kind: claim
status: holds
created: 2026-08-07
tags: rendering,menus,re,mouse
depends: runtime/overrides/screens.c
---

## Claim

The pre-fight overlay's six rows are at y 16, 39, 64, 87, 111, 137 -- NOT a uniform step -- and their labels are staggered in x at 92, 64, 40, 15, 37, 101. The list is drawn on a slant.

## Evidence

Ghidra decompile of FUN_00429730, the only function that touches OVERLAY_SEL (0x0044d06c, 21 references). Its highlight draw is six literal calls: draw_clip(0x5c,0x10,9), (0x40,0x27,10), (0x28,0x40,0xb), (0x0f,0x57,0xc), (0x25,0x6f,0xd), (0x65,0x89,0xe). The port had a uniform 24-px step from y 16, derived by measuring where the highlight blit landed for three PINNED selections -- and items 0, 2 and 5 are exactly the three a uniform step gets nearly right, so the sampling could not have shown the error. Confirmed downstream: with the row table in place a scripted mouse click at (150,25) selects item 0 and starts a match (screens reached charselect@1352 overlay@1751 match@1813).

## What would falsify it

a build of LF2 whose overlay art differs, or any run where clicking inside a row's y band selects a different item than the one whose highlight the game draws there
