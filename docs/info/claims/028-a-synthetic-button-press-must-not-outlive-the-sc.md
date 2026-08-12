---
id: C028
kind: claim
status: holds
created: 2026-08-12
tags: re,input
depends: runtime/overrides/input.c
---

## Claim

A synthetic button press must not outlive the screen it was issued on: fn_00431c70 clears the per-player held-button latch at 0x00451320 on every way out of a match, and fn_00431b70 edge-detects against it, so a press still held after a screen change reads as a fresh press to the next menu.

## Evidence

fn_00431b70 and fn_00431c70 decompiled; issue #22 is the observed consequence. tools/e2e.sh exit_to_menu passes with the screen scope in input_synth_confirm/synth_active and FAILS (lands on character selection) with the check compiled out -- run against both classes.

## What would falsify it

a menu gather other than fn_00431b70 that accepts a level rather than an edge, which would make the scope insufficient rather than merely necessary
