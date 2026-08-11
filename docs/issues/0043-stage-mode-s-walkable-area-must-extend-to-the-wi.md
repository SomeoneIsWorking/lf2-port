---
id: 43
title: Stage mode's walkable area must extend to the widescreen view, RE'd first
status: open
symptom: in stage mode the area a fighter may walk to is the game's 4:3 bound, so a wider view shows stage the player cannot reach
tags: reported,widescreen,stage-mode,gameplay
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11, with the explicit instruction: REVERSE ENGINEER IT FIRST. No constant
that makes one screen look right -- find the game's own bound and what it is expressed
against, then decide.

WHAT IS ALREADY KNOWN AND MUST NOT BE RE-DERIVED:
  - The CAMERA half is done (issue #36, resolved). fn_0041b5d0 bounds the camera twice: by
    `stage_width - 794` and, when non-zero, by the stage-mode SECTION LOCK at 0x00450bb0 --
    what holds the camera partway along a stage until the section is cleared. Both are the
    game saying "the right edge of the screen goes HERE" against a 794-wide screen, so both
    take the same 794 -> view substitution. That lives in geom_camera_max
    (runtime/overrides/geom.h) and is checked by `ctest geometry`.
  - THIS ISSUE IS THE OTHER HALF: where a FIGHTER may walk, which is not the camera. A wider
    camera bound without a wider walk bound shows stage the player cannot reach -- and the
    reverse would let a fighter walk off the visible picture.

WHAT HAS TO BE FOUND, and none of it is established yet:
  - WHICH WORD BOUNDS A FIGHTER'S X. The object step is fn_004064d0. The bound may be the
    stage width, a per-section value, or the same 0x00450bb0 lock read by the object code as
    well as the camera. Until that is read out of the binary this issue has no design.
  - WHETHER IT IS EXPRESSED AGAINST 794 AT ALL. The camera's bounds are, and that is what made
    the substitution legitimate there. If the walk bound is in WORLD units with no screen term
    in it, then it must NOT be widened -- the world would be the same size and only the view
    changed, which is the correct answer and the opposite of the change requested.
  - WHAT STAGE MODE DOES DIFFERENTLY. Sections are a stage-mode concept; VS has the lock at
    zero. `LF2_MODE=stage` drives the port into stage mode (runtime/overrides/menu.c) and
    `tools/e2e.sh stage_mode` already reaches it, so there is a route to observe this on.

HOW TO READ IT: tools/re/ghidra_scripts/DecompDump.py, see docs/running.md. fn_0041b5d0 is the
function that already gave up the camera bounds; the object step fn_004064d0 is the other
candidate and has not been decompiled.

DO NOT start by clamping a fighter's x to the view width in an override. That is the same
class of mistake as a synthesised keypress: it would produce the right-looking result without
anyone having found the mechanism, and it would be wrong the moment a section boundary or a
stage-mode transition disagreed with it.
