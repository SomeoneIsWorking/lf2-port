---
id: 54
title: Stage mode's own UI row (Man / HP / STAGE n-n) is anchored to the 794 screen, not centred with the picture
status: resolved
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
    stage-mode status row belongs to that band -- and it is drawn at y 110, directly under the
    panels, and is centred with them at 794 -- then 118 is simply measured short and the band
    is taller than whoever measured it had a stage-mode screen to see. That is the likely
    answer and it is ONE number.
  - If instead the row is not HUD, then it needs its own placement, and the reason it looks
    wrong is that nothing places screen furniture during a match.

WHAT MUST BE ESTABLISHED BEFORE CHANGING 118: where the row actually ends, and what else falls
between 118 and that. HUD_BAND_H gates every GDI draw in a match, so raising it moves anything
else in the widened band too -- the fighters' name tags are drawn in the field and are the
obvious thing to check (issue #55, which is separately not what it looked like either).
LF2_TEXT_DEBUG now prints the destination and its size on every string, which is enough to list
every draw in a match and where it lands.

AND A CORRECTION TO MY OWN PREVIOUS NOTE: it said "the screen_offset_x branch is skipped because
its guard is `dwid > 794` and this destination is not". That was inference and it is WRONG --
the destination is 978 and the branch is taken. The guard that declines is `panel_hud_up()`
inside the function. Reading the function beat guessing at it, as it did twice earlier today.

### Resolution (2026-08-11)
FIXED. The GDI text path gets its own band, taller than the blit path's, and the two cannot be
one number.

    enum { HUD_BAND_H = 118 };        /* blits: the world's layers start at y 128 */
    enum { HUD_TEXT_BAND_H = 131 };   /* text: the game's status line ends at 131 */

    hud_offset_x():  if (dst_w <= NATIVE_W || bottom > HUD_TEXT_BAND_H) return 0;

WHY THEY DIFFER, which is the whole of it. HUD_BAND_H stops at 118 because the WORLD's layer
blits start at y 128 -- measured, and recorded in the comment beside it -- so a blit band any
taller would take the stage's own layers with the HUD and shift the world sideways. That
constraint is about BLITS. The game's status line does not respect it: in stage mode it draws
"Man: n  HP: n" and "STAGE 1-1" at y 110 and "Difficult" at y 115, bottoms 126 and 131 --
below the blit band and above the world. Raising the shared constant would have fixed this row
and broken the stage.

THE STRIP IS EMPTY OF ANYTHING ELSE, measured rather than assumed. LF2_TEXT_DEBUG over a
stage-mode run at 1920x1080 lists 104 distinct (row, text) draws, and between y 115 and y 219
there is NOTHING. y 219 is character selection, where panel_hud_up() is false and hud_offset_x
declines regardless. So 131 captures the status line exactly and reaches nothing else.

VERIFIED at both widths, the game's own as the control:
    794x550    STAGE 1-1 at x 360, Man at 10, Man at 645   -- the game's own layout, untouched
    1920x1080  STAGE 1-1 at x 453, Man at 103, Man at 738  -- each +93
93 is (978 - 792) / 2, which is exactly the offset the HUD panels take. The row and the panels
now come from one number, which is what the screenshot showed them not doing. Confirmed on a
frame: the left readout starts at the panel strip's left edge, STAGE 1-1 is centred under the
panels, and the right readout ends at its right edge.

`ctest` 9/9. At 794 the guard `dst_w <= NATIVE_W` returns 0 before anything else is consulted,
so the 4:3 game cannot be reached by this at all.

AND A CORRECTION TO TWO EARLIER NOTES IN THIS ENTRY, both of which were inference presented as
finding: the destination is NOT a 794-wide HUD surface (it is the composition, 978x550) and the
`dwid > 794` branch is NOT what declines (it is taken). What declined was `panel_hud_up()`
inside screen_offset_x -- by design, because during a match the world is placed by the camera
shift instead -- and the band test in hud_offset_x. Reading both functions is what settled it;
each earlier guess was wrong.
