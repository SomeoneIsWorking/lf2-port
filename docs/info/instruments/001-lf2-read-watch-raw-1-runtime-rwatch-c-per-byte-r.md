---
id: I001
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

LF2_READ_WATCH_RAW=1 (runtime/rwatch.c) -- per-byte read profile over a watched span

## Validated by

Ships a selftest (LF2_READ_WATCH_SELFTEST=1 with RAW=1): four reads of one byte must come back as exactly '+00c 4' with every other byte 0, and it does. Separately validated against BOTH classes on real data -- an idle object reports exactly one hot byte out of 1056, a live one reports ~200 -- so it is not uniformly reporting either everything or nothing.

## Known failure modes

(none recorded yet)
