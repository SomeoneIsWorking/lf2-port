---
id: C026
kind: claim
status: holds
created: 2026-08-12
tags: verification,determinism
depends: runtime/win32/imports.c#guest_ns
---

## Claim

The port's guest state is deterministic across identical runs to one byte in 106 MB -- everything that varies is the security cookie or the wall-clock date string

## Evidence

MEASURED with tools/re/diff_data.py over two runs of an identical route, software renderer, 794x550, dumped at two anchored in-match frames:

    .data   12745 dwords compared, FIVE differed -- the SAME five at both frames
              0044eea4 / 0044eea8   __security_cookie and its complement, seeded per process
              0044fda4              ASCII, e.g. 5f353130 -> 5f393230
              00451d58 / 00458360   ASCII '/06' -> '/22', the wall-clock date
    heap    26,704,508 dwords compared, ONE differed
              20000040              a single ASCII digit of the same clock string

WHY IT MATTERS: it is what makes a state-level acceptance gate possible at all. Issue #55 needs fn_0041a5a0 hand-ported, and that pass does not only draw -- its effects loop advances per-effect counters and decrements obj[0x36c] -- so a pixel comparison can pass while the port quietly corrupts the game. A state comparison can only be built on a game whose state is reproducible, and nothing had ever asserted that.

THE CLOCK IS NOT A HOLE IN CLAIM C014. C014 says guest TIME is frames x 33.33 ms and never reads the wall; these strings are the CRT date, formatted once from the host clock at startup, and are not the guest's timeline.

tools/e2e.sh objects masks exactly these -- the cookie as two dwords, the date strings as their +/-16 byte buffers, because which dwords of a string differ depends on how far the clock moved -- and requires ZERO differences elsewhere. Its negative arm (a different game mode) differs by 73-78 dwords in .data and by the heap's size, so the comparison has been shown to fire.

## What would falsify it

any pair of identical runs differing at an address outside {0044eea4, 0044eea8} and the three date-string buffers at 0044fda4 / 00451d58 / 00458360 (.data) and 20000040 (heap). tools/e2e.sh objects asserts exactly this and fails on one differing dword.
