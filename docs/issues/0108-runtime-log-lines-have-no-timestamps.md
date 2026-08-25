---
id: 108
title: Runtime log lines have no timestamps
status: resolved
symptom: Game and diagnostic stderr output cannot be correlated in time because log lines contain no timestamp
tags: reported,logging,diagnostics
created: 2026-08-25
updated: 2026-08-25
---

## Root cause

LF2 had no process logger. Hundreds of first-party call sites wrote directly to stdout or stderr,
while RmlUi used its default system-interface sink. Neither boundary added time, and changing the
record format directly would also have broken route analyzers that matched diagnostics at column
zero.

## What was tried / dead ends

Timestamping individual call sites was rejected: it would duplicate formatting policy across the
runtime, miss RmlUi, and leave fragmented `fprintf` sequences capable of producing partial records.

## Resolution

### Resolution (2026-08-25)
Pinned Lucent as the sole runtime logger; a force-included stdio bridge assembles complete legacy records, RmlUi's custom system interface maps its severity into Lucent, and the shared route decoder preserves payload matching. The Clang suite passed 39/39 (shader tools skipped), the real smoke route passed, and a zero-argument launcher run showed timestamps on first-party and RmlUi records with no empty delimiter records.
