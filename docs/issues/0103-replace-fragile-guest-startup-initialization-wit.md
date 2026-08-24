---
id: 103
title: Replace fragile guest startup initialization with a native RE port
status: resolved
symptom: The game's initialization sequence often softlocks on a MacBook; startup can remain on a black visible window instead of reaching the mode menu.
tags: reported,startup,macos,softlock,re,override
created: 2026-08-24
updated: 2026-08-25
---

## Root cause

The previous "direct startup" still entered the PE entry point and LF2's WinMain, then
intercepted presentation and selected loader states inside that sequence. It therefore retained
the monolithic guest initialization path whose Cocoa event starvation could leave a black window.

## What was tried / dead ends

Hiding the window, suppressing the loading picture, and calling the mode-2 initializer from the
old front end all changed what was visible without replacing the entry sequence. Those paths were
removed rather than renamed.

## Resolution

`runtime/app/port_entry.c` is now the process entry after PE loading. It calls only the three game
constructors from the PE initializer table, creates the host window, performs the RE-ported
frontend/world initialization in nine bounded phases, presents one real mode-menu frame, then
hands off music and runs a native main loop. Each phase pumps Cocoa events without advancing game
state. Neither guest address `00445560` nor WinMain `0043cf40` is dispatched; the smoke route
requires the native-entry marker and rejects every retired startup marker.

### Resolution (2026-08-25)
Replaced the guest PE entry/WinMain path with port_entry_run plus nine RE-ported initialization phases and Cocoa event pumps. Smoke and ./run.sh prove modemenu-first native entry; real-Mac acceptance remains an external host check, but the softlock-prone guest sequence is no longer on the path.
