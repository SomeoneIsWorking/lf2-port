---
id: C039
kind: claim
status: falsified
created: 2026-08-21
tags: startup
depends: runtime/overrides/boot_guest.c#fn_00419e40, runtime/overrides/boot_guest.h#boot_guest_target_mode, runtime/app/startup.c#startup_present_enabled, runtime/video/ddraw.c
reconfirmed: 2026-08-22
verified_at: 2026-08-22 12:41:09
falsified_on: 2026-08-24
---

## Claim

Normal LF2 boot constructs the world in loader mode and reveals the first post-load mode-menu frame without launcher input

## Evidence

fn_00419e40 override preserves the original constructor then sets initial top mode 1; startup.c suppresses presentation and keeps SDL hidden through the real loader. tools/e2e.py smoke passes with modemenu as the first script-visible screen, and bounded zero-argument ./run.sh logs constructor loader state, data-loaded mode menu, first menu frame presented, then modemenu@4 with no input script.

## What would falsify it

a zero-input default boot presents the launcher, loading picture, or black window; requires a key/click/pad event; or changes top mode through a post-construction frontend branch

## Re-confirmed 2026-08-21

A bounded zero-argument ./run.sh invocation after commit 7817223 logged constructor loader state, data-loaded mode menu, first menu frame presented, then modemenu@4 without an input script.

## Re-confirmed 2026-08-22

Bounded zero-argument ./run.sh on the current tree again logged constructor local-loader state, data-loaded mode menu, first menu frame presented, and modemenu@4 with no input script. boot_mode also proves only exact retired top mode 0 is routed to loader while unknown values remain visible.

## FALSIFIED 2026-08-24

User observation and the fn_004246b0/fn_0041bc90 call graph disproved the claim's premise: startup hid SDL and declared READY before the actual mode-2 data initializer ran. The hidden-window phase machine was presentation suppression, not a loading-screen bypass.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
