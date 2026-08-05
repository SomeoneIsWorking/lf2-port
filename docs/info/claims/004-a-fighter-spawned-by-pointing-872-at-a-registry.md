---
id: C004
kind: claim
status: holds
created: 2026-08-05
tags: coop,hud,re
depends: runtime/overrides.c
---

## Claim

A fighter spawned by pointing +872 at a registry data block gets the correct HUD portrait; the portrait follows the data block, not the character-select cursor at +0x364

## Evidence

Two-sided, both comparisons internal to a single run so the VS randomiser cannot explain them: two spawns of object id 1 draw identical HUD portraits, and spawns of id 1 and id 52 draw different ones. Separately, two spawns of the same id with +0x364 forced to 0 and 5 draw identical portraits. scratch/screenshots/ab_hud.png, ab2_full.png.

## What would falsify it

a spawned fighter whose HUD portrait does not match its object id, or a portrait that changes when only +0x364 changes
