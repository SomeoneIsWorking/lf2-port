---
id: 54
title: Stage mode's own UI row (Man / HP / STAGE n-n) is anchored to the 794 screen, not centred with the picture
status: closed
symptom: reported with a screenshot. In stage mode at a wide window the row of stage-mode text -- 'Man: 1  HP: 199' on the left, 'STAGE 1-1' in the middle, the second player's 'Man / HP' to its right -- is laid out for a 794-wide screen and sits against the left of the picture, so its centre item is well left of the window's centre while the player panels above it are centred
tags: reported,widescreen,stage-mode,hud
created: 2026-08-11
updated: 2026-09-09
---

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

### Note (2026-08-11)
BOTH GUARDS NOW READ FROM SOURCE, and the "which surface" question in the note above is
answered: it is the COMPOSITION, 978x550, measured. So the surface was never the issue and my
previous note's two-way fork was the wrong fork.
    text (360,110) dst 50898000 978x550  "STAGE 1-1"
    text ( 10,110) dst 50898000 978x550  "Man:   1      HP:    7"
    text (645,110) dst 50898000 978x550  "Man:   1      HP:   50"
dwid is 978, so h_TextOutA's `if (dwid > 794)` branch IS taken. The offset is still zero,
because BOTH functions decline for reasons that have nothing to do with the surface width --
runtime/video/ddraw.c:
    int hud_offset_x(int dst_w, int bottom)
    {
        if (!lf2_wide_width() || !panel_hud_up()) return 0;
        if (dst_w <= NATIVE_W || bottom > HUD_BAND_H) return 0;   <- 126 > 118, declines
        return (dst_w - HUD_W) / 2;
    }
    int screen_offset_x(void)
    {
        const int wide = lf2_wide_width();
        if (!wide || panel_hud_up()) return 0;                    <- in a match, declines
        ...
    }
So during a match screen_offset_x is zero BY DESIGN -- the world is placed by the camera shift
instead, and the HUD band is placed by hud_offset_x. That is a coherent split and this row falls
through the crack in it: at y 110 its bottom is 126, and HUD_BAND_H is 118.
THE FIX IS ONE OF THESE TWO CONSTANTS, and choosing between them is the remaining work:
  - HUD_BAND_H = 118 is described as "the in-match HUD strip and the band it owns". If the

(full record: git history)
