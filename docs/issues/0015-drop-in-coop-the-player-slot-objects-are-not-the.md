---
id: 15
title: Drop-in coop: the joined-players mask and the player record are found; the active-object list is not
status: resolved
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
the two-player path that `the recorded controller_2p runtime scenario` covers, so it is left alone.

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

### Note (2026-08-05)
NEXT LEAD, and where a fresh session should start.

.data records nothing that looks like a registration at match start: the four-way diff across
"Fight!" gives 8 dwords, all small counters/timers (0x00452170, 0x00452180, 0x00452194,
0x004554e0, 0x00457bc8..0x00457bd0, 0x00457cfc). So whatever list a fighter has to be in is
on the heap, not in .data.

The heap diff across "Fight!" has one cluster that is not a player record: 1052 bytes at
0x25f149a0. Its CHANGED-OFFSET PATTERN matches the player record's layout closely -- +0x10,
+0x18, +0x5c, +0x6c, +0x70..0x7c, +0x308, +0x418, ending at 0x418 against a 0x420 stride --
which reads like a second object of the same type. That is the shape of a world-object array
separate from the eight player records, and it is the first thing to confirm.

Not confirmed: it is not on the 0x420 grid of the player array
((0x25f149a0 - 0x25f11c40) / 0x420 is not an integer), so if it is the same type it belongs
to a different array with its own base. Find that base and its count, and the question
becomes whether adding an entry there (plus the mask bit and the record init already known)
is what puts a fighter in the world.

### Resolution (2026-08-05)
RESOLVED as an RE question. The gate is found, and adding a fighter to a RUNNING match is
demonstrated on screen.

THE MECHANISM. `this` is 0x00458b00. At this+404 is an array of FOUR HUNDRED object
pointers on a 0x420 stride, not eight -- the eight player slots are simply its first eight
entries. Every entry holds a live pointer to a pre-allocated record from the moment the
data loads, so being in the array is not what puts an object in the world.

What does is a BYTE PER INDEX at this+4 (0x00458b04). fn_004064d0 walks the table as

    ESI = this + 404
    EAX = LD32(ESI)                       // obj = table[k]
    if (obj->0x338 > 0) obj->0x338--;     // a countdown, run for EVERY entry
    if (LD8(this + 4 + k) != 1) goto next // <-- the gate

VERIFIED, on screen. LF2_COOP_SPAWN=<dst> clones a live fighter's record onto entry dst,
sets +0x354 to dst and writes 1 to the gate byte. The record then animates (+0x000 cycles),
walks (x/y change), COLLIDES and takes damage (HP 500 -> 432), draws with its own name tag,
and gets a HUD bar. Two runs identical up to the spawn frame -- both with exactly two
fighters at frame 2201 -- diverge to two fighters and three at frame 2260
(scratch/screenshots/{control,spawned}.png).

Without the gate byte the same clone is INERT: the record survives 300 frames completely
unchanged, neither updated nor reset. That is the negative control, and it is what the
earlier probe was doing.

CORRECTION to the previous note. 0x25f149a0 is NOT off the 0x420 grid of the player array:
(0x25f149a0 - 0x25f11c40) / 0x420 is exactly 11. It is entry 11 of the same table -- the
computer opponent. The arithmetic in the last session was wrong, and the "separate
world-object array with its own base" it implied does not exist.

HOW IT WAS FOUND, in order, because each step ruled out the next-most-likely story:

1. LF2_COOP_REFS scans the image, the heap in use and the stack for the eight player
   pointers. Each is referenced from EXACTLY ONE place -- consecutive dwords at 0x00458c94
   -- and from NOWHERE in 101.9 MiB of heap. So there is no separate heap list of active
   objects; that was the thing the previous session set out to find.
2. LF2_READ_WATCH_RAW (new) over the table gives the per-entry read profile in a match:
   entries 0..19 read once per frame, 20..49 never, 50..~62 heavily, the rest never. So
   idle fighter slots ARE visited every frame and skipped -- the loop is not bounded by a
   count.
3. The same profile over one idle OBJECT: exactly one byte address is read, +0x338, once
   per frame, and nothing else in 1056 bytes. That is the countdown above, not the gate,
   and its value is 0 on live and idle alike -- which is why the gate is not in the object.
4. The profile over `this` puts the hot region at this+0x04..0x193, ~28 reads per byte per
   frame. The disassembly for the +824 (0x338) countdown lands in fn_004064d0
   and shows the byte test one instruction later.

WHAT REMAINS for drop-in coop proper, none of it blocked: pick a free index (the game gave
its computer opponent 11, so the choice is not "the next player slot"), build the record
from the chosen character's data instead of cloning a neighbour, and bind the new fighter
to the joining device -- the ported gather already writes buttons to `this+404+4i` for
i < 4, and that cap is now known to be a port limitation against a 400-entry table.

INSTRUMENTS ADDED, all env-gated and all in the ported gather:
  LF2_COOP_REFS=<frame>          who points at the player records; carries a positive
                                 control that FAILS loudly if the scan cannot see this+404
  LF2_COOP_REFS_ADDR=<hex>[,..]  extra scan targets
  LF2_COOP_TABLE=<frame>|live[+n] the whole table with the gate byte per entry. `live`
                                 fires off the game's state, not a frame number, and the
                                 dump says NOT A MATCH outright when every entry is still
                                 an untouched default
  LF2_COOP_PAIR=<i>,<j>|auto     dwords differing between two entries
  LF2_COOP_SPAWN=<dst>[,<src>]   the probe above, with a follow-up watch that reports a
                                 reset separately from no effect
  LF2_READ_WATCH_RAW=1           per-byte read profile for the existing read-watch, with
                                 its own selftest; the filtered mode hides exactly the
                                 array sweeps this needed

METHOD NOTE, paid for twice now. The first table dump came back 400 lines of untouched
defaults and looked like a clean result: the scripted route had not reached the match on
the frame it reached it on last run, because the data load does not take a fixed number of
frames. Frame-numbered probes into a match are unreliable; LF2_COOP_TABLE=live+<n> keys off
the game's own state instead, and the dump names the negative rather than printing it.
