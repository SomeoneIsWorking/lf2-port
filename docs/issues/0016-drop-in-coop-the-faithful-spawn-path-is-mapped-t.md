---
id: 16
title: Drop-in coop: the faithful spawn path is mapped; the clone probe must be replaced by it
status: resolved
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
    that cap is a port limitation, and changing it has to keep tools/routes/controller_2p_test.sh
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

VERIFIED TWO-SIDED, and it is now a test (tools/routes/coop_dropin_test.sh, ctest `coop_dropin`):
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
placed in slot 4 is drawn by the game's own name plate as "5". tools/routes/controller_2p_test.sh
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

A GAP IN THE EXISTING TESTS, found on the way: tools/routes/controller_2p_test.sh proves a second
pad JOINS at character selection -- it asserts the word "Computer" is not drawn -- but it
quits at frame 1900 and never reaches a match. So "a second human's fighter is driven in a
match" had never been tested. tools/routes/coop_dropin_test.sh now covers that shape for a
drop-in joiner (two-sided, press vs quiet); the character-select route for two humans is
still uncovered, and a route that reaches a match with two joined humans is the thing to
build for it -- the scripted attempts here stalled on character selection, where both
joined players have to confirm before player one can proceed.

### Note (2026-08-05)
TEST GAP CLOSED, and closing it found a defect in the drop-in feature itself.

tools/routes/two_human_match_test.sh (ctest `two_human_match`) now reaches a match with TWO HUMAN
players and asserts, two-sided, that pad two drives its fighter: ~1350 px of travel with a
direction pressed, 0 px without. It also asserts player two's fighter is at OBJECT INDEX 1,
which is the dynamic half of a claim that was only static before.

THE ROUTE, since two earlier attempts stalled: character selection asks each joined player
for a Fighter and then a Team, and player one cannot proceed until every joined player has
finished. Pad two therefore needs three presses, not one -- join at 1250 (the window
controller_2p_test measured), then 1380 and 1560. With only the join press the screen sits
there with both players joined and nothing happening, which is not obviously a missing
press until you look at the actual screen: player two's rows are drawn in cyan, still being
chosen, while player one's are white and done.

THE DEFECT, found because the test failed for the right reason. The drop-in fires when a
device claims a slot "while a match is running", and my test for that was "some object has
its gate byte set". That is FALSE as a match test: tracking entry 1 through character
selection shows its gate byte going up and down there with the object still at the origin.
The drop-in never actually misfired in any run -- the byte happened to be clear at the
instant a joining pad was seen -- but that is luck, and a join screen that spawned a fighter
would be a real defect.

A second attempt (also exclude panel_charselect_up) was still wrong: there is a window
before that panel is first drawn where a gate byte is already set, and a position sampled in
it enters a movement measurement as a jump from x=0, which is what made the test's quiet arm
report 835 px of movement for a fighter that was provably stationary.

THE FIX IS NOT A THIRD HEURISTIC. The port already had the signal: panel_hud_up() is the
in-match HUD strip, drawn only while the world view is up, and the widescreen code depends
on it for exactly this distinction. coop_match_running() uses it now. The lesson is the one
worth keeping: two home-made screen tests were invented before checking what the codebase
already had for the same question.

+0x354 IS NOW READABLE AS THE OBJECT'S OWN TABLE INDEX, on four consistent observations
rather than the single imitation it was: player one at index 0 reads 0, player two at index
1 reads 1, the computer opponent at index 11 reads 11, and a non-fighter object at index 50
reads 99 -- the same 99 an untouched record carries. Still not derived from code, but it is
a rule that four cases agree on instead of one case copied.

### Note (2026-08-05)
THE LATE-JOINER DESIGN QUESTIONS, settled where they can be and NAMED where they cannot.

1. WHICH SLOT. The drop-in no longer simply takes the slot the device-claim loop picked.
   That loop only avoids slots another DEVICE holds, while the game's own roster has its own
   opinion: a slot the character-select screen filled with a computer carries a non-zero
   device selector, and that computer's fighter is already on the stage at a high index.
   Joining such a slot does not replace it -- the match just gains a fighter. So a slot the
   roster considers empty (selector 0) is preferred now, and taking a computer's slot
   because nothing else is free is ANNOUNCED rather than done quietly.

2. WHICH CHARACTER. Split out: LF2_COOP=1 enables the feature, LF2_COOP_CHAR=<object id>
   picks the character and defaults to 1. Overloading one variable with both was awkward and
   made "on" and "Bandit" the same value.

   NOT solved, and deliberately not faked: picking at RANDOM would need the game's own
   roster of playable characters, and that is not located. The registry at this+2004 holds
   every object -- fighters, weapons and effects alike -- and nothing found so far separates
   them. The observed ids do fall in suggestive ranges (1-11, 30-39, 50-52 against 100+),
   but hard-coding a range is a magic constant, and it would break on any mod that adds
   characters. Finding the roster the character-select screen iterates is the next RE step
   for this, and it is small: that screen draws a portrait per selectable character, so
   whatever it walks IS the roster.

3. A TEST THAT LIED, caught and replaced rather than tuned. coop_dropin measured "the pad
   drives the joined fighter" as DISPLACEMENT, and that is the wrong signal for a fighter
   that joins mid-fight: it lands beside the brawl and gets knocked about. The idle arm
   drifted 56 px in one run and 69 in the next, against ~120 for the driven arm -- no
   threshold separates those, and the first version passed twice by luck before failing.
   Picking a threshold that happened to pass would have been a test that lies.

   The claim splits in two instead, and each half is measured where it is clean:

     coop_dropin        the pad's input reaches the JOINED fighter's record (the watch
                        accumulates every button seen since the join, so a press cannot fall
                        between samples)
     two_human_match    the game turns input in a player record into movement -- on a
                        fighter at its own start position, where displacement is clean:
                        ~1350 px against 0

   Together they cover "the pad drives the fighter it joined", and neither is confounded by
   the fight moving things on its own. Three consecutive runs of the new coop_dropin pass,
   where the displacement version flipped between runs.

### Note (2026-08-05)
THE ROSTER IS FOUND, and the last open design item is closed properly rather than by a
range check.

A data block's TYPE is at +1784, next to its id at +1780. Located against the game's own
data.txt, whose <object> section declares an id AND a type for each entry -- and the
registry at this+2004 IS that list, in file order, all 65 of them. The offset was required
to match the declared type on EVERY entry rather than on a sample: +1784 is the only offset
in the first 2048 bytes that does, and it does so at byte, word and dword width alike.

  type 0 = character.  The rest are weapons, throwables, effects and the criminal.

So the roster of playable characters is "registry entries whose type is 0, less the
template at id 0" -- and the game agrees: that comes to 23, which is LF2's selectable
roster. The template is excluded by ID, because that is what it is (data\template.dat, the
template object, which character selection does not offer), not by an offset that happened
to work.

A late joiner with no LF2_COOP_CHAR now gets a character from that roster. The pick is
DETERMINISTIC, from the frame the join lands on: a joiner wants a varied character, not an
unpredictable one, and a run that cannot be reproduced is worse to debug than one that
always picks the same fighter. If the roster cannot be read the join is REFUSED with the
reason, rather than falling back to a hard-coded id -- a silent fallback would make a broken
registry read look like a working feature.

Why this matters beyond the feature: the alternative was hard-coding the id ranges the
characters happen to occupy (1-11, 30-39, 50-52). That would have worked on this data.txt
and broken on any mod that adds a character, and nothing in the code would have said why.

A BUG CAUGHT WHILE WRITING IT, worth recording because it was invisible: the first version
of the "no character, do not join" path used `break`. That sits inside the per-device loop,
so it would have skipped the remaining devices' button bookkeeping for the frame -- a
keyboard going dead for a frame whenever a pad failed to join. Restructured so no control
flow escapes the loop.

### Resolution (2026-08-05)
DONE. Drop-in coop works end to end and every piece of it is located rather than guessed.

  the gate         a byte per object index at 0x00458b04; fn_004064d0 tests it
  the table        this+404, 400 object pointers on a 0x420 stride, players at 0..7
  the mapping      player slot i IS object index i -- the game's gather walks the
                   device-selector table and the pointer table in lockstep over eight entries
  the spawn        fn_004061d0 resets the record, +872 points at the data block
  the registry     this+2004, data.txt's <object> list in file order
  the block        id at +1780, type at +1784 (0 = character)
  the roster       type-0 entries less the template at id 0 -- 23, which is LF2's own

The feature: LF2_COOP=1 turns it on, LF2_COOP_CHAR pins a character or one is taken from the
roster. A device pressing for the first time while a match is running claims a slot the
game's roster considers empty, gets a fighter built there, and drives it.

Two regression tests, split so each half is measured where its signal is clean:
  coop_dropin       the pad's input reaches the joined fighter's record
  two_human_match   input in a player record becomes movement (~1350 px against 0)

Left deliberately as they are, both documented in the code:
  - it stays OPT-IN, because a late joiner gets no character-select screen; the character is
    chosen for them, and that is a product call rather than a missing mechanism.
  - joining a computer's slot when no roster-free slot remains does not replace that
    computer: its fighter is at its own high index and stays. The run says so.
  - +0x354 is read as the object's own table index on four consistent observations
    (0->0, 1->1, 11->11, non-fighter->99) rather than derived from code.

WHAT THIS ISSUE COST, worth keeping for the next investigation of the same shape. Four
things were believed and wrong, and each was caught by measurement rather than by review:
  1. the mystery object was "off the 0x420 grid" -- arithmetic error, it was entry 11
  2. +0x364 was "the chosen character" -- it is the character-select cursor
  3. a spawned fighter's HUD portrait was "wrong" -- it was right; a screenshot was misread
  4. player N's fighter was "not always index N" -- it is, for humans; computers are the
     ones unbound by the gather
Three more were instruments that would have lied: a table dump that read 400 untouched
defaults as a result, an A/B across runs of a game that randomises, and a movement test on a
fighter being knocked about.
