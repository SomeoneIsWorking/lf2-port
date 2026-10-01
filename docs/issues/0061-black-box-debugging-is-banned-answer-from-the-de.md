---
id: 61
title: Black-box debugging is banned: answer from the decompilation, not from probing the running game
status: closed
tags: reported,workflow
created: 2026-08-12
updated: 2026-09-09
---

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
