---
id: 24
title: Scripted-route report cried NEVER FIRED on every run, including clean ones
status: resolved
symptom: every LF2_VIRTUAL_PAD run using the button@screen form ends with 'at least one scripted press NEVER FIRED', even when all three screens were reached and every press went down
tags: reported,instrument,testing,virtual-pad
created: 2026-08-06
updated: 2026-08-06
---

OBSERVED while using the route report as evidence for issue #22, and it is an INSTRUMENT
fault rather than a route fault: the warning was on in a run where all fifteen presses
demonstrably fired (charselect@906 overlay@1746 match@1968, and the exit-to-menu chain ran
to completion off the last three presses).

CAUSE, exactly. runtime/gamepad.c had one sticky flag:

    if (un) { script_unresolved = 1; continue; }

item_frame() sets `un` for a press whose screen has not appeared YET -- its own comment says
so: "Returns -1 when it cannot fire YET (its screen has not appeared), which is different
from never." play_script() runs every frame from frame 0, where no screen has appeared, so
the flag was set on the first frame of every screen-keyed route and never cleared. The
report conflated NOT-YET with NEVER.

WHY IT MATTERED more than a stray line: docs/running.md and CLAUDE.md both told the next
session to trust that warning ("A press whose screen never appears NEVER FIRES, and the run
says so at exit"). A warning that is always on is a warning that gets read as noise, and the
failure it guards -- a press aimed at a screen the run never reached -- is precisely the one
that reads from outside as "the feature did nothing".

FIXED by making fired-ness a property of the press instead of an inference: one flag per
item in script order, set when the button actually goes down. The report now always prints
its denominator ('N of M presses fired') and NAMES each press that did not, with its own
text from the script. A typo'd button name -- silently skipped before, which is the same
failure one level up -- is now its own reported state.

VALIDATED AGAINST BOTH CLASSES, run rather than reasoned:
  A  south:100,south:200                      -> '2 of 2 presses fired', no warning
  B  ...,south@match+10 in a run ending at    -> '2 of 3', names press 2 `south@match+10'
     charselect
  C  sooth:100,south:200                      -> '1 of 2', names press 0 as not a button
                                                 name this build knows
Before the fix, case A printed the warning too, which is what made the report worthless.
