---
id: C014
kind: claim
status: holds
created: 2026-08-06
tags: timing,determinism
depends: runtime/imports.c#guest_ns, runtime/ddraw.c#frame_pace
---

## Claim

The guest clock must be the presented-frame counter, with a Sleep credited as a floor (ms+1) and real-time pacing in the host's present -- this makes the game's timeline independent of machine load

## Evidence

Same binary, same route: idle box gives charselect@906 overlay@1746 match@1968; under fourteen busy loops it gives charselect@906 overlay@1746 match@1968. Identical. With the previous wall-derived clock the loaded run reported 'screens reached -- NONE'. Full suite 15/15, 32 fps at 14% of a core. Each of the three parts was measured by its failure: crediting a Sleep as exactly ms lands the pacer on its own boundary (elapsed==33 sends it to the sleep path, remaining==0 sends it past the Sleep) and it stops dead -- 59,331,701 clock reads at frame 0 with no frame ever presented at 99% CPU; not crediting sleeps during play hangs the startup waits, which produce no frames -- 1.8s of CPU in 200s of wall; and a host pacer that keeps its anchor across the load has every later frame due far in the future -- under 12 fps for a whole run.

## What would falsify it

a route whose screen frames differ between an idle and a loaded run, or a frame rate that is not ~30 on a machine that can keep up -- either would mean the clock is not the frame counter or the host is not pacing it
