---
id: 51
title: modemenu_mouse gates on the GAME MODE word, not a screen, so it is live during a match
status: resolved
symptom: runtime/overrides/screens.c treats 0x00451160 as the mode menu's selection and gates its mouse handler on LD32(0x00451160) < 8; that word is the game MODE and reads 1/4/5 during a match, so the gate is true in a match as well
tags: input,mouse,menu,latent,re
created: 2026-08-11
updated: 2026-08-12
---

FOUND 2026-08-11 by the RE done for issue #44, not by a report -- so it is a latent defect and
its user-visible consequence has NOT been measured yet. Filed before it is lost.

WHAT THE PORT BELIEVES: runtime/overrides/screens.c calls 0x00451160 MODEMENU_SEL and treats
`LD32(MODEMENU_SEL) < MODEMENU_ITEMS` as "we are on the mode menu, and this is the highlighted
row". modemenu_mouse() returns early unless that holds.

WHAT THE BINARY SAYS: 0x00451160 is the GAME MODE, not a screen and not a menu cursor.
FUN_00429730 and the score-board code read it as 1/4/5 DURING A MATCH. So the gate is satisfied
in a match as well as on the mode menu, and the handler's early-out is not doing the job its
name claims. This is the same trap the port already documented for the pre-fight overlay --
"the first attempt used a .data flag, and it was the game mode wearing a convincing disguise"
-- caught there and not here.

THE HONEST IDENTIFIER, from the same RE: the mode menu is FUN_00431d10, reached from
FUN_00429730 when the screen-state word DAT_0044d020 == 10. And each front-end screen can also
be recognised by its own full-screen COLORFILL colour, which is what the port already prefers
(identify a screen by what the game DRAWS): front end 0x10206c, mode menu 0x122565, character
select 0x000000. The two menu colours each appear EXACTLY ONCE in the whole binary, which is
what makes them identifications rather than coincidences.

WHAT IS NOT ESTABLISHED: whether this actually misbehaves in play. The handler also requires a
pointer position inside a menu row band and a click, so a match may simply never satisfy the
rest of it -- or a click during a match may be silently changing the mode menu's selection.
MEASURE IT before deciding how much it matters: LF2_OVERLAY_DEBUG has the shape to copy.

Do not fix this by adding a second condition that happens to exclude matches. Use the screen
signal the RE found.

### Resolution (2026-08-12)
FIXED, and the misfire is MEASURED rather than assumed -- the entry asked for that explicitly
('MEASURE IT before deciding how much it matters').

    modemenu: the old game-mode gate was true on 1352 of 1800 frame(s) where the mode menu was
              NOT drawn -- the handler was live off its own screen

Three quarters of a run that walks the front end, the mode menu, character selection, the
pre-fight overlay and a match. The gate  is the GAME MODE, which reads
1/4/5 in a match, so it was satisfied nearly everywhere.

THE FIX is the identifier the entry itself named, and the one this port already prefers: ask what
the game DRAWS. The mode menu paints its whole screen 0x122565, a literal that appears exactly
once in the binary, and runtime/video/ddraw.c already watches for it (per-screen framing, issue
#44). panel_modemenu_up() joins panel_frontend_up() -- added this session for the route anchors --
and screens.c gates on that instead of the mode word.

VERIFIED BOTH WAYS, which matters because a gate can fail by being too tight as easily as too
loose: tools/e2e.sh mouse still reaches charselect, the overlay and a match by pointer alone
(7 of 7 clicks fired), and that route drives the mode menu WITH THE MOUSE -- so a gate that had
become too tight would have stalled it at the front end. The counter above is the other side.

LF2_MODEMENU_DEBUG=1 keeps the discriminator available: it prints how many frames the old gate
would have been true on while the menu was not drawn, with its denominator, and says outright
when a run shows no misfire that it says nothing about runs reaching other screens.
