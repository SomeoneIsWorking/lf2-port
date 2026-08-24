---
id: C062
kind: claim
status: holds
created: 2026-08-25
tags: parser
depends: runtime/overrides/object_parser.c#fn_0040ef70, runtime/overrides/object_frames.c#object_parser_load_frames, tools/routes/object_parser_test.py
---

## Claim

The native fn_0040ef70 frame parser produces the same LF2 object state as the original parser for all 65 shipped objects.

## Evidence

2026-08-25 tools/e2e.py object_parser: 65/65 complete 0x25360 object blocks plus dynamic sound, itr, bdy records and cumulative checksum were byte-identical; zero differing files.

## What would falsify it

Any shipped object dump differs, the original/native object sets differ, the checksum differs, or a valid object file aborts the native parser.
