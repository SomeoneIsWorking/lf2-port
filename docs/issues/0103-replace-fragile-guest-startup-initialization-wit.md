---
id: 103
title: Replace fragile guest startup initialization with a native RE port
status: closed
symptom: The game's initialization sequence often softlocks on a MacBook; startup can remain on a black visible window instead of reaching the mode menu.
tags: reported,startup,macos,softlock,re,override
created: 2026-08-24
updated: 2026-09-09
---

## Root cause
The previous "direct startup" still entered the PE entry point and LF2's WinMain, then
intercepted presentation and selected loader states inside that sequence. It therefore retained
the monolithic guest initialization path whose Cocoa event starvation could leave a black window.

## What was tried / dead ends
Hiding the window, suppressing the loading picture, and calling the mode-2 initializer from the
old front end all changed what was visible without replacing the entry sequence. Those paths were
removed rather than renamed.
