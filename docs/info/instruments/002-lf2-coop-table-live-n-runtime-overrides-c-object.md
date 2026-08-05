---
id: I002
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

LF2_COOP_TABLE=live+<n> (runtime/overrides.c) -- object table dump keyed off game state

## Validated by

Replaces a frame-numbered dump that FAILED silently: the scripted route does not reach the match on a fixed frame, so a dump at frame 2400 came back 400 untouched defaults and read as a result. The dump now says NOT A MATCH outright when no entry differs from the initialised default, and  fires off the first non-default entry instead of a frame number.

## Known failure modes

(none recorded yet)
