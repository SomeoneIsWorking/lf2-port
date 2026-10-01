---
id: 81
title: Top-row digit key mapping uses SDL scancode zero as an arithmetic base
status: closed
symptom: Bindings for top-row digit keys 1 through 9 translate back to the wrong SDL scancodes
tags: input,keyboard,reported-by-test
created: 2026-08-21
updated: 2026-09-09
---

## Root cause
`vk_to_scancode` treated SDL scancodes as if `SDL_SCANCODE_0` began a contiguous numeric sequence. SDL orders the top row from `1` through `9` and then `0`, so adding a digit offset to the zero scancode maps 1..9 outside the digit row.
