---
id: C009
kind: claim
status: holds
created: 2026-08-05
tags: coop,drop-in,character-select
depends: runtime/overrides/coop.c#coop_build
---

## Claim

A fighter already in the world can be re-skinned to another character only by REBUILDING the record (gate off, fn_004061d0, new data block, same position back) -- not by writing the new data block into +872 and leaving the rest

## Evidence

The rebuild path is the game's own inlined spawn from fn_0041bc90, run again. Measured over a scripted run (scratch/logs/select3.log): cycling right ran the roster 50 -> 38 -> 39 -> 37 and left returned 37 -> 39, with each character drawing correctly on the stage and in the HUD portrait (game/scratch/frame_002403.png id 38 vs frame_002443.png id 39 -- visibly different fighters, different HUD portraits). The reason a pointer swap alone is not enough is that a character's animation frame numbers are its own: a record left holding the old character's frame would index the new character's data with it.

## What would falsify it

a rebuild-free data-pointer swap that survives a full animation cycle on the new character -- if one does, +872 is more self-contained than this assumes
