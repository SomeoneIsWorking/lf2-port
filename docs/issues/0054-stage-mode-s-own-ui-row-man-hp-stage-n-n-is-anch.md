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

### Note (2026-08-11)
CAUSE FOUND, measured. The row goes through GDI TextOutA, and it falls BETWEEN the two rules
that would have moved it -- each declines it for a different reason.

MEASURED, LF2_TEXT_DEBUG on a stage-mode run at 1920x1080 (view 978, so screen_offset_x() is
(978-794)/2 = 92). The debug prints x AFTER both offsets have been applied:

    text (360,110) 9  "STAGE 1-1"
    text ( 10,110) 22 "Man:   1      HP:    7"
    text (645,110) 22 "Man:   1      HP:   50"

Ten, not 102. Neither offset was added.

WHY, and both halves are in runtime/win32/gdi.c's h_TextOutA:

    x += hud_offset_x(dwid, y + 16);        -- returns 0: y+16 = 126, and this row at y 110
                                               sits just BELOW the in-match HUD band, so the
                                               band test declines it
    if (dwid > 794) x += screen_offset_x(); -- skipped: the destination surface is not wider
                                               than 794, so the screen-centring branch is not
                                               taken either

The panels above it are drawn by runtime/overrides/hud.c and DO take hud_offset_x, which is
exactly the disagreement in the screenshot: the panel strip is centred and the row under it is
not. They are not two different bugs, they are one rule with a gap in it.

WHAT IS NOT YET ESTABLISHED, and it decides the fix rather than being a detail:
  - WHICH SURFACE this text lands in, and how wide it is. `dwid <= 794` is inferred from the
    branch not being taken, not read. If it is an off-screen 794-wide HUD surface then the row
    should move with the panels (the hud_offset_x band test is what needs widening); if it is
    the composition then the screen_offset_x branch is the one at fault and its `dwid > 794`
    guard is the wrong question to ask about a 794-wide screen inside a wider composition.
  - WHETHER THE ROW BELONGS TO THE HUD. It reads like it does -- it is the stage's own status
    line and sits directly under the panels -- but "looks like it belongs" is not a reason to
    give it the HUD's offset. The game draws it at y 110 with the panels above; find what the
    game considers that band to be.

DO NOT widen hud_offset_x's band until the surface is known. Moving this row by the HUD's
offset when it is actually screen content would line it up at one window size and not another,
which is the same class of mistake as adding a constant.
