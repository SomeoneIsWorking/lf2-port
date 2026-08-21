---
id: C039
kind: claim
status: holds
created: 2026-08-21
tags: startup
depends: runtime/overrides/boot_guest.c#fn_00419e40, runtime/app/startup.c#startup_present_enabled, runtime/video/ddraw.c
reconfirmed: 2026-08-21
verified_at: 2026-08-21 11:37:26
---

## Claim

Normal LF2 boot constructs the world in loader mode and reveals the first post-load mode-menu frame without launcher input

## Evidence

fn_00419e40 override preserves the original constructor then sets initial top mode 1; startup.c suppresses presentation and keeps SDL hidden through the real loader. tools/e2e.sh smoke passes with modemenu as the first script-visible screen, and bounded zero-argument ./run.sh logs constructor loader state, data-loaded mode menu, first menu frame presented, then modemenu@4 with no input script.

## What would falsify it

a zero-input default boot presents the launcher, loading picture, or black window; requires a key/click/pad event; or changes top mode through a post-construction frontend branch

## Re-confirmed 2026-08-21

A bounded zero-argument ./run.sh invocation after commit 7817223 logged constructor loader state, data-loaded mode menu, first menu frame presented, then modemenu@4 without an input script.
