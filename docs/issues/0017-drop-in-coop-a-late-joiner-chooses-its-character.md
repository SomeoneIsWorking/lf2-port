---
id: 17
title: Drop-in coop: a late joiner chooses its character on the stage, and the flash is a gate toggle rather than a fade
status: closed
symptom: a player joining a running match is handed a character it did not pick; there is no character-select screen after the match has started
tags: coop,drop-in,character-select,rendering,method
created: 2026-08-05
updated: 2026-09-09
---

A late joiner missed the character-select screen, so the choice is made ON THE STAGE: the
fighter is built straight away and left FLASHING in place while its device cycles the
game's roster with left/right and confirms with attack. Locking in is the same record
simply stopping flashing -- there is no second spawn, so what was previewed is exactly what
plays.
### The flash is a GATE TOGGLE, not an alpha fade -- and that is a limit, not a preference
The obvious reading of "fade in and out" is alpha. There is none available. The port's blit
path (runtime/video/ddraw.c) is a colour-keyed copy of 8-bit paletted sprites: no blend anywhere
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
