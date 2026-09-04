# LF2 native/JIT migration

This document is the LF2 execution plan. The product consumes the player's
authenticated executable as runtime data and has one title-owned adapter to
`shared/x86port`.

## Product contract

The shipped process has two cooperating owners:

1. LF2-native code owns the process shell, bounded startup, title policy,
   selected game behavior, and established Win32/DirectX/SDL host seams.
2. `shared/x86port` discovers, translates, and caches all remaining x86-32
   guest code while the game runs.

JIT execution is the gameplay default. A bounded interpreter fallback may run
only after a failed or unsupported compilation or an unsafe translated exit.
Each fallback must preserve architectural progress, report a reason, and
increment denominated coverage counters. An explicit interpreter mode belongs
only to separately built diagnostics. Fallback coverage cannot satisfy a
gameplay, conformance, or performance gate.

The executable remains player-owned data. A persistent block cache, if later
justified, is disposable OS user data keyed to the exact image, core, host, and
configuration; a fresh installation never requires it.

## Current boundary

The x86-64 target compiles `runtime/cpu/jit_executor.c` against
`x86port_runtime`. The current pinned runtime executes `MUL EDX` and reaches the
mode menu. It then refuses when control enters PE/DOS-header data at
`0x0040000C`; the next task is to recover the incorrect control-flow owner. The
shared runtime does not yet expose the bounded fallback contract.

New execution evidence comes from:

- the original LF2 v2.0a executable under Wine or another independent oracle;
- direct binary analysis;
- focused tests against `x86port`'s production decoder/JIT; or
- the separately built test oracle with an explicit controlled negative.

Instruction semantics are fixed in `shared/x86port`, never at one LF2 address.

## Ownership boundary

### Preserve in LF2

- Exact game identity, game-tree validation, PE mapping, and the existing 4 GiB
  guest memory layout.
- `runtime/app/port_entry.c` as native entry. Guest PE entry `0x00445560` and
  WinMain `0x0043cf40` stay bypassed.
- Bounded native initialization and every native override still grounded in
  `docs/re-frontier.md`.
- Win32/CRT/GDI/DirectDraw/DirectShow/DirectSound/input/socket HLE and COM
  callbacks.
- Renderer, audio, input, UI, configuration, packaging, and platform modules.

### Consume from x86port

- Canonical x86 architectural state, decode, flags, x87/SSE semantics, control
  flow, exceptions, and product JIT backends.
- Executable-memory publication, block-cache lifetime, invalidation, runtime
  interception, bounded exits, and denominated execution statistics.
- The bounded fallback policy and implementation when that product contract is
  available.
- A separate oracle library visible only to separately built tests.

LF2 adds one narrow adapter between these owners. It does not duplicate CPU
state or memory semantics.

## Runtime calls and overrides

- Native code calls a guest address with an explicit calling convention,
  register/argument setup, and bounded return sentinel.
- The JIT stops at import/HLE thunks, native override addresses, explicit
  executor exits, and the caller's return sentinel.
- LF2 dispatches the intercepted address, updates CPU state, and either
  continues or returns the declared bounded exit.
- A scoped original call disables only the selected override, enters the same
  guest address through the JIT, and restores override state on every exit.
- Changing override state invalidates any translated call path that captured
  the former decision.

Still-valid address and ABI facts live in the typed native-override owner.

## First bounded JIT discriminator

A separately built test target must:

1. Validate and map the player's LF2 v2.0a executable through the production
   PE/memory owner.
2. Initialize independent but identical CPU/memory instances for JIT and
   oracle execution.
3. Execute guest constructor `0x004031b0` with `ECX=0x00458440`, the established
   stack contract, and return sentinel `0x004462e0`.
4. Compare EIP, general registers, EFLAGS/lazy-flag meaning, x87/SSE state,
   stack balance, return reason, and every guest-memory write.
5. Report translated blocks/instructions and any fallback entries with
   denominators and reason categories.
6. Run a controlled negative that changes one post-state or semantic and proves
   the comparison fails at the first differing field/address.

Build and selector inspection separately prove that gameplay defaults to JIT
and exposes no explicit interpreter mode.

## Expansion order

1. Route every non-overridden call made by native entry through the same
   address-based executor and compare each new boundary with the oracle.
2. Register HLE/import and COM sentinels as runtime interception points and
   preserve their guest ABI and memory effects.
3. Prove native override enabled/disabled/scoped-original sequences without
   recursion.
4. Expand reached JIT coverage through native initialization, the mode menu,
   character selection, VS Mode, and Stage Mode.
5. Wire executable-write notification and override-table changes into block
   invalidation; verify mutation and no-mutation cases.
6. Integrate the shared bounded fallback only through the JIT engine's failure
   exits, never through a title configuration or engine selector.

The existing renderer and DirectDraw adapter remain in place. A visual mismatch
is assigned to graphics only after CPU and HLE legs agree on the draw/service
inputs reaching that boundary.

## Representative-gameplay restoration gate

Use a bounded Stage Mode fight because it exercises the front end, world
updates, player input, enemy/object processing, camera/stage rules, rendering,
sound effects, music, and timing. The gate passes only when one frozen semantic
tree:

- provisions from the player's authenticated asset without maintainer-only
  tools or a pre-populated cache;
- builds and launches the JIT-default product with no explicit interpreter
  selector;
- reaches native entry, bounded initialization, mode menu, character select,
  pre-fight overlay, and an active Stage Mode fight;
- observes player movement/attack and an enemy/world response through
  state-anchored input;
- exercises DirectDraw/GDI presentation, sound effects, music, guest time, and
  normal quit through their preserved owners;
- reports nonzero JIT, HLE, native-override, and scoped-original work with
  denominators;
- reports every fallback entry by reason and coverage, while requiring the
  gameplay/performance assertions to be proven on JIT-executed regions;
- proves executable-write invalidation and override transition positives and
  negatives; and
- compares CPU, memory, timing, service events, audio, and frame checkpoints
  against an independent oracle on each released host architecture.

Boot, a logo, a menu, a clean trace, fallback-only coverage, or a single frame
does not pass this gate.

## Host qualification

- x86-64 and ARM64 are qualified separately.
- ARM64 requires W^X publication, instruction-cache coherence, ABI transition,
  invalidation, Android lifecycle, and sustained-device evidence.
- Unsupported hosts refuse by name; they do not expose a gameplay interpreter
  mode.
- Fallback counts are reported separately and never substitute for native JIT
  correctness or performance.
