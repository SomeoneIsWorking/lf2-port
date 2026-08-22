---
id: I022
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

`tools/routes/parallax_jitter.py` plus `tools/routes/parallax_jitter_test.py` — authenticated
full-resolution Lion Forest scrolling-cadence pixel gate

## Validated by

The offline suite accepts synthetic distributed motion and rejects accepted stalls, missing
negative stalls, weak catch-up spikes, all-static frames, a stationary camera trace, and a wrong
stage. The final offscreen `tools/e2e.py parallax_jitter` run then serially authenticated 32/32
fired actions, Lion Forest background 1 after match initialization, a 3440x1440 native engine
target, 21 captures, and 21 moving-camera trace records in each arm. It measured accepted 0 stalls
at 109..820 changed mountain bytes versus `LF2_BG_INTEGER_RASTER=1` at 18 stalls and a 4,859-byte
catch-up jump. The saved final-run traces have the same ordered camera sequence, and the registered
route now refuses an accepted/negative trajectory mismatch before invoking the pixel analyzer; its
offline test supplies that deliberate mismatch. Each invocation also allocates a unique retained
evidence directory, and the offline negative proves an existing run directory cannot be reused.
The analyzer imports the route suite's authoritative `tools/routes/ppm.py` reader; the offline
test asserts that function identity so a private parser cannot silently return.

## Known failure modes

The ROI is Lion Forest's keyed upper-mountain band at a 3440x1440 output; it does not certify
another stage or output size. The analyzer alone consumes already-captured PPMs and cannot
authenticate their provenance; only the registered E2E wrapper proves the exact stage, native
engine target, fired movement, moving camera, capture count, and serialized accepted/negative
recipe before invoking it. It also requires the two arms' ordered camera trajectories to match.
