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

The recorded renderer comparison had all arms green: the engine matched the software compositor at maximum channel differences 1 (251/436700 px) and 2 (386/436700 px) on the two frames. Its negative arm changed 40,423 and 134,928 pixels by up to 255, so the engine produced the matching frames. The run reported 1,800 frames, 107,459 quads, 225 textures, and zero dropped; one SDL_GPU device, one D32_FLOAT depth buffer, and one texture pool.

## What would falsify it

a render-route run where the engine arm differs from software by more than the 4-level tolerance, or where the engineskip arm stops differing
