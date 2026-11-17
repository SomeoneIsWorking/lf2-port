---
id: 128
title: Complete LF2 x86port JIT gameplay coverage
status: open
symptom: The x86-64 LF2 adapter executes through x86port_runtime, but the first live run stops at the first missing JIT emitter before gameplay.
state_items: S005,S015,S016,S017,S018,S019
tags: migration,jit,x86port,execution
created: 2026-09-04
updated: 2026-11-17
---

## Root cause

The retained native entry, guest memory, Win32/HLE, DirectDraw COM, and native
overrides require one runtime adapter for every non-native guest address.
`shared/x86port` owns that execution boundary, but its emitter coverage has not
yet reached the complete LF2 gameplay path.

## Constraints

- JIT execution is the gameplay default. A bounded interpreter fallback may
  handle only failed/unsupported compilation or unsafe translated execution;
  it must be reason-coded and counted.
- Explicit interpreter mode is diagnostic-only, and fallback coverage is not
  gameplay or performance evidence.
- Existing native owners remain native and unowned game behavior remains guest
  behavior executed through `x86port`.
- Renderer, DirectDraw, widescreen, and shader changes are not CPU
  prerequisites.
- Missing instruction semantics are fixed in `shared/x86port`, never by
  special-casing an LF2 address.

## Current implementation boundary

Native call sites use `lf2_jit_call`/`lf2_jit_call_original` and the typed
runtime override registry. CMake requires `x86port_runtime`; the consumer cannot
see `x86port_test_oracle`. `tools/build/check_source_policy.py` prevents exact
retired execution interfaces from returning anywhere in first-party source,
documentation, or tools.

The adapter's call context was not scoped across recursive HLE/guest calls:
entering a child replaced the engine's interception, dispatch, and translation
boundary pointers with the child's stack-local context; returning left those
pointers referring to an expired frame. The adapter now restores the enclosing
call's context before returning. `test_jit_calls` executes the production adapter
and real JIT with synthetic code. Before the change a nested HLE call missed the
parent return sentinel and executed its INT3 negative. It now preserves registers,
stack balance and return EIP across three calls, two nested HLE entries and three
scoped original calls (seven entered blocks, three translations, zero refusals).

A Clang-built silent, unpaced run with x86port `e1522b2` and jit-common `03ac795`
loaded the authenticated game, initialized data/art/music, reached the mode menu,
and exited normally at the requested ten presented frames. It entered 734,144
blocks and translated 6,848 instructions across 1,068 blocks, with zero refusals
or cache flushes. The previous `0x0040000C` refusal did not recur in this bounded
observation. This is startup evidence; representative gameplay and independent
state/output conformance remain open.

A hidden-window Stage Mode run on 8fdbeeb (x86port as pinned) fights Stage 1-1 through
the JIT. From `game/` with `SDL_AUDIODRIVER=dummy LF2_WINDOW=hidden LF2_UNPACED=1
LF2_QUIT_AFTER=3000` and `LF2_VIRTUAL_PAD="down@modemenu+60,south@modemenu+120,
south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+330,
up@overlay+99,up@overlay+159,south@overlay+219,right@match+100,south@match+150,..."`
it reaches charselect at frame 124, the Stage Mode panel at 456 and the fight at 678;
player 1 is hit from 500 to 300 HP, dies, and the summary shows. 77,609,027 blocks
entered, 3,676 translated (19,553 instructions), zero refusals and cache flushes,
156 sound plays and music. Screen anchors count from the screen's first frame: an extra
Attack timed against the absolute frame lands on the Stage Mode panel and activates its
focused item (Reset Random re-rolls a Random fighter), which is the game's own behaviour.
Still open: the oracle comparisons, fallback accounting, and invalidation proof the gate
requires.

Demo mode aborted 1.5 s into its match with "JIT call budget exhausted at guest 004239f2":
`jit_executor.c:execute` spent one of its 256 hang-guard slices on every native interception,
so a guest loop that calls an override more than 256 times in one call (the per-glyph
`fn_0043f010` call at `0x004239ed`) was taken for a hang. Only slices that run guest code
now count; `test_jit_calls` drives one call through 1000 override hits. The demo now runs
its 3600 frames (1,195,203,812 blocks entered, zero refusals) with six AI fighters on screen.

## Acceptance

`docs/migration.md` owns the complete plan. Resolve this issue only after the
representative Stage Mode gate passes on each released host, the gameplay
default is proven to be JIT execution, every bounded fallback is reason-coded
and denominated, and fallback coverage is excluded from gameplay/performance
claims.
