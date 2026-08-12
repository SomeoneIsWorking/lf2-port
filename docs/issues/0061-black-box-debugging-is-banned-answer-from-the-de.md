---
id: 61
title: Black-box debugging is banned: answer from the decompilation, not from probing the running game
status: open
tags: reported,workflow
created: 2026-08-12
updated: 2026-08-12
---

## What was reported

> ban black-box debugging

Standing instruction, following on from *\"decomp the game when you need, don't just try to
interpret the game states\"*. It names a method, not a bug.

## What counts as black-box debugging here

Anything that treats `lf2.exe` as opaque and tries to infer its rules from the outside:

- **Write-a-word-and-watch.** `LF2_EXIT_PROBE` is the worked example: a `.data` diff between two
  screens produced a candidate list, and each candidate cost a run to write and observe. Six
  were eliminated that way. The answer was in `FUN_00431d10`, one decompilation away.
- **`.data` dumps read as a state machine.** Sampling `0044d020` across frames gave
  `1 -> 10 -> 3 -> 1` and the note that recorded it concluded \"the game walks there on its own\".
  It does not; see issue #22. A sampled sequence cannot distinguish \"the code goes there\" from
  \"an input drove it there\", and the branch is three lines of C.
- **Frame dumps as the primary evidence for a state question.** Two screens that share a blit
  destination can share a picture; \"which screen is this\" is not answerable from pixels.
- **Bisecting a route** to find which press causes something, when the dispatch table is in the
  binary.

## What to do instead

`tools/re/ghidra_scripts/DecompDump.py` with `LF2_DECOMP_TARGETS` (invocation in
`docs/running.md`) dumps any function to `scratch/decomp/`. It takes seconds. Read the branch,
name the word, then measure ONE thing to confirm the read -- observation confirms an RE finding,
it does not substitute for one.

## Why this is right

The bandaid rule says name the cause before fixing. On a static-recompilation port the cause is
always a specific branch in translated code, so \"I cannot name it\" means \"I have not read it\",
not \"I need another run\". Probing also produces confidently-wrong notes: #22's \"REFUTED\" note
was written from a sampled sequence and is wrong.

## Consequences to carry out

- Delete `LF2_EXIT_PROBE` and its `exit_probe*` machinery from `runtime/overrides/screens.c`.
- Re-check any claim in `docs/info/claims/` whose evidence is a probe run rather than a read.
