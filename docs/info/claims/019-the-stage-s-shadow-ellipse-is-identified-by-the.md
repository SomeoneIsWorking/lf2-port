---
id: C019
kind: claim
status: holds
created: 2026-08-06
tags: rendering,re
depends: runtime/ddraw.c
---

## Claim

The stage's shadow ellipse is identified by the OBJECT its clip draw is made on, and that object is learned at runtime from bg.dat's shadowsize -- not from a field in the background record. The record offset -1128, which sits beside the shadowsize and looks like a shadow pointer, belongs to the NEIGHBOURING background's record.

## Evidence

The wrong answer was measured before the right one, and the instrument is what separated them. Reading a pointer at -1128 (adjacent to shadowsize 37 9 and stage width 1500 for Brokeback Clif) and comparing it against every clip draw's  gave 0 matches out of 40000 draws with a stage loaded, across 32 distinct objects -- a negative with its denominator, not a silence. The records are contiguous at a 2448-byte stride, so that dword belongs to the previous background.
WHAT WORKS, verified: the background record does carry bg.dat's  (Brokeback Clif's record holds 37 and 9, matching the file), and the ellipse is the only draw whose SOURCE SURFACE matches that size. Latching the clip draw's object at the first such blit per stage identifies it: 'stage 6 draws its ellipse on object 25f06060 (source 35x7, shadowsize 37x9)', after which the hint fires on 1980 of 32000 sprite blits and produces 2778 ground markers and 2778 cast shadows in a single run -- every marker consumed.

## What would falsify it

a stage where the first blit whose source matches shadowsize is not the shadow (a sprite of the same size drawn before any object), or a run reporting ground markers with no cast shadows
