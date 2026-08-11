---
id: I010
kind: instrument
status: trusted
created: 2026-08-11
---

## Instrument

tools/e2e.sh render -- the GPU-vs-software frame comparison

## Validated by

2026-08-11: re-run after the controls hint moved into the display list, and BOTH frames are now genuinely GPU-drawn -- frame_001300 max channel diff 1 (251/436700 px), frame_002250 max channel diff 2 (386/436700 px), with LF2_RENDER_DEBUG=1 on the same route independently reporting gpu=on frames=900 where it previously reported frames=0

## What it does

Runs the same scripted route four times -- `soft`, `gpu`, `gpu` with every 7th draw dropped,
and `gpu` with the lighting on -- and diffs the dumped frames per channel. Two frames: 1300
(character selection) and 2250 (a match).

## Known failure modes

### THE MENU FRAME WAS VACUOUS UNTIL 2026-08-11, and the test could not have said so

Character selection is a screen where the controls hint is up, and until 2026-08-11 the hint
turned the GPU path off for the whole frame in `hostwin_present` (issue #52). So the `gpu` arm
dumped the SOFTWARE compositor's buffer, and "frame_001300.ppm: gpu matches software" was one
buffer compared against itself. It passed for the entire life of the renderer and could not
have failed.

### WHY THE TEST'S OWN NEGATIVE CONTROL DID NOT CATCH IT

This is the part worth learning from. The `LF2_RENDER_SKIP=7` arm drops every 7th draw and
asserts the frame must differ; it did differ, by 40423 pixels. But `LF2_RENDER_SKIP` is
implemented in the DISPLAY-LIST RECORDING, so dropping draws changes the composition the
software compositor builds as well. The control was measuring the skip, not the renderer, and
it went green with the renderer entirely uninvolved.

A negative control that shares a cause with the positive proves nothing about the path between
them.

### BEFORE TRUSTING A "gpu matches software" LINE, CHECK THE RENDERER RAN

`render: gpu=on frames=0` is the tell. Note that `software fallbacks=0` sits right beside it
and is reassuring nonsense: both counters live inside `render_present`, so a frame the gate
never submits increments neither. The zero that means "never asked" and the zero that means
"never failed" are written identically.
