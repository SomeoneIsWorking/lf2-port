---
id: C002
kind: claim
status: holds
created: 2026-08-05
tags: coop,re,memory
depends: runtime/overrides.c
---

## Claim

There is no heap-side list of active game objects; each player record is referenced from exactly one place, its slot in the .data pointer table at 0x00458c94

## Evidence

LF2_COOP_REFS scanned 27,847,172 aligned dwords across the image (3.3 MiB), the heap in use (101.9 MiB) and the stack (1.0 MiB) for all eight player-record pointers: 8 hits in .data at 0x00458c94..0x00458cb0, 3 transient stack copies, ZERO heap hits. The scan's positive control (each pointer must be found at this+404) passed.

## What would falsify it

a reference stored unaligned, tagged, or as base+offset would be invisible to this scan; so would a list in the VRAM/PCM arenas, which were not scanned
