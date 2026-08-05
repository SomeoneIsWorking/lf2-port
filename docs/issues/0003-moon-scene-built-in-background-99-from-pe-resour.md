---
id: 3
title: Moon scene = built-in background 99 from PE resources; moon drawn without color key
status: resolved
symptom: match on a flat green/blue stage with street lamps and a moon in an opaque black box; looks like a broken stage
tags: rendering,background,bg99,colorkey,resources
created: 2026-07-30
updated: 2026-08-05
---

The scene is NOT a broken bg/sys stage: it is the game's built-in background (id 99) assembled from PE resource bitmaps -- BACK99_1 (rail bar), BACK99_2 (the moon), BACK99_3 (the lamp), flag from BARS-adjacent art -- over flat programmatic fills. The flat bands are how it is designed to look. Verified by dumping all 64 RT_BITMAP resources (scratch/frames/rsrc/).

**The one real defect:** the moon (BACK99_2) draws as an opaque black box -- its black background should be transparent. h_StretchBlt ignores its rop argument (runtime/gdi.c), so if the game uses a SRCAND/SRCPAINT pair for this draw the port copies opaquely; alternatively the surface misses its SetColorKey. Needs a live bg-99 repro to trace which; background selection is random, so build a deterministic repro first (find the bg-index variable, or add a diagnostic override).

Selecting Background at the pre-fight overlay by script is timing-fragile (docs/running.md warns); a first .data diff across a cycle press did not isolate the index.

### Resolution (2026-08-05)
RESOLVED, and the cause was issue #9, not a missing colour key.

The scene is confirmed live, with a deterministic repro. Background 99 is the game's
built-in "Lee On Road" (the name string is at 0x448900, the constructor is FUN_004122f0),
assembled from PE resources loaded by the LOWERCASE names back99_1/2/3 -- an uppercase
search of the binary finds nothing, which nearly sank this a second time. LF2_RSRC_DEBUG on
a run confirms all three are loaded.

REPRO: reach the VS pre-fight overlay, click the Background row, one Right press takes
"Random" -> "Lee On Road", then click Fight!. Walking right a few hundred frames brings the
moon into view.

  LF2_CLICK_SCRIPT="403,228:900;150,100:2050;150,28:2200"
  LF2_KEY_SCRIPT="<the smoke route>,0x27:2100,0x27:2500,...,0x27:2860"
  LF2_FRAME_DUMP=2900

WHAT IT LOOKS LIKE NOW: correct. Night road, round moon with no box, street lamp, railing,
flat road -- the flat programmatic bands are the design, as this issue already said.

WHY IT WAS BROKEN: the moon IS drawn unkeyed, and that is not a defect. LF2_BLT_FRAME shows
blt 2, an 88x88 source at (247,120), flags=01000000 -- while the street lamps beside it
(14x154) are keyed at 01008000. The game does not key the moon because it does not need to:
the frame opens with a full-screen DDBLT_COLORFILL to BLACK and the moon's own background is
black.

That fill is what was broken. DDBLTFX.dwFillColor was read from offset 16 instead of 80
(issue #9), so the clear painted a leftover stack dword -- measured at 0x00023400, a dark
green -- instead of black. A black-backed moon on a dark green sky is an opaque black box,
and the flat green/blue bands in the original report are the same garbage fill. One bug, two
symptoms, and the second one sent this issue after a colour key that was never wrong.

The h_StretchBlt rop hypothesis in the original note is therefore also unnecessary: the moon
does not come through StretchBlt at all, it is a DirectDraw Blt.
