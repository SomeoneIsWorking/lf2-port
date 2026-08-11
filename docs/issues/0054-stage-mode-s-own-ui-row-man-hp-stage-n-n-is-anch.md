---
id: 54
title: Stage mode's own UI row (Man / HP / STAGE n-n) is anchored to the 794 screen, not centred with the picture
status: open
symptom: reported with a screenshot. In stage mode at a wide window the row of stage-mode text -- 'Man: 1  HP: 199' on the left, 'STAGE 1-1' in the middle, the second player's 'Man / HP' to its right -- is laid out for a 794-wide screen and sits against the left of the picture, so its centre item is well left of the window's centre while the player panels above it are centred
tags: reported,widescreen,stage-mode,hud
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11 with a screenshot, filed on receipt. Reporter's words: "stage UI isn't
centered, it is left aligned".

WHAT THE SCREENSHOT SHOWS, measured off it rather than described: the blue player-panel strip
above the row IS centred in the window. The text row below it is not -- 'STAGE 1-1' sits at
roughly 38% of the picture's width instead of 50%, and the leftmost 'Man:' starts hard against
the picture's left edge. The two disagree, which is the tell: the panels go through a path the
port has already taught about the wide view and this row does not.

WHERE TO START, and what NOT to assume. The port already centres the in-match HUD band; the
question is whether this row is part of that band or a separate draw. runtime/overrides/hud.c
is the panel pass. The stage-mode row is text, so it is either GDI (h_TextOutA in
runtime/win32/gdi.c, which already applies a widescreen offset for text inside the HUD band --
see the comment there about text moving with the panels it sits on) or the game's own 8x16
bitmap sheet (runtime/overrides/text.c). Which of the two it is decides everything about the
fix, and it is one LF2_DRAW_PATHS or one glyph-debug run to find out. Do not guess from the
look: both routes end up as pixels in the same place.

DO NOT fix this by adding a constant to the row's x. The offset that belongs here is
screen_offset_x() -- the same composition-space offset every other centred screen element
takes -- and if the row needs something else then the reason has to be found first.

RELATED: issue #23 (a stage's sky layer stops at 794 and leaves black to its right), which is
visible in the SAME screenshot and is a different defect with a different cause -- do not
conflate them. Issue #55 (the player tag lags the fighter) is also in that screenshot.
