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

### Note (2026-08-05)
CORRECTION: the HUD portrait of a spawned fighter is NOT wrong. The note above claiming it
was is withdrawn -- it came from eyeballing a screenshot and matching the wrong sprite on
the stage to the wrong bar in the HUD.

Measured properly, as a two-sided discriminator, each comparison INTERNAL to one run so the
VS-mode randomiser cannot account for it:

  - spawn id 1 at entry 13 and id 1 at entry 14  -> HUD positions 4 and 5 draw the SAME
    portrait
  - spawn id 1 at entry 13 and id 52 at entry 14 -> HUD positions 4 and 5 draw DIFFERENT
    portraits

So the portrait follows the object's data block at +872, which the spawn already sets. It
was never a gap.

That also disposes of the +0x364 question: two spawns of the SAME id with +0x364 forced to
0 and to 5 draw identical portraits, so the character-select cursor is not what the HUD
reads. (scratch/screenshots/ab_hud.png and ab2_full.png.)

METHOD, because this cost a round trip: the first A/B was run as two SEPARATE processes
with one variable changed between them. VS mode randomises the characters already on the
stage, so all three portraits differed and the one variable explained none of it. Comparing
two spawns inside ONE run is what makes the comparison matched, and LF2_COOP_SPAWN now
takes a semicolon-separated LIST for exactly that. A screenshot A/B across runs of a game
that randomises anything is not an experiment.

Also added, for the same reason: LF2_COOP_SHOT=<n> captures the frame n frames after the
spawn, via gfx_request_frame_dump(). A capture aimed at a fixed frame number and a probe
that fires off game state disagree whenever the load takes a different number of frames --
one arm of the first A/B never reached a match at all, and its screenshot would have been
compared as though it showed the same experiment.

### Note (2026-08-05)
DROP-IN COOP WORKS END TO END, opt-in behind LF2_COOP=<object id>.

A device that presses for the first time while a match is ALREADY running claims a free
player slot, and because character selection is over there is no fighter waiting for it, so
one is built there: the faithful spawn (registry lookup, fn_004061d0 reset, +872, position,
gate byte) plus the slot's device-selector entry and its bit in the joined mask.

VERIFIED TWO-SIDED, and it is now a test (tools/coop_dropin_test.sh, ctest `coop_dropin`):
the same join run twice, differing only in whether the pad presses a direction afterwards.

  press  the joined fighter travels ~180 px and its animation counter cycles
  quiet  it drifts <10 px while it lands and then stays put

"A fighter appeared" was never the claim -- one wandering under its own AI would satisfy a
one-sided check just as well. Both arms also assert the join HAPPENED first, because a run
whose route never reached the match would otherwise sail through the `quiet` assertion: a
fighter that does not exist does not move either. That assertion was checked against a
negative log and does not fire on it.

THE FOUR-PLAYER CAP IS GONE, on evidence rather than optimism. The gather looped `i < 4`
against a device-selector table of eight entries. It is PLAYER_SLOTS = (DEVSEL_END -
DEVSEL) / 4 now, so the count comes from the table rather than from a literal, and a fighter
placed in slot 4 is drawn by the game's own name plate as "5". tools/controller_2p_test.sh
still passes, which is what the cap was left alone for.

NEW OPEN QUESTION, and it is a real one: PLAYER N'S FIGHTER IS NOT ALWAYS OBJECT INDEX N.
In an ordinary match the joined mask reads 3 -- two players -- while the computer opponent's
fighter is at index 11 and object index 1 is EMPTY. The port's gather reaches a player's
fighter as this+404+4i, which works whenever a fighter is at index i, and that is what makes
the drop-in work at all. But it means the game can place a player's fighter off its own
index, and nothing here explains when or why. It matters for anything attributing score or
a HUD row. Related: in the observed drop-in the claimed slot ALREADY had its selector and
its mask bit set, so both of those writes were no-ops and the spawn alone did the work --
which is a hint that the mask and selector describe the character-select roster rather than
what is on the stage.

STILL OPEN: what +0x354 means, and which free index the game itself picks (it gave its
computer opponent 11 with 1..10 free).

DESIGN QUESTION, deliberately not answered: which character a late joiner gets. There is no
character select to show them mid-match, so LF2_COOP takes the object id and defaults to 1
(Bandit). That is why the feature is opt-in rather than on -- the mechanism is settled, the
interface is not.

### Note (2026-08-05)
RESOLVED: player slot i IS object index i, and the previous note's "counter-example" was a
misreading of what the joined mask means.

The game's own gather settles it statically -- fn_00419a60__orig walks the device-selector
table and the object table IN LOCKSTEP:

    EBP = 0x450b4c              // &devsel[0]
    EAX = this + 404            // &table[0]
  loop:
    if ((int32_t)LD32(EBP) <= 0) goto next
    ESI = LD32(EAX)             // the object this slot's control config drives
    ...
  next:
    EBP += 4;  EAX += 4;  ECX += 1
    if (EBP < 0x450b6c) goto loop

So a HUMAN player's fighter must be at its own index -- there is no other route by which the
game could deliver its buttons -- and EIGHT is the game's own bound, read off the loop
rather than assumed. The port's mapping is the game's mapping.

WHAT THE ODD READING ACTUALLY WAS: a computer's fighter is not bound by that loop, because
its AI writes buttons straight into the object it drives and never goes through the gather.
So it can live at any index, and one does: index 11. The joined mask reading 2 while object
index 1 is empty is therefore consistent -- the mask tracks the CHARACTER-SELECT ROSTER, in
which a slot marked "Computer" counts as taken, not what occupies the player object slots.
No mechanism is missing.

CONSEQUENCE, worth knowing before it surprises someone: joining into a slot the game filled
with a computer does NOT replace that computer. Its fighter is at its own high index and
stays on the stage, so the match GAINS a fighter rather than swapping one. In the verified
drop-in run the stage ends up with three fighters: the human at index 0, the computer at
index 11, and the joiner at index 1. Whether that is the wanted behaviour is a design
question, not a defect.

A GAP IN THE EXISTING TESTS, found on the way: tools/controller_2p_test.sh proves a second
pad JOINS at character selection -- it asserts the word "Computer" is not drawn -- but it
quits at frame 1900 and never reaches a match. So "a second human's fighter is driven in a
match" had never been tested. tools/coop_dropin_test.sh now covers that shape for a
drop-in joiner (two-sided, press vs quiet); the character-select route for two humans is
still uncovered, and a route that reaches a match with two joined humans is the thing to
build for it -- the scripted attempts here stalled on character selection, where both
joined players have to confirm before player one can proceed.
