# LF2 native/JIT migration

This is the LF2-specific execution plan under
`shared/jit-common/docs/migration.md`. It replaces the offline x86-to-C plan;
there is no static gameplay alternative.

## Product contract

The shipped process has two cooperating execution owners:

1. LF2-native code owns the process shell, bounded startup, title policy,
   selected game behavior, and every established Win32/DirectX/SDL host seam.
2. `shared/x86port` dynamically translates all remaining x86-32 guest code from
   the authenticated LF2 image and caches host blocks at runtime.

The executable remains player-owned data. No build, installer, launcher, or
release stage emits guest C/C++, guest object files, or a precompiled title
substrate. A persistent runtime cache, if later justified, is disposable OS
user data keyed to the exact image, core, host, and configuration; a fresh
installation never requires it.

An interpreter is permitted only in a separately built test target, including
diagnostic tests. The gameplay library/executable must not link interpreter
execution, interpreter-backed instruction helpers, an engine selector, or a
fallback route. Build-graph, link-map/symbol, and selector inspection must prove
that absence; runtime counters cannot prove code was not linked.

## Migration freeze

Until S005 is implemented, do not build, launch, regenerate, profile, or derive
new evidence from the current generated-C product. Preserve the checked-in
source only long enough to migrate its verified ABI, address, behavior, and
service-boundary facts. New evidence comes from:

- the original LF2 v2.0a executable under Wine or another independent oracle;
- direct binary analysis;
- focused tests against `x86port`'s production decoder/JIT; or
- an interpreter in a separately built test target with explicit positive and
  negative discrimination.

Do not add another static function, entry seed, lifted-body exception, or
generated-symbol test during the migration.

## Ownership boundary

### Preserve in LF2

- Exact game identity, game-tree validation, PE mapping, and the existing 4 GiB
  guest memory layout.
- `runtime/app/port_entry.c` as the native entry. Guest PE entry `0x00445560`
  and WinMain `0x0043cf40` stay bypassed.
- The bounded native initialization phases and every native override whose
  behavior is still grounded in `docs/re-frontier.md`.
- The current Win32/CRT/GDI/DirectDraw/DirectShow/DirectSound/input/socket HLE
  and COM-vtable callbacks.
- Renderer, audio, input, UI, configuration, packaging, and platform modules.

### Consume from x86port

- The canonical x86 architectural state, decode, flags, x87/SSE semantics,
  control flow, exceptions, and product JIT backends.
- Executable-memory publication, translated-block cache/lifetime, invalidation,
  runtime interception, bounded exits, and denominated execution statistics.
- A separate interpreter library consumed only by separately built test
  targets, including diagnostic tests.

LF2 adds one narrow adapter between these owners. It does not duplicate CPU
state or memory semantics. During transition, one explicit conversion boundary
may bridge the established LF2 state to `x86port`; the final product must have
one authoritative register/flag/x87/SSE representation, not two synchronized
copies.

## Runtime calls and overrides

Replace direct generated symbols with one address-based executor interface:

- Native code calls a guest address with an explicit calling convention,
  arguments/register setup, and bounded return sentinel.
- The JIT stops at import/HLE thunks, native override addresses, explicit
  executor exits, and the caller's return sentinel.
- LF2's dispatcher handles the intercepted address and either updates CPU state
  and continues or returns the declared bounded exit.
- A scoped original call disables only the currently selected override, enters
  the same guest address through the JIT, and restores override state on every
  exit. It never resolves to a generated `__orig` function.
- Installing, removing, disabling, or restoring an override invalidates any
  translated call path that captured the former decision.

The existing `re/overrides.txt` and `fn_<address>` link convention are not the
runtime registry. Migrate still-valid address/ABI facts into the typed native
override owner, then remove generation-only lists with the static pipeline.

## First bounded JIT discriminator

The first executable work is a separately built test target, including its
diagnostic modes, not a gameplay build.
It must:

1. Validate and map the player's LF2 v2.0a executable through the production
   PE/memory owner.
2. Initialize independent but identical CPU/memory instances for the JIT and
   test interpreter.
3. Execute guest constructor `0x004031b0`, the first non-overridden function
   called by the native entry, with `ECX=0x00458440`, the established guest
   stack contract, and return sentinel `0x004462e0`.
4. Stop on that sentinel through a named bounded executor exit.
5. Compare EIP, all general registers, EFLAGS/lazy-flag meaning, x87/SSE state,
   stack balance, and every guest-memory write.
6. Report blocks entered/translated and instructions translated with their
   denominators. Zero fallback/helper counters are useful test telemetry but do
   not prove the gameplay target excludes interpreter code.
7. Independently inspect the gameplay build graph, link map/symbols, and
   selector surface to prove no interpreter execution, interpreter-backed
   helper, or fallback machinery is present.
8. Run a controlled negative that alters one post-state or translated semantic
   and proves the comparison fails at the first differing field/address.

This proves the real image, LF2 memory adapter, x86port product translation,
native caller, and return boundary agree. It does not prove gameplay and does
not authorize a mixed generated-C/JIT product run.

## Expansion order

1. Route every non-overridden call made by the native entry through the same
   address-based executor, using the separately built test target (including
   its diagnostic modes) until each boundary agrees with the interpreter or
   original executable.
2. Register HLE/import and COM sentinels as runtime interception points and
   preserve their existing guest ABI and memory effects. Prove at least one
   CRT import and one DirectDraw COM call in both positive and controlled-
   negative cases.
3. Convert native override entry to the typed address registry. Replace every
   `__orig` reference with the scoped original-call operation and prove one
   disabled/enabled/original sequence without recursion.
4. Expand reached JIT coverage through native initialization, the mode menu,
   character selection, VS Mode, and Stage Mode. Unsupported instructions fail
   by name and guest PC; fix semantics in `x86port`, never at an LF2 address.
5. Wire executable-write notification and override-table changes into block
   invalidation. LF2 has no established overlay system; prove the generic
   positive mutation case and report the number of executable writes observed
   in the representative run rather than assuming it is zero.
6. Only after the gameplay target is composed entirely from native owners and a
   no-interpreter `x86port` product library may the product be launched.

The existing renderer and DirectDraw adapter remain in place throughout these
steps. No shader, scene, widescreen, or renderer rearchitecture is a CPU
dependency. A visual mismatch is assigned to graphics only after the JIT and
HLE legs agree on the draw/service inputs that reach the current boundary.

## Representative-gameplay retirement gate

Use a bounded Stage Mode scenario because it exercises the title's front end,
world updates, player input, enemy/object processing, camera/stage rules,
native rendering, sound effects, music, and timing. Reuse the existing
state-anchored Stage Mode route after it is adapted to the JIT product; do not
restore frame-number-only scripts.

The gate passes only when all of these are true in one frozen semantic tree:

### Provisioning and composition

- A fresh checkout provisions from the player's authenticated installer,
  executable/tree, or supported bounded ZIP without Ghidra, Wine, an offline
  translator, generated guest source, or a pre-populated runtime cache.
- Gameplay link and symbol inspection proves that no interpreter, interpreter-
  backed helper, generated guest body, `__orig` generated function, or engine
  selector is present.
- The zero-argument launcher selects only this product. Unsupported host
  backends refuse by name; they do not select an interpreter.

### Reached behavior

- The run reaches the native entry, bounded initialization, mode menu,
  character selection, pre-fight overlay, and an active Stage Mode fight.
- A state-anchored input sequence moves/attacks with the player and observes an
  enemy/world response. The route reports every requested action and every
  reached screen so silence cannot pass.
- DirectDraw and GDI presentation, sound effects, background music, guest time,
  interrupt/exit handling, and normal quit all occur through their preserved
  owners.
- The shipping dispatcher executes nonzero HLE calls and native overrides. The
  existing background override/original A/B at `0x0041a250` is migrated to the
  scoped JIT original-call path and retains its established native-width
  identity/negative discriminator without modifying graphics architecture.

### Conformance and performance

- Denominated telemetry reports nonzero JIT block entries/translations and
  native/HLE/original-call entries plus counts for invalidations and executable
  writes. Zero fallback/helper counters are supplementary; only build-graph,
  link-map/symbol, and selector inspection establishes interpreter absence.
- Deterministic checkpoints compare CPU registers/flags/x87/SSE state, relevant
  guest memory, timing/interrupt exits, ordered service events, audio events,
  and presented frames against an independent Wine/hardware or test-interpreter
  oracle. Every tolerance is stated by field; no blanket pixel or float fudge
  is allowed.
- The separately built test target demonstrates both a passing case and a
  deliberately differing diagnostic case. Every sampled/compared class reports
  its denominator.
- Frame-time percentiles, sustained behavior, memory, loading, rendering, and
  audio meet a declared budget on every released host class. Desktop evidence
  does not qualify Android.

Boot, a logo, menu entry, attract mode, a single screenshot, or an internal
trace is only a checkpoint. It cannot retire the old path.

## Removal milestone

When the representative gate passes, remove in the same milestone:

- `recompiler/` and every CMake/bootstrap/package invocation of it;
- generated `lf2_recomp.c` inputs and outputs;
- the generated-symbol dispatcher and direct `fn_<address>`/`__orig` contract;
- `re/entries.tsv`, `re/overrides.txt`, and other metadata used only to seed
  generation, after moving independently useful address/ABI facts to their
  living runtime or RE owner;
- decoder/lifter/static-differential tests that do not exercise `x86port`'s
  shipping implementation; and
- static methodology in active documentation.

Do not retain these under `legacy`, behind a compatibility flag, or as a
permanent oracle. Update project state, codemap, launcher docs, and the public
feature catalogue from the landed native/JIT result.
