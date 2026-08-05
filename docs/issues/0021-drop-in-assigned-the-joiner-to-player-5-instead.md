---
id: 21
title: Drop-in assigned the joiner to Player 5 instead of Player 2
status: resolved
symptom: a second human dropping into a running match got slot 4 (shown as player 5) while player 2's HUD box sat empty
tags: reported,coop,drop-in,players
created: 2026-08-05
updated: 2026-08-05
---

REPORTED IN PLAY, and the cause was a deliberate choice of mine that was wrong.

The joiner skipped any slot whose DEVICE SELECTOR (0x00450b4c + 4i) was non-zero, on the
reasoning that a non-zero selector means character selection listed a computer there. The
reasoning is correct; the conclusion was not. That computer's fighter sits at its own high
table index and is never reached through the input gather, so its selector says nothing
about whether slot 1 can hold a human. In a one-human match every low slot carries a
selector, so the joiner walked up to slot 4 and was drawn as "5".

Visible in scratch/logs/select3.log as:

    coop: slot 1 is on the game's roster already (selector 2), taking empty slot 4 instead

FIXED: the joiner takes the LOWEST slot with no fighter in it (gate byte 0) that no other
device holds. What disqualifies a slot is a fighter occupying it, not the roster's opinion
about it. Joining a slot the roster listed as a computer does not replace that computer --
the match gains a fighter, which is what a drop-in is -- and that case is now announced
rather than avoided.
