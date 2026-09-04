---
id: C017
kind: claim
status: holds
created: 2026-08-06
tags: rendering,widescreen,re
depends: runtime/overrides/world.h
reconfirmed: 2026-08-06
verified_at: 2026-08-06 13:29:33
---

## Claim

An LF2 background layer has a SPAN (bg.dat 'width:') and an optional LOOP (bg.dat 'loop:'), and fn_0041a250 draws it as: off = -(camera*(span-794))/(stage_width-794); if loop, tile from layer_x by loop while x < span, else draw once at layer_x. The 794 is the game's screen width and appears ONLY in that parallax -- it is not a loop bound. A layer's span is authored so the layer covers the 794 screen at EVERY camera position exactly, with no margin, which is why a non-looping layer has no picture left over for a wider viewport.

## Evidence

THREE independent sources agreeing.
(1) THE CODE. fn_0041a250 (828 bytes) was read end to end from the retail disassembly: 0041a309 SUB EAX,0x31a / 0041a30e IMUL EAX,[0x00450bc4] / 0041a316 SUB EBP,0x31a / 0041a31c IDIV EBP / 0041a31e NEG EAX is the parallax verbatim, with [0x450bc4] the camera and 0x31a = 794. The loop branch at 0041a4a4..0041a4ef steps EBP by the loop field and terminates on CMP EBP,[layer span] -- the span, not 794.
(2) THE DATA. tools/re/bg_table_check.py compares LF2_BG_TABLE=all against every shipped bg.dat: 12 of 12 background records matched exactly, 152 layers, span/x/y/loop each, no unmatched file. The checker has a --selftest that asserts it REJECTS a wrong loop value, so 'all ok' is not the only thing it can print.
(3) THE PIXELS. Brokeback Clif at a 1600x550 window, four camera positions from LF2_BLT_FRAME. Ground layer bc4 (span 1500, loop 800) wraps its source at 800 with no gap at every position; the cliffs bc1|bc2|bc3 (span 1379, no loop) never repeat. At maximum camera the ground offset is 706 = 1500-794 and the cliffs' is 585 = 1379-794, and the cliff chain ends at screen x 794 to the pixel -- which is the 'covers the screen exactly, no margin' property, measured rather than argued.

## What would falsify it

a stage whose non-looping layer still has picture beyond the 794 window at some camera, or any bg.dat whose runtime record disagrees with tools/re/bg_table_check.py

## Re-confirmed 2026-08-06

Re-verified after the mechanism was implemented from the retail routine. `runtime/overrides/background.c` now owns the formula the claim states. Prior native-width captures at two camera positions matched the retail behavior, the deliberate `LF2_BG_SKEW=3` arm differed, and the wide-view capture exercised the widened formula. `world.h` changed in the same commit only by gaining the remaining field constants; the claim's substance is unaltered.

## Re-confirmed 2026-08-06

Baseline re-stamped with a second-precision timestamp; the evidence is the five-arm background identity verification recorded above.
