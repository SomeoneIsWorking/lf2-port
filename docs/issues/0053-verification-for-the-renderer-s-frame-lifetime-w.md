---
id: 53
title: Verification for the renderer's frame lifetime was put in a route script instead of offline, doubling a 300-second test
status: closed
symptom: reported. The retained-frame bookkeeping added for issue #52 is pure state -- list lengths, a rewind, a tile-pool high-water mark -- and none of it needs a running game, but it was verified by adding a SECOND full game run to the recorded pause_dropout runtime scenario as a negative control. That is a slow test made twice as slow to check something a millisecond of arithmetic can check
tags: reported,testing,verification,renderer
created: 2026-08-11
updated: 2026-09-09
---

REPORTED 2026-08-11, and it is the same correction as the one that produced the one-fast-suite
rule: "your tests need to be static, don't run 10 minute tests".
WHAT WAS DONE WRONG. Issue #52 gave the renderer a retained frame: a frame's display lists are
cleared by the first call that RECORDS over them rather than by the present that finished them,
and render_hold_begin rewinds a held frame's overlay before the next one is recorded. The
things that can break in that are all bookkeeping:
  - a spent frame is cleared exactly once, by the first recording call
  - a held frame rewinds to the length the game last built, so an overlay is recorded once per
    frame rather than appended to itself
  - the tile arena and the texture pool rewind with it
  - the overlay boundary separates what is lit from what is not
  - the tile pool serves any tile that fits in a texture's corner, and needs one texture per
    tile LIVE IN A FRAME rather than per distinct size
Not one of those needs a window, a GPU, or the game. All of them were verified by running the
game instead -- and the negative control was a SECOND 300-second run of the same route with the
pause removed. The suite this project keeps under two seconds gained nothing; the route suite
got five minutes longer.
THE SHAPE OF THE FIX IS ALREADY IN THE TREE. runtime/overrides/geom.h is the port's pure
geometry, included by the shipping overrides so the test exercises what ships, and walked by
tests/test_geom.c in a millisecond. The frame lifetime wants the same treatment: the list
lengths, the spent/held flags, the rewind and the pool's fit-and-claim are arithmetic over
plain integers and can live in a header with no SDL in it, which render.c includes.
WHAT MUST NOT HAPPEN, because it is the tempting cheap version: leaving the assertions in the
route script and merely deleting the second run. That removes the negative control and leaves
