---
id: 61
title: Black-box debugging is banned: answer from the decompilation, not from probing the running game
status: resolved
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

The bandaid rule says name the cause before fixing. In this binary-grounded port the cause is
still a specific branch or host boundary, so \"I cannot name it\" means \"I have not read it\",
not \"I need another run\". Probing also produces confidently-wrong notes: #22's \"REFUTED\" note
was written from a sampled sequence and is wrong.

## Consequences to carry out

- Delete `LF2_EXIT_PROBE` and its `exit_probe*` machinery from `runtime/overrides/screens.c`.
- Re-check any claim in `docs/info/claims/` whose evidence is a probe run rather than a read.

### Note (2026-08-12)
### Note (2026-08-12) -- the consequences are carried out, and the rule has already paid twice

DONE:
  - `LF2_EXIT_PROBE` and the whole `exit_probe*` machinery deleted from
    runtime/overrides/screens.c, and removed from docs/running.md.
  - The rule is a section of CLAUDE.md, naming the three banned methods with the case that
    burned each.
  - Every claim in docs/info/claims/ re-checked for probe-only evidence. Two mentioned a probe
    and both survive: C020's is a standalone SDL spike run against BOTH classes (a shader that
    passes the texture through was the negative), and C015's is a STATIC scan of the lifted
    binary with a watch as its confirmation, which is the right order.
  - One confidently-wrong note fixed at its source: runtime/overrides/world.h said the two mode
    words are never written back "so there is no exit sequence to drive". There is -- those
    words are the outer layer, and leaving a MATCH is SCREEN_WORD, which the game drives itself.

WHAT THE RULE FOUND, in the same session it was written:
  - #22. Six probe runs had eliminated .data candidates for "which word sends the game back to
    its menu". The answer was three lines of fn_00431d10, and the sampled sequence those runs
    were judged against had been written up as "the game walks there on its own" when it is one
    confirm press.
  - #60. The entry had concluded the caption's x lived in a 20 KB monolith, because a search of
    the call sites for the literals 0x31a/0x319 found neither. The constant is 0x316 and the x
    is computed from the string's length, so no literal search could ever have found it --
    fn_0041b130 is 598 bytes and is now a hand-port.

Both were entries that had stalled: one as "a design question", the other as "decide whether a
static label is worth a data-word hunt". Neither was either.

### Resolution (2026-08-12)
Carried out: LF2_EXIT_PROBE and its machinery deleted, the rule written into CLAUDE.md with the case that burned each banned method, every claim re-checked for probe-only evidence (two mention a probe, both survive), and world.h's wrong 'there is no exit sequence to drive' corrected at source. The rule found the real cause of #22 and #60 in the same session, both of which had stalled as 'design questions'.
