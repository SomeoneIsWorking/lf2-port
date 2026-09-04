---
id: C028
kind: claim
status: falsified
created: 2026-08-12
updated: 2026-08-17
tags: re,input
depends: runtime/overrides/input.c
falsified_on: 2026-08-17
---

## Claim

A synthetic button press must not outlive the screen it was issued on: fn_00431c70 clears the per-player held-button latch at 0x00451320 on every way out of a match, and fn_00431b70 edge-detects against it, so a press still held after a screen change reads as a fresh press to the next menu.

## Evidence

fn_00431b70 and fn_00431c70 decompiled; issue #22 is the observed consequence. tools/e2e.py exit_to_menu passes with the screen scope in input_synth_confirm/synth_active and FAILS (lands on character selection) with the check compiled out -- run against both classes.

## What would falsify it

a menu gather other than fn_00431b70 that accepts a level rather than an edge, which would make the scope insufficient rather than merely necessary

## Obsolete (2026-08-17)

The instrument this claim regulated is DELETED. LEAVE MATCH now calls the game's own exit
code directly (screens.c's guest_end_match / guest_overlay_exit, issue #22): no synthetic
button press exists any more, so the edge-vs-screen rule it stated is moot for the port. The
underlying game properties it rested on (fn_00431c70 clears the latch, fn_00431b70
edge-detects) are still true and still written in screens.c's header comment. The claim is
kept only as the record of why the deleted scope existed.

## FALSIFIED 2026-08-17

The instrument the claim regulated (input_synth_confirm / any_playing_device) is DELETED: LEAVE MATCH now calls the game's own exit code directly (screens.c guest_end_match/guest_overlay_exit, issue #22), so no synthetic button press exists for the screen-scope rule to apply to. The underlying game properties (fn_00431c70 clears the latch, fn_00431b70 edge-detects) still hold and are recorded in screens.c.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
