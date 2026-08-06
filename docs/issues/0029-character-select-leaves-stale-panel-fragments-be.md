---
id: 29
title: Character select leaves stale panel fragments beside itself after a window resize
status: open
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
