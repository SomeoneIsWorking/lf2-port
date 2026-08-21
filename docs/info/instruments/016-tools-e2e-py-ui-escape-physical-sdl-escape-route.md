---
id: I016
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

tools/e2e.py ui_escape — physical SDL Escape route

## Validated by

Negative before the Escape fix: the focused X11/XTEST route logged SDL Escape down/up but
RmlUi reported 0 opens. Current positive: the same route opens/closes the document with
physical Escape, activates Graphics with a real window-relative XTEST click, activates focused
content with the configured keyboard Attack action, then proves that key reaches the game once
the modal closes. It distinguishes physical pointer/keyboard failure from scripted input and
raw-key-only navigation.

## Known failure modes

(none recorded yet)
