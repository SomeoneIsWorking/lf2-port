---
id: C007
kind: claim
status: holds
created: 2026-08-05
tags: coop,testing,method
depends: the recorded coop_dropin runtime scenario
---

## Claim

Displacement cannot test whether a pad drives a fighter that joined mid-fight; the claim has to be split

## Evidence

coop_dropin measured pad control as x displacement over 120 frames. A fighter that joins mid-match spawns beside the ongoing brawl and is knocked about: the idle arm drifted 56 px in one run and 69 in the next, against ~120 px for the driven arm. The test passed twice before failing, so the flakiness was not visible at first. Replaced by two measurements each taken where it is clean -- coop_dropin now asserts the pad's buttons reach the joined record (accumulated every frame, so a press cannot fall between samples), and two_human_match measures movement on a fighter at its own start position (~1350 px against 0). Three consecutive coop_dropin runs pass where the displacement version flipped.

## What would falsify it

a run where the quiet arm's fighter receives a direction it was never sent, or where the press arm's does not
