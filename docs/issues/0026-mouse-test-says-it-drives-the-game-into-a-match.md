---
id: 26
title: mouse_test says it drives the game into a match and never reaches one
status: closed
symptom: the recorded mouse runtime scenario passes every assertion while the run stops at the post-load panel: no overlay, no match, and the 'sound effects (a match started)' check is satisfied by menu sounds
tags: reported,testing,verification,mouse
created: 2026-08-06
updated: 2026-09-09
---

### Note (2026-08-06)
PARTLY FIXED, 2026-08-06: the test no longer claims what it does not do, and its assertion now
discriminates. What is left open is a mouse route that actually reaches a match.
WHAT CHANGED. The "sound effects (a match started) >= 2" check is gone, replaced by an
assertion on the screens the run REACHED plus a check that no scripted click was left unfired.
The route is screen-keyed from character selection onward, and it now answers the game's "How
many Computer Players?" dialog, which is where it had been stopping.
VALIDATED AGAINST BOTH CLASSES rather than reasoned about:
  route intact             -> charselect@1352, 5 of 5 clicks fired, PASSED
  the click at 1350 broken -> "screens reached -- NONE", three clicks named as NEVER FIRED,
                              FAILED
AND THE OLD THRESHOLD IS WORSE THAN "WEAK": in that failing run keyed blits measured 13489,
against 6744 in the passing one. The count this test leaned on reads HIGHER when the route
breaks, so no threshold on it could have discriminated in either direction.
A GAP THE NEGATIVE CONTROL FOUND, recorded rather than papered over: breaking the FIRST click
-- the launcher's "game start" -- does NOT fail the test. The run still reaches character
selection, because the click at 1350 then lands on the launcher instead and everything after
it is keyed to the screen rather than the clock. So this test does not prove the launcher
click does anything. My first attempt at a negative control was exactly that broken click, and
it passed, which is the only reason the gap is known.
STILL OPEN -- reaching a match by mouse. After the CPU-count dialog the game walks each
computer player through its own selection, and each needs its own confirm; the keyboard route
does this with a run of attack presses. Frame dumps of the sequence are in scratch/m26*/ and
show exactly where it stops: character selection with the computers still picking. The clicks
to drive that are not yet worked out, and the pre-fight overlay is never reached, so
`overlay@`/`match@` cannot be asserted yet.
