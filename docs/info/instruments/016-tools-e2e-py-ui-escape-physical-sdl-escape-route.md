---
id: I016
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

tools/e2e.py ui_escape — physical SDL Escape route

## Validated by

Negative before the fix: the focused X11/XTEST route logged SDL Escape down/up but RmlUi
reported 0 opens. Positive after the edge-latch fix: the same route logs two physical down
events, menu commands at active=0 then active=1, and exactly one rendered RmlUi opening. It
therefore distinguishes physical keyboard failure from scripted-key success.

## Known failure modes

(none recorded yet)
