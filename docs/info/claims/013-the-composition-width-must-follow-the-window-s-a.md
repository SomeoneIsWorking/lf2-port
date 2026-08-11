---
id: C013
kind: claim
status: holds
created: 2026-08-05
tags: widescreen,rendering
depends: runtime/ddraw.c#hostwin_window_geometry, runtime/ddraw.c#surfaces_follow_window
---

## Claim

The composition width must follow the window's ASPECT, not its pixel width, and the surfaces that follow it must be allocated once at a fixed maximum pitch

## Evidence

A 1920x1080 window at pixel width would ask the game to compose 1920x550 and present it letterboxed into 1080 rows -- the world at a quarter scale in a strip. By aspect it asks for 550*1920/1080 = 978, which fills the window. tools/e2e.sh widescreen asserts 794x550->794, 1600x550->1600, 1920x1080->978 and 800x900->794, the last two being the cases a naive implementation gets wrong in each direction. The allocate-once half is forced by vram_alloc being a bump allocator with no free (runtime/ddraw.c): a surface reallocated per SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED would take a fresh WIDE_MAX*4*550 = 9 MB slice for every event in a drag. Measured mid-run: scratch/wide20mid/frame_002180.png is 794 wide and frame_002260.png, after a scripted resize to 1400 during a match, is 1400 wide with the tiling layers continued and the HUD re-centred, from surfaces that were never reallocated.

## What would falsify it

a window whose aspect the composition does not follow within a pixel, or a long drag that exhausts the vram arena -- either would mean the width is not derived from the aspect or the surfaces are not allocated once
