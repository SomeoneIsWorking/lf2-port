---
id: 57
title: tools/e2e.py is treated as a test suite and takes minutes; the rule is nothing over 10 seconds
status: closed
symptom: reported, repeatedly. The route scripts boot the game and each run takes 25-83 seconds before a single assertion; thirteen scripts, several making 2-5 runs each, so a full sweep is 20+ minutes. They have been run as a routine gate after every change
tags: reported,testing,verification,workflow
created: 2026-08-11
updated: 2026-09-09
---

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

(full record: git history)
