---
id: C005
kind: claim
status: holds
created: 2026-08-05
tags: coop,drop-in,input
depends: runtime/overrides.c
---

## Claim

A device pressing mid-match can join as a real player: it claims a free slot, a fighter is built there, and the pad drives it

## Evidence

Two-sided in tools/coop_dropin_test.sh (tools/e2e.sh coop_dropin): identical joins differing only in whether a direction is pressed afterwards give ~180 px of travel with animation vs <10 px and a frozen animation counter. Both arms first assert the join happened, and that assertion was checked against a negative log where it correctly does not fire.

## What would falsify it

the quiet arm moving as far as the press arm (the fighter is AI-driven, not pad-driven), or the join assertion passing on a run that never reached a match
