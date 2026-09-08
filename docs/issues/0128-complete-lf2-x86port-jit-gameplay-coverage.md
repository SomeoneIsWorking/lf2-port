---
id: 128
title: Complete LF2 x86port JIT gameplay coverage
status: open
symptom: The x86-64 LF2 adapter executes through x86port_runtime, but the first live run stops at the first missing JIT emitter before gameplay.
state_items: S005,S015,S016,S017,S018,S019
tags: migration,jit,x86port,execution
created: 2026-09-04
updated: 2026-09-08
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
see `x86port_test_oracle`. `tests/test_execution_boundary.py` guards the
positive product boundary, while `tests/test_source_policy.py` prevents exact
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

## Acceptance

`docs/migration.md` owns the complete plan. Resolve this issue only after the
representative Stage Mode gate passes on each released host, the gameplay
default is proven to be JIT execution, every bounded fallback is reason-coded
and denominated, and fallback coverage is excluded from gameplay/performance
claims.
