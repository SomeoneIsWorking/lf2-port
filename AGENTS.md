# LF2 port — working agreement

Little Fighter 2 v2.0a is becoming one native/dynarec product. Hand-written
native owners provide the host shell, Win32/DirectX services, renderer, audio,
input, UI, and selected game behavior. Every other guest instruction must run
on demand from the player's authenticated `lf2.exe` through `shared/x86port`'s
JIT.

The checked-in source still contains the retired offline x86-to-C path. It is
migration input, not a supported product or oracle. Do not build, launch,
regenerate, extend, or use that path for new evidence.

## Read before non-trivial work

| Authority | Answers |
| --- | --- |
| `docs/project-goals.md` | durable product outcomes and success conditions |
| `docs/project-state.md` | verified, partial, blocked, and missing capabilities; current focus |
| `docs/migration.md` | the LF2-specific native/JIT migration and acceptance gates |
| `docs/codemap.md` | subsystem ownership and where new work belongs |
| `docs/re-frontier.md` | which native replacements are grounded in the binary |
| `docs/issues/` | atomic work, findings, and dead ends |
| `docs/info/claims/`, `docs/info/instruments/` | evidence and instrument trust |
| `docs/platform-boundary.md` | observed Win32, DirectDraw, DirectSound, GDI, and input surface |
| `docs/isa-scope.md` | title-specific x86 coverage evidence for the JIT |

Start with `info.py brief <terms>` and search the issue catalog before
re-deriving an address, ABI, state transition, or failed approach.

## Non-negotiable execution contract

- The gameplay product is native code plus the `x86port` JIT. There is no
  offline or install-time emission of guest C/C++, objects, or precompiled
  title code.
- An interpreter may exist only in a separately built test target, including
  diagnostic tests. The gameplay build must not link it, select it, or fall
  back to it. Enforce
  this with build, link, and selector audits, not a counter that happened to
  remain zero.
- `x86port` owns x86 decode, architectural state, instruction semantics,
  dynamic translation, executable memory, and translated-block lifetime. LF2
  must not fork those responsibilities.
- LF2 owns exact executable identity, PE mapping, title callbacks, the native
  override table, Win32/HLE services, and product policy.
- Override lookup uses runtime guest address and image identity. A native
  override can make a scoped original call through the JIT without recursively
  selecting itself. Changing override state invalidates captured call paths.
- Unsupported guest behavior fails with its guest PC and decoded bytes. It
  never becomes a no-op, interpreter step, or guessed translation.
- The current generated-C product is not a comparison arm. New differential
  evidence comes from Wine/the original executable, binary analysis, or the
  interpreter available only in a separately built test target.

## Preserve the working seams

The CPU migration must adapt the existing product instead of rewriting it.

- `runtime/app/port_entry.c` remains the native process entry. The guest PE
  entry and WinMain remain bypassed; bounded native initialization remains.
- `runtime/cpu/` retains the 4 GiB guest address space, PE loading, and memory
  access policy, adapted once to `x86port`'s CPU/memory interface.
- `runtime/win32/` retains the Win32, CRT, GDI, DirectDraw COM, DirectShow,
  DirectSound, input, and socket/HLE service boundary.
- `runtime/overrides/` retains the verified native behavior and ABI facts. Its
  generated-symbol calls are replaced by address-based runtime dispatch and
  scoped original calls.
- `runtime/video/`, `runtime/audio/`, `runtime/input/`, `runtime/ui/`, and
  `runtime/platform/` remain peer host subsystems. A renderer refactor is not a
  prerequisite for CPU execution. First preserve the same calls and data at
  their existing boundaries; investigate graphics only after CPU/service state
  proves a graphics-owned divergence.
- Existing routes and claims are reusable evidence only after their producers
  run through the JIT product and their positive/negative reachability checks
  still hold.

## First JIT discriminator

Before any gameplay product run, a separately built test target must map the
authenticated LF2 v2.0a executable through the production memory owner and
execute guest constructor `0x004031b0` through `x86port`'s JIT. Preserve the
native entry contract already recorded in `port_entry.c`: `ECX=0x00458440`, a
guest return sentinel at `0x004462e0`, and no guest PE-entry or WinMain call.

Compare registers, EFLAGS, x87/SSE state, the guest stack, EIP/return reason,
and every guest write with the test target's interpreter. Require nonzero
translated-block counts and a controlled negative that the comparison rejects.
Build-graph, link-map/symbol, and selector inspection must independently prove
that the gameplay target cannot contain interpreter execution, interpreter-
backed helpers, or fallback machinery; zero fallback telemetry is supplementary
and cannot establish that absence. This discriminator is not permission to run
a mixed static/JIT gameplay binary.

## Representative-gameplay retirement gate

The offline translator, generated corpus, symbol dispatcher, generation seeds,
and static-only tests are removed together only after the native/JIT product:

- provisions a fresh checkout from the player's authenticated game asset
  without a translation step;
- proves by build/link/selector inspection that gameplay contains the JIT and
  no interpreter execution, interpreter-backed helper, fallback machinery, or
  generated guest body;
- reaches the existing menu, character-select, VS, and Stage Mode frontier;
- runs a bounded Stage Mode fight with player input, enemy/world updates,
  DirectDraw/GDI presentation, sound effects, music, timing, and interrupts;
- reports denominated nonzero JIT blocks, HLE calls, native overrides, and one
  scoped original call; any zero-fallback counters are supplementary and do
  not replace the build/link/selector exclusion proof;
- exercises one current override/original A/B through the shipping dispatcher
  and the relevant executable-write invalidation positive/negative cases; and
- compares CPU, memory, timing/interrupt, service-event, audio, and frame
  checkpoints against an independent oracle on every released host
  architecture, within explicitly documented tolerances.

Boot, a logo, a menu, a clean trace, or a single frame is not this gate.

## Ownership and quality guardrails

- Follow this repository's cohesive-owner map in `docs/codemap.md`: the entry
  point composes lifecycle, execution, platform, and presentation owners; it
  does not absorb their implementations. Split new work by its narrowest
  responsibility and keep dependencies flowing through the mapped interfaces.
  New runtime source is capped at 1,200 lines, existing oversized files may not
  grow, and 2,000+ lines require extraction before extension.
  `tools/build/check_structure.py` remains the mechanical authority and must
  cover new first-party modules.
- Lucent is the only process logger. LF2's bridge may assemble C/stdio
  fragments, but timestamps, channels, sinks, and serialization stay in
  Lucent. Emit one record per call site; never wrap logging in a debug `if`.
- `runtime/app/config.*` owns typed settings and persistence;
  `runtime/app/user_paths.*` owns the LF2 path below Lucent's OS user-data
  directory; `runtime/ui/` edits configuration but does not own it. `LF2_*`
  environment variables are diagnostic/maintainer overrides, never features
  or a second configuration system.
- Add behavior to the smallest owning module. Do not grow a god file, duplicate
  x86 semantics, or create a title-local JIT/cache beside `x86port`.
- Tests and diagnostics exercise production seams. Every diagnostic reports
  the scanned/entered denominator and proves that it can show the opposite
  answer.
- Build products live under `build/`; disposable diagnostics live under one
  stable activity path in `scratch/`, never `/tmp`. Never issue raw `rm`.
- Project automation is Python. `run.sh` is only the thin locked-environment
  launcher.

## Issue and evidence discipline

Use `docs/issues/` as the only atomic queue. File the symptom, measured cause,
and rejected shortcuts; link affected state items. Update
`docs/project-state.md` when capability coverage changes and
`docs/codemap.md` only when ownership or placement changes. A verified finding
belongs in one nearest living authority, not copied across every registry.

## Game content

Never commit or package `lf2.exe`, the installer, extracted game data,
`re/instructions.tsv`, or anything from which the original program can be
reconstructed. Source and packaged builds accept player-owned game files and
validate the exact LF2 v2.0a identity. Curated documentation screenshots are
the only tracked visual-output exception.
