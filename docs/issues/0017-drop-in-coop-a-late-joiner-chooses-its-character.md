---
id: 17
title: Drop-in coop: a late joiner chooses its character on the stage, and the flash is a gate toggle rather than a fade
status: resolved
symptom: a player joining a running match is handed a character it did not pick; there is no character-select screen after the match has started
tags: coop,drop-in,character-select,rendering,method
created: 2026-08-05
updated: 2026-08-05
---

A late joiner missed the character-select screen, so the choice is made ON THE STAGE: the
fighter is built straight away and left FLASHING in place while its device cycles the
game's roster with left/right and confirms with attack. Locking in is the same record
simply stopping flashing -- there is no second spawn, so what was previewed is exactly what
plays.

### The flash is a GATE TOGGLE, not an alpha fade -- and that is a limit, not a preference

The obvious reading of "fade in and out" is alpha. There is none available. The port's blit
path (runtime/ddraw.c) is a colour-keyed copy of 8-bit paletted sprites: no blend anywhere
in it, and nothing in the game's own data carries an alpha channel. A true fade would mean
inventing per-object blending in the porting layer -- identifying which blits belong to
which object inside fn_0043f010, which draws every screen -- and that would be this port's
invention rather than the game's mechanism.

So the flash is the object's own EXISTENCE GATE (the byte at 0x00458b04 + index, claim
C001) toggled on an eight-frame period. That is the game's own switch for "is this object
in the world", used on a schedule.

MEASURED, not asserted: the gate transitions are logged with their frame numbers, so the
period is readable off a run -- 2308 hidden, 2316 shown, 2324 hidden, 2332 shown ... and
the lock-in reports the total ("having flashed 17 times"). A fighter that never flashed
would look, from outside, exactly like one flashing too fast to see, which is why the
negative is printed rather than inferred.

### Cycling REBUILDS; it does not swap the data pointer

The tempting cheap version is to write the new character's data block into +872 and leave
the rest of the record alone. It is wrong: a character's animation frame numbers do not
carry over to another character, so the new fighter would hold a frame belonging to the old
one. Cycling therefore runs the game's own spawn sequence again -- gate off, fn_004061d0,
the new data block into +872 and its +144 into +796, the SAME position back, gate on.

Carrying the position is why struct coop_pos exists. Taking it from a live fighter again
each time (which is what the original spawn does) would walk the preview across the stage
one press at a time.

The rebuild is also what keeps a choosing player out of the fight: it restores position and
full health at every flash, so seconds spent choosing cannot leave the joiner dead or
across the stage. Its device's buttons are withheld from the record until lock-in for the
same reason -- left and right are choosing a character there, and a fighter that also
walked while its player chose would be acting on both readings of one press.

### VERIFIED end to end

One scripted run (tools/coop_select_test.sh, and scratch/logs/select3.log):

  - 23 playable characters read from the game's own registry
  - flash: gate down and up on an 8-frame period, 17 cycles before lock-in
  - cycle: right 50 -> 38 -> 39 -> 37, left 37 -> 39. Left undoes right.
  - lock: id 39 locked in -- the one the last press left on screen
  - frame dumps show the fighter on the stage labelled "5" with its HUD portrait, and a
    VISIBLY different character after each cycle (game/scratch/frame_002403.png vs 002443)

The negative is asserted too: with the choice open, no direction reaches the fighter's
record.

### Drop-out

A device that disappears (a pad unplugged) takes its fighter with it -- gate cleared,
device selector zeroed, joined-mask bit cleared -- but ONLY for a slot this port filled.
coop_leave refuses out loud for any other, because a slot the game's own character
selection filled is not this code's to empty.

### And the file split that came with it

runtime/overrides.c had reached 2500 lines and mixed four unrelated subsystems. It is now
runtime/overrides/, split by what the code is ABOUT rather than by which address it
replaces. The line worth keeping is coop.c (what the game does) against coop_debug.c (how
this port knows it did it) -- the input gather was a page of device routing buried in 250
lines of LF2_COOP_* instruments, now coop_debug_tick().

The split was verified as a NO-OP: the same scripted selection run before and after
produces byte-identical coop log lines.
