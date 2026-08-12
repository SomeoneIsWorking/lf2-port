---
id: C034
kind: claim
status: holds
created: 2026-08-12
tags: 
---

## Claim

The port's own engine reproduces the SDL_Render path it replaces, pixel for pixel

## Evidence

tools/e2e.sh render, all arms green: the engine matches the software compositor at max channel diff 1 (251/436700 px) and 2 (386/436700 px) on the two frames it compares -- the SAME numbers the old GPU path gives. Its own negative arm (LF2_ENGINE=1 LF2_RENDER_SKIP=7) changes 40423 and 134928 px by up to 255, so the engine is what drew the matching frames. LF2_ENGINE_DEBUG over the run: 1800 frames, 107459 quads, 225 textures, 0 dropped. One SDL_GPU device, one D32_FLOAT depth buffer, one texture pool.

## What would falsify it

a render-route run where the engine arm differs from software by more than the 4-level tolerance, or where the engineskip arm stops differing
