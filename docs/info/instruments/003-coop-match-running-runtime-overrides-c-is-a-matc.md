---
id: I003
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

coop_match_running() (runtime/overrides.c) -- 'is a match on screen', via panel_hud_up()

## Validated by

Two home-made versions were wrong and were caught by a test failing for the right reason. 'Some object has its gate byte set' is false during character selection (entry 1's byte goes up and down there with the object at the origin); adding 'and the character-select panel is not up' still admitted a window before that panel is first drawn. The third version uses panel_hud_up(), the established in-match signal the widescreen code already depends on. Validated against both classes: the two-sided two_human_match test now reports ~1350 px of travel in the press arm and 0 px in the quiet arm, where the earlier versions reported 835 px of phantom movement for a provably stationary fighter.

## Known failure modes

(none recorded yet)
