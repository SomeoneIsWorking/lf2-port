---
id: 20
title: Widescreen is set by LF2_WIDESCREEN instead of following the window size
status: open
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
    FIXED there, and a resize then only changes s->w. vram_alloc (runtime/ddraw.c) is a bump
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
