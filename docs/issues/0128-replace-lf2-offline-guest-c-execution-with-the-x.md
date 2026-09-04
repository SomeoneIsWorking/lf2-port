---
id: 128
title: Replace LF2 offline guest-C execution with the x86port JIT
status: open
symptom: The gameplay target still builds generated lf2_recomp.c and calls generated fn_<address> symbols instead of executing non-native guest code on demand from the authenticated LF2 executable.
state_items: S005,S015,S016,S017,S018,S019
tags: migration,jit,x86port,execution
created: 2026-09-04
updated: 2026-09-04
---

## Root cause

LF2's guest-call contract is a link-time symbol contract. CMake runs the local
translator over `re/entries.tsv`, compiles `lf2_recomp.c`, omits addresses in
`re/overrides.txt`, and resolves both ordinary `fn_<address>` and `__orig`
symbols at link time. The otherwise reusable native entry, guest memory,
Win32/HLE, DirectDraw COM, and native overrides therefore cannot execute a
non-native address without the generated corpus.

The correct replacement is one LF2 adapter to `shared/x86port`'s product JIT:
runtime address dispatch for ordinary calls, import/HLE and override
interception, scoped original calls, bounded exits, and code invalidation over
the authenticated executable's live bytes.

## What was tried / dead ends

- Do not keep the generated product as a fallback or permanent oracle; no
  further static product runs are allowed.
- Do not ship `x86port`'s interpreter fallback/helper path or an engine
  selector. The interpreter may exist only in a separately built test target,
  including diagnostic tests.
- Do not port the rest of LF2 by hand. Existing native owners stay native and
  unowned guest behavior stays guest behavior executed by the JIT.
- Do not make a renderer, DirectDraw, widescreen, or shader refactor a CPU
  prerequisite. Preserve current calls and data at those boundaries first.
- Do not special-case an unsupported instruction or one LF2 guest address;
  correct shared x86 semantics belong in `shared/x86port`.

## Resolution

`docs/migration.md` is the plan and acceptance authority. The first bounded
discriminator executes `0x004031b0` with the established native-entry state in
a JIT-versus-test-interpreter target, with nonzero JIT blocks, complete
state/write comparison, and a controlled negative. Build-graph, link-map/symbol,
and selector inspection—not a zero-fallback counter—must prove the gameplay
target contains no interpreter execution, interpreter-backed helper, or
fallback machinery.

Resolve this issue only after the representative Stage Mode gate passes on each
released host, gameplay link/selector audits prove interpreter absence, the
static product has not been used for new evidence, and the translator,
generated corpus, generated-symbol dispatch, generation-only seeds, static-only
tests, and stale methodology are removed rather than retained as legacy.
