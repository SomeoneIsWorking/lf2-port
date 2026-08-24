---
id: C061
kind: claim
status: holds
created: 2026-08-25
tags: startup
depends: runtime/app/main.c#main, runtime/app/port_entry.c#port_entry_run, tools/routes/smoke_test.py
---

## Claim

Normal boot uses the native port entry and never dispatches LF2 PE entry 00445560 or guest WinMain 0043cf40.

## Evidence

2026-08-25: smoke passed with native-entry/data-init markers in order, modemenu first, 17/17 keys, and no retired entry/loading marker; LF2_QUIT_AFTER=1 ./run.sh passed the same zero-argument path.

## What would falsify it

A normal run logs either guest entry address, enters WinMain, shows a retired startup screen, or reaches a screen other than modemenu first.
