---
id: 16
title: Drop-in coop: the faithful spawn path is mapped; the clone probe must be replaced by it
status: open
symptom: a device that is not assigned to a player still cannot join a stage that is already running
tags: coop,drop-in,re,groundwork
created: 2026-08-05
updated: 2026-08-05
---

Groundwork with a concrete route, not a bug. Issue #15 found the GATE (the byte at
0x00458b04 + index) and proved a fighter can be added to a running match. It got there by
CLONING a live fighter's record, which is a probe, not a mechanism: it copies the source's
character, HP and everything else, so it can only ever duplicate a fighter already present.

The game's own spawn is inlined in fn_0041bc90 (around 0x004211db in the lifted C) and reads
as:

    reg   = LD32(this + 2004)            // pointer to the object-data registry
    count = LD32(reg + 81273728)         // its entry count, at a fixed offset
    for (i = 0; i < count; i++)          // find the data block for the wanted object
        if (LD32(LD32(reg + 4i) + 1780) == <wanted>) break;
    data = LD32(reg + 4i)
    obj  = LD32(this + 404 + 4k)
    ECX = obj; fn_004061d0()             // __thiscall reset: zeroes the record
    obj->872 = data                      // +0x368 IS the object-data pointer
    obj->796 = data->144
    obj->88 / +96 / +104 = <constants from .rdata>
    this[4 + k] = 1                      // the gate
    obj->16 / +20 / +24 / +104 / +96 = <position, from a source object>

That names every piece a faithful drop-in needs:

  - fn_004061d0 is the object's own reset, called __thiscall with ECX = the record. It is
    what replaces the clone.
  - this+2004 (+0x7d4) is the registry pointer. It is the dword the read profile in #15
    found hot -- about 94 reads a frame -- and now has a name.
  - +0x368 / +872 is the object-data pointer, which is why idle entries past the player
    slots all share one value: they all point at the same default block.

WHAT IS NOT ESTABLISHED, and must be measured before it is written:

  - what field 1780 of a data block actually is. fn_0041bc90 compares it against 0x3e7
    (999) here; fn_004064d0 compares the SAME field against 7 and 8. So it is not obviously
    "the character id", and reading it as one is exactly the kind of guess that produces a
    magic constant. Dump the registry and look.
  - which free index to take. The game gave its computer opponent index 11 with 1..10 free,
    so "the next free slot" is not what it does, and the reason matters.
  - how the new fighter binds to the joining device. The ported gather in fn_00419a60 walks
    `i < 4` over the device-selector table at 0x00450b4c, against a 400-entry object table;
    that cap is a port limitation, and changing it has to keep tools/controller_2p_test.sh
    passing.

The instruments from #15 are all still there and are the ones to use:
LF2_COOP_TABLE=live+<n>, LF2_COOP_PAIR, LF2_COOP_SPAWN, LF2_COOP_REFS, LF2_READ_WATCH_RAW.

### Note (2026-08-05)
DONE: the clone is gone. The spawn now builds the fighter the way the game does, and it
spawns a character that is not on the stage -- which a clone could never do.

`LF2_COOP_SPAWN=<index>[,<object id>]` (id defaults to 1, Bandit) does:

    data = the registry entry whose block carries object id <id>
    obj  = LD32(this + 404 + 4*index)
    ECX = obj; fn_004061d0()          // the game's own __thiscall reset
    obj->872 = data;  obj->796 = data->144
    position <- +16/+20/+24 and the doubles at +88/+96/+104 of a live fighter, offset
    obj->852 = index
    this[4 + index] = 1               // the gate

VERIFIED: spawning id 1 into a running match whose two fighters are other characters puts
a BANDIT on the stage -- it animates, walks left across the screen over 300 frames, and
draws. scratch/screenshots/faithful.png. The negative control is run too: an id that is not
in the registry (777) is REFUSED with the count it searched, rather than silently spawning
nothing.

FIELD 1780 IS THE data.txt OBJECT ID, settled against the game's own data file rather than
by inference. All 65 registry entries carry an id that appears in game/data/data.txt, with
no exceptions; the only two data.txt ids absent from the registry are 3 and 12, and both of
those are BACKGROUNDS (`bg\...`), not objects. That also explains fn_004064d0 comparing the
same field against 7 and 8: those are Firen and Freeze, not a type code, and the 999 in
fn_0041bc90's spawn is object id 999, which data.txt has.

CORRECTION: +0x364 is NOT the character. It is the character-select slot cursor, which the
port already documented and uses in charselect_mouse. Reading it as the chosen character
came from one coincidence -- entry 0 held 10 while pointing at object id 10 -- and the
computer opponent breaks it: it holds 21 while pointing at object id 1. The character is
the data block at +872 / its id at +1780.

STILL OPEN, and newly visible now that a spawn can pick its own character:

  - THE HUD PORTRAIT IS WRONG for a spawned fighter. It gets a bar, but the portrait drawn
    is not the character it is. So the HUD reads identity from a field the spawn does not
    set -- plausibly the char-select cursor at +0x364, which fn_004061d0 zeroes. Finding
    that field is the next small step and it is worth doing before binding a device, since
    a joining player picking a character will need it anyway.
  - +0x354 still has no established meaning. fn_004061d0 resets it to 99, one spawn site
    copies it from the spawning object, and the game's own computer opponent holds its own
    index. Setting it to the destination index is imitation of that one observation, and it
    is what produces the HUD bar. Marked as such in the code.
  - Which free index to take. The game gave its computer opponent index 11 with 1..10 free.
  - Binding the new fighter to the joining device: the gather still walks `i < 4` over the
    device-selector table at 0x00450b4c against a 400-entry object table.
