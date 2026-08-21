---
id: I017
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

tools/e2e.py texture_cache — long Stage-mode GPU texture-cache churn route

## Validated by

Trusted after showing both answers on 2026-08-21: the pre-fix engine printed its 512-texture pool FULL diagnostic and dropped later art; the LRU build filled 512 entries, performed 168 evictions, and reported zero failed requests/dropped art. It also requires a reached Stage match and nonzero evictions, so a short or wrong-screen run fails.

## Known failure modes

(none recorded yet)
