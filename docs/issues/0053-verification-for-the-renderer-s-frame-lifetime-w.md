---
id: 53
title: Verification for the renderer's frame lifetime was put in a route script instead of offline, doubling a 300-second test
status: resolved
symptom: reported. The retained-frame bookkeeping added for issue #52 is pure state -- list lengths, a rewind, a tile-pool high-water mark -- and none of it needs a running game, but it was verified by adding a SECOND full game run to tools/routes/pause_dropout_test.sh as a negative control. That is a slow test made twice as slow to check something a millisecond of arithmetic can check
tags: reported,testing,verification,renderer
created: 2026-08-11
updated: 2026-08-11
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
the positive, which is the thing this project has already been bitten by -- a check that cannot
fail (issue #26, instrument I010). The control has to survive; it has to move offline with the
thing it controls.

WHAT A ROUTE SCRIPT IS STILL FOR, so this does not swing too far: whether the pause menu is
actually presented by the renderer in a real run is a fact about a running game, and one cheap
assertion on the existing pause run is the right place for it. What does not belong there is a
whole extra run to establish that a counter counts.

### Resolution (2026-08-11)
The frame lifetime moved into runtime/video/framelife.h -- pure bookkeeping over plain
integers, no SDL -- which runtime/video/render.c INCLUDES and calls into, so the test is not
exercising a copy. tests/test_framelife.c walks it in `ctest framelife`, 0.00 s.

WHAT IS ASSERTED OFFLINE NOW:

  - a frame is marked spent at the mid-update present and cleared by the first later recording
    call, because LF2 can begin the following frame before its update returns
  - only a successfully copied completed output is valid for frozen presentation, and only for
    the same composition source; a failed live copy invalidates the previous snapshot
  - a resized frozen output contains the immutable raster without changing its aspect
  - the overlay boundary is set once and does not move, and a list that does not exist reports
    no picture rather than reading off the end
  - the pool serves any free texture the tile FITS IN, bucketed to 32 -- a 880-wide string
    reuses the 928 texture a 900-wide one made, which is the actual fix for the exhaustion --
    and takes the smallest that fits so a glyph does not take the wide one
  - two tiles in one frame never share a texture, because both are live at once
  - exhaustion is counted, and the peak is PER FRAME rather than a running total

THE ROUTE KEEPS ONE RUNTIME ASSERTION. `tools/e2e.py pause_dropout` checks that a real pause in
a real match restores the native snapshot (121 frames in issue #94's verification), because
that is a fact about a running game. The old extra negative run remains unnecessary; the pure
state negatives live in `ctest framelife`.

Suite: 9 tests, 1.97 s, nothing in it over a second. `tools/e2e.sh render background` and
`pause_dropout` all pass after the refactor, and the paused frame at 1920x1080 still draws
correctly.
