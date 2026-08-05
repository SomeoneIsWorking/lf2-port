---
id: C008
kind: claim
status: holds
created: 2026-08-05
tags: coop,re,data
depends: runtime/overrides.c
---

## Claim

An LF2 object-data block's type is at +1784 (0 = character), adjacent to its id at +1780; the playable roster is the type-0 entries less the template

## Evidence

The registry at this+2004 is data.txt's <object> list in file order -- all 65 entries, ids matching exactly. Requiring a candidate offset to equal data.txt's declared type on EVERY one of the 65 entries leaves exactly one offset in the first 2048 bytes, +1784, at byte, word and dword width alike. Type-0 entries number 24; less the template at id 0 that is 23, which is LF2's selectable roster, and the running game reports '23 playable characters on the game's roster'.

## What would falsify it

a data.txt whose declared type disagrees with +1784 for any object, or a roster count that does not match what character selection offers
