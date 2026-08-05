---
id: 8
title: Load time: frame pacing during the data load; blit throttling is a dead end
status: open
symptom: loading takes ~10-15 s; optimising parsing and import dispatch changed nothing
tags: load,performance,pacing,rendering,dead-end
created: 2026-08-05
updated: 2026-08-05
---

FIXED PART: the load advances one data file per main-loop tick and the tick period is 33 ms, so the load costs (files x 33 ms) -- 315 files. h_Sleep now returns immediately while the game is loading, restoring pacing the moment it stops. Active loading 8.4-10.5 s -> 4.3-6.3 s, ~110k sleeps skipped.

SIGNAL: 'is the game loading' = the game opening its own .dat/.txt/.bmp, 300 ms window. Chosen after keying off fn_004242e0 (the loading screen presenter) engaged for NINE frames of a whole run -- that function is the ad grid and is NOT called during the loading itself.

METRIC WARNING: both the 'parse span' (first fscanf to last) and a first-open-to-last-open span START AT BOOT, because the menu reads .txt indexes before the player chooses anything. Both therefore include menu idle and understate any improvement. Use active-loading time (gaps under the window only).

DEAD END -- do not retry throttling repaints:
  1. fn_0043f010 throttle: no effect. Not the paint path during loading; sampling shows fn_00415160, fn_00401250, GDI StretchBlt.
  2. Skipping blits in surf_Blt/surf_BltFast: ABORTS the game (TerminateProcess). Two separate reasons, both real: (a) lf2_loading_now() is true at boot from the menu's .txt files, so init blits were skipped and the game's DirectDraw verification failed; (b) even gated on the game proper it still aborts, because surf_Blt is how the game COMPOSES surfaces, not just how it displays them -- skipping blits corrupts content the game later depends on. Throttling at the blit level is the wrong idea, not a wrong implementation.
  3. Deciding per blit rather than per frame latches: with every blit skipped there is no present, so a decision taken in present_primary() never updates again and drawing stops for ever.

FOURTH DEAD END -- driving the load to completion in one tick. RESOLVED as impossible by
this route, with the ABI mistake fixed and the idea then falsified on its own terms.

  The ABI part is worth keeping: the stack contract of a recompiled body is readable
  straight out of the generated C -- `R(ESP) += n` at its return. Measured for every
  override: fn_00423b00 +4, fn_004246b0 +8, fn_0043f010 +28, fn_00419a60 +16,
  fn_0043c4a0 +4, fn_00423940 +4. The comment on fn_004246b0 claimed "no arguments,
  RET c3" and is WRONG -- it is RET 4 -- which is what made the first drive leak four
  bytes an iteration and abort the game. That comment is now corrected in place.

  With the ABI right, the drive runs without crashing and still does not work: driving
  fn_004246b0 ran 240 steps and loaded ZERO files, and driving fn_0043e9a0 (the main
  loop's actual per-tick work) ran 300 steps and also loaded ZERO. So the load step is
  not advanced by being CALLED; it is gated on real time inside the game. Calling it
  faster does nothing, which is also why every repaint-side fix failed.

  Both drives reported the file count loaded inside them. Without that they would have
  read as successes -- the drive ran, the load was fast, and the speedup was entirely
  the sleep skip. "The mechanism ran" and "the mechanism did the work" are different
  claims and only the second one matters.

ORIGINAL NOTE ON THE FOURTH ATTEMPT: The load is a state
machine advancing one step per call to fn_004246b0, so the port (which already overrides
that function) can call the original body in a loop until the game stops opening files,
loading everything inside a single frame. That removes the repaint problem entirely --
there is no frame between steps. It CRASHES: the recompiled body ends in RET, so each
extra call needs the guest return address pushed back and ECX restored, and even with
that the guest ESP drifts across iterations and the game aborts. Either the body does not
unwind identically on every path, or something in it assumes one call per frame. The idea
is sound and is the right shape for a real port of the loading screen; making it work
needs the exact stack contract of the recompiled body established first, not assumed.

REMAINING: ~4-6 s, and sampling says it is drawing, not parsing (parsing is 0.34 s). A safe throttle would have to target only the loading screen's own full-screen repaint, identified specifically, leaving composition blits alone. Not attempted.

Also: a faster load shifts the frame-scheduled input in tools/*_test.sh, so controller_2p may flake more often.
