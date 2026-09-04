---
id: C013
kind: claim
status: holds
created: 2026-08-05
tags: widescreen,rendering
depends: runtime/video/ddraw.c#hostwin_window_geometry, runtime/overrides/geom.h
reconfirmed: 2026-08-11
verified_at: 2026-08-11 13:37:05
---

## Claim

The composition width must follow the window's ASPECT, not its pixel width, and the surfaces that follow it must be allocated once at a fixed maximum pitch

## Evidence

A 1920x1080 window at pixel width would ask the game to compose 1920x550 and present it letterboxed into 1080 rows -- the world at a quarter scale in a strip. By aspect it asks for 550*1920/1080 = 978, which fills the window. The recorded widescreen scenario asserted 794x550->794, 1600x550->1600, 1920x1080->978 and 800x900->794, the last two being the cases a naive implementation gets wrong in each direction. The allocate-once half is forced by vram_alloc being a bump allocator with no free (runtime/video/ddraw.c): a surface reallocated per SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED would take a fresh WIDE_MAX*4*550 = 9 MB slice for every event in a drag. Measured mid-run: scratch/wide20mid/frame_002180.png is 794 wide and frame_002260.png, after a scripted resize to 1400 during a match, is 1400 wide with the tiling layers continued and the HUD re-centred, from surfaces that were never reallocated.

## What would falsify it

a window whose aspect the composition does not follow within a pixel, or a long drag that exhausts the vram arena -- either would mean the width is not derived from the aspect or the surfaces are not allocated once

## Re-confirmed 2026-08-11

RE-CONFIRMED 2026-08-11, and the record needs a correction: this claim read 'holds' throughout the period when the code did NOT do what it says. Issue #20's work replaced the aspect rule with the window's PIXEL WIDTH (1920x1080 -> 1920x550, letterboxed), and this claim -- including the very assertion list in its evidence -- was left standing against it. Nobody noticed because the claim's own falsifier was never run. Issue #41 made the aspect true again, by a different mechanism: the rule is a world SCALE of min(win_h/550, win_w/794) with the composition as win_w/scale, and the composition following the window's aspect is a CONSEQUENCE of that, not the rule itself. The recorded widescreen scenario asserted all four windows and whether the drawn rectangle filled the window: 794x550->794 fill, 1600x550->1600 fill, 1920x1080->978 fill, 800x900->794 band. A 1920x1080 GPU capture had zero fully-black rows at the top and zero black columns at either side, where the pixel-width design put 265 black rows above and below. The allocate-once half was unchanged and not retested that day.
