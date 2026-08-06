---
id: C021
kind: claim
status: holds
created: 2026-08-06
tags: rendering,re,background,lighting
depends: runtime/overrides/assets.c, runtime/overrides/world.h
---

## Claim

bg.dat's zboundary lives at BG_LAYER_SPAN-1120 and -1116 in the background record, and it is WHERE THE FLOOR IS ON THE SCREEN, not merely a movement clamp. LF2's depth axis projects straight down the screen -- which is why the game can depth-sort on z and why it draws the shadow ellipse at y = z -- so the rows below zmin are a horizontal surface and the rows above it are the backdrop standing behind it.

## Evidence

Four ways. (1) The pair sits inside the per-background scalar block between fields already mapped and verified: -1124 stage width (1500), -1120/-1116 (300/510), -1104/-1100 shadowsize (37/9), -1096 layer count (5) -- Brokeback Clif, LF2_BG_RECORD dump. (2) 300 and 510 are exactly the 'zboundary: 300 510' claim C018 records for that stage from an unrelated direction, walking a fighter to the back wall and watching object+0x18 stop at 300. (3) The game's OWN drawing agrees: the shadow ellipses in a match span screen y 302..441, inside the band and touching its far edge. (4) Run against every stage, not one: LF2_BG_TABLE prints 12 of 12 backgrounds giving an ordered pair inside 550 rows (289..510 across the set), which a wrong stride could not do -- a wrong stride returns the neighbouring pointer and would put the floor at row 600000000.

## What would falsify it

a stage whose ground markers fall outside the band the record gives for it, or a background whose pair is not ordered inside 550 rows -- bg_z_bounds refuses that case and bg_z_report counts it
