---
id: 26
title: mouse_test says it drives the game into a match and never reaches one
status: open
symptom: tools/mouse_test.sh passes every assertion while the run stops at the post-load panel: no overlay, no match, and the 'sound effects (a match started)' check is satisfied by menu sounds
tags: reported,testing,verification,mouse
created: 2026-08-06
updated: 2026-08-06
---

OBSERVED while measuring route anchors for issue #25, using the new per-stream report.
mouse_test's OWN environment, run verbatim:

  scripted input: screens reached -- charselect@1352
  LF2_CLICK_SCRIPT: 5 of 5 items fired
  audio: buffers=116 plays=4 ... peak=32768/32767
  colour-key: SetColorKey=391 keyed blits=6744 unkeyed blits=9739

Only ONE screen is ever reached. The pre-fight overlay never opens and no match ever starts,
so the clicks commented "character select: click a portrait to join" and "overlay: Fight!"
are landing on something else entirely. Every assertion still passes:

  screen transitions >= 3     passes on the launcher -> panel transitions alone
  sound effects >= 2          plays=4, all of them menu sounds
  keyed blits >= 1000         6744, which is the character-select screen drawing sprites

THE THRESHOLD IS THE BUG, not the number. tools/smoke_test.sh's own comment records what this
discriminator is worth: "a run that stopped at the overlay measured plays=1, one that reached
the match measured plays=7". mouse_test asks for >= 2 and calls it "(a match started)". Four
menu sounds clear it, so the check cannot fail for the reason it names, and the test's first
line -- "driving the game from the mouse alone into a match" -- is not true of what it runs.

WHY IT WENT UNNOTICED: nothing in the run said which screens it reached. That report only
arrived with issue #25's shared module, and this was the first time mouse_test's environment
was run with it. The test has presumably been green and not-reaching-a-match for as long as
it has existed.

WHAT IS STILL WORTH SOMETHING: it does prove the mouse drives the launcher and the post-load
panel with no key and no pad, which is a real claim and the one it can support today.

TWO THINGS TO FIX, and they are separable:
  1. The assertions must discriminate. A match assertion has to key on something only a match
     produces -- panel_hud_up() is reported by the run now, so "screens reached" containing
     `match@` is available and is not a threshold anybody can drift under.
  2. The route must actually get there, which is blocked on the same question as #25: the
     first click does nothing, the second one starts the game, and every later click is doing
     the job the comment gives the one before it. Fixing the assertions FIRST is the right
     order -- then the route failure is visible instead of inferred.

DO NOT simply raise the threshold from 2 to 7. That is the same instrument with a different
number on it, and it would still pass or fail for reasons nobody has tied to a match.

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
