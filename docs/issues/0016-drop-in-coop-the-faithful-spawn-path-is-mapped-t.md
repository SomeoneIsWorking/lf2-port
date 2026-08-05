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
