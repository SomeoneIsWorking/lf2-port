---
id: 56
title: No HiDPI handling: on a 4K display the frame is drawn at the logical size and upscaled by the compositor
status: resolved
symptom: reported. On a 4K display the picture looks like a 1080p frame scaled up rather than a frame drawn at the panel's real resolution -- so every gain from issue #41's per-quad scaling and issue #45's outline fonts is thrown away by a final upscale nobody in the port asked for
tags: reported,widescreen,rendering,renderer,hidpi
created: 2026-08-11
updated: 2026-08-12
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

### Note (2026-08-12)
### RESOLVED (2026-08-12) -- the 4K panel was SIMULATED, and the fix is verified on it

The previous note said "what is still needed is one run on the reporter's 4K panel". That was
wrong about what is possible here. A nested compositor IS a display:

    kwin_wayland --virtual --width 3840 --height 2160 -s <socket> -- <command>
    kscreen-doctor output.1.scale.2          # inside it

gives a headless Wayland session on a 4K virtual output at 200%, and SDL then reports exactly
what a real panel reports. The port, run inside it at a 794x550 window:

    window: 794x550 points -> 1588x1100 pixels (display scale 2.00) -- a SCALED display: the
            frame is composed at the pixel size
    widescreen: window 1588x1100 -> composition 794x550 at scale 2.000, drawn into 1588x1100
                at (0,0) -- fills the window

That is the answer this entry said to look for, line for line: SDL_WINDOW_HIGH_PIXEL_DENSITY
takes effect, the geometry is seeded from the PIXEL size, and the composition is the game's own
794 columns of world at a world scale of 2 -- drawn at four times the pixels rather than blown
up to them.

TWO SIMULATIONS THAT DO NOT WORK, recorded so they are not retried:
  - Xvfb at -dpi 192 with Xft.dpi merged. SDL3's X11 backend reports points == pixels BY
    CONSTRUCTION; density is 1.00 whatever the server's DPI says. Measured.
  - kwin_wayland's own `--scale 2`. Its help says "the scale for WINDOWED mode" and means it:
    with `--virtual` the output comes up at Scale: 1 and clients see density 1.00. The scale
    has to be set on the running output afterwards. Measured, twice, before reading the help.

TWO TESTS, and the split is the usual one:
  - `ctest geometry`'s test_density walks FIVE densities (1.0, 1.25, 1.5, 1.75, 2.0) over four
    point sizes offline, asserting the invariant that actually matters: a scaled display
    changes the RESOLUTION and nothing else -- same field of view, world scale multiplied by
    the density, and the same window POINT landing on the same composition pixel. Stated ACROSS
    densities on purpose: a port that composed from the point size while drawing into the pixel
    size has a perfectly self-consistent round trip at every single density, so a within-density
    test cannot see the reported bug. Negative control run: with the density multiply removed
    from geom_pointer_to_compose, 161 checks fail.
  - `tools/e2e.sh hidpi` is the nested-compositor run above, and it REFUSES rather than passing
    when the output comes up unscaled -- it reads the port's own "unscaled, so this run says
    nothing about HiDPI" line and fails on it, because every assertion after that would be
    about an ordinary run.

The pointer's density multiply moved out of runtime/win32/win32.c into geom.h as
geom_pointer_to_compose, which is why it is testable at all; win32.c calls it through
lf2_pointer_to_compose.

WHAT IS STILL NOT COVERED, stated so the pass is not overread: a real panel, a real compositor,
and any density the four sizes above do not reach. The nested output is kwin's, which is the
same compositor the reporter runs, so this is closer to the real thing than it sounds -- but it
is still a simulation and the entry should be reopened if the reporter sees the symptom.

### Resolution (2026-08-12)
The fix (SDL_WINDOW_HIGH_PIXEL_DENSITY, geometry seeded from SDL_GetWindowSizeInPixels, the pointer multiplied by the density) was already in but unverified. It is now verified on a SIMULATED 4K display -- kwin_wayland --virtual at 3840x2160 with kscreen-doctor setting the output to scale 2 -- where the port reports 794x550 points -> 1588x1100 pixels at density 2.00 and composes 794 columns of world at scale 2.000 into the full drawable. tools/e2e.sh hidpi is that run; ctest geometry's test_density walks five densities offline and fails 161 checks with the density multiply removed.
