---
id: C003
kind: claim
status: holds
created: 2026-08-05
tags: coop,re,data
depends: runtime/overrides/coop.c#coop_data_for_id
---

## Claim

Field 1780 of an LF2 object-data block is the object id from data.txt

## Evidence

All 65 entries of the registry at this+2004 carry a +1780 value that appears as an 'id:' in game/data/data.txt, with zero exceptions; the only two data.txt ids absent from the registry are 3 and 12, both of which are backgrounds (bg\...) rather than objects. Consistent with fn_0041bc90 spawning by comparing it against 999 (a data.txt id) and fn_004064d0 comparing it against 7 and 8 (Firen and Freeze).

## What would falsify it

a registry entry whose +1780 is not a data.txt object id, or a mod whose data.txt ids do not match what the registry reports
