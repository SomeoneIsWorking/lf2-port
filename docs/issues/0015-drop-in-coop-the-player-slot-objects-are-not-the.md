---
id: 15
title: Drop-in coop: the joined-players mask and the player record are found; the active-object list is not
status: open
symptom: a device that is not assigned to a player cannot join a stage that is already running
tags: coop,drop-in,players,re,groundwork
created: 2026-08-05
updated: 2026-08-05
---

Groundwork, not a fix. What is established, with the instrument that established it:

`LF2_COOP_DEBUG=1` prints the player slot table whenever it changes -- the device selector
per slot (`0x00450b4c[i]`) and the object pointer per slot (`this+404+4i` in fn_00419a60).
`LF2_COOP_DIFF=<frame>` dumps the dwords where a playing slot's object differs from an idle
one. Both live in the ported input gather, which is the one place that has `this`.

FINDING 1: all EIGHT player objects already exist, from the moment character selection runs.
They are 0x420 apart, the same stride and the same table as the character-select cursor
objects at 0x00458c94. So joining cannot be "an object gets created" -- the object is
already there.

FINDING 2: and it is not a flag in that object either. Mid-match, a playing slot's object and
an idle slot's differ in exactly TWO dwords of 264, both of which look like floats
(0x40690000 / 0x40822000 and 0x00000000 / 0xc0690000) -- coordinates, not state. There is no
"is playing" field and no chosen-fighter id in this structure.

So the object at `this+404+4i` is the player's INPUT/cursor record, and the fighter that
walks around the stage is a separate object linked elsewhere. Finding that link is the next
step, and it is the real work: the fighter array, how a fighter is created at match start,
and what binds it to a player index.

ALSO NOTED, unverified: the device selector table spans eight entries
(0x00450b4c..0x00450b6c) but the ported gather loops `i < 4`. If the game really supports
eight players, that cap is a port limitation -- but changing it without evidence would risk
the two-player path that `tools/controller_2p_test.sh` covers, so it is left alone.

DEAD END TO AVOID: watching 0x00450b50 for the write that makes a slot live. The device
selector is the CONTROL CONFIG index (slots 0..3 read 1,2,3,4 from character selection
onwards and never change), not a joined flag, so the watch reports a single write at match
start and says nothing about joining.

### Reopened (2026-08-05)
CORRECTION -- the two "findings" below were measured on the CHARACTER SELECT screen, not in
a match, because the click route used for them lands in VS mode and its char-select screen
looks nothing like a match in the .data the hook reads. The instrument was fine; the state
it was pointed at was wrong. Both conclusions are withdrawn.

What is actually true, all measured in a running match:

1. THE JOINED-PLAYERS BITMASK IS 0x00451288. Diffing .data across a character-select join
   gives 0 -> 1, and across a SECOND join 1 -> 3. A count would have gone 1 -> 2; a mask goes
   1 -> 3. (0x00450bd4 also moved on the first join and was ruled out on the second, where it
   went 2 -> 0: a cycling value, not state.)

2. THE PER-SLOT RECORD IS THE FIGHTER. `this+404+4i` in fn_00419a60, 0x420 apart, the same
   objects as the character-select cursors at 0x00458c94. In a match a playing slot and an
   idle one differ in 32 dwords of 264, and they read like a fighter:
     +0x010 / +0x018   position, as ints
     +0x05c / +0x06c   position, as floats
     +0x2fc            HP      (436 hurt vs 500 full)
     +0x300            MP
     +0x354            99 on an idle slot, 0 on a playing one
     +0x364            the chosen character (10 vs 0) -- the same field charselect_mouse uses
     +0x368            a per-slot pointer
     +0x418            5 vs 0

3. FIGHTERS ARE NOT ALLOCATED AT MATCH START. Heap-diffing across "Fight!" changes only 95
   dwords in 26.7 M, and the clusters land inside the slot records themselves (+0x10..0xb8
   and +0x2fc..0x418 of slot 0). So match start INITIALISES pre-existing records; there is no
   spawn to call.

4. AND AN INITIALISED RECORD IS NOT SUFFICIENT. Two negatives, both run:
     - setting a mask bit mid-match does nothing at all (the mask is read at match start);
     - setting the bit AND cloning a playing slot's whole record onto an idle one, with its
       own +0x368 and a shifted position, still produces no third fighter -- nothing draws,
       nothing collides, the HUD stays at two.

So what remains is the list of ACTIVE objects: the thing the game iterates to update and draw,
which a player has to be in. That is the next step, and it is the only one left before
drop-in is mechanical.

Instruments, all in the ported input gather (the one place with `this`): LF2_COOP_DEBUG (slot
table on change), LF2_COOP_DIFF=<frame> (playing vs idle record), LF2_COOP_SNAP=<a>,<b> (one
slot across two frames -- note it cannot span a join, because the gather only runs in-match),
LF2_COOP_TEST=<frame> (the mask + clone probe above).

METHOD NOTE worth keeping: LF2_COOP_DIFF must be pointed at a frame that is VERIFIABLY a
match. Dump the frame alongside it. A diff of two idle records looks like a clean, confident
result -- two differing dwords -- and says nothing.
