---
id: 8
title: Load time: frame pacing during the data load; blit throttling is a dead end
status: resolved
symptom: loading takes ~10-15 s; optimising parsing and import dispatch changed nothing
tags: load,performance,pacing,rendering,dead-end
created: 2026-08-05
updated: 2026-08-24
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

### Resolution (2026-08-05)
RESOLVED: 8.4-10.5 s -> 1.2 s of active loading. The cause was NOT drawing.

The note below said "the remaining 4-6 s is drawing, not parsing". That was wrong, and it
was wrong because it came from stack sampling rather than from timing the sections with a
denominator. LF2_LOAD_PROF=1 (new, runtime/loadprof.[ch]) times surf_Blt, StretchBlt,
present and the colour fill while the game is opening its data files and prints their total
against the active loading time. It measured drawing at 0.48 s of 3.35 s -- 14%.

THE REAL COST: the game decrypts every data file ONE BYTE AT A TIME through the C runtime.
FUN_004148a0 is fscanf(in,"%c",&c) / fprintf(out,"%c",c-key[i]) in a loop, which is fine
natively and is not fine through a recompiled CPU where each is a guest->host import call.
2,546,141 fscanf calls per load; after the port 466,509.

It is now a native override (fn_004148a0 in runtime/overrides.c):
  key    "SiuHungIsAGoodBearBecauseHeIsVeryGood", 37 bytes
  header the first 0x7b = 123 bytes are discarded and the key index advances with them, so
         the payload starts at key index 123 % 37 = 12
  byte   out = (in - key[i]) mod 256, i = (i+1) % 37
Text-mode CRLF collapsing on input and raw output are both reproduced, because the CRT does
them and the game's parser depends on the result.

Proved rather than eyeballed: LF2_DECRYPT_DUMP=<dir> copies each decrypted file out, and it
lives in the OVERRIDE so the control run dumps too. Run once with LF2_SLOW_DECRYPT=1 (the
game's own loop) and once without: 77 files, 2.2 MB, all byte-identical.

SECOND FIX: a skipped Sleep now credits the guest clock. Skipping the sleep without moving
the clock does not end the caller's deadline loop, it turns the wait into a spin -- 142,721
skipped sleeps in a 3.35 s load, 453 per data file. Crediting the requested time drops that
to ~1,900. It costs about 0.10 s of loading time (1.08 s vs 1.19 s measured) and buys back
145,000 pointless import dispatches, and the whole run finishes 2.7 s sooner.

DEAD END, measured, do not retry: SCALING the guest clock during the load. 1x = 3.6 s,
2x = 3.5 s, 4x = 3.5 s, 8x = 3.8 s, 16x = 4.7 s, 32x = 7.0 s. A jumping clock makes the game
do more catch-up work, not less. The lever is fast-forwarding through waits the port decides
to skip, not running time faster.

HOW THE LOADER WAS FOUND: LF2_LOAD_SITES=1 lists the distinct guest return addresses that
open data files, with a count and the first path each. 13 sites, and the shape gives it away
-- every object file is opened, decrypted to data/temporary.txt, and reopened, so the sites
come in pairs with matching counts (77/77, 65/65, 12/12).

REMAINING: 1.2 s, of which 74% is now genuinely drawing (0.9 s: 2782 surf_Blt, 363
StretchBlt, 394 colour fills, 342 presents over the load). Cutting it further means fewer
loading-screen repaints, and note that presented frames are what tools/*_test.sh schedule
input against, so anything that changes the frame count shifts every scripted route.

### Note (2026-08-24)
2026-08-24 correction: the failed repeated-drive experiments do not prove the actual data initializer is time-gated. fn_004246b0 mode 1 only presents the loading picture and changes top mode to 2; fn_0043e9a0 does not establish the one-shot load gate. The real synchronous initializer is fn_0041bc90's 0x0041be98..0x0041c57b branch when 0x0044d05c == 1. Repeatedly calling the two wrong functions loaded zero files because neither owned that branch.
