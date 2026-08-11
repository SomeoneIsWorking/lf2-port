---
id: 18
title: two_human_match's scripted route no longer reaches the match on this machine -- the test fails at HEAD too
status: resolved
symptom: tools/e2e.sh two_human_match fails both arms with 'no live fighter at object index 1'; coop_dropin and coop_select on the same build pass
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
tools/routes/coop_dropin_test.sh already says this in passing ("the data load does not take a fixed
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

FIXED in runtime/input/gamepad.c: when LF2_VIRTUAL_PAD or LF2_VIRTUAL_PAD2 is set the run is a
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
clock is WALL-CLOCK derived. guest_ns() in runtime/win32/imports.c reads CLOCK_MONOTONIC and feeds
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

  runtime/win32/imports.c -- guest_ns() stops reading CLOCK_MONOTONIC entirely and returns a
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

WHAT IS UNRESOLVED, and why this is not committed: with the change in, the full ctest run went from
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

### Note (2026-08-06)
THE SPIN IS NAMED, 2026-08-06, and it changes what the virtual clock's fix should be.

HOW: a new instrument, LF2_CLOCK_SITES (runtime/win32/imports.c, registered as I005), records every
guest call site that reads the clock with two numbers -- total reads, and the longest RUN of
reads with no Sleep between them. The second is the one that discriminates: a well-behaved
deadline loop reads constantly and sleeps between reads, so call count alone cannot tell it
from a spin. Over a 2400-frame route:

  from=0043d162  timeGetTime  reads=241581  longest run=399
  from=0043d195  timeGetTime  reads=241581  longest run=401
  every other site               reads<=2254  longest run<=126

WHAT IT IS: fn_0043cf40 (0x0043cf40..0x0043d222) is the game's MAIN LOOP, and both hot sites
are inside its pacer. Read off the disassembly:

  0043d110  loop head: PeekMessage; if a message -> Translate/Dispatch, JMP 0043d1ef, which
            skips the pacing AND the Sleep entirely
  0043d160  now = timeGetTime(); elapsed = now - last
  0043d164  if (elapsed <= 0x21)   -> 0043d193: remaining = last + 0x21 - now, then sleep
  0043d169  else (the frame is OVERDUE):
              if (elapsed > 0x64) last = now - 0x64        <-- resync to 100 ms behind
              run the frame (CALL 0x0043e9a0)
              last += 0x21
  0043d1df  if (remaining <= 0) NO SLEEP -- straight round the loop again
            else Sleep(min(remaining, 5))

So when the game falls behind by more than 100 ms it resyncs to exactly 100 ms behind, which
is still more than the 33 ms frame period, and then runs frames BACK TO BACK with no Sleep at
all until it catches up. With a real clock it catches up because real time passes. With a
clock advanced only by Sleep, it never catches up -- it runs frames forever. That is the 111 s
of user CPU measured with CLOCK_READ_NS=0.

WHY THIS CHANGES THE FIX: crediting a microsecond per READ was a guard against the symptom,
and it is a poor one -- it ties the guest's timeline to how many times it happens to look at
the clock, and it is what distorted the pacing. The loop's own catch-up condition points at
the right credit instead: the virtual clock should advance with the game's PRODUCED FRAMES.
A per-presented-frame credit makes the catch-up loop terminate (each iteration produces a
frame, so time moves), it is bounded and deterministic, and it is tied to the thing the game
is actually doing rather than to an incidental read count.

SIZING IT is the open question. A full 33 ms per frame would double-count against the Sleep
credit during normal play (the game sleeps to fill the frame it just produced), so the credit
has to be the WORK portion -- something like 1 ms -- or the two have to be combined rather
than added, e.g. the clock takes the greater of its sleep credit and frames * 33 ms. Measure
before choosing: the catch-up path needs about 67 iterations to close a 100 ms deficit at
1 ms a frame, which is bounded but visible.

Step 2 of the earlier list -- whether smoke's slowdown was the pacer or its own
instrumentation -- is very likely answered by this too: with reads inflating virtual time,
a run doing heavy per-frame host work reads the clock more, so it paid more virtual time per
frame and the pacer then waited for the wall to catch up. Worth confirming once the credit
moves to frames.

### Note (2026-08-06)
WHERE THE SPIN HAPPENS, and it sharpens the fix. The instrument now records the presented
frame the longest run occurred on, because 'during the load' and 'during play' are different
diagnoses:

  from=0043d162  reads=218937  longest run=379  at frame 8
  from=0043d195  reads=218937  longest run=381  at frame 9
  from=0043d174  reads=4       longest run=185  at frame 2141

Frames 8-9 are the data load; frame 2141 is the moment the match starts. Both are exactly
where the game's work per iteration is heaviest, which is when the pacer's  stays
negative and the loop goes round without sleeping. The 218,937 reads are 91 per presented
frame sustained across the whole run, so this is not a startup quirk -- the loop is reading
its clock about ninety times for every frame it produces.

THE SIZING QUESTION IS ANSWERED BY TAKING THE MAXIMUM, NOT THE SUM. The clock should be

    virtual = max(frames * 33.33 ms, accumulated Sleep credit)

rather than the two added. Both counters are monotonic so the maximum is too, and the two
track each other during normal play (the game sleeps out roughly a frame period per frame it
produces), so neither double-counts. During the load and at the match transition the game
does not sleep at all and the frame term carries the clock forward, which is precisely what
the catch-up loop needs to converge. Adding them instead would give 66 ms per frame of
virtual time during play -- the game would believe it was running at 15 fps and the wall
pacer would halve the speed.

With that, the per-read credit can go entirely, and with it the distortion that is the
likeliest cause of the smoke slowdown.

### Resolution (2026-08-06)
FIXED. The guest clock is now the FRAME COUNTER -- guest time is exactly
presented_frames * 33.33 ms plus the sleeps the game took -- and real-time pacing moved to
the host, into the present. The guest counts, the host paces.

THE PROOF, which is the thing this entry existed for: the same route, same binary.

  idle box:               charselect@906 overlay@1746 match@1968
  under 14 busy loops:    charselect@906 overlay@1746 match@1968

Identical. Before it, the loaded run reported 'screens reached -- NONE' -- character selection
never appeared at all in 2800 frames. Full suite 15/15, smoke back to 82 s from the 150 s it
took under the earlier attempt, 32 fps at 14% of a core.

THREE THINGS THAT LOOK LIKE DETAILS AND ARE LOAD-BEARING, each measured rather than reasoned:

1. A SLEEP MUST BE CREDITED AS A FLOOR, NOT AN EQUALITY -- credit ms + 1. Sleep(n) returns
   after AT LEAST n; nanosleep never returns early. Credit exactly n and the game's pacer
   lands on its own boundary and stops dead: it sleeps  when remaining <= 5, so it
   arrives at elapsed == 33 exactly, where  sends it down the sleep path and
    sends it straight past the Sleep. Neither working nor waiting. Measured:
   59,331,701 clock reads at frame 0, no Sleep, no frame ever presented, 99% of a core. A
   real clock crosses that boundary through overshoot; an exact one has to be told.

2. SLEEPS MUST STILL BE CREDITED DURING PLAY, not only on the load's fast path. The startup
   waits happen before the load is even flagged and produce no frames, so a clock that only
   frames advance waits for a time that can never arrive -- measured, 1.8 s of CPU in 200 s
   of wall and no screen reached. It costs nothing during play, and that is a property of the
   game rather than luck: 33.33 ms is above the 33 ms threshold fn_0043cf40 compares against,
   so once frames flow the loop always takes its overdue branch and never sleeps.

3. THE HOST PACER MUST DROP ITS ANCHOR WHILE LOADING, not merely skip pacing. Frames keep
   being counted through the load and the wall does not follow them, so an anchor taken
   before it leaves every later frame due far in the future -- measured, the whole run slept
   its way along at under 12 fps with 1.8 s of CPU.

WHAT THIS REPLACES: the earlier attempt credited a microsecond per clock READ as a hang
guard. That guarded the symptom, tied the guest's timeline to how often it happened to look
at the clock, and is what made the smoke run -- the one with three debug variables on -- pay
more virtual time per frame and slow to 150 s. It is gone. LF2_CLOCK_SITES (I005), the
instrument that named the spin, stays.

The routes being screen-keyed (commit d96993d) is still worth having and is unaffected: a
press whose screen never appears still never fires and the run still says so.
