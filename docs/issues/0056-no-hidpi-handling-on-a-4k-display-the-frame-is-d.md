---
id: 56
title: No HiDPI handling: on a 4K display the frame is drawn at the logical size and upscaled by the compositor
status: open
symptom: reported. On a 4K display the picture looks like a 1080p frame scaled up rather than a frame drawn at the panel's real resolution -- so every gain from issue #41's per-quad scaling and issue #45's outline fonts is thrown away by a final upscale nobody in the port asked for
tags: reported,widescreen,rendering,renderer,hidpi
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11. Filed on receipt, NOT yet reproduced -- this machine's session has been
running headless at LF2_WINDOW_SIZE=1920x1080, which is exactly the configuration that cannot
show this.

WHAT THE REPORT MEANS IN SDL3 TERMS, so the next session starts from the right two functions.
SDL3 distinguishes a window's size in POINTS from its size in PIXELS, and on a 4K panel at
200% scaling those differ by exactly the factor being described:

    SDL_GetWindowSize()           1920x1080   points  -- what a "1080p" frame would be
    SDL_GetWindowSizeInPixels()   3840x2160   pixels  -- the panel

and a window created WITHOUT SDL_WINDOW_HIGH_PIXEL_DENSITY is a low-DPI window: SDL reports
the two as equal, the drawable really is 1920x1080, and the compositor scales it to the panel.
That produces precisely "drawn in 1080p" and it is the first thing to check, before any
arithmetic.

WHY THIS PORT IS EXPOSED TO IT PARTICULARLY. Issue #41's whole design is that the window
supplies TWO numbers -- a world scale from the height and a composition width from the width
-- and runtime/video/render.c draws every quad into a target the size of the RENDER OUTPUT.
render_present already asks SDL_GetCurrentRenderOutputSize for that target, which is in
pixels. So the renderer may well already be doing the right thing while the geometry that
feeds it is computed from points, or the window may simply never have been asked for a
high-density drawable. Those are different bugs with the same symptom and they must be told
apart before either is fixed.

WHAT TO ESTABLISH, in this order:
  1. WHICH SIZE THE PORT COMPOSES FROM. hostwin_window_geometry in runtime/video/ddraw.c is
     where the window becomes a composition; find whether the width and height it uses come
     from points or pixels, and print both next to each other. The existing "widescreen:
     window WxH -> composition ..." line reports one number today and cannot show a
     discrepancy it does not measure.
  2. WHETHER THE WINDOW IS HIGH-DENSITY AT ALL. runtime/win32/win32.c creates it; if
     SDL_WINDOW_HIGH_PIXEL_DENSITY is absent then points == pixels by construction and step 1
     will look correct while the whole frame is still upscaled by the compositor.
  3. WHAT THE POINTER MAPPING DOES. mouse_lparam maps a window point back into the
     composition through geom_window_to_compose. SDL delivers mouse coordinates in POINTS. If
     the composition is placed in PIXELS the two disagree by the display scale, and every hit
     test on a 4K screen is out by a factor -- silently, because a menu that activates the
     wrong entry looks like nothing at all in a screenshot. `ctest geometry` walks that round
     trip today and would not catch this, because it has no notion of a display scale.

WHAT MUST NOT BE DONE: adding a scale factor read from an env var, or multiplying the
composition by a constant that makes one 4K screenshot look right. The display scale is a
property SDL reports; if the port needs it, it asks for it.

THIS ALSO PUTS A CAVEAT ON EVERY 1920x1080 MEASUREMENT IN THIS REPO. The frame dumps behind
issues #41, #44, #45, #48 and #52 were all taken headless at an exact pixel size where points
and pixels coincide. None of them is invalidated -- they measure what they say they measure --
but none of them exercised this path either, and no route test sets a display scale.
