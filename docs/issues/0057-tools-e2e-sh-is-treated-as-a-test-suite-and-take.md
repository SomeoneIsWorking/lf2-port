---
id: 57
title: tools/e2e.sh is treated as a test suite and takes minutes; the rule is nothing over 10 seconds
status: resolved
symptom: reported, repeatedly. The route scripts boot the game and each run takes 25-83 seconds before a single assertion; thirteen scripts, several making 2-5 runs each, so a full sweep is 20+ minutes. They have been run as a routine gate after every change
tags: reported,testing,verification,workflow
created: 2026-08-11
updated: 2026-08-12
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

### Note (2026-08-12)
LEVER (b) TAKEN: A ROUTE NOW STARTS AT THE THING IT TESTS, and the approach march was mostly a
wait for nothing at all.

WHAT WAS MEASURED FIRST, because the 900 was never justified anywhere: the front end is DRAWN
AND TAKING INPUT ON FRAME 1. The mode menu follows six frames after the first press, wherever
that press lands -- pressing at 900 gave charselect@906, pressing at 60 gave charselect@66,
pressing at 1 gives charselect@7. Every route opened with `south:900`, a number picked to be
safely past a data load that does not begin until a mode is confirmed. So 840 frames of every
single run were the game idling on its first screen.

THE FIX IS AN ANCHOR, NOT A SMALLER NUMBER. `@frontend` joins charselect/overlay/match in
runtime/app/script.c, and like them it comes from what the game DRAWS -- the flat backdrop
colour only the front end paints (0x10206c, pushed at exactly one site in .text, already used
for per-screen framing in issue #44). A route keyed to it cannot silently pass on a build that
never reached the screen, which a smaller frame number could.

Also fixed, because it is the SAME defect one level down: LF2_FRAME_DUMP took absolute frame
numbers only. The pad scripts were given screen anchors in issue #25 and the dumps kept their
stopwatches, so `LF2_FRAME_DUMP=2250` meant "a frame with fighters on it" only for as long as
nothing upstream moved. It now accepts `@screen+N` through the same script_when.

ALL TEN PAD ROUTES CONVERTED, and the sweep is green. THREE THINGS BROKE ON THE WAY, all of
them instruments rather than the port, and all three are worth recording because each reported
something alarming and false:

  1. resize_test built its dump filename as `frame_00$FRAME.ppm`. That only ever worked while
     FRAME was four digits; at 710 it looked for frame_00710.ppm, the dumper wrote
     frame_000710.ppm, and the route said "the route did not reach frame 710, so NOTHING was
     measured". Now printf "%06d".
  2. render_test decided which frame was the MATCH one by matching the filename `*002250*` --
     the number the match dump happened to land on. Anchored, the match frame arrived as
     001351, fell through to the menu branch, and the route reported "the light changed 182635
     px on a frame with NO fighters in it", which reads as a serious renderer regression. The
     pixel counts were IDENTICAL to the passing run. It now switches on the frame's ROLE
     (FRAMES is ordered menu-then-match and the glob sorts ascending).
  3. Both dump routes only failed when ZERO frames were dumped, so a run that produced one of
     two printed "ok (1 frame(s) compared)" -- a full pass over half the coverage. They now
     require every frame that was ASKED for and print the denominator.

AND ONE REAL PROPERTY OF THE GAME, which cost a route its saving: `@match` means the HUD strip
is up, NOT that fighters are on screen. The stage load runs after the overlay's confirm with
the HUD drawn across it, and that gap does not hold still -- controller_test saw 73 keyed blits
at @match+691 and 71988 by @match+1131. Its LF2_QUIT_AFTER is 2200 rather than the 1760 the
-840 arithmetic implied, and it is now covering far MORE gameplay than before (71988 keyed
blits against 4569 on the old route). Do not tighten it back without re-running it.

WHAT THIS DOES NOT DO: it does not make the sweep a suite. A route still boots the game, and
13 of them still take minutes. The saving is real and it is per run, but lever (a) -- ctest is
the suite, the routes are investigation tools -- is unchanged and is still the answer to the
reporter's actual complaint. This entry stays open for that.

### Resolution (2026-08-12)
INTEGRATION VERIFIED, and the entry can now be closed on the answer it always pointed at.

The whole tree, after a day that hand-ported two guest functions (issues #55, #58), rewired
every route onto screen anchors, changed the renderer pinning of nine of them, and replaced a
mouse gate:

    ctest             10 tests, 1.53 s -- nothing in it boots the game
    tools/e2e.sh      14 script(s) -- 14 passed, 0 failed, 0 skipped, 452 s
    amdgpu faults     0 in the boot

That matters more than the individual greens it is made of: every route had been verified alone,
and isolated verification does not compose -- the object-pass port in particular sits under the
renderer's character identification, the background pass and the coop tests alike.

THE RESOLUTION IS LEVER (a), which this entry named first and which the reporter asked for three
times: `ctest` is THE suite. It is 10 tests in about a second and a half, nothing in it boots
the game, and it is what may be run after every edit. tools/e2e.sh is an INVESTIGATION TOOL --
run one when it answers a specific question about a running game, say in the commit message when
they were not run, and never treat the sweep as a gate.

WHAT WAS DONE TO THE ROUTES ANYWAY, because "they are not a gate" is not a licence to leave them
slow or wrong: the front-end wait went (840 frames off every run -- the front end is drawn and
taking input on frame 1, and the 900 was never justified); every device's script takes screen
anchors including the launcher click and the keyboard (#25); LF2_FRAME_DUMP takes them too;
nine routes that assert nothing about the renderer no longer run on the GPU (#40); and
`timeout -k` means the wall-clock guard can actually kill.

AND THREE INSTRUMENTS WERE FOUND LYING while doing it, each reporting something alarming and
false: a filename built as frame_00$FRAME that broke when the frame got shorter; a match-frame
classifier keyed to the literal *002250*; and two dump routes that failed only on ZERO dumps, so
one-of-two printed "ok". Those are the reason the sweep is worth having at all, and the reason
it must not be trusted without its negatives.

WHAT IS NOT CLAIMED: the sweep is 452 s and no arrangement makes thirteen game-booting scripts a
ten-second suite. That was never the goal -- the goal was that the thing run after every edit is
fast, and it is.
