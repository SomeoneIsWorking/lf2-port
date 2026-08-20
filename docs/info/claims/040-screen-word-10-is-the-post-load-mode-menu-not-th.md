---
id: C040
kind: claim
status: holds
created: 2026-08-21
tags: re,screens
depends: runtime/overrides/world.h, runtime/overrides/screens.c#screen_name
---

## Claim

Screen word 10 is the post-load mode menu, not the discarded top-level launcher

## Evidence

fn_00431d10 is the eight-item VS/Stage/Championship/Battle/Demo/Playback/Quit dispatcher using 0x00451160; exit_to_menu reaches screen 10 and panel_modemenu_up reports that screen. The discarded launcher is selected instead by world top-level mode 0 before the loader.

## What would falsify it

a run draws the post-load mode menu with a screen word other than 10, draws the discarded launcher while the world is in game-proper mode at screen 10, or decompilation shows fn_00431d10 is not the eight-mode dispatcher
