---
id: I014
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/re/stage_gaps.py -- which columns of a widescreen view each stage has no picture for; the hand-weaving work order (issues #23, #62)

## Validated by

Run against both classes. Positive: at a 978 view it names 9 of 12 stages short, and its backmost-RUN detection resolves the three multi-piece backdrops correctly (Brokeback Clif's bc1+bc2+bc3 -> 1379, CUHK's doubled floor1 -> 1594, the Templates' pic1+pic2 -> 967), which is what distinguishes 'covers the view' from 'short'. Negative: --game /nonexistent exits 2 saying it searched NOTHING rather than reporting no gaps, and Stanley Prison -- whose backmost layer LOOPS -- is reported as needing nothing rather than being silently absent. --all prints the WRONG predicate (every non-looping layer narrower than the view) next to why it is wrong, so the prop trap cannot be rediscovered as a finding.

## Known failure modes

(none recorded yet)
