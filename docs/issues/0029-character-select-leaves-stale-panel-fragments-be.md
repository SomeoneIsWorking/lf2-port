---
id: 29
title: Character select leaves stale panel fragments beside itself after a window resize
status: resolved
symptom: after resizing the window while character selection is up, blue vertical lines and a partial second copy of the panel are left standing to the LEFT of the centred panel, over the black surround; the panel itself draws correctly
tags: reported,widescreen,rendering,resize,charselect
created: 2026-08-06
updated: 2026-08-06
---

REPORTED 2026-08-06 with a screenshot: a ~1900x800 window at character selection, the panel
correctly centred, and to its left four stray vertical blue rules plus a clipped fragment of
an older, narrower panel showing 'P...' rows -- i.e. the panel as it was drawn at the PREVIOUS
centring offset, never erased.

NOT INVESTIGATED YET. What is worth writing down before anyone starts:

THE LIKELY SHAPE, to be confirmed, not assumed. The front-end screens are drawn at a fixed 794
and CENTRED by offsetting the backbuffer->primary copy (menu.c / ddraw.c, issue #20). Nothing
in that path clears the surround, because at a steady size the surround is written once and
never changes. A resize MOVES the offset, so whatever the previous offset painted stays where
it was. That predicts exactly what the screenshot shows -- fragments to one side, at the old
offset, only after a resize -- and it predicts they would survive until something else
overpaints them.

WHAT TO MEASURE FIRST, so the fix is not aimed at a guess: whether the stale pixels are on the
COMPOSE surface or the PRIMARY. Dump two consecutive frames across a resize (LF2_FRAME_DUMP)
and check whether the fragment is present in the composition or appears only after the centred
copy. Those are different bugs with the same look.

DO NOT fix this by clearing the whole primary every frame. That is a per-frame full-surface
write to hide a one-frame staleness, and it would hide any future version of this bug too. The
clear belongs where the offset CHANGES.

Related: #20 (widescreen follows the window), #23 (the layer band), #28 (the camera clamp).

### Resolution (2026-08-06)
The centring offset was applied to the FINAL full-width copy of the composition to the primary, not to the 794-wide content inside it. Measured with LF2_BLT_FRAME: 'blt 13 dst=(256,0)-(1562,550) src=[1306x550]' into a 1306-wide primary -- the copy hangs 256 px off the right and never writes the leftmost 256 px at all. The comment above screen_offset_x() claimed the band either side was covered by 'the game's own full-screen clear'; that clear is real but goes to the COMPOSE surface, and the shift is precisely what moves it off the primary's left band. At a steady size the band is black because the primary started black, which is why this only ever showed after a resize -- the band then held pixels drawn at the previous size and offset, i.e. a ghost of the old panel. Fixed by primary_clear_on_move() in runtime/video/ddraw.c, which clears the primary when the offset or the surface size changes -- the moment the previously-written region stops matching the one about to be written. NOT a per-frame clear, which the entry warned against and which would have hidden any future version of this. The recorded resize scenario shrank and re-grew the window while character selection was up, asserted that the band was black and the frame had 184900 lit pixels, and proved its negative by leaving 65145 stray pixels when the clear was disabled.
