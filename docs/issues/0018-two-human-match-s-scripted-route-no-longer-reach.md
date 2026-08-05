---
id: 18
title: two_human_match's scripted route no longer reaches the match on this machine -- the test fails at HEAD too
status: open
symptom: ctest two_human_match fails both arms with 'no live fighter at object index 1'; coop_dropin and coop_select on the same build pass
tags: test,flaky,timing,coop,two-player
created: 2026-08-05
updated: 2026-08-05
---

NOT caused by the character-selection work or the runtime/overrides split. Established by
building HEAD (c910061, before either) in a separate worktree and running the same test
against it: identical failure, both arms.

WHAT ACTUALLY HAPPENS: the route never gets into the fight. `LF2_COOP_TABLE=live+60` fires
off the game's own state -- the first frame any table entry stops being an untouched default
-- and in a failing run there is NO `coop table:` line at all, so the trigger never armed.
`coop track` says entry 1 is NOT in the world for all 43 of its samples. The run is sitting
on character selection when LF2_QUIT_AFTER=2450 ends it.

The test's own header names this as the risk: character selection asks each joined player
for a Fighter and then a Team, and player one cannot proceed until every joined player has
finished, so pad two needs presses at frames that depend on how long the data load took.
The load does not take a fixed number of frames, and this machine is evidently not the one
the frame numbers were tuned on.

THE FIX IS NOT A NEW FRAME NUMBER. Re-tuning JOIN="south:1250,south:1380,south:1560" would
work here and break on the next machine -- the same failure the `live+<n>` trigger was
introduced to remove for table dumps. The route needs to be driven off game state the way
the trigger is: press when the screen says it is waiting, not at frame 1380.

Left OPEN deliberately. The coverage it provides is real (two humans in a match, which
controller_2p does not reach) and dropping the test would be worse than a red one.
