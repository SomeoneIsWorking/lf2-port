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

### Note (2026-08-11)
FIRST FIX LANDED, and the numbers in the entry above were WRONG -- taken on a machine at load
average 10-21 and not trustworthy as absolutes. Corrected, and the "lighting off is twice as
slow" anomaly was contention, not a property of the port.

WHAT A RUN ACTUALLY COSTS, measured as a slope so startup and per-frame come apart:

    100 frames   1.32 s        600 frames   2.89 s
    1200 frames  4.63 s       2400 frames   8.22 s      (software compositor)

which is about 1 s of startup and 3 ms a frame. The software path runs at 95-96% CPU: it is
CPU-bound and there is no mystery in it.

THE GPU PATH WAS THREE TIMES THAT AND SPENT A THIRD OF ITS WALL CLOCK WAITING -- 23.0 s for the
same 2400 frames at 74% CPU. The wait was ONE CALL: render_readback, run on every presented
frame whether or not anything wanted the pixels. A readback is a full GPU-to-CPU stall (the CPU
waits for every queued draw to retire before the copy), so paying it per frame throws away
exactly the pipelining a GPU renderer exists for.

Both of its consumers are off in an ordinary run: the screen-change detector needs
LF2_SCREEN_HASH and the dump needs the frame to be named in LF2_FRAME_DUMP. The readback is now
gated on one of them actually wanting THIS frame.

    before   2400 frames, GPU   23.0 s   74% CPU
    after    2400 frames, GPU   14.9 s   95% CPU

A 35% cut on every GPU run in the tree, and the idle is gone -- 95% CPU means the remaining
difference from the software path is work, not waiting. Verified that the gate does not break
what it gates: a dump at 1920x1080 still writes the GPU frame at 1920x1080 rather than the
978x550 composition, which is what the readback exists to make possible.

WHAT IS STILL TRUE AND STILL UNANSWERED: 14.9 s is still not 10, a route makes several runs,
and there are thirteen of them. The remaining levers are unchanged -- (a) stop treating them as
a gate, (b) start a route at the thing it tests instead of walking 900 frames of menus first.
Nothing here makes the sweep a suite.

AND A NOTE ON HOW THE FIRST NUMBERS GOT INTO THIS ENTRY: they were measured back-to-back while
other work of mine was still running, and recorded as though they were clean. A timing taken
under unknown load is not a measurement. `uptime` before and after, or it does not count.
