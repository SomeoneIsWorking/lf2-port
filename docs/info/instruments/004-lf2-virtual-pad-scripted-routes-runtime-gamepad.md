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

(none recorded yet)
