---
id: C060
kind: claim
status: holds
created: 2026-08-24
tags: startup
depends: runtime/win32/win32.c#h_CreateWindowExA, runtime/app/startup.c#startup_load_data, runtime/overrides/boot_guest.c#boot_guest_load_data, runtime/overrides/boot_guest.c#fn_0043e940, runtime/overrides/text.c#fn_0043f010
---

## Claim

Normal LF2 startup creates a visible window and completes its one-shot resource and data initialization synchronously without entering the mode-1 loading screen.

## Evidence

The ui_escape X11/XTEST route observed the visible game window before the synchronous-load completion marker and verified normal physical input afterward. The paced smoke route reported modemenu first, reached match with 17/17 anchored keys, and found no delayed-reveal messages. A bounded zero-argument ./run.sh launch logged visible creation before synchronous loading and presented modemenu at frame 1.

## What would falsify it

A normal startup presents the retired front end or loading picture, hides the SDL window until loading completes, requires an opening input event, or reaches a screen before modemenu.
