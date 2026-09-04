---
id: 20
title: Widescreen is set by LF2_WIDESCREEN instead of following the window size
status: resolved
symptom: the field of view does not change when the window is resized; the width has to be given as an env var before launch
tags: reported,widescreen,rendering,config
created: 2026-08-05
updated: 2026-08-05
---

REPORTED: the game's wideness should depend on the window size. An env var read once at
startup is a developer's escape hatch, not the feature.

DESIGN, settled by reading the code rather than guessed:

  - Compose width = NATIVE_H * window_aspect (550 * w/h), NOT the window's pixel width.
    Using the width directly letterboxes hugely and shrinks the pixels: a 1920x1080 window
    wants 978x550 of world scaled up, not 1920x550 in a 1080-tall window.
  - The compose surface must be ALLOCATED ONCE at the display's maximum width with its PITCH
    FIXED there, and a resize then only changes s->w. vram_alloc (runtime/video/ddraw.c) is a bump
    allocator with NO FREE, so reallocating per resize event would exhaust the arena during
    a single drag of the window edge.
  - surf_Lock reports w/h/pitch fresh on every call, so the game picks a changed width up on
    its next frame; a pitch that never changes keeps anything the game cached valid.
  - wide_apply() already rewrites the game's three viewport width words EVERY FRAME
    (0x0044d014, 0x0044d78c, 0x00453cd4), so that half needs no new machinery.
  - The PRIMARY surface is hw.width x hw.height and there is currently no resize handler at
    all: SDL_SetRenderLogicalPresentation is set once at startup, so today a resize only
    letterboxes the same 794x550 logical picture. hw.width has to start tracking the window,
    which is what GetClientRect and screen_offset_x() both read.

LF2_SCREEN is the same smell and should go the same way once the window is the source of
truth.

### Resolution (2026-08-05)
The window is the source of truth now, and LF2_WIDESCREEN is deleted.

The design in this entry was followed as written, and each part earned its place:

  - Composition width = NATIVE_H * window_aspect (550 * w/h), NOT the window's pixel width.
    A 1920x1080 window gets 978x550 of world scaled up to fill it; using the width would put
    a 1920-wide picture in a 1080-tall window with the pixels shrunk to a quarter.
  - A window NARROWER in aspect than 794x550 clamps to 794 and letterboxes, because the HUD
    strip is 792 wide and there is nothing sensible below that. That case is in the test for
    exactly the reason the widening cases are: without it a build that always widened passes.
  - The following surfaces are allocated ONCE at WIDE_MAX with their PITCH FIXED there, and a
    resize moves only s->w. vram_alloc has no free, so reallocating per resize event would
    exhaust the arena during one drag of a window edge. WIDE_MAX is 4096 -- the bound the port
    already validated widths against when this was an env var -- and costs 9 MB of a 1 GiB
    arena per surface.
  - surf_Lock already reported w/h/pitch fresh on every call, so the game picks a changed
    width up on its next frame and a constant pitch keeps anything it cached valid.
  - wide_apply() now writes on CHANGE IN BOTH DIRECTIONS rather than only when a word still
    reads 794: a window dragged from wide back to narrow would otherwise leave the game
    believing in the widest viewport it ever had. It still refuses to touch a word holding
    anything other than 794 or the width it last wrote, which is what stops a word that turns
    out not to be a viewport width from being stamped every frame.
  - SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED (not RESIZED -- they differ on a scaled display) is
    wired to the same entry point, and the presentation and the streaming texture follow it.
  - dd_SetDisplayMode no longer resizes the window. A request for 794x550 that snapped a
    user's resized window back would undo the resize on the next mode set; it now says once
    what it was asked for and what the window gives instead.

LF2_SCREEN, which this entry called the same smell, never existed -- the declaration in
hostwin.h named a variable no code read. It is gone with the rest.

WHAT REPLACED THE KNOB, and why it is not the knob renamed: LF2_WINDOW_SIZE=<w>x<h> sets the
WINDOW's initial size, and the composition is derived from it by exactly the code a window
manager drives. A headless run has nobody to drag an edge.

VERIFIED. The recorded widescreen scenario asserted the whole table -- 794x550 -> 794 (off), 1600x550 -> 1600,
1920x1080 -> 978, 800x900 -> 794 (off) -- plus the MID-RUN case, which is the actual headline
and which no scripted run could otherwise reach: offscreen SDL has no window manager and never
delivers a resize event, so LF2_WINDOW_RESIZE=<frame>:<w>x<h> stands in for one, driving the
same entry point the real event does. It is asserted three ways (the step fired, the
composition changed, and it was 794 before) and it reports at exit when a step's frame was
never reached -- checked against a step at frame 9999 in a 150-frame run, which printed NEVER
FIRED rather than passing silently. The picture was looked at, not just the log:
scratch/wide20mid/frame_002180.png is 794 wide and frame_002260.png, after a resize to 1400
during the match, is 1400 wide with the tiling layers continued and the HUD re-centred.
Full suite 14/14.

What this did NOT fix, and is now issue #23: a stage's sky layer is a single fixed-width
picture that neither tiles nor stretches, so it still ends at 794 with black beyond. It is
pre-existing -- the same band appears on the commit before this one -- and the wrong fix
(stretching it) is named there.
