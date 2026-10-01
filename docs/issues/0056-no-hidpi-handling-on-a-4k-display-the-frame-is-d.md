---
id: 56
title: No HiDPI handling: on a 4K display the frame is drawn at the logical size and upscaled by the compositor
status: closed
symptom: reported. On a 4K display the picture looks like a 1080p frame scaled up rather than a frame drawn at the panel's real resolution -- so every gain from issue #41's per-quad scaling and issue #45's outline fonts is thrown away by a final upscale nobody in the port asked for
tags: reported,widescreen,rendering,renderer,hidpi
created: 2026-08-11
updated: 2026-09-09
---

### Note (2026-08-12)
THE INSTRUMENT IS VERIFIED, THE FIX IS NOT -- and the line itself says so, which is the point.
Run headless at 1920x1080:
    window: 1920x1080 points -> 1920x1080 pixels (display scale 1.00) -- unscaled, so this run
            says nothing about HiDPI
    widescreen: window 1920x1080 -> composition 978x550 at scale 1.964, drawn into 1920x1080
So the reporting path works and prints all three numbers (points, pixels, density). At density
1.00 it refuses to claim anything, which is what it must do: an offscreen run cannot distinguish
'HiDPI is handled' from 'there is no HiDPI here', and a line that printed the same text either way
would be the fifth vacuous control this session.
WHAT IS STILL NEEDED is one run on the reporter's 4K panel, windowed, and the single line above
read back. The code that would make it right is already in: runtime/win32/win32.c creates the
window with SDL_WINDOW_HIGH_PIXEL_DENSITY, seeds the geometry from SDL_GetWindowSizeInPixels
rather than the point size, and multiplies the pointer by SDL_GetWindowPixelDensity() before
lf2_window_to_compose so hit tests stay in composition space.
WHAT THE ANSWER LOOKS LIKE: on a 4K panel at 200% the line should read points 1920x1080 ->
pixels 3840x2160, scale 2.00, and the composition that follows should be computed from 3840. If
it reads 1920x1080 pixels there, the flag is not taking effect and this is still open. If it
reads 3840x2160, the frame is being drawn at the panel's real resolution and the entry closes.

### Reopened (2026-08-21)
USER 2026-08-21: screen DPI causes RmlUi mouse position to be misread; RmlUi pointer input must be DPI-compatible. The old proof covered the game pointer path, not necessarily the RmlUi backend's direct SDL event coordinates.
