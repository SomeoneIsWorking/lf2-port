---
id: C035
kind: claim
status: holds
created: 2026-08-20
tags: startup
depends: runtime/app/startup.c#startup_before_game_frame, runtime/overrides/boot_guest.c#boot_guest_enter_loader, runtime/video/ddraw.c
---

## Claim

Normal LF2 boot reaches the post-load mode menu without presenting the retired front end or loading picture and without synthesising any input.

## Evidence

tools/e2e.sh smoke: first script-visible screen modemenu; startup logs both direct guest transition and post-load presentation; 3 transitions and clean match/exit. runtime/app/startup.c invokes boot_guest_enter_loader before the blocking front-end body and ddraw.c suppresses presentation until top mode 2.

## What would falsify it

a zero-input normal boot presents either retired picture, requires a key/click/pad event, or reaches a screen before modemenu
