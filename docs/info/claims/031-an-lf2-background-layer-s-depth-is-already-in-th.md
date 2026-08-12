---
id: C031
kind: claim
status: holds
created: 2026-08-12
tags: re,renderer,hd2d
depends: runtime/overrides/geom.h
---

## Claim

An LF2 background layer's DEPTH is already in the shipped data: its scroll rate (span-794)/(stage_width-794) is a perspective divide written as a ratio, so depth = 1/rate with 1.0 being the plane the fighters stand in. The hand-weaving does not have to author depths for the layers the game already draws -- only for new solids.

## Evidence

Derived from fn_0041a250's own parallax, which runtime/overrides/background.c already implements. Applied to all 12 shipped stages it produces a depth ordering matching each stage's own DRAWING ORDER, which nothing forced: Tai Hom Village comes out 134, 17.5, 13.9, 1.75, 1.45, 1.33, 1.11, 1.00 in file order; CUHK puts its sky at 4.66, buildings at 2.1-2.6, front floor at 1.00. And it PREDICTS what no ordering argument could suggest -- The Great Wall's road3 has span 2600 on a 2400 stage, rate 1.125, depth 0.89, i.e. IN FRONT of the fighters -- which is exactly what that layer is, the strip at y 481 along the bottom of the screen. geom_layer_depth in runtime/overrides/geom.h, walked by ctest geometry's test_layer_depth (13 checks fail under a constant-depth mutant); tools/re/stage_gaps.py --depth prints it per stage.

## What would falsify it

a stage whose derived depths contradict its drawing order, or a layer drawn in front of the fighters whose rate is below 1 -- the derivation assumes the camera keeps the fighters centred, which is fn_0041bb7d's SUB ESI,0x18d and holds only while it does
