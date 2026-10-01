---
id: 16
title: Drop-in coop: the faithful spawn path is mapped; the clone probe must be replaced by it
status: closed
symptom: a device that is not assigned to a player still cannot join a stage that is already running
tags: coop,drop-in,re,groundwork
created: 2026-08-05
updated: 2026-09-09
---

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

(full record: git history)
