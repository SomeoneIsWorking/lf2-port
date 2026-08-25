---
id: 110
title: Boot remains about 1.5 s before the mode menu; the sprite-surface fill and its decode dominate
status: open
symptom: LF2_STARTUP_TRACE shows object-registry at about 0.67 s and a further 0.47 s between the window and data init; perf attributes most of it to h_StretchBlt and rle8_decode, with decrypt_file's fputc loop and an un-parsed key-script re-read per input poll behind them.
tags: reported,startup,performance
created: 2026-08-25
updated: 2026-08-25
---

## Reported

USER 2026-08-25: "Try to make faster boot".

## Measured

perf over a one-frame headless run (boot only): `h_StretchBlt` 24% of cycles, `rle8_decode`
15%, `decrypt_file` fputc 2%. Steady state separately: `getenv`+`strncmp` from
`h_PeekMessageA`/key polling ~18% — `key_script_pressed` and `pump_autoclick` re-fetch and
re-parse their env strings on EVERY poll/pump, in production runs too, not just scripted ones.

The StretchBlt inner loop divides per pixel (`(int64_t)x * sw / dw`) and palette-looks-up per
pixel even though issue #50 established the load path is 1:1 by construction. rle8_decode
writes through bounds checks byte-at-a-time where runs are memset/memcpy.

## The constraint

- Byte-exactness: these are host copies of guest art; the fast paths must produce identical
  pixels or they are wrong. The render/decrypt differential controls already exist.
- Env vars are diagnostics: caching their VALUES is fine; the LF2_* names keep meaning what
  they meant.
