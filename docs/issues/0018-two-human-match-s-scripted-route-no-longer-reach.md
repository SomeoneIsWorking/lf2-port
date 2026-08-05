---
id: 18
title: two_human_match's scripted route no longer reaches the match on this machine -- the test fails at HEAD too
status: open
symptom: ctest two_human_match fails both arms with 'no live fighter at object index 1'; coop_dropin and coop_select on the same build pass
tags: test,flaky,timing,coop,two-player
created: 2026-08-05
updated: 2026-08-05
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
