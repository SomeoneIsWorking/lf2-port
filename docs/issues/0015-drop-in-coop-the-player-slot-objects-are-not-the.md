---
id: 15
title: Drop-in coop: the player slot objects are not the fighters, so joining is not a spawn OR a flag in them
status: investigating
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
