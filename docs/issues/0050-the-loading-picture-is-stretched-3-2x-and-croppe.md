---
id: 50
title: The loading picture is stretched 3.2x and cropped on a wide window
status: resolved
symptom: at 1710x370 the loading screen's bitmap is drawn horizontally stretched to about 3.2x and cropped to its left third, instead of being drawn at its authored 794x550
tags: reported,rendering,widescreen,frontend,loading
created: 2026-08-11
updated: 2026-08-11
---

FOUND 2026-08-11 while capturing every front-end screen for issue #44. It BLOCKS #44's
loading-screen half: there is no point deciding how to frame a picture that is not being drawn
correctly in the first place.

MEASURED, on a 1710x370 window (composition 2542x550, software compositor). The loading screen
is presented on only TWO frames of a run (902-903), which is why nobody had looked at it.

THE MECHANISM, as established: the loading screen's background is the resource bitmap MENU_WAIT,
794x550, drawn WHOLE at (0,0) as a BLIT -- not a colour fill. The offscreen surface it lives in
is one of the surfaces that FOLLOW THE WINDOW (runtime/video/ddraw.c surfaces_follow_window), so at a
wide composition that surface is 2542 columns wide. The game's own GetDC + StretchBlt then
fills all 2542 columns of it, while the draw that puts the picture on screen takes a fixed
794-wide SOURCE rect -- so the source rect samples the left third of a picture that has already
been stretched across the full width.

SO THE TWO HALVES DISAGREE about how wide the picture is: the fill is width-driven and follows
the surface, the sample is a literal 794. That is the same class of mismatch the port has hit
before, where one number followed the viewport and its partner did not.

WHAT TO ESTABLISH BEFORE FIXING: which of the two should move. Either the StretchBlt's
destination should stay the authored 794 (leaving the rest of the surface untouched, so the
picture is drawn at its real size and the framing question from #44 becomes meaningful), or the
source rect should follow the surface (which would sample a stretched picture on purpose, and
is almost certainly wrong). Read the game's own call sites -- the RE for #44 found MENU_WAIT
drawn from three of them -- and let those decide it rather than picking whichever makes the
screenshot look right.

RELATED: #44 (per-screen framing, which needs this fixed first), #20 and #41 (the composition
following the window is what exposes mismatches like this one).

### Resolution (2026-08-11)
The game's picture loader creates a surface at the BITMAP's own size and StretchBlts 1:1 into it; the port's follow-the-window rule was resizing that surface under it, so the fill covered 2542 columns while the draw sampled a fixed 794. A surface whose requested size matches the bitmap just loaded is a picture holder and keeps its size. Verified at 1710x370: the loading picture is drawn at its authored 794x550, centred.
