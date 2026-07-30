---
id: 2
title: Intermittent in-match world freeze (scripted route), ~50-75% of runs
status: resolved
symptom: match reached, HUD drawn, fighters animate but never move; CPU never attacks; HP bars byte-identical across 150 frames
tags: freeze,match,intermittent,scripted-input
created: 2026-07-30
updated: 2026-07-30
---

**Instrument:** scratch/bin/freeze_probe.sh — drives the smoke route to a match, dumps frames 2250/2400, compares the P1 HP-bar crop (130x10+57+18); zero differing pixels = frozen (Difficult CPU always attacks in a live match).

**Established:** occurs with the ad-loader override active AND with LF2_ADS_ON=1 restoring the original ad path (3/4 frozen either way), so fn_0043c4a0's override is NOT the cause. Also not stage-specific (frozen on moon stage and Forbidden Tower; live runs on plaza/volcano/tower).

**Open:** what state distinguishes a frozen run — suspects: game's own pause (focus loss / WM_ACTIVATEAPP under SDL dummy driver), or the scripted route leaving the pre-fight overlay logically open. Diff .data between a frozen and live run at the same frame, filtering wall-clock fields (the ad init writes time-of-day at 0x451d48 area, which poisoned a first diff).

### Resolution (2026-07-30)
Root cause was the %lf scanf store: gscan dropped the l modifier and stored 4 of 8 bytes, so every parsed double (all character physics: walking_speed, jump_height, ...) kept its magnitude from stale guest heap residue -- sometimes plausible, sometimes zero, which is a world where nobody can move. Fixed in runtime/imports.c (scan_store honours is_long). 8/8 freeze probes live after the fix, vs ~50-75% frozen before. Same root cause as the Mac walk-in-place/jump-softlock reports.
