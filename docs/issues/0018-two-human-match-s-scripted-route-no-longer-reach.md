---
id: 18
title: two_human_match's scripted route no longer reaches the match on this machine -- the test fails at HEAD too
status: open
symptom: ctest two_human_match fails both arms with 'no live fighter at object index 1'; coop_dropin and coop_select on the same build pass
tags: test,flaky,timing,coop,two-player
created: 2026-08-05
updated: 2026-08-06
---

NOT caused by the character-selection work or the runtime/overrides split. Established by
building HEAD (c910061, before either) in a separate worktree and running the same test
against it: identical failure, both arms.

WHAT ACTUALLY HAPPENS: the route never gets into the fight. `LF2_COOP_TABLE=live+60` fires
off the game's own state -- the first frame any table entry stops being an untouched default
-- and in a failing run there is NO `coop table:` line at all, so the trigger never armed.
`coop track` says entry 1 is NOT in the world for all 43 of its samples. The run is sitting
on character selection when LF2_QUIT_AFTER=2450 ends it.

The test's own header names this as the risk: character selection asks each joined player
for a Fighter and then a Team, and player one cannot proceed until every joined player has
finished, so pad two needs presses at frames that depend on how long the data load took.
The load does not take a fixed number of frames, and this machine is evidently not the one
the frame numbers were tuned on.

THE FIX IS NOT A NEW FRAME NUMBER. Re-tuning JOIN="south:1250,south:1380,south:1560" would
work here and break on the next machine -- the same failure the `live+<n>` trigger was
introduced to remove for table dumps. The route needs to be driven off game state the way
the trigger is: press when the screen says it is waiting, not at frame 1380.

Left OPEN deliberately. The coverage it provides is real (two humans in a match, which
controller_2p does not reach) and dropping the test would be worse than a red one.

### Note (2026-08-05)
ROOT CAUSE FOUND, and it is not this test in particular: every route-scripted test here is
frame-number-pinned, and the frame the data load finishes on depends on MACHINE LOAD.

Observed directly. coop_select passed at 21:07 and failed at 21:15 on the SAME commit --
confirmed by stashing the working tree, rebuilding and re-running, which failed identically.
At that moment `ps` showed five other ports building and running on the same box:

    159% zelda3d   111% xenia_oracle   106% sms-recomp   98% spiderman_port   97% io_loot

with load average 6.7 on 16 cores.

WHY LOAD MOVES A FRAME-NUMBERED SCRIPT, which is not obvious -- frames are counted by
presents, so a busier machine should just take longer to REACH frame 2300, not be on a
different screen when it gets there. The data load presents while it works. Under
contention it gets through fewer presents for the same loading work, so the load finishes
EARLIER in frame terms and the whole route shifts relative to the script.
tools/coop_dropin_test.sh already says this in passing ("the data load does not take a fixed
number of frames"); what is new is that the variable behind it is OTHER PROCESSES, so a
run's result depends on what else the machine happens to be doing.

CONSEQUENCE FOR ANYONE READING A RED RUN: a failing route test is not evidence of a
regression until it has been reproduced against a stashed or committed tree on a quiet
machine. Two of these were nearly attributed to a code change that had nothing to do with
them.

THE FIX IS STILL NOT A NEW FRAME NUMBER. It is to drive the routes off game state the way
LF2_COOP_TABLE=live+<n> already does -- press when the screen the press is meant for is up.
That is one mechanism, a "wait until panel X is drawn, then press" script form, and it would
retire the frame numbers in all of these tests at once.

### Note (2026-08-05)
REPORTED directly: the guest timeline must not depend on CPU load. Tagged for the active queue.

### Note (2026-08-05)
CORRECTION -- the CPU-load root cause above is WRONG, and it was asserted on evidence that
did not support it. Left in place rather than deleted, because the way it was reached is the
lesson.

What was actually observed: the test passed at 21:07 and failed at 21:15 on the same commit.
The stash-and-rebuild step proved only that the working tree was not responsible. `ps`
happened to show five other ports busy, and that got written up as the cause. Nothing tested
it -- no run was made with the machine quiet, and no mechanism was measured. It was a
plausible story fitted to one coincidence.

THE REAL CAUSE, and it is visible in the run's own first lines:

    controller 0 connected: Xbox One S Controller
    virtual pad 0: attached as joystick 4
    controller 1 connected: lf2 virtual pad

A PHYSICAL CONTROLLER was plugged into the machine -- the user had started playing. It binds
gamepad slot 0, because bind_available fills free slots from whatever SDL reports and the
hardware is there first. The front-end menu is driven by gamepad_drive_ui(), which reads
slot[0] ONLY. So the scripted route pressed into slot 1 while the menu sat waiting on an
idle controller nobody was touching. The run never left the front end: `input: 0 gathers`,
only bgm/main.wma ever loaded, and no screen appeared at all.

That also explains the timing exactly, with no appeal to load: the failures began when the
user plugged a pad in, not when the machine got busy.

FIXED in runtime/gamepad.c: when LF2_VIRTUAL_PAD or LF2_VIRTUAL_PAD2 is set the run is a
test, and ONLY virtual pads bind. A physical controller is ignored and SAID SO, because a
run that silently ignored the hardware would be the next confusion.

STILL TRUE AND STILL WORTH DOING, on its own merits rather than as this bug's fix: absolute
frame numbers in a route are fragile, and pad scripts now accept `button@match+30` /
`@charselect` / `@overlay`, firing off the game's own drawing. A press whose screen never
appears now reports that it never fired instead of going quiet -- which is what surfaced
this bug in one run.

### Note (2026-08-06)
SEEN AGAIN, 2026-08-06, and this is the third time: coop_dropin's quiet arm failed with 'no
mid-match join happened' inside a full ctest run, and PASSED on its own immediately after on
the same tree (184s, 1/1). The suite was competing with frame-dump runs of my own on a
16-core box under load ~20.

The diagnosis holds and the remedy is still not applied where it matters: coop_dropin's route
is still ABSOLUTE-frame ('south:2300'), not state-keyed ('south@match+30'). controller_test
and the pad-script machinery already support the @<screen>+N form and virtual_pad_report says
which screens a run reached. Converting the coop_* routes is the fix; until then a red
coop_dropin in a loaded suite means nothing until it is re-run alone.

### Note (2026-08-06)
MEASURED, 2026-08-06: screen-keying the routes is NOT the fix, and the note above that called
it 'the fix' was wrong. Correcting it here rather than deleting it, because the reasoning is
the trap.

WHAT WAS DONE: every route in tools/ was converted from absolute frame numbers to the
screen-keyed form from charselect onward (south@charselect+58 ... south@overlay+219,
right@match+108). All five affected tests pass on a quiet machine.

WHAT IT DOES NOT DO: with fourteen busy loops on the box, coop_dropin fails BOTH arms, and
the run reports 'virtual pad: screens reached -- NONE'. Not a late screen, not a shifted
press -- character selection never appears at all within 2800 presented frames. The front-end
presses at frames 900-1080 (which cannot be screen-keyed, since no screen exists before them)
no longer reach a menu that is where they expect it.

WHY, and this is the ROOT CAUSE this entry already named without following through: the guest
clock is WALL-CLOCK derived. guest_ns() in runtime/imports.c reads CLOCK_MONOTONIC and feeds
GetTickCount, QueryPerformanceCounter and timeGetTime. So the relationship between a PRESENTED
FRAME and the game's own sense of time is set by how fast the machine happens to be: under
load the port presents fewer frames per second, so by frame 900 the game has lived through far
more of its own timeline than it does on an idle box. No frame-based script can be robust
across that, and no amount of screen-keying reaches the front end, which is where the route
starts.

THE FIX, not yet done: give the guest a VIRTUAL clock that advances a fixed tick per presented
frame, and let the HOST enforce real-time pacing on top of it. The game then sees a perfectly
regular timeline whatever the machine is doing -- which is what 'you can't have things based
on CPU load' asks for -- and a frame-numbered script means the same thing on every box. Note
the hazard before starting: LF2 paces itself off GetTickCount, so a frame-derived clock with
no host pacing would make the game run as fast as the CPU allows. The pacing has to move to
the host in the same change.

WHAT THE CONVERSION IS STILL WORTH, so it is not reverted: a press whose screen never appeared
now NEVER FIRES and the run says so, instead of pressing into whatever happened to be on
screen. The loaded run above failed with 'screens reached -- NONE' and a NEVER FIRED line --
which is a diagnosis. Before the conversion the same run failed with assertions about a join
that never happened, which is a mystery.

### Note (2026-08-06)
THE VIRTUAL CLOCK: BUILT, MEASURED, AND NOT COMMITTED, 2026-08-06. It fixes the determinism
outright and costs wall time somewhere I did not root-cause, so it is written up here rather
than shipped half-verified. The next session should start from this, not from scratch.

THE DESIGN, which worked:

  runtime/imports.c -- guest_ns() stops reading CLOCK_MONOTONIC entirely and returns a
  VIRTUAL timeline advanced only by the guest's own behaviour:
    * Sleep(ms) credits ms in full, whether or not the port actually sleeps (a skipped Sleep
      is a promise the time passed; without the credit the game's wait becomes a spin).
    * every READ of the clock credits 1 microsecond -- the hang guard, see below.
  h_Sleep then PACES THE WALL AGAINST IT rather than sleeping the requested amount: sleep
  until wall reaches virtual, re-anchoring when the debt passes 250 ms. wall_ns() is a new
  helper used ONLY for that and never to tell the guest what time it is.

THE RESULT, which is the whole point of this entry:

  route -> screens reached, idle box:        charselect@908 overlay@1747 match@1968
  the same route under 14 busy loops:        charselect@908 overlay@1747 match@1968

  BYTE IDENTICAL. Before the change the same loaded run reported 'screens reached -- NONE'.
  The guest timeline stops depending on the machine, which is what was asked for.

WHY PACING THE WALL IS NOT OPTIONAL: sleeping the requested ms and calling it done measured
20 fps where the game asks for 30. nanosleep OVERSHOOTS, LF2 paces a frame by sleeping ~1 ms
at a time and re-reading until a ~33 ms deadline passes, and a virtual clock that credits
exactly what was asked for no longer absorbs the overshoot. Sleeping until the wall REACHES
the virtual clock restored 28.7 fps.

WHY THE PER-READ CREDIT IS NOT OPTIONAL, measured rather than assumed: with CLOCK_READ_NS set
to 0 the same run went from 20 s of user CPU to 111 s and hit its timeout. Something in the
game watches the clock WITHOUT sleeping, and with a clock that only sleeps advance it, that
loop waits for a time that can never arrive. 1 us per read terminates it.

WHAT IS UNRESOLVED, and why this is not committed: with the change in, Test project /home/bhamil/repo/pc/lf2 went from
79 s to over 150 s and timed out, while all nine other tests passed -- controller,
controller_2p, coop_dropin, coop_select, pause_dropout, widescreen, two_human_match, mouse.
Smoke is the run with LF2_CK_DEBUG, LF2_AUDIO_DEBUG and LF2_SCREEN_HASH all on, so the
suspicion is an interaction between heavy per-frame host work and the pacer, but I did NOT
establish that, and the theory that it was smoke's absolute-frame route is DISPROVED: keying
its key script to screens (which needs script_trigger_frame exported from gamepad.c to
win32.c -- LF2_KEY_SCRIPT does not understand @screen today) left it still short of the match
at plays=1.

NEXT STEPS, in order: (1) find the read-spin site -- imports.c already has the 'which guest
loop is sleeping' instrument, and the same trick on the GetTickCount/QPC/timeGetTime callers
would name it, after which the per-read credit may be replaceable by fixing that loop; (2)
measure smoke with and without its three debug variables to settle whether the slowdown is the
pacer or the instrumentation; (3) teach LF2_KEY_SCRIPT the @screen form so the last route
stops being a stopwatch.
