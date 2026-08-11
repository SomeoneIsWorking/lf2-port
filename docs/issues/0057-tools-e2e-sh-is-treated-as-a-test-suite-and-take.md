---
id: 57
title: tools/e2e.sh is treated as a test suite and takes minutes; the rule is nothing over 10 seconds
status: open
symptom: reported, repeatedly. The route scripts boot the game and each run takes 25-83 seconds before a single assertion; thirteen scripts, several making 2-5 runs each, so a full sweep is 20+ minutes. They have been run as a routine gate after every change
tags: reported,testing,verification,workflow
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11, and this is the THIRD time the same correction has been given -- "just
have one suite that is fast" (which produced ctest), then "your tests need to be static, don't
run 10 minute tests" (which produced tests/test_framelife.c), and now "no tests over 10
seconds, but your tests take 5+ minutes". The first two were answered by moving one thing
offline each time while leaving tools/e2e.sh being run as a gate. That is the defect.

MEASURED, so the next session argues with numbers rather than with intent. A bare headless run
to frame 3000, LF2_UNPACED=1, before any assertions at all:

    software compositor           25 s   (14.6 s user, 66% CPU)
    GPU renderer                  35 s
    GPU renderer, lighting off    83 s   (37% CPU -- mostly waiting)

Two things fall out of that immediately:
  - A THIRD OF THE WALL CLOCK IS IDLE even with pacing off, and under the GPU renderer it is
    nearly two thirds. Nothing in this port sleeps when unpaced (frame_pace returns early), so
    that wait is inside SDL -- presentation on the offscreen driver, or GPU submission. It has
    never been looked at.
  - THE LIGHTING BEING OFF MADE IT SLOWER, by a factor of two. That is backwards and unexplained
    and may be the same wait.

WHY THE SCRIPTS ARE LONG, which is a different question from why a frame is slow: every route
walks the game's menus in real frames -- roughly 900 frames of front end, a data load, then
character selection -- before it reaches the thing it is testing. The assertions are cheap; the
approach march is not.

WHAT THE RULE ACTUALLY IMPLIES. There is no arrangement in which thirteen game-booting scripts
are a 10-second suite. So either they stop being a SUITE, or the approach march goes away:

  (a) STOP RUNNING THEM AS A GATE. `ctest` is the suite; the route scripts are investigation
      tools that a person runs when they want to answer a question about a running game. This
      is the smallest honest change and it is what the reporter has now asked for three times.
      It costs coverage: background_test's byte-identity arm is the only thing standing between
      this port and a silent regression of the 4:3 picture, and nothing offline replaces it.
  (b) MAKE A ROUTE START AT THE THING IT TESTS. Most of every run is menus. If a route could
      begin in a match, runs would be a few hundred frames rather than three thousand. The port
      already drives the game's own mode menu with LF2_MODE rather than faking state, so there
      is precedent for doing this the honest way -- but a save-state or a synthesised match
      would be exactly the fake this project forbids, and that distinction is the whole
      difficulty.
  (c) FIND THE IDLE. A third to two thirds of the wall clock is spent not computing. If that is
      one SDL call it may be the cheapest large win available, and it has never been measured.

DO NOT answer this by moving one more thing offline and continuing to run the sweep. That is
what happened the last two times.

WHAT IS ALREADY RIGHT AND SHOULD NOT BE UNDONE: `ctest` is 9 tests in 1.4 s and nothing in it
boots the game. runtime/overrides/geom.h and runtime/video/framelife.h exist precisely so that
claims that can be checked offline are, and both are included by the shipping code rather than
copied. The audio pan, the frame lifetime and the stage-mode walk bound all moved this way.
