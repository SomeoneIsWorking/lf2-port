---
id: I004
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

LF2_VIRTUAL_PAD scripted routes (runtime/gamepad.c) -- button:<frame> or button@<screen>[+n], with virtual_pad_report() naming the screens reached

## Validated by

Validated by the case it was built for and by a case it must NOT pass. Positive: with a physical Xbox pad attached, a scripted route reached NO screen and virtual_pad_report() said 'screens reached -- NONE' plus 'at least one scripted press NEVER FIRED' -- which is how the physical-pad slot-0 hijack was found rather than being blamed on CPU load. Negative control: with the physical pad ignored, the same script reports charselect@1083, overlay@1921, match@2142 and a state-keyed press at @match+30 lands in the match. So it distinguishes 'the route worked' from 'the route never got there', which is exactly the pair that a silent script confuses.

## Known failure modes

**It lied about NEVER FIRED for as long as the screen-keyed form existed, and was fixed on
2026-08-06 (issue #24, commit 298e4eb).** The "screens reached" half was always sound; the
"at least one scripted press NEVER FIRED" half was on in EVERY screen-keyed run, clean ones
included, because a single sticky flag conflated "cannot fire yet, its screen has not
appeared" -- true of every screen-keyed press on frame 0 -- with "never fired".

Note where it hid: the validation above is a real positive and a real negative, and the
negative was checked for the screens it reached and NOT for the absence of the warning. A
control that only confirms the half you are looking at leaves the other half unvalidated.
Any conclusion drawn from a NEVER FIRED line in output from before that commit is worthless
in both directions; the screens-reached line from the same runs is fine.

Now: one line per script with its denominator ("21 of 21 presses fired"), always printed, and
each press that did not fire named with its own text out of the script. Re-validated against
three classes -- all-reachable, a press keyed to a screen the run never reaches, and a typo'd
button name -- and the all-reachable case is the one that used to fail.
