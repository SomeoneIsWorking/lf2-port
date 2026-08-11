---
id: 60
title: "Stage mode (Difficult)" is drawn at a fixed x and takes no widescreen offset
status: open
symptom: measured. The stage-mode caption at the bottom of the screen is drawn at x 613 identically at 794 and at 1920x1080, so on a wider view it sits where 613 falls in a 978-wide composition instead of where it falls in the game's 794 -- the same left-anchoring issue #54 fixed for the status row, on a different draw path
tags: widescreen,stage-mode,hud
created: 2026-08-11
updated: 2026-08-11
---

FOUND 2026-08-11 while eliminating draw paths for issue #55, and visible in the reporter's
stage-mode screenshot at the bottom of the picture.

MEASURED with LF2_GAMETEXT_DEBUG, the same route at two widths:

    794x550     gametext x=613 y=531 ... "Stage mode (Difficult)"
    1920x1080   gametext x=613 y=531 ... "Stage mode (Difficult)"

Identical. The string is laid out against the game's own 794 -- 613 + 22 glyphs is 789, so it
is right-anchored to that screen -- and nothing moves it when the composition is wider.

WHY IT GETS NOTHING, and it is the same gap issue #54 turned up rather than a new one: this is
the game's own sheet text, so it reaches the screen through fn_0043f010 and the Blt path, and
during a match screen_offset_x() returns 0 by design (panel_hud_up) because the world is placed
by the camera shift instead. The caption is not world and not HUD band, so nothing claims it.

WHAT MAKES IT DIFFERENT FROM #54, and why it is filed separately rather than reopened there:
#54's row is GDI TextOutA and was fixed by giving that path its own band (HUD_TEXT_BAND_H).
This one is on the OTHER path -- the game's bitmap sheet through Blt -- so the same fix does
not reach it, and its y is 531, at the very bottom of the screen, nowhere near a HUD band.

WHAT IS NOT ESTABLISHED: where this caption belongs when the view is wider. It is
right-anchored to 794 in the game's own layout, so "add screen_offset_x()" would put it 92 px
right of where it is, which may or may not be what a 978-wide screen wants -- the game's intent
was "near the right of the screen", and the honest reading of that on a wider screen is
arguable. Look at it before choosing, and note that the port's own controls hint already sits
along that same bottom edge (runtime/video/ddraw.c, controls_hint_draw) and is placed
deliberately -- whatever is done here should not collide with it.
